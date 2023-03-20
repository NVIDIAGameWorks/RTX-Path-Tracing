/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma pack_matrix(row_major)

#define RAB_FOR_RESTIR_GI_PASS
#define NON_PATH_TRACING_PASS 1

// Only enable the boiling filter for RayQuery (compute shader) mode because it requires shared memory
#if USE_RAY_QUERY
#define RTXDI_ENABLE_BOILING_FILTER
#define RTXDI_BOILING_FILTER_GROUP_SIZE RTXDI_SCREEN_SPACE_GROUP_SIZE
#endif

#include "../RTXDI/RtxdiApplicationBridge.hlsli"

#include "../../external/RTXDI/rtxdi-sdk/include/rtxdi/GIResamplingFunctions.hlsli"

[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 GlobalIndex : SV_DispatchThreadID, uint2 LocalIndex : SV_GroupThreadID, uint2 GroupIdx : SV_GroupID)
{
    const RTXDI_ResamplingRuntimeParameters runtimeParams = g_RtxdiBridgeConst.runtimeParams;

    uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(GlobalIndex, runtimeParams);

    RAB_Surface currentSurface = RAB_GetGBufferSurface(pixelPosition, false);

    RTXDI_GIReservoir resultReservoir = RTXDI_EmptyGIReservoir();
    
    if (RAB_IsSurfaceValid(currentSurface))
    {
        const float4 secondaryPositionNormal = u_SecondarySurfacePositionNormal[pixelPosition];
        RTXDI_GIReservoir initialSample = RTXDI_MakeGIReservoir(
            secondaryPositionNormal.xyz,
            octToNdirUnorm32(asuint(secondaryPositionNormal.w)),
            u_SecondarySurfaceRadiance[pixelPosition].xyz,
            /* samplePdf = */ 1.0);
        
        if (g_RtxdiBridgeConst.enableTemporalResampling)
        {
            RAB_RandomSamplerState rng = RAB_InitRandomSampler(pixelPosition, 5);

            float3 motionVector = u_MotionVectors[pixelPosition].xyz;

            motionVector = convertMotionVectorToPixelSpace(g_Const.view, g_Const.previousView, pixelPosition, motionVector);

            RTXDI_GITemporalResamplingParameters tparams;
            tparams.screenSpaceMotion = motionVector;
            tparams.sourceBufferIndex = g_RtxdiBridgeConst.temporalInputBufferIndex;
            tparams.maxHistoryLength = g_RtxdiBridgeConst.maxHistoryLength;
            tparams.maxReservoirAge = g_RtxdiBridgeConst.maxReservoirAge;
            tparams.depthThreshold = g_RtxdiBridgeConst.temporalDepthThreshold;
            tparams.normalThreshold = g_RtxdiBridgeConst.temporalNormalThreshold;
            tparams.enablePermutationSampling = g_RtxdiBridgeConst.enablePermutationSampling;
            tparams.enableFallbackSampling = g_RtxdiBridgeConst.enableFallbackSampling;
            
            resultReservoir = RTXDI_GITemporalResampling(pixelPosition, currentSurface, initialSample,
                rng, tparams, runtimeParams);
        }
        else
        {
            resultReservoir = initialSample;
        }
    }

    // Sometimes, something in the renderer produces an invalid output, which then propagates
    // through resampling and eventually covers the screen. That shouldn't happen, but if it does,
    // the following filter can be enabled to stop the propagation.
    static const bool c_SanitizeOutput = false;
    if (c_SanitizeOutput)
    {
        if (any(isinf(resultReservoir.position)) || any(isnan(resultReservoir.position)) ||
            any(isinf(resultReservoir.radiance)) || any(isnan(resultReservoir.radiance)) ||
            any(isinf(resultReservoir.normal)) || any(isnan(resultReservoir.normal)) ||
            isinf(resultReservoir.weightSum) || isnan(resultReservoir.weightSum))
        {
            resultReservoir = RTXDI_EmptyGIReservoir();
        }
    }

#ifdef RTXDI_ENABLE_BOILING_FILTER
    if  (g_RtxdiBridgeConst.boilingFilterStrength > 0)
    {
        RTXDI_GIBoilingFilter(LocalIndex, g_RtxdiBridgeConst.boilingFilterStrength, runtimeParams, resultReservoir);
    }
#endif

    RTXDI_StoreGIReservoir(resultReservoir, runtimeParams, pixelPosition,
        g_RtxdiBridgeConst.temporalOutputBufferIndex);
}