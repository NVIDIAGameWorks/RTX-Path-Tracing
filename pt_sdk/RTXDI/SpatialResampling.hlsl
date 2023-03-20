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

#if USE_RAY_QUERY
[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 GlobalIndex : SV_DispatchThreadID)
#else
[shader("raygeneration")]
void RayGen()
#endif
{
#if !USE_RAY_QUERY
    uint2 GlobalIndex = DispatchRaysIndex().xy;
#endif

    const RTXDI_ResamplingRuntimeParameters runtimeParams = g_RtxdiBridgeConst.runtimeParams;

    uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(GlobalIndex, runtimeParams);

    RAB_RandomSamplerState rng = RAB_InitRandomSampler(pixelPosition, 3);

    RAB_Surface surface = RAB_GetGBufferSurface(pixelPosition, false);

    RTXDI_Reservoir spatialResult = RTXDI_EmptyReservoir();
    
    if (RAB_IsSurfaceValid(surface))
    {
        RTXDI_Reservoir centerSample = RTXDI_LoadReservoir(runtimeParams,
            GlobalIndex, g_RtxdiBridgeConst.spatialInputBufferIndex);

        RTXDI_SpatialResamplingParameters sparams;
        sparams.sourceBufferIndex = g_RtxdiBridgeConst.spatialInputBufferIndex;
        sparams.numSamples = g_RtxdiBridgeConst.numSpatialSamples;
        sparams.numDisocclusionBoostSamples = g_RtxdiBridgeConst.numDisocclusionBoostSamples;
        sparams.targetHistoryLength = g_RtxdiBridgeConst.maxHistoryLength;
        sparams.biasCorrectionMode = g_RtxdiBridgeConst.spatialBiasCorrection;
        sparams.samplingRadius =  g_RtxdiBridgeConst.spatialSamplingRadius;
        sparams.depthThreshold =  g_RtxdiBridgeConst.spatialDepthThreshold;
        sparams.normalThreshold = g_RtxdiBridgeConst.spatialNormalThreshold;

        RAB_LightSample lightSample = (RAB_LightSample)0;
        spatialResult = RTXDI_SpatialResampling(pixelPosition, surface, centerSample, 
             rng, sparams, runtimeParams, lightSample);
    }

    RTXDI_StoreReservoir(spatialResult, runtimeParams, GlobalIndex, g_RtxdiBridgeConst.spatialOutputBufferIndex);
}