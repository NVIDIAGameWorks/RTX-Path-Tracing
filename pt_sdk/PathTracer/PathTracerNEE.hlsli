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
#include "Scene/Lights/EnvMapSampler.hlsli"

#include "LightSampling/LightSamplingLocal.hlsli"

#define ENVMAP_IMPORTANCE_SAMPLING_TYPE     1   // 0 - reference MIP descent; 1 - pre-sampling

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
    inline bool GenerateEnvMapSample(const PathVertex vertex, inout SampleGenerator sampleGenerator, out PathLightSample ls)
    {
        ls = PathLightSample::make();   // Default initialization to avoid divergence at returns. TODO: might be unnecessary; check for perf and side-effects

        if ( !(kUseEnvLights && Bridge::EnvMap::HasEnvMap()) )
            return false;
        
        // Sample environment map.
#if ENVMAP_IMPORTANCE_SAMPLING_TYPE == 0    // slower, full (MIP descent) sampling - works well with low discrepancy sampling
        EnvMapSample lightSample = Bridge::EnvMap::Sample(sampleNext2D(sampleGenerator));
#elif ENVMAP_IMPORTANCE_SAMPLING_TYPE == 1  // faster, using the pre-sampled list - doesn't get any benefit from low discrepancy sampling
        EnvMapSample lightSample = Bridge::EnvMap::SamplePresampled(sampleNext1D(sampleGenerator));
#else
        #error unsupported ENVMAP_IMPORTANCE_SAMPLING_TYPE
#endif

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
        ls.Li = lightSample.pdf > 0.f ? lightSample.Le / lightSample.pdf : float3(0,0,0);
        ls.Pdf = lightSample.pdf;
        ls.Distance = kMaxRayTravel;
        ls.Direction = lightSample.dir;
        
        //ls.lightType = (uint)PathLightType::EnvMap;

        return any(ls.Li > 0.f);
    }
    
    /** Generates a light sample on the emissive geometry.
        \param[in] vertex Path vertex.
        \param[in] upperHemisphere True if only upper hemisphere (w.r.t. shading normal) should be considered.
        \param[in,out] sampleGenerator Sample generator.
        \param[out] ls Struct describing valid samples.
        \return True if the sample is valid and has nonzero contribution, false otherwise.
    */
    inline bool GenerateEmissiveSample(const PathVertex vertex, const bool upperHemisphere, inout SampleGenerator sampleGenerator, out PathLightSample ls)
    {
        ls = PathLightSample::make();   // Default initialization to avoid divergence at returns. TODO: might be unnecessary; check for perf and side-effects
        return false;
#if 0 // to be implemented yet
        if (!kUseEmissiveLights) return false;

        TriangleLightSample tls;
        if (!emissiveSampler.sampleLight(vertex.pos, vertex.normal, upperHemisphere, sampleGenerator, tls)) return false;

        // Setup returned sample.
        ls.Li = tls.pdf > 0.f ? tls.Le / tls.pdf : float3(0);
        ls.pdf = tls.pdf;
        // Offset shading and light position to avoid self-intersection.
        float3 lightPos = computeRayOrigin(tls.posW, tls.normalW);
        ls.origin = vertex.getRayOrigin(lightPos - vertex.pos);
        float3 toLight = lightPos - ls.origin;
        ls.distance = length(toLight);
        ls.dir = normalize(toLight);

        ls.lightType = (uint)PathLightType::Emissive;

        return any(ls.Li > 0.f);
#endif
    }

    /** Generates a light sample on the analytic lights.
        \param[in] vertex Path vertex.
        \param[in,out] sampleGenerator Sample generator.
        \param[out] ls Struct describing valid samples.
        \return True if the sample is valid and has nonzero contribution, false otherwise.
    */
    inline bool GenerateAnalyticLightSample(const PathVertex vertex, inout SampleGenerator sampleGenerator, out PathLightSample ls)
    {
        ls = PathLightSample::make();   // Default initialization to avoid divergence at returns. TODO: might be unnecessary; check for perf and side-effects
        
        uint lightCount = Bridge::getAnalyticLightCount(); // gScene.getLightCount();
        if (!kUseAnalyticLights || lightCount == 0) return false;
        
        // Sample analytic light source selected uniformly from the light list.
        // TODO: Sample based on estimated contributions as pdf.
        uint lightIndex = min(uint(sampleNext1D(sampleGenerator) * lightCount), lightCount - 1);
        
        // Sample local light source.
        AnalyticLightSample lightSample;
        if (!Bridge::sampleAnalyticLight(vertex.pos, /*gScene.getLight*/(lightIndex), sampleGenerator, lightSample)) return false;
        
        // Setup returned sample.
        ls.Pdf = lightSample.pdf / lightCount;
        ls.Li = lightSample.Li * lightCount;
        // Analytic lights do not currently have a geometric representation in the scene.
        // Do not worry about adjusting the ray length to avoid self-intersections at the light.
        ls.Distance = lightSample.distance;
        ls.Direction = lightSample.dir;
        
        //ls.lightType = (uint)PathLightType::Analytic;
        
        return any(ls.Li > 0.f);
    }

#if 0 // phasing out, obsolete
    
    /** Samples a light source in the scene.
        This function calls that the sampling function for the chosen light type.
        The upper/lower hemisphere is defined as the union of the hemispheres w.r.t. to the shading and face normals.
        \param[in] lightType Light Type.
        \param[in] vertex Path vertex.
        \param[in] sampleUpperHemisphere True if the upper hemisphere should be sampled.
        \param[in] sampleLowerHemisphere True if the lower hemisphere should be sampled.
        \param[in,out] sampleGenerator Sample generator.
        \param[out] ls Struct describing valid samples.
        \return True if the sample is valid and has nonzero contribution, false otherwise.
    */
    inline bool GenerateLightSample(const uint lightType, const PathVertex vertex, const bool sampleUpperHemisphere, const bool sampleLowerHemisphere, inout SampleGenerator sampleGenerator, out PathLightSample ls)
    {
        ls = PathLightSample::make();   // Default initialization to avoid divergence at returns. TODO: might be unnecessary; check for perf and side-effects
        
        bool valid = false;
        switch (lightType)
        {
            case ( (uint)PathLightType::EnvMap ):           valid = GenerateEnvMapSample(vertex, sampleGenerator, ls); break;
            case ( (uint)PathLightType::Emissive ):         valid = GenerateEmissiveSample(vertex, sampleUpperHemisphere && !sampleLowerHemisphere, sampleGenerator, ls); break;
            case ( (uint)(uint)PathLightType::Analytic ):   valid = GenerateAnalyticLightSample(vertex, sampleGenerator, ls); break;
        };
        if (!valid) return false;
        
        if( !(sampleUpperHemisphere && sampleLowerHemisphere) )
        {
            // Reject samples in non-requested hemispheres.
            float cosTheta = dot(vertex.normal, ls.Direction);
            // Flip the face normal to point in the same hemisphere as the shading normal.
            float3 faceNormal = sign(dot(vertex.normal, vertex.faceNormal)) * vertex.faceNormal;
            float cosThetaFace = dot(faceNormal, ls.dir);
            if (!sampleUpperHemisphere && (max(cosTheta, cosThetaFace) >= -kMinCosTheta)) return false;
            if (!sampleLowerHemisphere && (min(cosTheta, cosThetaFace) <= kMinCosTheta)) return false;
        }

        return true;
    }
    
    /** Return the probabilities for selecting different light types.
        \param[out] p Probabilities.
    */
    inline void FalcorStyle_GetLightTypeSelectionProbabilities(out float p[3])
    {
        // Set relative probabilities of the different sampling techniques.
        // TODO: These should use estimated irradiance from each light type. Using equal probabilities for now.
        p[0] = (kUseEnvLights && Bridge::EnvMap::HasEnvMap()) ? 1.f : 0.f;
        p[1] = 0; // kUseEmissiveLights ? 1.f : 0.f;
        p[2] = kUseAnalyticLights ? 1.f : 0.f;

        // Normalize probabilities. Early out if zero.
        float sum = p[0] + p[1] + p[2];
        if (sum == 0.f) return;

        float invSum = 1.f / sum;
        p[0] *= invSum;
        p[1] *= invSum;
        p[2] *= invSum;
    }

    inline float FalcorStyle_GetEnvMapSelectionProbability()   { float p[3]; FalcorStyle_GetLightTypeSelectionProbabilities(p); return p[0]; }
    inline float FalcorStyle_GetEmissiveSelectionProbability() { float p[3]; FalcorStyle_GetLightTypeSelectionProbabilities(p); return p[1]; }
    inline float FalcorStyle_GetAnalyicSelectionProbability()  { float p[3]; FalcorStyle_GetLightTypeSelectionProbabilities(p); return p[2]; }

    /** Select a light type for sampling.
        \param[out] lightType Selected light type.
        \param[out] pdf Probability for selected type.
        \param[in,out] sampleGenerator Sample generator.
        \return Return true if selection is valid.
    */
    inline bool FalcorStyle_SelectLightType(out uint lightType, out float pdf, inout SampleGenerator sampleGenerator)
    {
        // lightType = (uint)PathLightType::EnvMap; pdf = 1.0; return true; // <- use to override for testing
        
        float p[3];
        FalcorStyle_GetLightTypeSelectionProbabilities(p);

        float u = sampleNext1D(sampleGenerator);
        lightType = 0.0;
        pdf = 0.0;

        [unroll]
        for (lightType = 0; lightType < 3; ++lightType)
        {
            if (u < p[lightType])
            {
                pdf = p[lightType];
                return true;
            }
            u -= p[lightType];
        }

        return false;
    }

    // 'result' argument is expected to have been initialized to 'NEEResult::empty()'
    inline void HandleNEE_FalcorStyle(inout NEEResult inoutResult, const PathState preScatterPath, const ScatterResult scatterInfo, bool flagLightSampledUpper, bool flagLightSampledLower,
                                        const ShadingData shadingData, const ActiveBSDF bsdf, inout SampleGenerator sampleGenerator, const WorkingContext workingContext)
    {
        sampleGenerator.startEffect(SampleGeneratorEffectSeed::NextEventEstimation);
        
        // Determine if BSDF has non-delta lobes.
        const uint lobes = bsdf.getLobes(shadingData);

        PathLightSample ls = PathLightSample::make();
        bool validSample = false;

        // Setup path vertex.
        PathVertex vertex = PathVertex::make(preScatterPath.getVertexIndex(), shadingData.posW, shadingData.N, shadingData.faceN);

        // Decide on which light type to sample
        uint lightType;
        float selectionPdf;
        if (FalcorStyle_SelectLightType(lightType, selectionPdf, sampleGenerator))
        {
            // Sample a light.
            validSample = GenerateLightSample(lightType, vertex, flagLightSampledUpper, flagLightSampledLower, sampleGenerator, ls);
                
            // Account for light type selection.
            ls.pdf *= selectionPdf;
            ls.Li /= selectionPdf;
        }

#if 0 // there's cost to enabling this
        if( debugPath && isPrimaryHit )
        {
            DebugLogger::DebugPrint( 0, validSample?(1):(0) );
            DebugLogger::DebugPrint( 1, float4(ls.origin, ls.lightType) );      // origin, type
            DebugLogger::DebugPrint( 2, float4(ls.dir, ls.distance) );          // dir, distance
            DebugLogger::DebugPrint( 3, float4(ls.Li, ls.pdf) );                // radiance, pdf
        }
#endif

        if (validSample)
        {
            // Apply MIS weight.
            if (ls.lightType != (uint) PathLightType::Analytic)
            {
                float scatterPdfForDir = bsdf.evalPdf(shadingData, ls.dir, kUseBSDFSampling);
                ls.Li *= EvalMIS(1, ls.pdf, 1, scatterPdfForDir);
            }

#if PTSDK_DIFFUSE_SPECULAR_SPLIT
            float3 bsdfThpDiff, bsdfThpSpec;
            bsdf.eval(shadingData, ls.dir, bsdfThpDiff, bsdfThpSpec);
            float3 bsdfThp = bsdfThpDiff + bsdfThpSpec;
#else
            float3 bsdfThp = bsdf.eval(sd, ls.dir);
#endif

            float3 neeContribution = /*preScatterPath.thp **/bsdfThp * ls.Li;

            if (any(neeContribution > 0))
            {
                const RayDesc ray = ls.getVisibilityRay().toRayDesc();
                    
                // Trace visibility ray
                // If RTXDI is enabled, a visibility ray has already been fired so we can skip it 
                // here. ( Non-visible lights result in validSample=false, so we won't get this far)
                //logTraceRay(PixelStatsRayType::Visibility);
                bool visible = Bridge::traceVisibilityRay(ray, preScatterPath.rayCone, preScatterPath.getVertexIndex(), workingContext.debug);
                    
                if (visible)
                {
                    float neeFireflyFilterK = ComputeNewScatterFireflyFilterK(preScatterPath.fireflyFilterK, workingContext.ptConsts.camera.pixelConeSpreadAngle, ls.pdf, 1.0);

                    inoutResult.DiffuseRadiance = FireflyFilter(bsdfThpDiff * ls.Li, workingContext.ptConsts.fireflyFilterThreshold, neeFireflyFilterK);
                    inoutResult.SpecularRadiance = FireflyFilter(bsdfThpSpec * ls.Li, workingContext.ptConsts.fireflyFilterThreshold, neeFireflyFilterK);
                    inoutResult.RadianceSourceDistance = ls.distance;
                    inoutResult.Valid = true;
                }
            }
        }
       
        // compute NEE MIS here for emissive and environment; in case the scatter was not valid or is a delta bounce we just early out, leaving at default weights of 1.0 (performance optimization)
        if (scatterInfo.Valid && !scatterInfo.IsDelta)
        {                                       
            // environment map
            if (kUseEnvLights && Bridge::EnvMap::HasEnvMap())
            {
                // If NEE and MIS are enabled, and we've already sampled the env map,
                // then we need to evaluate the MIS weight here to account for the remaining contribution.

                // Evaluate PDF, had it been generated with light sampling.
                float lightPdf = FalcorStyle_GetEnvMapSelectionProbability() * Bridge::EnvMap::EvalPdf(scatterInfo.Dir);
              
                // Compute MIS weight by combining this with BSDF sampling.
                // Note we can assume postScatterPath.pdf > 0.f since we shouldn't have got here otherwise.
                inoutResult.ScatterEnvironmentMISWeight = EvalMIS(1, scatterInfo.Pdf, 1, lightPdf);

                // an example of debugging envmap MIS for the specific pixel selected in the UI, at the first bounce (vertex index 1)
                //if( workingContext.debug.IsDebugPixel() )
                //    workingContext.debug.Print( postScatterPath.getVertexIndex(), postScatterPath.pdf, lightPdf, misWeight );
            }
            
            // emissive triangles
            if (kUseEmissiveLights)
            {
                // not yet using emissive lights importance sampling so this part is empty
                
                inoutResult.ScatterEmissiveMISWeight = 1.0; // <- note, 1.0 is the default state, so setting it here is unnecessary
            }
        }
    }
#endif
    
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
                float neeFireflyFilterK = ComputeNewScatterFireflyFilterK(preScatterPath.fireflyFilterK, workingContext.ptConsts.camera.pixelConeSpreadAngle, lightSample.Pdf, 1.0);

                float3 diffRadiance = FireflyFilter(bsdfThpDiff * lightSample.Li, workingContext.ptConsts.fireflyFilterThreshold, neeFireflyFilterK);
                float3 specRadiance = FireflyFilter(bsdfThpSpec * lightSample.Li, workingContext.ptConsts.fireflyFilterThreshold, neeFireflyFilterK);
                
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
    inline void HandleNEE_MultipleSamples(inout NEEResult inoutResult, const PathState preScatterPath, const ScatterResult scatterInfo, bool flagLightSampledUpper, bool flagLightSampledLower,
                                        const ShadingData shadingData, const ActiveBSDF bsdf, inout SampleGenerator sampleGenerator, const WorkingContext workingContext)
    {
        sampleGenerator.startEffect(SampleGeneratorEffectSeed::NextEventEstimation, false); // disabled until we figure out how to get it working with presampling
        //sampleGenerator.startEffect(SampleGeneratorEffectSeed::NextEventEstimation, preScatterPath.getCounter(PackedCounters::DiffuseBounces)<DisableLowDiscrepancySamplingAfterDiffuseBounceCount);
        
        // There's a cost to having these as a dynamic constant so an option for production code is to hard code
        const uint distantSamples       = (kUseEnvLights && Bridge::EnvMap::HasEnvMap()) ? (workingContext.ptConsts.NEEDistantFullSamples)   : (0);
        const uint localSamples         = (true)                                         ? (workingContext.ptConsts.NEELocalFullSamples)     : (0);

        // we must initialize to 0 since we're accumulating multiple samples
        inoutResult.DiffuseRadiance = 0;
        inoutResult.SpecularRadiance = 0;
        inoutResult.RadianceSourceDistance = 0;
        
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
        const float localPdfEstimateK = 2; // 2-3 is good for basic quality light sampling, ~5 is good when local light sampling quality is high (based on purely empirical data :) )
        
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
                validSample = GenerateEnvMapSample(vertex, sampleGenerator, lightSample);
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
                float lightPdf = Bridge::EnvMap::EvalPdf(scatterInfo.Dir);
                
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
        const bool flagLightSampledUpper = (lobes & (uint) LobeType::NonDeltaReflection) != 0;
        const bool flagLightSampledLower = (lobes & (uint) LobeType::NonDeltaTransmission) != 0;

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
        const bool applyReSTIRDI = workingContext.ptConsts.useReSTIRDI && hasNonDeltaLobes && preScatterPath.hasFlag(PathFlags::stablePlaneOnDominantBranch) && preScatterPath.hasFlag(PathFlags::stablePlaneOnPlane);
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

#if 0
        HandleNEE_FalcorStyle(result, preScatterPath, scatterInfo, flagLightSampledUpper, flagLightSampledLower, shadingData, bsdf, sampleGenerator, workingContext);
#else
        HandleNEE_MultipleSamples(result, preScatterPath, scatterInfo, flagLightSampledUpper, flagLightSampledLower, shadingData, bsdf, sampleGenerator, workingContext);
#endif
        
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
