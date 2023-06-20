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

// Only enable the boiling filter for RayQuery (compute shader) mode because it requires shared memory
#if USE_RAY_QUERY
#define RTXDI_ENABLE_BOILING_FILTER
#define RTXDI_BOILING_FILTER_GROUP_SIZE RTXDI_SCREEN_SPACE_GROUP_SIZE
#endif

#include "RtxdiApplicationBridge.hlsli"

#include "../../external/RTXDI/rtxdi-sdk/include/rtxdi/ResamplingFunctions.hlsli"

#if USE_RAY_QUERY
[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)] 
void main(uint2 GlobalIndex : SV_DispatchThreadID, uint2 LocalIndex : SV_GroupThreadID, uint2 GroupIdx : SV_GroupID)
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

    RAB_RandomSamplerState rng = RAB_InitRandomSampler(pixelPosition, 2);

    RAB_Surface surface = RAB_GetGBufferSurface(pixelPosition, false);

    //Adds a small random offet to the motion vector. Do we need this? 
    // We current do not have a method to check for complex surfaces 
    //bool usePermutationSampling = false;
    //if (g_ResamplingConst.enablePermutationSampling)
    //{
    //    // Permutation sampling makes more noise on thin, high-detail objects.
    //    usePermutationSampling = !IsComplexSurface(pixelPosition, surface);
    //}

    RTXDI_Reservoir temporalResult = RTXDI_EmptyReservoir();
    int2 temporalSamplePixelPos = -1;
    RAB_LightSample selectedLightSample = (RAB_LightSample)0;

    if (RAB_IsSurfaceValid(surface))
    {
        RTXDI_Reservoir curSample = RTXDI_LoadReservoir(runtimeParams,
            GlobalIndex, g_RtxdiBridgeConst.reStirDI.initialOutputBufferIndex);

        float3 motionVector = u_MotionVectors[pixelPosition].xyz;

        motionVector = convertMotionVectorToPixelSpace(g_Const.view, g_Const.previousView, pixelPosition, motionVector);

        RTXDI_TemporalResamplingParameters tparams;
        tparams.screenSpaceMotion = motionVector;
        tparams.sourceBufferIndex = g_RtxdiBridgeConst.reStirDI.temporalInputBufferIndex;
        tparams.maxHistoryLength = g_RtxdiBridgeConst.reStirDI.maxHistoryLength;
        tparams.biasCorrectionMode = g_RtxdiBridgeConst.reStirDI.temporalBiasCorrection;
        tparams.depthThreshold = g_RtxdiBridgeConst.reStirDI.temporalDepthThreshold;
        tparams.normalThreshold = g_RtxdiBridgeConst.reStirDI.temporalNormalThreshold;
        tparams.enableVisibilityShortcut = g_RtxdiBridgeConst.reStirDI.discardInvisibleSamples;
        tparams.enablePermutationSampling = g_RtxdiBridgeConst.reStirDI.enablePermutationSampling;
        
        temporalResult = RTXDI_TemporalResampling(pixelPosition, surface, curSample,
            rng, tparams, runtimeParams, temporalSamplePixelPos, selectedLightSample);
    }

#ifdef RTXDI_ENABLE_BOILING_FILTER
    if  (g_RtxdiBridgeConst.reStirDI.boilingFilterStrength > 0)
    {
        RTXDI_BoilingFilter(LocalIndex, g_RtxdiBridgeConst.reStirDI.boilingFilterStrength, runtimeParams, temporalResult);
    }
#endif
    
    RTXDI_StoreReservoir(temporalResult, runtimeParams, GlobalIndex, g_RtxdiBridgeConst.reStirDI.temporalOutputBufferIndex);
}