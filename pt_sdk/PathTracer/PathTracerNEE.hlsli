/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __PATH_TRACER_NEE_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __PATH_TRACER_NEE_HLSLI__

#include "PathTracerTypes.hlsli"

//#include "Scene/ShadingData.hlsli"
#include "Lighting/Distant.hlsli"

#include "LightSampling/LightSamplingLocal.hlsli"

namespace PathTracer
{
    
#if 1   // switch this off to disable entire NEE codepath!
    
    /** Evaluates the currently configured heuristic for multiple importance sampling (MIS).
        \param[in] n0 Number of samples taken from the first sampling strategy.
        \param[in] p0 Pdf for the first sampling strategy.
        \param[in] n1 Number of samples taken from the second sampling strategy.
        \param[in] p1 Pdf for the second sampling strategy.
        \return Weight for the contribution from the first strategy (p0).
    */
    inline float EvalMIS(float n0, float p0, float n1, float p1)
    {
        float retVal = 0.0;
        switch (kMISHeuristic)
        {
        case MISHeuristic::Balance:
        {
            // Balance heuristic
            float q0 = n0 * p0;
            float q1 = n1 * p1;
            retVal = q0 / (q0 + q1);
        } break;
        case MISHeuristic::PowerTwo:
        {
            // Power two heuristic
            float q0 = (n0 * p0) * (n0 * p0);
            float q1 = (n1 * p1) * (n1 * p1);
            retVal = q0 / (q0 + q1);
        } break;
        case MISHeuristic::PowerExp:
        {
            const float kMISPowerExponent = 1.5;    // <- TODO: get it from PathTracerParams
            // Power exp heuristic
            float q0 = pow(n0 * p0, kMISPowerExponent);
            float q1 = pow(n1 * p1, kMISPowerExponent);
            retVal = q0 / (q0 + q1);
        } break;
        }
        return saturate(retVal); // only [0, 1] is valid
    }
    
    /** Generates a light sample on the environment map.
        \param[in] vertex Path vertex.
        \param[in,out] sampleGenerator Sample generator.
        \param[out] ls Struct describing valid samples.
        \return True if the sample is valid and has nonzero contribution, false otherwise.
    */
    inline bool GenerateEnvMapSample(const uint envMapISType, const PathVertex vertex, inout SampleGenerator sampleGenerator, out PathLightSample ls)
    {
        ls = PathLightSample::make();   // Default initialization to avoid divergence at returns. TODO: might be unnecessary; check for perf and side-effects

        if ( !(kUseEnvLights && Bridge::HasEnvMap()) )
            return false;
        
        // Sample environment map.
        EnvMapSampler envSampler = Bridge::CreateEnvMapImportanceSampler();
        DistantLightSample lightSample;
        if (envMapISType == 0)      // low quality uniform sampling
            lightSample = envSampler.UniformSample( sampleNext2D(sampleGenerator) );
        else if (envMapISType == 1) // slower, full (MIP descent) sampling - works well with low discrepancy sampling
            lightSample = envSampler.MIPDescentSample( sampleNext2D(sampleGenerator) );
        else if (envMapISType == 2) // faster, using the pre-sampled list - doesn't get any benefit from low discrepancy sampling
            lightSample = envSampler.PreSampledSample( sampleNext1D(sampleGenerator) );
        else 
            return false;

#if 0 // pdf correctness test
    float rpdf = envMapSampler.evalPdf(lightSample.dir);
    float relDiff = abs((lightSample.pdf - rpdf) / (1e-6 + abs(lightSample.pdf) + abs(rpdf)));
    if( relDiff > 5e-7 )
    {
        lightSample.pdf = 1e10;
        lightSample.Le.r += 1e20;
    }
#endif
       
        // Setup returned sample.
        ls.Li = lightSample.Pdf > 0.f ? lightSample.Le / lightSample.Pdf : float3(0,0,0);
        ls.Pdf = lightSample.Pdf;
        ls.Distance = kMaxRayTravel;
        ls.Direction = lightSample.Dir;
        
        //ls.lightType = (uint)PathLightType::EnvMap;

        return any(ls.Li > 0.f);
    }

    // This will ray cast and, if light visible, accumulate radiance properly, including doing weighted sum for 
    bool ProcessLightSample(inout NEEResult accum, inout float luminanceSum, const PathLightSample lightSample, 
                                const ShadingData shadingData, const ActiveBSDF bsdf, const PathState preScatterPath, const WorkingContext workingContext)
    {
        float3 bsdfThpDiff, bsdfThpSpec;
        bsdf.eval(shadingData, lightSample.Direction, bsdfThpDiff, bsdfThpSpec);
        float3 bsdfThp = bsdfThpDiff + bsdfThpSpec;
        
        float lum = luminance(bsdfThp*lightSample.Li);

        bool visible = false;
        if (lum > workingContext.ptConsts.NEEMinRadianceThreshold)
        {
            const RayDesc ray = lightSample.ComputeVisibilityRay(shadingData.posW, shadingData.faceN).toRayDesc();
            
            visible = Bridge::traceVisibilityRay(ray, preScatterPath.rayCone, preScatterPath.getVertexIndex(), workingContext.debug);
            if (visible)
            {
                float grazingFadeOut = (shadingData.shadowNoLFadeout>0)?(ComputeLowGrazingAngleFalloff( ray.Direction, shadingData.vertexN, shadingData.shadowNoLFadeout, 2.0 * shadingData.shadowNoLFadeout )):(1.0);
                
                float neeFireflyFilterK = ComputeNewScatterFireflyFilterK(preScatterPath.fireflyFilterK, workingContext.ptConsts.camera.pixelConeSpreadAngle, lightSample.Pdf, 1.0);

                float3 diffRadiance = grazingFadeOut * FireflyFilter(bsdfThpDiff * lightSample.Li, workingContext.ptConsts.fireflyFilterThreshold, neeFireflyFilterK);
                float3 specRadiance = grazingFadeOut * FireflyFilter(bsdfThpSpec * lightSample.Li, workingContext.ptConsts.fireflyFilterThreshold, neeFireflyFilterK);
                
                accum.DiffuseRadiance += diffRadiance;
                accum.SpecularRadiance += specRadiance;
                
                // weighted sum for sample distance
                float lum = luminance(diffRadiance+specRadiance);
                accum.RadianceSourceDistance += lightSample.Distance * lum;
                luminanceSum += lum;
                
                accum.Valid = true;
            }
        }
        return visible;
    }
    
    void FinalizeLightSample( inout NEEResult accum, const float luminanceSum )
    {
        accum.RadianceSourceDistance /= luminanceSum + 1e-30;
    }
    
    // 'result' argument is expected to have been initialized to 'NEEResult::empty()'
    inline void HandleNEE_MultipleSamples(inout NEEResult inoutResult, const PathState preScatterPath, const ScatterResult scatterInfo, const ShadingData shadingData, const ActiveBSDF bsdf, 
                                            inout SampleGenerator sampleGenerator, const WorkingContext workingContext, int sampleCountMultiplier)
    {
        sampleGenerator.startEffect(SampleGeneratorEffectSeed::NextEventEstimation, false); // disabled until we figure out how to get it working with presampling
        //sampleGenerator.startEffect(SampleGeneratorEffectSeed::NextEventEstimation, preScatterPath.getCounter(PackedCounters::DiffuseBounces)<DisableLowDiscrepancySamplingAfterDiffuseBounceCount);
        
        // There's a cost to having these as a dynamic constant so an option for production code is to hard code
        const uint distantSamples       = (kUseEnvLights && Bridge::HasEnvMap()) ? (sampleCountMultiplier * workingContext.ptConsts.NEEDistantFullSamples)   : (0);
        const uint localSamples         = (true)                                 ? (sampleCountMultiplier * workingContext.ptConsts.NEELocalFullSamples)     : (0);

        // we must initialize to 0 since we're accumulating multiple samples
        inoutResult.DiffuseRadiance = 0;
        inoutResult.SpecularRadiance = 0;
        inoutResult.RadianceSourceDistance = 0;
        
#ifdef ENVMAP_IMPORTANCE_SAMPLING_TYPE
        const uint envMapISType = ENVMAP_IMPORTANCE_SAMPLING_TYPE;
#else
        const uint envMapISType = workingContext.ptConsts.NEEDistantType;
#endif
        
        const uint totalSamples = distantSamples + localSamples;
        
        if (totalSamples == 0)
            return;
        
        float luminanceSum = 0.0; // for sample distance weighted average 
        
        // Setup path vertex.
        const PathVertex vertex = PathVertex::make(preScatterPath.getVertexIndex(), shadingData.posW, shadingData.N, shadingData.faceN);
        
        // With NEE, we effectively draw samples from two distributions: one is the path scatter BSDF itself and the first post-scatter hit on emissive or environment, and the other
        // is NEE light importance sampling (i.e. ReGIR for local and envmap importance sampling for distant) 
        // We use MIS as a method to weigh samples from each technique (i.e. BSDF vs ReGIR) on a per-sample basis, since we don't know in advance which one will provide better
        // specific sample. (see https://pbr-book.org/3ed-2018/Monte_Carlo_Integration/Importance_Sampling#MultipleImportanceSampling)
        // In simpler cases this is straight-forward since for BSDF we can always evaluate BSDF's pdf for any light sample's direction.
        // It is however more difficult to evaluate ReGIR's pdf for a given direction (or light index). Some light sampling approaches provide specialized functionality for this
        // purpose, such as re-tracing the tree hierarchy in "Importance Sampling of Many Lights with Adaptive Tree Splitting", last paragraph in Chapter 6 
        // (https://fpsunflower.github.io/ckulla/data/many-lights-hpg2018.pdf) but we don't (yet) have this for ReGIR.
        // So instead we do half-MIS here (pending better solutions), where we assume fixed probability on the ReGIR (local lights) side.
        // (It should be noted that this applies to local lights only - the environment map importance sampling method supports getting pdf for arbitrary directions so for
        // distant lights we can use full MIS)
        const float localPdfEstimateK = 1.0; // 0.3-3 is good for basic quality light sampling, ~5 is good when local light sampling quality is high (based on purely empirical data :) )
        
        for (int globalIndex = 0; globalIndex < totalSamples; globalIndex++)
        {
            PathLightSample lightSample;
            bool validSample = false;
            float sampleWeight = 0.0f;
            float lightMISPdf = 0.0f;
            
            if ( globalIndex < distantSamples )
            {
                // sampleGenerator.startEffect(SampleGeneratorEffectSeed::NextEventEstimationLoc, true, globalIndex, environmentSamples); // for many samples this gives better distribution!
                sampleWeight = 1.0 / (float)distantSamples;
                validSample = GenerateEnvMapSample(envMapISType, vertex, sampleGenerator, lightSample);
                lightMISPdf = lightSample.Pdf;
            } 
            else
            {
                sampleWeight = 1.0 / (float)localSamples;
                validSample = RTXDI_MINI_SampleLocalLightsFromWorldSpace(
                    sampleGenerator,
                    shadingData,
                    bsdf,
                    0,
                    0,
                    workingContext,
                    lightSample
                );
                lightMISPdf = localPdfEstimateK;

#if 0 // some minimal overhead to this
                if (g_Const.debug.debugViewType==(int)DebugViewType::ReGIRIndirectOutput && validSample)
                        workingContext.debug.DrawDebugViz(workingContext.pixelPos, float4(lightSample.Li, 1));
#endif
            }
            
            if (validSample)   // sample's bad, skip 
            {
                // account for MIS
                float scatterPdfForDir = bsdf.evalPdf(shadingData, lightSample.Direction, kUseBSDFSampling);
                lightSample.Li *= EvalMIS(1, lightMISPdf / sampleWeight, 1, scatterPdfForDir); // note, sampleWeight has not yet been applied to lightSample.pdf
                
                // account for multiple samples
                lightSample.Pdf /= sampleWeight; // <- this will affect firefly filtering and EvalMIS below!
                lightSample.Li *= sampleWeight;
                
                // this computes the BSDF throughput and (if throughput>0) then casts shadow ray and handles radiance summing up & weighted averaging for 'sample distance' used by denoiser
                ProcessLightSample(inoutResult, luminanceSum, lightSample, shadingData, bsdf, preScatterPath, workingContext);
            }
        }
        
        // Compute NEE MIS here. In case the scatter was not valid or is a delta bounce we just early out, leaving the default weights of 1.0
        // NOTE: It makes more sense to compute this at the place of use, only in case it's needed. I.e. envmap MIS weight is only needed if next hit is skybox. But
        //       the code is more complex and scattered then, and it wasn't a huge perf win practice (actually loss in outdoors scenes). So test before changing.
        if (scatterInfo.Valid && !scatterInfo.IsDelta)
        {                                       
            if (distantSamples > 0 )
            {
                // If NEE and MIS are enabled, and we've already sampled the env map, then we need to evaluate the MIS weight here to account for the remaining contribution.

                // Evaluate PDF, had it been generated with light sampling.
                EnvMapSampler envSampler = Bridge::CreateEnvMapImportanceSampler();
                float lightPdf;
                if ( envMapISType == 0 )
                    lightPdf = envSampler.UniformEvalPdf( scatterInfo.Dir );
                else if ( envMapISType == 1 )
                    lightPdf = envSampler.MIPDescentEvalPdf( scatterInfo.Dir );
                else if ( envMapISType == 2 )
                    lightPdf = envSampler.PreSampledEvalPdf( scatterInfo.Dir );
                else
                    lightPdf = 0;
                
                // Compute MIS weight by combining this with BSDF sampling. We early out if scatterInfo.Pdf == 0 (the scatterInfo.IsDelta check)
                inoutResult.ScatterEnvironmentMISWeight = EvalMIS(1, scatterInfo.Pdf, distantSamples, lightPdf); // distantSamples - accounts for multiple samples!

                // an example of debugging envmap MIS for the specific pixel selected in the UI, at the first bounce (vertex index 1)
                //if( workingContext.debug.IsDebugPixel() )
                //    workingContext.debug.Print( postScatterPath.getVertexIndex(), postScatterPath.pdf, lightPdf, misWeight );
            }
            if (localSamples > 0)
            {
                inoutResult.ScatterEmissiveMISWeight = EvalMIS(1, scatterInfo.Pdf, localSamples, localPdfEstimateK); // localSamples - accounts for multiple samples!
            }
        }

        FinalizeLightSample(inoutResult, luminanceSum);
    }
    
    inline NEEResult HandleNEE(const uniform OptimizationHints optimizationHints, const PathState preScatterPath, const ScatterResult scatterInfo,
                                    const ShadingData shadingData, const ActiveBSDF bsdf, inout SampleGenerator sampleGenerator, const WorkingContext workingContext)
    {
        // Determine if BSDF has non-delta lobes.
        const uint lobes = bsdf.getLobes(shadingData);
        const bool hasNonDeltaLobes = ((lobes & (uint) LobeType::NonDelta) != 0) && (!optimizationHints.OnlyDeltaLobes);

        // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        // TODO: This is for performance reasons, to exclude non-visible samples. Check whether it's actually beneficial in practice (branchiness)
        // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        //const bool flagLightSampledUpper = (lobes & (uint) LobeType::NonDeltaReflection) != 0;
        const bool onDominantBranch = preScatterPath.hasFlag(PathFlags::stablePlaneOnDominantBranch);
        const bool onStablePlane = preScatterPath.hasFlag(PathFlags::stablePlaneOnPlane);

        // Check if we should apply NEE.
        const bool applyNEE = (workingContext.ptConsts.NEEEnabled && !optimizationHints.OnlyDeltaLobes) && hasNonDeltaLobes;

        NEEResult result = NEEResult::empty(kUseEmissiveLights, kUseEnvLights);
        
        if (!applyNEE)
            return result;
        
        // this is checked by hasNonDeltaLobes - not needed
        // // Check if the scatter event is samplable by the light sampling technique.
        // if (!(flagLightSampledUpper || flagLightSampledLower))
        //     return;
        
        // Check if sample from RTXDI should be applied instead of NEE.
#if PATH_TRACER_MODE==PATH_TRACER_MODE_FILL_STABLE_PLANES
        const bool applyReSTIRDI = workingContext.ptConsts.useReSTIRDI && hasNonDeltaLobes && onDominantBranch && onStablePlane;
#else
        const bool applyReSTIRDI = false;
#endif
        
        // When ReSTIR DI is handling lighting, we skip NEE; at the moment RTXDI handles only reflection; in the case of first bounce transmission we still don't attemp to use
        // NEE due to complexity, and also the future where ReSTIR DI might handle transmission.
        if (applyReSTIRDI)
        {
            // in case this is not a transmission event (so it's just a reflection :) ), it is fully handled by ReSTIR DI, so set MIS weights to 0. Otherwise leave them at 1!
            if (!scatterInfo.IsTransmission) 
            {
                result.ScatterEnvironmentMISWeight = 0.0;
                result.ScatterEmissiveMISWeight = 0.0;
            }
            return result;
        }

        HandleNEE_MultipleSamples(result, preScatterPath, scatterInfo, shadingData, bsdf, sampleGenerator, workingContext, (onDominantBranch&&onStablePlane)?(workingContext.ptConsts.NEEBoostSamplingOnDominantPlane):(1));
        
        result.DiffuseRadiance  *= Bridge::getNoisyRadianceAttenuation();
        result.SpecularRadiance *= Bridge::getNoisyRadianceAttenuation();
        
        // Debugging tool to remove direct lighting from primary surfaces
        const bool suppressNEE = preScatterPath.hasFlag(PathFlags::stablePlaneOnDominantBranch) && preScatterPath.hasFlag(PathFlags::stablePlaneOnPlane) && workingContext.ptConsts.suppressPrimaryNEE;
        if (suppressNEE)    // keep it a valid sample so we don't add in normal path
        {
            result.DiffuseRadiance = 0;
            result.SpecularRadiance = 0;
        }
        
        return result;
    }
    
#else // disabled NEE!

inline NEEResult HandleNEE(const uniform OptimizationHints optimizationHints, const PathState preScatterPath, const ScatterResult scatterInfo,
                                const ShadingData shadingData, const ActiveBSDF bsdf, inout SampleGenerator sampleGenerator, const WorkingContext workingContext)
{
    return NEEResult::empty();
}
#endif
 
}

#endif // __PATH_TRACER_NEE_HLSLI__
