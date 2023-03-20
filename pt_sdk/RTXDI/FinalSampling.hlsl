/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "ShaderParameters.h"
#include "RtxdiApplicationBridge.hlsli"
#include "../../external/RTXDI/rtxdi-sdk/include/rtxdi/ResamplingFunctions.hlsli"

// this is for debugging viz
//RWTexture2D<float4>                     u_DebugVizOutput    : register(u50);

// Get the final sample for the given pixel computed using RTXDI.
bool getFinalSample(uint2 pixel, out float3 Li, out float3 dir, out float distance)
{
    RTXDI_ResamplingRuntimeParameters runtimeParams = g_RtxdiBridgeConst.runtimeParams;
    RAB_Surface surface;
    RAB_LightSample lightSample = RAB_EmptyLightSample();
    
    // Get the reservoir
    uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(pixel, runtimeParams);
    surface = RAB_GetGBufferSurface(pixelPosition, false);
    RTXDI_Reservoir reservoir = RTXDI_LoadReservoir(runtimeParams, pixel, g_RtxdiBridgeConst.finalShadingReservoir);

    // Abort if we don't have a valid surface
    if (!RAB_IsSurfaceValid(surface) || !RTXDI_IsValidReservoir(reservoir)) return false;

    // Load the selected light and the specific light sample on it
    uint lightIdx = RTXDI_GetReservoirLightIndex(reservoir);
    float2 lightUV = RTXDI_GetReservoirSampleUV(reservoir);
    RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIdx, false);
    lightSample = RAB_SamplePolymorphicLight(lightInfo, surface, lightUV);

    // Check the light is visible to the surface 
    if (g_RtxdiBridgeConst.enableFinalVisibility)
    {
        const RayDesc ray = setupVisibilityRay(surface, lightSample, g_RtxdiBridgeConst.rayEpsilon);

        if (!GetFinalVisibility(SceneBVH, ray))
        {
            RTXDI_StoreVisibilityInReservoir(reservoir, 0, true);
            RTXDI_StoreReservoir(reservoir, runtimeParams, pixel, g_RtxdiBridgeConst.finalShadingReservoir);
            return false;
        }
    }

    // Compute incident radience
    ComputeIncidentRadience(surface, RTXDI_GetReservoirInvPdf(reservoir), lightSample, Li, dir, distance);
    
    return true;
}

[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 dispatchThreadID : SV_DispatchThreadID)
{
	const uint2 pixelPos = dispatchThreadID.xy;
	if (any(pixelPos > g_RtxdiBridgeConst.frameDim)) return;

	float3 dir = float3(0,0,0);
	float distance = 0;
	float3 Li = 0;

	bool isValid = getFinalSample(pixelPos, Li, dir, distance);

	//Output
	u_RtxdiOutDirectionValid[pixelPos] = float4(dir, isValid ? 1.0 : 0.0);
	u_RtxdiLiDistance[pixelPos] = float4(Li, distance - g_RtxdiBridgeConst.rayEpsilon);
}	