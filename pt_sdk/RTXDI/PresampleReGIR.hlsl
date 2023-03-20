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

[numthreads(256, 1, 1)]
void main(uint GlobalIndex : SV_DispatchThreadID)
{
    const RTXDI_ResamplingRuntimeParameters params = g_RtxdiBridgeConst.runtimeParams;
    
    RAB_RandomSamplerState rng = RAB_InitRandomSampler(uint2(GlobalIndex & 0xfff, GlobalIndex >> 12), 1);
    RAB_RandomSamplerState coherentRng = RAB_InitRandomSampler(uint2(GlobalIndex >> 8, 0), 1);

    RTXDI_PresampleLocalLightsForReGIR(rng, coherentRng, GlobalIndex, 
        g_RtxdiBridgeConst.numRegirBuildSamples, params);
}