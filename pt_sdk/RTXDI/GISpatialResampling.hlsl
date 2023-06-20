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

#define NON_PATH_TRACING_PASS 1

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
        RAB_RandomSamplerState rng = RAB_InitRandomSampler(pixelPosition, 6);

        RTXDI_GIReservoir inputReservoir = RTXDI_LoadGIReservoir(runtimeParams, pixelPosition,
            g_RtxdiBridgeConst.reStirGI.spatialInputBufferIndex);

        RTXDI_GISpatialResamplingParameters sparams;
        sparams.sourceBufferIndex = g_RtxdiBridgeConst.reStirGI.spatialInputBufferIndex;
        sparams.depthThreshold = g_RtxdiBridgeConst.reStirGI.spatialDepthThreshold;
        sparams.normalThreshold = g_RtxdiBridgeConst.reStirGI.spatialNormalThreshold;
        sparams.numSamples = g_RtxdiBridgeConst.reStirGI.numSpatialSamples;
        sparams.samplingRadius = g_RtxdiBridgeConst.reStirGI.spatialSamplingRadius;
        sparams.biasCorrectionMode = g_RtxdiBridgeConst.reStirGI.spatialBiasCorrection;
        
        resultReservoir = RTXDI_GISpatialResampling(pixelPosition, currentSurface, inputReservoir,
            rng, sparams, runtimeParams);
    }

    RTXDI_StoreGIReservoir(resultReservoir, runtimeParams, pixelPosition,
        g_RtxdiBridgeConst.reStirGI.spatialOutputBufferIndex);
}