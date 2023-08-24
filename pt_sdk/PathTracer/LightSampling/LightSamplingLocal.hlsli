/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __PATH_TRACER_NEE_LOCAL_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __PATH_TRACER_NEE_LOCAL_HLSLI__

// WAR for dulipcate Miss functions
#undef USE_RAY_QUERY
#define USE_RAY_QUERY 1

#undef RTXDI_REGIR_MODE
#define RTXDI_REGIR_MODE RTXDI_REGIR_GRID

//#include "../PathTracerShared.h"
#include "../../RTXDI/PolymorphicLight.hlsli"

#include <rtxdi/Reservoir.hlsli>

#define RTXDI_ENABLE_PRESAMPLING 1

#if !defined(RTXDI_ENABLE_PRESAMPLING) || RTXDI_ENABLE_PRESAMPLING==0
#error not supported
#endif

namespace PathTracer
{
    typedef PolymorphicLightInfo RAB_LightInfo;
    typedef PathTracerSurfaceData RAB_Surface;
    
    struct RTXDI_SampleParameters
    {
        uint numRegirSamples;
        uint numLocalLightSamples;
        uint numInfiniteLightSamples;
        uint numEnvironmentMapSamples;
        uint numBrdfSamples;

        uint numMisSamples;
        float localLightMisWeight;
        float environmentMapMisWeight;
        float brdfMisWeight;
        float brdfCutoff; 
        float brdfRayMinT;
    };

    // Sample parameters struct
    // Defined so that so these can be compile time constants as defined by the user
    // brdfCutoff Value in range [0,1] to determine how much to shorten BRDF rays. 0 to disable shortening
    RTXDI_SampleParameters RTXDI_InitSampleParameters(
        uint numRegirSamples,
        uint numLocalLightSamples,
        uint numInfiniteLightSamples,
        uint numEnvironmentMapSamples,
        uint numBrdfSamples,
        float brdfCutoff RTXDI_DEFAULT(0.0),
        float brdfRayMinT RTXDI_DEFAULT(0.001f))
    {
        RTXDI_SampleParameters result;
        result.numRegirSamples = numRegirSamples;
        result.numLocalLightSamples = numLocalLightSamples;
        result.numInfiniteLightSamples = numInfiniteLightSamples;
        result.numEnvironmentMapSamples = numEnvironmentMapSamples;
        result.numBrdfSamples = numBrdfSamples;

        result.numMisSamples = numLocalLightSamples + numEnvironmentMapSamples + numBrdfSamples;
        result.localLightMisWeight = float(numLocalLightSamples) / result.numMisSamples;
        result.environmentMapMisWeight = float(numEnvironmentMapSamples) / result.numMisSamples;
        result.brdfMisWeight = float(numBrdfSamples) / result.numMisSamples;
        result.brdfCutoff = brdfCutoff;
        result.brdfRayMinT = brdfRayMinT;

        return result;
    }

    struct RAB_LightSample
    {
        float3 position;
        float3 normal;
        float3 radiance;
        float solidAnglePdf;
    
        PolymorphicLightType lightType;
    };    
    
    //Empty type constructors
    RAB_Surface RAB_EmptySurface()
    {
        return PathTracerSurfaceData::makeEmpty();
    }

    RAB_LightInfo RAB_EmptyLightInfo()
    {
        return (RAB_LightInfo)0;
    }

    RAB_LightSample RAB_EmptyLightSample()
    {
        return (RAB_LightSample)0;
    }    
    
    float3 RAB_GetSurfaceWorldPos(RAB_Surface surface)
    {
        return surface.ComputeNewRayOrigin();
    }
    
    // Returns the direction and distance from the surface to the light sample
    void RAB_GetLightDirDistance(RAB_Surface surface, RAB_LightSample lightSample,
        out float3 o_lightDir,
        out float o_lightDistance)
    {
        if (lightSample.lightType == PolymorphicLightType::kEnvironment /*|| lightSample.lightType == PolymorphicLightType::kDirectional*/)
        {
            o_lightDir = -lightSample.normal;
            o_lightDistance = DISTANT_LIGHT_DISTANCE;
        }
        else
        {
            float3 toLight = lightSample.position - RAB_GetSurfaceWorldPos(surface);
            o_lightDistance = length(toLight);
            o_lightDir = toLight / o_lightDistance;
            //o_lightDistance = max(0, o_lightDistance - g_RtxdiBridgeConst.rayEpsilon);
        }
    }
    
    // Returns the solid angle PDF of the light sample 
    float RAB_LightSampleSolidAnglePdf(RAB_LightSample lightSample)
    {
        return lightSample.solidAnglePdf;
    }    
    
    // Return true if the light sample comes from an analytic light
    bool RAB_IsAnalyticLightSample(RAB_LightSample lightSample)
    {
        return lightSample.lightType == PolymorphicLightType::kPoint || 
            lightSample.lightType == PolymorphicLightType::kDirectional;
    }
    
    // Samples a polymorphic light relative to the given receiver surface.
    // For most light types, the "uv" parameter is just a pair of uniform random numbers, originally
    // produced by the RAB_GetNextRandom function and then stored in light reservoirs.
    // For importance sampled environment lights, the "uv" parameter has the texture coordinates
    // in the PDF texture, normalized to the (0..1) range.
    RAB_LightSample RAB_SamplePolymorphicLight(RAB_LightInfo lightInfo, RAB_Surface surface, float2 uv)
    {
        PolymorphicLightSample pls = PolymorphicLight::calcSample(lightInfo, uv, RAB_GetSurfaceWorldPos(surface));

        RAB_LightSample lightSample;
        lightSample.position = pls.position;
        lightSample.normal = pls.normal;
        lightSample.radiance = pls.radiance;
        lightSample.solidAnglePdf = pls.solidAnglePdf;
        lightSample.lightType = getLightType(lightInfo);
   
        return lightSample;
    }    
    
    // Loads triangle light data from a tile produced by the presampling pass.
    RAB_LightInfo RTXDI_MINI_LoadCompactLightInfo(uint linearIndex)
    {
        uint4 packedData1, packedData2;
        packedData1 = u_RisLightDataBuffer[linearIndex * 2 + 0];
        packedData2 = u_RisLightDataBuffer[linearIndex * 2 + 1];
        return unpackCompactLightInfo(packedData1, packedData2);
    }
    
    RAB_LightInfo RTXDI_MINI_LoadLightInfo(uint index, bool previousFrame)
    {
        return t_LightDataBuffer[index];
    }   
    
    // // Performs importance sampling of the surface's BRDF and returns the sampled direction.
    // bool RAB_GetSurfaceBrdfSample(RAB_Surface surface, inout SampleGenerator rng, out float3 dir)
    // {
    //     BSDFSample result;
    //     surface.Sample(result, true);
    // 
    //     dir = result.wo;
    //     return dot(RAB_GetSurfaceNormal(surface), dir) > 0.f;
    // }

    // Computes the PDF of a particular direction being sampled by RAB_GetSurfaceBrdfSample.
    float RAB_GetSurfaceBrdfPdf(RAB_Surface surface, float3 dir)
    {
    #ifdef RAB_NO_TRANSMISSION_MATERIAL  // we have BSDFs so this early out breaks some surfaces
       if (dot(RAB_GetSurfaceNormal(surface), dir) <= 0.f)
            return 0;
    #endif
        return surface.EvalPdf(dir, true);
    }

    void RTXDI_MINI_RandomlySelectLocalLight(
        inout SampleGenerator rng,
        uint firstLocalLight,
        uint numLocalLights,
#if RTXDI_ENABLE_PRESAMPLING
        bool useRisBuffer,
        uint risBufferBase,
        uint risBufferCount,
#endif
        out RAB_LightInfo lightInfo,
        out uint lightIndex,
        out float invSourcePdf
    )
    {
        float rnd = sampleNext1D(rng);
        lightInfo = (RAB_LightInfo)0;
        bool lightLoaded = false;
    #if RTXDI_ENABLE_PRESAMPLING
        if (useRisBuffer)
        {
            uint risSample = min(uint(floor(rnd * risBufferCount)), risBufferCount - 1);
            uint risBufferPtr = risSample + risBufferBase;

            uint2 tileData = RTXDI_RIS_BUFFER[risBufferPtr];
            lightIndex = tileData.x & RTXDI_LIGHT_INDEX_MASK;
            invSourcePdf = asfloat(tileData.y);

            if ((tileData.x & RTXDI_LIGHT_COMPACT_BIT) != 0)
            {
                lightInfo = RTXDI_MINI_LoadCompactLightInfo(risBufferPtr);
                lightLoaded = true;
            }
        }
        else
    #endif
        {
            lightIndex = min(uint(floor(rnd * numLocalLights)), numLocalLights - 1) + firstLocalLight;
            invSourcePdf = float(numLocalLights);
        }

        if (!lightLoaded)
        {
            lightInfo = RTXDI_MINI_LoadLightInfo(lightIndex, false);
        }
    }
    

    // Heuristic to determine a max visibility ray length from a PDF wrt. solid angle.
    float RTXDI_MINI_BrdfMaxDistanceFromPdf(float brdfCutoff, float pdf)
    {
        const float kRayTMax = 3.402823466e+38F; // FLT_MAX
        return brdfCutoff > 0.f ? sqrt((1.f / brdfCutoff - 1.f) * pdf) : kRayTMax;
    }

    // Computes the multi importance sampling pdf for brdf and light sample.
    // For light and BRDF PDFs wrt solid angle, blend between the two.
    //      lightSelectionPdf is a dimensionless selection pdf
    float RTXDI_MINI_LightBrdfMisWeight(RAB_Surface surface, RAB_LightSample lightSample, 
        float lightSelectionPdf, float lightMisWeight, bool isEnvironmentMap,
        RTXDI_SampleParameters sampleParams)
    {
        float lightSolidAnglePdf = RAB_LightSampleSolidAnglePdf(lightSample);
        if (sampleParams.brdfMisWeight == 0 || RAB_IsAnalyticLightSample(lightSample) || 
            lightSolidAnglePdf <= 0 || isinf(lightSolidAnglePdf) || isnan(lightSolidAnglePdf))
        {
            // BRDF samples disabled or we can't trace BRDF rays MIS with analytical lights
            return lightMisWeight * lightSelectionPdf;
        }

        float3 lightDir;
        float lightDistance;
        RAB_GetLightDirDistance(surface, lightSample, lightDir, lightDistance);

        // Compensate for ray shortening due to brdf cutoff, does not apply to environment map sampling
        float brdfPdf = RAB_GetSurfaceBrdfPdf(surface, lightDir);
        float maxDistance = RTXDI_MINI_BrdfMaxDistanceFromPdf(sampleParams.brdfCutoff, brdfPdf);
        if (!isEnvironmentMap && lightDistance > maxDistance)
            brdfPdf = 0.f;

        // Convert light selection pdf (unitless) to a solid angle measurement
        float sourcePdfWrtSolidAngle = lightSelectionPdf * lightSolidAnglePdf;

        // MIS blending against solid angle pdfs.
        float blendedPdfWrtSolidangle = lightMisWeight * sourcePdfWrtSolidAngle + sampleParams.brdfMisWeight * brdfPdf;

        // Convert back, RTXDI divides shading again by this term later
        return blendedPdfWrtSolidangle / lightSolidAnglePdf;
    }

    float2 RTXDI_MINI_RandomlySelectLocalLightUV(inout SampleGenerator rng)
    {
        float2 uv;
        uv.x = sampleNext1D(rng);
        uv.y = sampleNext1D(rng);
        return uv;
    }
    
    // Computes the weight of the given light samples when the given surface is
    // shaded using that light sample. Exact or approximate BRDF evaluation can be
    // used to compute the weight. ReSTIR will converge to a correct lighting result
    // even if all samples have a fixed weight of 1.0, but that will be very noisy.
    // Scaling of the weights can be arbitrary, as long as it's consistent
    // between all lights and surfaces.
    float RAB_GetLightSampleTargetPdfForSurface(RAB_LightSample lightSample, RAB_Surface surface)
    {
        if (lightSample.solidAnglePdf <= 0)
            return 0;

        float3 toLight;// = normalize(lightSample.position - RAB_GetSurfaceWorldPos(surface));
        float dis;
        RAB_GetLightDirDistance(surface, lightSample, toLight, dis);

    #ifdef RAB_NO_TRANSMISSION_MATERIAL  // we have BSDFs so this early out breaks some surfaces
        if (dot(toLight, RAB_GetSurfaceNormal(surface)) <= 0)
            return 0;
    #endif

        float3 fullBRDF = surface.Eval(toLight);
        return luminance(fullBRDF * lightSample.radiance) / lightSample.solidAnglePdf;

    }    

    // Adds a new, non-reservoir light sample into the reservoir, returns true if this sample was selected.
    // Algorithm (3) from the ReSTIR paper, Streaming RIS using weighted reservoir sampling.
    bool RTXDI_MINI_StreamSample(
        inout RTXDI_Reservoir reservoir,
        uint lightIndex,
        float2 uv,
        float random,
        float targetPdf,
        float invSourcePdf)
    {
        // What's the current weight
        float risWeight = targetPdf * invSourcePdf;

        // Add one sample to the counter
        reservoir.M += 1;

        // Update the weight sum
        reservoir.weightSum += risWeight;

        // Decide if we will randomly pick this sample
        bool selectSample = (random * reservoir.weightSum < risWeight);

        // If we did select this sample, update the relevant data.
        // New samples don't have visibility or age information, we can skip that.
        if (selectSample) 
        {
            reservoir.lightData = lightIndex | RTXDI_Reservoir_LightValidBit;
            reservoir.uvData = uint(saturate(uv.x) * 0xffff) | (uint(saturate(uv.y) * 0xffff) << 16);
            reservoir.targetPdf = targetPdf;
        }

        return selectSample;
    }
    
    // Performs normalization of the reservoir after streaming. Equation (6) from the ReSTIR paper.
    void RTXDI_MINI_FinalizeResampling(
        inout RTXDI_Reservoir reservoir,
        float normalizationNumerator,
        float normalizationDenominator)
    {
        float denominator = reservoir.targetPdf * normalizationDenominator;

        reservoir.weightSum = (denominator == 0.0) ? 0.0 : (reservoir.weightSum * normalizationNumerator) / denominator;
    }    
    
    // Returns false if the blended source PDF == 0, true otherwise
    bool RTXDI_MINI_StreamLocalLightAtUVIntoReservoir(
        inout SampleGenerator rng,
        RTXDI_SampleParameters sampleParams,
        RAB_Surface surface,
        uint lightIndex,
        float2 uv,
        float invSourcePdf,
        RAB_LightInfo lightInfo,
        inout RTXDI_Reservoir state,
        inout RAB_LightSample o_selectedSample)
    {
        RAB_LightSample candidateSample = RAB_SamplePolymorphicLight(lightInfo, surface, uv);
        float blendedSourcePdf = RTXDI_MINI_LightBrdfMisWeight(surface, candidateSample, 1.0 / invSourcePdf,
            sampleParams.localLightMisWeight, false, sampleParams);
        float targetPdf = RAB_GetLightSampleTargetPdfForSurface(candidateSample, surface);
        float risRnd = sampleNext1D(rng);

        if (blendedSourcePdf == 0)
        {
            return false;
        }
        bool selected = RTXDI_MINI_StreamSample(state, lightIndex, uv, risRnd, targetPdf, 1.0 / blendedSourcePdf);

        if (selected) {
            o_selectedSample = candidateSample;
        }
        return true;
    }    

    // SDK internal function that samples the given set of lights generated by RIS
    // or the local light pool. The RIS set can come from local light importance presampling or from ReGIR.
    RTXDI_Reservoir RTXDI_MINI_SampleLocalLightsInternal(
        inout SampleGenerator rng,
        RAB_Surface surface,
        RTXDI_SampleParameters sampleParams,
        RTXDI_LocalLightRuntimeParameters params,
#if RTXDI_ENABLE_PRESAMPLING
        bool useRisBuffer,
        uint risBufferBase,
        uint risBufferCount,
#endif
        const WorkingContext workingContext,
        out RAB_LightSample o_selectedSample)
    {
        RTXDI_Reservoir state = RTXDI_EmptyReservoir();
        o_selectedSample = RAB_EmptyLightSample();

        if (params.numLocalLights == 0)
            return state;

        if (sampleParams.numLocalLightSamples == 0)
            return state;

        for (uint i = 0; i < sampleParams.numLocalLightSamples; i++)
        {
            uint lightIndex;
            RAB_LightInfo lightInfo;
            float invSourcePdf;

            RTXDI_MINI_RandomlySelectLocalLight(rng, params.firstLocalLight, params.numLocalLights,
#if RTXDI_ENABLE_PRESAMPLING
                useRisBuffer, risBufferBase, risBufferCount,
#endif
                lightInfo, lightIndex, invSourcePdf);

            // if( workingContext.debug.IsDebugPixel() )
            //     workingContext.debug.Print( i, lightIndex, sampleParams.numLocalLightSamples );

            float2 uv = RTXDI_MINI_RandomlySelectLocalLightUV(rng);
            bool zeroPdf = RTXDI_MINI_StreamLocalLightAtUVIntoReservoir(rng, sampleParams, surface, lightIndex, uv, invSourcePdf, lightInfo, state, o_selectedSample);
            if (zeroPdf)
                continue;

        }
    
        RTXDI_MINI_FinalizeResampling(state, 1.0, sampleParams.numMisSamples);
        state.M = 1;
        
        return state;
    }


    // Sampling lights for a surface from the ReGIR structure or the local light pool.
    // If the surface is inside the ReGIR structure, and ReGIR is enabled, and
    // numRegirSamples is nonzero, then this function will sample the ReGIR structure.
    // Otherwise, it samples the local light pool.
    RTXDI_Reservoir RTXDI_MINI_SampleLocalLightsAllVariants(
        inout SampleGenerator rng,
        RAB_Surface surface,
        RTXDI_SampleParameters sampleParams,
        RTXDI_ResamplingRuntimeParameters params,
        const WorkingContext workingContext,
        out RAB_LightSample o_selectedSample)
    {
        RTXDI_Reservoir reservoir = RTXDI_EmptyReservoir();
        o_selectedSample = RAB_EmptyLightSample();

        float tileRnd = sampleNext1D(rng);
        uint tileIndex = uint(tileRnd * params.risBufferParams.tileCount);

        uint risBufferBase = tileIndex * params.risBufferParams.tileSize;
        uint risBufferCount = params.risBufferParams.tileSize;
        uint numSamples = sampleParams.numLocalLightSamples;
        
        // if enabled, we get power importance sampling (or use ReGIR), otherwise it's uniform
        bool useRisBuffer = (params.localLightParams.enableLocalLightImportanceSampling != 0) && (workingContext.ptConsts.NEELocalType>0);
        
        if (workingContext.ptConsts.NEELocalType==2)    // ReGIR
        {
            float3 cellJitter = float3(
                sampleNext1D(rng),
                sampleNext1D(rng),
                sampleNext1D(rng));
            cellJitter -= 0.5;

            float3 samplingPos = RAB_GetSurfaceWorldPos(surface);
            float jitterScale = RTXDI_ReGIR_GetJitterScale(params, samplingPos);
            samplingPos += cellJitter * jitterScale;

            uint cellIndex = RTXDI_ReGIR_WorldPosToCellIndex(params, samplingPos);            
            if (cellIndex >= 0)
            {
                uint cellBase = uint(cellIndex) * params.regirCommon.lightsPerCell;
                risBufferBase = cellBase + params.regirCommon.risBufferOffset;
                risBufferCount =  params.regirCommon.lightsPerCell;
                numSamples = sampleParams.numRegirSamples;
                useRisBuffer = true;
            }
        }

        reservoir = RTXDI_MINI_SampleLocalLightsInternal(rng, surface, sampleParams, params.localLightParams,
            useRisBuffer, risBufferBase, risBufferCount, workingContext, o_selectedSample);

        return reservoir;
    }
    
    bool RTXDI_MINI_SampleLocalLightsFromWorldSpace(
	    inout SampleGenerator rng,
	    ShadingData sd,
	    ActiveBSDF bsdf,
	    const float viewDepth,
	    const uint planeHash,
        const WorkingContext workingContext,
	    out PathTracer::PathLightSample ls
    )
    {
        RTXDI_SampleParameters sampleParams = RTXDI_InitSampleParameters(
		    g_RtxdiBridgeConst.reStirDI.numRegirBuildSamples,
		    workingContext.ptConsts.NEELocalCandidateSamples,
		    0, // infinite light samples
		    0, // environment map samples
		    0,
		    0,
		    0.001f);

	    // Create the surface
	    // We currently only need the shading data bsdf for the surface
        RAB_Surface surface = PathTracerSurfaceData::create(
		    sd.mtl,
		    sd.T,
		    sd.B,
		    sd.N,
		    sd.V,
		    sd.posW,
		    sd.faceN,
		    sd.frontFacing,

		    viewDepth,
		    planeHash,

		    bsdf.data);

	    // Sample light from world space light grid
        RAB_LightSample lightSample = RAB_EmptyLightSample();
        RTXDI_Reservoir reservoir = RTXDI_EmptyReservoir();

        reservoir = RTXDI_MINI_SampleLocalLightsAllVariants(
		    rng,
		    surface,
		    sampleParams,
		    g_RtxdiBridgeConst.runtimeParams,
            workingContext,
		    lightSample);

        if (!RTXDI_IsValidReservoir(reservoir) || lightSample.solidAnglePdf <= 0)
            return false;

	    // Adjust the radiance and pdf 
        ls.Pdf = lightSample.solidAnglePdf / RTXDI_GetReservoirInvPdf(reservoir);
        ls.Li = (lightSample.radiance / ls.Pdf);

        // No more baking in of the offset in lighting code!
        float3 toLight = lightSample.position - surface.GetPosW();
        ls.Distance = length(toLight);
        ls.Direction = toLight / ls.Distance;
	
        return true;
    }

}

#endif // __PATH_TRACER_NEE_LOCAL_HLSLI__
