/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __PATH_TRACER_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __PATH_TRACER_HLSLI__

#include "PathTracerTypes.hlsli"

#include "Scene/ShadingData.hlsli"
#include "Scene/Lights/EnvMapSampler.hlsli"
// #include "Rendering/Materials/InteriorListHelpers.hlsli" not yet ported


// Global compile-time path tracer settings for debugging, performance or quality tweaks; could be in a separate file or Config.hlsli but it's convenient to have them in here where they're used.
namespace PathTracer
{
    static const bool           kUseEnvLights                   = true;     // this setting will be ignored by ReSTIR-DI (RTXDI)
    static const bool           kUseEmissiveLights              = true;     // this setting will be ignored by ReSTIR-DI (RTXDI)
    static const bool           kUseAnalyticLights              = true;     // this setting will be ignored by ReSTIR-DI (RTXDI)
    static const bool           kUseBSDFSampling                = true;     // this setting will be ignored by ReSTIR-DI (RTXDI)

    static const MISHeuristic   kMISHeuristic                   = MISHeuristic::Balance;
    
    static const float          kSpecularRoughnessThreshold     = 0.25f;
    
    static const uint           kMaxRejectedHits                = 16;       // Maximum number of rejected hits along a path (PackedCounters::RejectedHits counter, used by nested dielectrics). The path is terminated if the limit is reached to avoid getting stuck in pathological cases.
}

#include "PathTracerNestedDielectrics.hlsli"
#include "PathTracerStablePlanes.hlsli"

#if defined(PTSDK_COMPILE_WITH_NEE) && PTSDK_COMPILE_WITH_NEE!=0
#include "PathTracerNEE.hlsli"
#endif

namespace PathTracer
{
    inline PathState EmptyPathInitialize(uint2 pixelPos, float pixelConeSpreadAngle, uint subSampleIndex)
    {
        PathState path;
        path.id                     = PathIDFromPixel(pixelPos);
        path.flagsAndVertexIndex    = 0;
        path.sceneLength            = 0;
        path.fireflyFilterK         = 1.0;
        path.packedCounters         = 0;
        
        path.setCounter(PackedCounters::SubSampleIndex, subSampleIndex);

        for( uint i = 0; i < INTERIOR_LIST_SLOT_COUNT; i++ )
            path.interiorList.slots[i] = 0;

        path.origin                 = float3(0, 0, 0);
        path.dir                    = float3(0, 0, 0);
        // path.pdf                    = 0;
        // path.normal                 = float3(0, 0, 0);

        path.thp                    = float3(1, 1, 1);
#if PATH_TRACER_MODE!=PATH_TRACER_MODE_FILL_STABLE_PLANES
        path.L                      = float3(0, 0, 0);
#else
        path.denoiserSampleHitTFromPlane = 0.0;
        path.denoiserDiffRadianceHitDist = float4(0, 0, 0, 0);
        path.denoiserSpecRadianceHitDist = float4(0, 0, 0, 0);
        path.secondaryL             = 0.0;
#endif

        path.setHitPacked( HitInfo::make().getData() );
        path.setActive();
        path.setDeltaOnlyPath(true);

        path.rayCone                = RayCone::make(0, pixelConeSpreadAngle);

#if PATH_TRACER_MODE==PATH_TRACER_MODE_BUILD_STABLE_PLANES
        path.imageXform             = float3x3( 1.f, 0.f, 0.f,
                                                0.f, 1.f, 0.f,
                                                0.f, 0.f, 1.f);
        path.setFlag(PathFlags::stablePlaneOnDominantBranch, true); // stable plane 0 starts being dominant but this can change; in the _NOISY_PASS this is predetermined and can't change
#endif
        path.setStablePlaneIndex(0);
        path.stableBranchID         = 1; // camera has 1; makes IDs unique
        
        // these will be used for the first bounce
        path.emissiveMISWeight      = kUseEmissiveLights ? 1.0 : 0.0;
        path.environmentMISWeight   = kUseEnvLights ? 1.0 : 0.0;

        return path;
    }

    inline void SetupPathPrimaryRay(inout PathState path, const Ray ray)
    {
        path.origin = ray.origin;
        path.dir    = ray.dir;
    }

    /** Check if the path has finished all surface bounces and needs to be terminated.
        Note: This is expected to be called after GenerateScatterRay(), which increments the bounce counters.
        \param[in] path Path state.
        \return Returns true if path has processed all bounces.
    */
    inline bool HasFinishedSurfaceBounces(const PathState path)
    {
        if (Bridge::getMaxBounceLimit()<path.getVertexIndex())
            return true;
        const uint diffuseBounces = path.getCounter(PackedCounters::DiffuseBounces);
        return diffuseBounces > Bridge::getMaxDiffuseBounceLimit();
    }

    /** Update the path throughouput.
        \param[in,out] path Path state.
        \param[in] weight Vertex throughput.
    */
    inline void UpdatePathThroughput(inout PathState path, const float3 weight)
    {
        path.thp *= weight;
    }

    /** Apply russian roulette to terminate paths early.
        \param[in,out] path Path.
        \param[in] u Uniform random number in [0,1).
        \return Returns true if path needs to be terminated.
    */
    inline bool HandleRussianRoulette(inout PathState path, inout SampleGenerator sampleGenerator, const WorkingContext workingContext)
    {
#if PATH_TRACER_MODE==PATH_TRACER_MODE_BUILD_STABLE_PLANES  // stable planes must be stable, no RR!
        return false;
#else
        if( !workingContext.ptConsts.enableRussianRoulette )
            return false;
        
        sampleGenerator.startEffect(SampleGeneratorEffectSeed::RussianRoulette, false); // path.getCounter(PackedCounters::DiffuseBounces)<DisableLowDiscrepancySamplingAfterDiffuseBounceCount ); <- there is some benefit to using LD sampling here but quality gain does not clearly outweigh the cost

        const float rrVal = luminance(path.thp);
        
#if 0   // old "classic" one
        float prob = max(0.f, 1.f - rrVal);
#else   // a milder version of Falcor's Russian Roulette
        float prob = saturate( 0.85 - rrVal ); prob = prob*prob*prob*prob;
#endif

        if (sampleNext1D(sampleGenerator) < prob)
            return true;
        
        UpdatePathThroughput(path, 1.0 / (1.0 - prob)); // in theory we should also do 'path.fireflyFilterK *= (1.0 - prob);' here
        return false;
#endif
    }

    /** Generates a new scatter ray given a valid BSDF sample.
        \param[in] bs BSDF sample (assumed to be valid).
        \param[in] sd Shading data.
        \param[in] bsdf BSDF at the shading point.
        \param[in,out] path The path state.
        \return True if a ray was generated, false otherwise.
    */
    inline ScatterResult GenerateScatterRay(const BSDFSample bs, const ShadingData shadingData, const ActiveBSDF bsdf, inout PathState path, const WorkingContext workingContext)
    {
        ScatterResult result;
        
        if (path.hasFlag(PathFlags::stablePlaneOnPlane) && bs.pdf == 0)
        {
            // Set the flag to remember that this secondary path started with a delta branch,
            // so that its secondary radiance would not be directed into ReSTIR GI later.
            path.setFlag(PathFlags::stablePlaneOnDeltaBranch);
        }

        path.dir = bs.wo;
        if (workingContext.ptConsts.useReSTIRGI && path.hasFlag(PathFlags::stablePlaneOnPlane) && bs.pdf != 0 && path.hasFlag(PathFlags::stablePlaneOnDominantBranch))
        {
            // ReSTIR GI decomposes the throughput of the primary scatter ray into the BRDF and PDF components.
            // The PDF component is applied here, and the BRDF component is applied in the ReSTIR GI final shading pass.
            UpdatePathThroughput(path, 1.0 / bs.pdf);
        }
        else
        {
            // No ReSTIR GI, or not SP 0, or a secondary vertex, or a delta event - use full BRDF/PDF weight
            UpdatePathThroughput(path, bs.weight);
        }
        //path.pdf = bs.pdf;
        result.Pdf = bs.pdf;
        result.Dir = bs.wo;
        result.IsDelta = bs.isLobe(LobeType::Delta);
        result.IsTransmission = bs.isLobe(LobeType::Transmission);

        path.clearEventFlags(); // removes PathFlags::transmission, PathFlags::specular, PathFlags::delta flags

        // Compute ray origin for next ray segment.
        path.origin = shadingData.computeNewRayOrigin(bs.isLobe(LobeType::Reflection));
        
        // Handle reflection events.
        if (bs.isLobe(LobeType::Reflection))
        {
            // We classify specular events as diffuse if the roughness is above some threshold.
            float roughness = bsdf.getProperties(shadingData).roughness;
            bool isDiffuse = bs.isLobe(LobeType::DiffuseReflection) || roughness > kSpecularRoughnessThreshold;

            if (isDiffuse)
            {
                path.incrementCounter(PackedCounters::DiffuseBounces);
            }
            else
            {
                // path.incrementBounces(BounceType::Specular);
                path.setSpecular();
            }
        }

        // Handle transmission events.
        if (bs.isLobe(LobeType::Transmission))
        {
            // path.incrementBounces(BounceType::Transmission);
            path.setTransmission();

            // Update interior list and inside volume flag if needed.
            UpdateNestedDielectricsOnScatterTransmission(shadingData, path, workingContext);
        }

        // Handle delta events.
        if (bs.isLobe(LobeType::Delta))
            path.setDelta();
        else
            path.setDeltaOnlyPath(false);

        float angleBefore = path.rayCone.getSpreadAngle();
        if (!path.isDelta())
            path.rayCone = RayCone::make(path.rayCone.getWidth(), min( path.rayCone.getSpreadAngle() + ComputeRayConeSpreadAngleExpansionByScatterPDF( bs.pdf ), 2.0 * M_PI ) );


        // if bouncePDF then it's a delta event - expansion angle is 0
        path.fireflyFilterK = ComputeNewScatterFireflyFilterK(path.fireflyFilterK, workingContext.ptConsts.camera.pixelConeSpreadAngle, bs.pdf, bs.lobeP);

        // Mark the path as valid only if it has a non-zero throughput.
        result.Valid = any(path.thp > 0.f);

#if PATH_TRACER_MODE==PATH_TRACER_MODE_FILL_STABLE_PLANES
        if (result.Valid)
            StablePlanesOnScatter(path, bs, workingContext);
#endif

        return result;
    }

    /** Generates a new scatter ray using BSDF importance sampling.
        \param[in] sd Shading data.
        \param[in] bsdf BSDF at the shading point.
        \param[in,out] path The path state.
        \return True if a ray was generated, false otherwise.
    */
    inline ScatterResult GenerateScatterRay(const ShadingData shadingData, const ActiveBSDF bsdf, inout PathState path, inout SampleGenerator sampleGenerator, const WorkingContext workingContext)
    {
        sampleGenerator.startEffect(SampleGeneratorEffectSeed::ScatterBSDF, path.getCounter(PackedCounters::DiffuseBounces)<DisableLowDiscrepancySamplingAfterDiffuseBounceCount);
        
        BSDFSample result;
        bool valid = bsdf.sample(shadingData, sampleGenerator, result, kUseBSDFSampling);

        ScatterResult res;
        if (valid)
            res = GenerateScatterRay(result, shadingData, bsdf, path, workingContext);
        else
            res = ScatterResult::empty();

        return res;
    }

    // Called after ray tracing just before handleMiss or handleHit, to advance internal states related to travel
    inline void UpdatePathTravelled(inout PathState path, const float3 rayOrigin, const float3 rayDir, const float rayTCurrent, const WorkingContext workingContext, uniform bool incrementVertexIndex = true, uniform bool updateOriginDir = true)
    {
        if (updateOriginDir)    // make sure these two are up to date; they are only intended as "output" from ray tracer but could be used as input by subsystems
        {
            path.origin = rayOrigin;    
            path.dir = rayDir;
        }
        if (incrementVertexIndex)
            path.incrementVertexIndex();                                        // Advance to next path vertex (PathState::vertexIndex). (0 - camera, 1 - first bounce, ...)
        path.rayCone = path.rayCone.propagateDistance(rayTCurrent);             // Grow the cone footprint based on angle; angle itself can change on scatter
        path.sceneLength = min(path.sceneLength+rayTCurrent, kMaxRayTravel);    // Advance total travel length

        // good place for debug viz
#if ENABLE_DEBUG_VIZUALISATION && !NON_PATH_TRACING_PASS// && PATH_TRACER_MODE!=PATH_TRACER_MODE_BUILD_STABLE_PLANES <- let's actually show the build rays - maybe even add them some separate effect in the future
        if( workingContext.debug.IsDebugPixel() )
            workingContext.debug.DrawLine(rayOrigin, rayOrigin+rayDir*rayTCurrent, float4(0.6.xxx, 0.2), float4(1.0.xxx, 1.0));
#endif
    }

    // Miss shader
    inline void HandleMiss(inout PathState path, const float3 rayOrigin, const float3 rayDir, const float rayTCurrent, const WorkingContext workingContext)
    {
        UpdatePathTravelled(path, rayOrigin, rayDir, rayTCurrent, workingContext);

#if PATH_TRACER_MODE==PATH_TRACER_MODE_BUILD_STABLE_PLANES && ENABLE_DEBUG_DELTA_TREE_VIZUALISATION
        if (path.hasFlag(PathFlags::deltaTreeExplorer))
        {
            DeltaTreeVizHandleMiss(path, rayOrigin, rayDir, rayTCurrent, workingContext);
            return;
        }
#endif

#if 0 // nice visualizer for low discrepancy samplers - don't forget to select a debug pixel, enable continuous debug mode and look at the sky :)
        if (workingContext.debug.IsDebugPixel())
        {
            SampleGenerator sampleGenerator = SampleGenerator::make(PathIDToPixel(path.id), /*path.getVertexIndex()*/1, Bridge::getSampleBaseIndex() + path.getCounter(PackedCounters::RemainingSamplesPerPixel) - 1 );
            sampleGenerator.startEffect( SampleGeneratorEffectSeed::Base, false );
            
            float2 smp = sampleNext2D( sampleGenerator );
            workingContext.debug.DrawDebugViz(workingContext.pixelPos + smp * 100.0f + uint2(0,0), float4(0, 0, 0, 1) );
            smp = sampleNext2D( sampleGenerator );
            workingContext.debug.DrawDebugViz(workingContext.pixelPos + smp * 100.0f + uint2(105,0), float4(0, 0, 0, 1) );
            smp = sampleNext2D( sampleGenerator );
            workingContext.debug.DrawDebugViz(workingContext.pixelPos + smp * 100.0f + uint2(210,0), float4(0, 0, 0, 1) );
            smp = sampleNext2D( sampleGenerator );
            workingContext.debug.DrawDebugViz(workingContext.pixelPos + smp * 100.0f + uint2(315,0), float4(0, 0, 0, 1) );
            smp = sampleNext2D( sampleGenerator );
            workingContext.debug.DrawDebugViz(workingContext.pixelPos + smp * 100.0f + uint2(420,0), float4(0, 0, 0, 1) );
            
            sampleGenerator.startEffect( SampleGeneratorEffectSeed::Base, true );
            smp = sampleNext2D( sampleGenerator );
            workingContext.debug.DrawDebugViz(workingContext.pixelPos + smp * 100.0f + uint2(0,   110), float4(0, 0, 0, 1) );
            smp = sampleNext2D( sampleGenerator );
            workingContext.debug.DrawDebugViz(workingContext.pixelPos + smp * 100.0f + uint2(105, 110), float4(0, 0, 0, 1) );
            smp = sampleNext2D( sampleGenerator );
            workingContext.debug.DrawDebugViz(workingContext.pixelPos + smp * 100.0f + uint2(210, 110), float4(0, 0, 0, 1) );
            smp = sampleNext2D( sampleGenerator );
            workingContext.debug.DrawDebugViz(workingContext.pixelPos + smp * 100.0f + uint2(315, 110), float4(0, 0, 0, 1) );
            smp = sampleNext2D( sampleGenerator );
            workingContext.debug.DrawDebugViz(workingContext.pixelPos + smp * 100.0f + uint2(420, 110), float4(0, 0, 0, 1) );

            sampleGenerator.startEffect( SampleGeneratorEffectSeed::Base, true );
            float smp1 = sampleNext1D( sampleGenerator );
            workingContext.debug.DrawDebugViz(workingContext.pixelPos + uint2(smp1 * 500.0f + 0, 220), float4(1, 0, 0, 1) );
            smp1 = sampleNext1D( sampleGenerator );
            workingContext.debug.DrawDebugViz(workingContext.pixelPos + uint2(smp1 * 500.0f + 0, 230), float4(1, 0, 0, 1) );
            smp1 = sampleNext1D( sampleGenerator );
            workingContext.debug.DrawDebugViz(workingContext.pixelPos + uint2(smp1 * 500.0f + 0, 240), float4(1, 0, 0, 1) );
            smp1 = sampleNext1D( sampleGenerator );
            workingContext.debug.DrawDebugViz(workingContext.pixelPos + uint2(smp1 * 500.0f + 0, 250), float4(1, 0, 0, 1) );
            smp1 = sampleNext1D( sampleGenerator );
            workingContext.debug.DrawDebugViz(workingContext.pixelPos + uint2(smp1 * 500.0f + 0, 260), float4(1, 0, 0, 1) );
            smp1 = sampleNext1D( sampleGenerator );
            workingContext.debug.DrawDebugViz(workingContext.pixelPos + uint2(smp1 * 500.0f + 0, 270), float4(1, 0, 0, 1) );
        }
#endif

        float3 environmentEmission = 0.f;

        if (path.environmentMISWeight > 0)
        {
            float3 Le = Bridge::EnvMap::Eval(path.dir);
            environmentEmission = path.environmentMISWeight * Le;
        }

        environmentEmission = FireflyFilter( environmentEmission, workingContext.ptConsts.fireflyFilterThreshold, path.fireflyFilterK );
        environmentEmission *= Bridge::getNoisyRadianceAttenuation();
        
        path.clearHit();
        path.terminate();

#if PATH_TRACER_MODE!=PATH_TRACER_MODE_REFERENCE
        if( !StablePlanesHandleMiss(path, environmentEmission, rayOrigin, rayDir, rayTCurrent, 0, workingContext) )
            return;
#endif

#if PATH_TRACER_MODE != PATH_TRACER_MODE_FILL_STABLE_PLANES // noisy mode should either output everything to denoising buffers, with stable stuff handled in MODE 1; there is no 'residual'
        if (any(environmentEmission>0))
            path.L += max( 0.xxx, path.thp*environmentEmission );   // add to path contribution!
#endif
    }

    // supports only TriangleHit for now; more to be added when needed
    inline void HandleHit(const uniform OptimizationHints optimizationHints, inout PathState path, const float3 rayOrigin, const float3 rayDir, const float rayTCurrent, const WorkingContext workingContext)
    {
        UpdatePathTravelled(path, rayOrigin, rayDir, rayTCurrent, workingContext);
        
        SampleGenerator sampleGenerator = SampleGenerator::make(PathIDToPixel(path.id), path.getVertexIndex(), Bridge::getSampleBaseIndex() + path.getCounter(PackedCounters::SubSampleIndex));
        
        const uint2 pixelPos = PathIDToPixel(path.id);
#if ENABLE_DEBUG_VIZUALISATION
        const bool debugPath = workingContext.debug.IsDebugPixel();
#else
        const bool debugPath = false;
#endif

        // Upon hit:
        // - Load vertex/material data
        // - Compute MIS weight if path.getVertexIndex() > 1 and emissive hit
        // - Add emitted radiance
        // - Sample light(s) using shadow rays
        // - Sample scatter ray or terminate

        const bool isPrimaryHit     = path.getVertexIndex() == 1;
        const HitType hitType       = HitInfo::make(path.hitPacked).getType();
        const bool isTriangleHit    = hitType == HitType::Triangle;

        const TriangleHit triangleHit = TriangleHit::make(path.hitPacked);

        const uint vertexIndex = path.getVertexIndex();

        SurfaceData bridgedData = Bridge::loadSurface(optimizationHints, triangleHit, rayDir, path.rayCone, path.getVertexIndex(), workingContext.debug);

#if PATH_TRACER_MODE==PATH_TRACER_MODE_FILL_STABLE_PLANES
        // an example of debugging RayCone data for the specific pixel selected in the UI, at the first bounce (vertex index 1)
        // if( workingContext.debug.IsDebugPixel() && path.getVertexIndex()==1 )
        //     workingContext.debug.Print( 4, path.rayCone.getSpreadAngle(), path.rayCone.getWidth(), rayTCurrent, path.sceneLength);
#endif

        
        // Account for volume absorption.
        float volumeAbsorption = 0;   // used for stats
        if (!path.interiorList.isEmpty())
        {
            const uint materialID = path.interiorList.getTopMaterialID();
            const HomogeneousVolumeData hvd = Bridge::loadHomogeneousVolumeData(materialID); // gScene.materials.getHomogeneousVolumeData(materialID);
            const float3 transmittance = HomogeneousVolumeSampler::evalTransmittance(hvd, rayTCurrent);
            volumeAbsorption = 1 - luminance(transmittance);
            UpdatePathThroughput(path, transmittance);
        }

        // Reject false hits in nested dielectrics but also updates 'outside index of refraction' and dependent data
        bool rejectedFalseHit = !HandleNestedDielectrics(bridgedData, path, workingContext);

#if PATH_TRACER_MODE==PATH_TRACER_MODE_BUILD_STABLE_PLANES && ENABLE_DEBUG_DELTA_TREE_VIZUALISATION
        if (path.hasFlag(PathFlags::deltaTreeExplorer))
        {
            DeltaTreeVizHandleHit(path, rayOrigin, rayDir, rayTCurrent, bridgedData, rejectedFalseHit, HasFinishedSurfaceBounces(path), volumeAbsorption, workingContext);
            return;
        }
#endif
        if (rejectedFalseHit)
            return;

        // These will not change anymore, so make const shortcuts
        const ShadingData shadingData    = bridgedData.shadingData;
        const ActiveBSDF bsdf   = bridgedData.bsdf;

#if ENABLE_DEBUG_VIZUALISATION && PATH_TRACER_MODE!=PATH_TRACER_MODE_BUILD_STABLE_PLANES
        if (debugPath)
        {
            // IoR debugging - .x - "outside", .y - "interior", .z - frontFacing, .w - "eta" (eta is isFrontFace?outsideIoR/insideIoR:insideIoR/outsideIoR)
            // workingContext.debug.Print(path.getVertexIndex(), float4(shadingData.IoR, bridgedData.interiorIoR, shadingData.frontFacing, bsdf.data.eta) );

            // draw tangent space
            workingContext.debug.DrawLine(shadingData.posW, shadingData.posW + shadingData.T * workingContext.debug.LineScale(), float4(0.7, 0, 0, 0.5), float4(1.0, 0, 0, 0.5));
            workingContext.debug.DrawLine(shadingData.posW, shadingData.posW + shadingData.B * workingContext.debug.LineScale(), float4(0, 0.7, 0, 0.5), float4(0, 1.0, 0, 0.5));
            workingContext.debug.DrawLine(shadingData.posW, shadingData.posW + shadingData.N * workingContext.debug.LineScale(), float4(0, 0, 0.7, 0.5), float4(0, 0, 1.0, 0.5));

            // draw ray cone footprint
            float coneWidth = path.rayCone.getWidth();
            workingContext.debug.DrawLine(shadingData.posW + (-shadingData.T+shadingData.B) * coneWidth, shadingData.posW + (+shadingData.T+shadingData.B) * coneWidth, float4(0.5, 0.0, 1.0, 0.5), float4(0.5, 1.0, 0.0, 0.5) );
            workingContext.debug.DrawLine(shadingData.posW + (+shadingData.T+shadingData.B) * coneWidth, shadingData.posW + (+shadingData.T-shadingData.B) * coneWidth, float4(0.5, 0.0, 1.0, 0.5), float4(0.5, 1.0, 0.0, 0.5) );
            workingContext.debug.DrawLine(shadingData.posW + (+shadingData.T-shadingData.B) * coneWidth, shadingData.posW + (-shadingData.T-shadingData.B) * coneWidth, float4(0.5, 0.0, 1.0, 0.5), float4(0.5, 1.0, 0.0, 0.5) );
            workingContext.debug.DrawLine(shadingData.posW + (-shadingData.T-shadingData.B) * coneWidth, shadingData.posW + (-shadingData.T+shadingData.B) * coneWidth, float4(0.5, 0.0, 1.0, 0.5), float4(0.5, 1.0, 0.0, 0.5) );
        }
#endif

        BSDFProperties bsdfProperties = bsdf.getProperties(shadingData);

        // Check if the scatter event is samplable by the light sampling technique.
        const bool isLightSamplable = path.isLightSamplable(); // same as !path.isDelta()

        // Collect emissive triangle radiance.
        float3 surfaceEmission = 0.0;
        if (path.emissiveMISWeight > 0 && (any(bsdfProperties.emission)) )
        {
            surfaceEmission = bsdfProperties.emission * path.emissiveMISWeight;
            surfaceEmission = FireflyFilter(surfaceEmission, workingContext.ptConsts.fireflyFilterThreshold, path.fireflyFilterK);
            surfaceEmission *= Bridge::getNoisyRadianceAttenuation();

#if PATH_TRACER_MODE != PATH_TRACER_MODE_FILL_STABLE_PLANES // noisy mode should either output everything to denoising buffers, with stable stuff handled in MODE 1; there is no 'residual'
        if (any(surfaceEmission>0))
            path.L += max( 0.xxx, path.thp*surfaceEmission );   // add to path contribution!
#endif
        }
        
        // Terminate after scatter ray on last vertex has been processed. Also terminates here if StablePlanesHandleHit terminated path. Also terminate based on "Russian roulette" if enabled.
        bool pathStopping = HasFinishedSurfaceBounces(path);
        
#if PATH_TRACER_MODE!=PATH_TRACER_MODE_REFERENCE
        StablePlanesHandleHit(path, rayOrigin, rayDir, rayTCurrent, optimizationHints.SERSortKey, workingContext, bridgedData, volumeAbsorption, surfaceEmission, pathStopping);
#endif

#if PATH_TRACER_MODE!=PATH_TRACER_MODE_BUILD_STABLE_PLANES // in build mode we've consumed emission and either updated or terminated path ourselves
        pathStopping |= HandleRussianRoulette(path, sampleGenerator, workingContext);    // note: this will update path.thp!
#endif

        if (pathStopping)
        {
            path.terminate();
            return;
        }

#if PATH_TRACER_MODE==PATH_TRACER_MODE_BUILD_STABLE_PLANES 
        // in build mode we've consumed emission and either updated or terminated path ourselves, so we must skip the rest of the function
        return;
#endif 
        
        const PathState preScatterPath = path;

        // Generate the next path segment!
        ScatterResult scatterResult = GenerateScatterRay(shadingData, bsdf, path, sampleGenerator, workingContext);
        
        // Compute NextEventEstimation a.k.a. direct light sampling!
#if defined(PTSDK_COMPILE_WITH_NEE) && PTSDK_COMPILE_WITH_NEE!=0
        NEEResult neeResult = HandleNEE(optimizationHints, preScatterPath, scatterResult, shadingData, bsdf, sampleGenerator, workingContext); 
#else
        NEEResult neeResult = NEEResult::empty(kUseEmissiveLights, kUseEnvLights);
#endif
        
        path.emissiveMISWeight = neeResult.ScatterEmissiveMISWeight;
        path.environmentMISWeight = neeResult.ScatterEnvironmentMISWeight;
    
        if (neeResult.Valid)
        {
#if PATH_TRACER_MODE==PATH_TRACER_MODE_FILL_STABLE_PLANES // fill
            StablePlanesHandleNEE(preScatterPath, path, neeResult.DiffuseRadiance, neeResult.SpecularRadiance, neeResult.RadianceSourceDistance, workingContext);
#else
            float3 neeContribution = neeResult.DiffuseRadiance + neeResult.SpecularRadiance;
            path.L += max(0.xxx, preScatterPath.thp * neeContribution); // add to path contribution!
#endif
        }
        
        if (!scatterResult.Valid)
        {
            path.terminate();
        }
    }
};

#endif // __PATH_TRACER_HLSLI__
