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
bool getFinalSample(const uint2 reservoirPos, const RAB_Surface surface, out float3 Li, out float3 dir, out float distance)
{
    RTXDI_ResamplingRuntimeParameters runtimeParams = g_RtxdiBridgeConst.runtimeParams;
    
    RAB_LightSample lightSample = RAB_EmptyLightSample();
    
    // Get the reservoir
    RTXDI_Reservoir reservoir = RTXDI_LoadReservoir(runtimeParams, reservoirPos, g_RtxdiBridgeConst.reStirDI.finalShadingReservoir);

    Li = 0.0.xxx;
    dir = 0.0.xxx;
    distance = 0.0;

    // Abort if we don't have a valid surface
    if (!RAB_IsSurfaceValid(surface) || !RTXDI_IsValidReservoir(reservoir)) return false;

    // Load the selected light and the specific light sample on it
    uint lightIdx = RTXDI_GetReservoirLightIndex(reservoir);
    float2 lightUV = RTXDI_GetReservoirSampleUV(reservoir);
    RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIdx, false);
    lightSample = RAB_SamplePolymorphicLight(lightInfo, surface, lightUV);

    // Check the light is visible to the surface 
    if (g_RtxdiBridgeConst.reStirDI.enableFinalVisibility)
    {
        const RayDesc ray = setupVisibilityRay(surface, lightSample, g_RtxdiBridgeConst.rayEpsilon);

        if (!GetFinalVisibility(SceneBVH, ray))
        {
            RTXDI_StoreVisibilityInReservoir(reservoir, 0, true);
            RTXDI_StoreReservoir(reservoir, runtimeParams, reservoirPos, g_RtxdiBridgeConst.reStirDI.finalShadingReservoir);
            return false;
        }
    }

    // Compute incident radience
    ComputeIncidentRadience(surface, RTXDI_GetReservoirInvPdf(reservoir), lightSample, Li, dir, distance);
    
    return true;
}

bool ReSTIRDIFinalContribution(const uint2 reservoirPos, const uint2 pixelPos, const RAB_Surface surface, out float3 diffuseContribution, out float3 specularContribution, out float hitDistance)
{
    diffuseContribution = 0.0.xxx;
    specularContribution = 0.0.xxx;
    hitDistance = 0.0;

    PathTracer::PathLightSample ls = PathTracer::PathLightSample::make();
    bool isValid = getFinalSample(reservoirPos, surface, ls.Li, ls.Direction, ls.Distance);
    if (!isValid)
        return false;

    // Apply sample shading
#if PTSDK_DIFFUSE_SPECULAR_SPLIT
    float3 bsdfThpDiff, bsdfThpSpec; 
    surface.Eval(ls.Direction, bsdfThpDiff, bsdfThpSpec);
    float3 bsdfThp = bsdfThpDiff + bsdfThpSpec;
#else
#error denoiser requires specular split currently but easy to switch back
    float3 bsdfThp = surface.Eval(ls.dir);
#endif

    float3 pathThp = Unpack_R11G11B10_FLOAT(u_Throughput[pixelPos]);

    // Compute final radiance reaching the camera (there's no firefly filter for ReSTIR here unfortunately)
    diffuseContribution  = bsdfThpDiff * ls.Li * pathThp;
    specularContribution = bsdfThpSpec * ls.Li * pathThp;

#if 0 // applying firefly filter here doesn't actually help so let's save on few instructions
    diffuseRadiance = FireflyFilter( diffuseRadiance, g_Const.ptConsts.fireflyFilterThreshold, 1.0 );
    specularRadiance = FireflyFilter( specularRadiance, g_Const.ptConsts.fireflyFilterThreshold, 1.0 );
#endif

    hitDistance = ls.Distance;

    // useful for debugging!
    DebugContext debug;
    debug.Init(pixelPos, g_Const.debug, u_FeedbackBuffer, u_DebugLinesBuffer, u_DebugDeltaPathTree, u_DeltaPathSearchStack, u_DebugVizOutput);

    switch (g_Const.debug.debugViewType)
    {
    case (int)DebugViewType::ReSTIRDIFinalOutput:
        debug.DrawDebugViz(pixelPos, float4(/*ls.Li*/diffuseContribution + specularContribution, 1));
        break;
    }
    
    return any(diffuseContribution+specularContribution)>0;
}

#ifndef USE_AS_INCLUDE
[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 dispatchThreadID : SV_DispatchThreadID)
{
    const uint2 reservoirPos = dispatchThreadID.xy;
	const uint2 pixelPos = RTXDI_ReservoirPosToPixelPos(dispatchThreadID.xy, g_RtxdiBridgeConst.runtimeParams);

    RAB_Surface surface = RAB_GetGBufferSurface(pixelPos, false);

    if (!RAB_IsSurfaceValid(surface))
        return;

    float3 diffuseContribution, specularContribution;
    float hitDistance;

    if (ReSTIRDIFinalContribution(reservoirPos, pixelPos, surface, diffuseContribution, specularContribution, hitDistance))
    {
        if (g_Const.ptConsts.denoisingEnabled)
        {
            StablePlanesContext stablePlanes = StablePlanesContext::make(pixelPos, u_StablePlanesHeader, u_StablePlanesBuffer, u_StableRadiance, u_SecondarySurfaceRadiance, g_Const.ptConsts);

            uint dominantStablePlaneIndex = stablePlanes.LoadDominantIndexCenter();
            uint address = stablePlanes.PixelToAddress(pixelPos, dominantStablePlaneIndex);

            float4 denoiserDiffRadianceHitDist;
            float4 denoiserSpecRadianceHitDist;
            UnpackTwoFp32ToFp16(u_StablePlanesBuffer[address].DenoiserPackedRadianceHitDist, denoiserDiffRadianceHitDist, denoiserSpecRadianceHitDist);

            denoiserDiffRadianceHitDist = StablePlaneCombineWithHitTCompensation(denoiserDiffRadianceHitDist, diffuseContribution, hitDistance);
            denoiserSpecRadianceHitDist = StablePlaneCombineWithHitTCompensation(denoiserSpecRadianceHitDist, specularContribution, hitDistance);

            u_StablePlanesBuffer[address].DenoiserPackedRadianceHitDist = PackTwoFp32ToFp16(denoiserDiffRadianceHitDist, denoiserSpecRadianceHitDist);
        }
        else
        {
            u_Output[pixelPos] += float4(diffuseContribution + specularContribution, 0);
        }
    }
}	
#endif // #ifndef USE_AS_INCLUDE