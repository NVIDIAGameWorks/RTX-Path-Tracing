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

#include "RtxdiApplicationBridge.hlsli"

#include "../../external/RTXDI/rtxdi-sdk/include/rtxdi/ResamplingFunctions.hlsli"

#define ENABLE_DEBUG 0

#if ENABLE_DEBUG
#include "../PathTracer/ShaderDebug.hlsli"
#endif

#if USE_RAY_QUERY
[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 dispatchThreadID : SV_DispatchThreadID)
#else
[shader("raygeneration")]
void RayGen()
#endif
{
#if !USE_RAY_QUERY
    uint2 dispatchThreadID = DispatchRaysIndex().xy;
#endif

    const RTXDI_ResamplingRuntimeParameters runtimeParams = g_RtxdiBridgeConst.runtimeParams;

    uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(dispatchThreadID, runtimeParams);

    RAB_RandomSamplerState rng = RAB_InitRandomSampler(pixelPosition, 1);
    RAB_RandomSamplerState tileRng = RAB_InitRandomSampler(pixelPosition / RTXDI_TILE_SIZE_IN_PIXELS, 1);

    
    RAB_Surface surface = RAB_GetGBufferSurface(pixelPosition, false);
    if (!RAB_IsSurfaceValid(surface))
        return;
  
    RTXDI_SampleParameters sampleParams = RTXDI_InitSampleParameters(
         g_RtxdiBridgeConst.numPrimaryRegirSamples,
         g_RtxdiBridgeConst.numPrimaryLocalLightSamples,
         g_RtxdiBridgeConst.numPrimaryInfiniteLightSamples,
         g_RtxdiBridgeConst.numPrimaryEnvironmentSamples,
         g_RtxdiBridgeConst.numPrimaryBrdfSamples,
         g_RtxdiBridgeConst.brdfCutoff,
        0.001f);

    RAB_LightSample lightSample;
    RTXDI_Reservoir reservoir = RTXDI_SampleLightsForSurface(rng, tileRng, surface, sampleParams, runtimeParams, lightSample);

#if ENABLE_DEBUG_RTXDI_VIZUALISATION
    DebugContext debug;
    debug.Init(pixelPosition, 0, g_Const.debug, u_FeedbackBuffer, u_DebugLinesBuffer, u_DebugDeltaPathTree, u_DeltaPathSearchStack, u_DebugVizOutput);
#endif
    if (g_RtxdiBridgeConst.enableInitialVisibility && RTXDI_IsValidReservoir(reservoir))
    {
        if (!RAB_GetConservativeVisibility(surface, lightSample))
        {
            RTXDI_StoreVisibilityInReservoir(reservoir, 0, true);
#if ENABLE_DEBUG_RTXDI_VIZUALISATION
            if (debug.IsDebugPixel())
            {
               debug.DrawLine(lightSample.position, RAB_GetNewRayOrigin(surface), float3(1, 0, 0), float3(1, 0, 0));
            }
#endif
        }
#if ENABLE_DEBUG_RTXDI_VIZUALISATION
        else //debug only
        {
            if (debug.IsDebugPixel())
            {
                debug.DrawLine(lightSample.position, RAB_GetNewRayOrigin(surface), float3(0, 1, 0), float3(0, 1, 0));
            }
        }
#endif
    }
    RTXDI_StoreReservoir(reservoir, runtimeParams, dispatchThreadID, g_RtxdiBridgeConst.initialOutputBufferIndex);
}