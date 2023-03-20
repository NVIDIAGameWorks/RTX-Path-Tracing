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

#include "../RTXDI/RtxdiApplicationBridge.hlsli"

#include "../../external/RTXDI/rtxdi-sdk/include/rtxdi/GIResamplingFunctions.hlsli"

static const float kMaxBrdfValue = 1e4;
static const float kMISRoughness = 0.2;

float GetMISWeight(float3 roughBrdf, float3 trueBrdf)
{
    roughBrdf = clamp(roughBrdf, 1e-4, kMaxBrdfValue);
    trueBrdf = clamp(trueBrdf, 0, kMaxBrdfValue);

    const float initWeight = saturate(calcLuminance(trueBrdf) / calcLuminance(trueBrdf + roughBrdf));
    return initWeight * initWeight * initWeight;
}

[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 GlobalIndex : SV_DispatchThreadID, uint2 LocalIndex : SV_GroupThreadID, uint2 GroupIdx : SV_GroupID)
{
    const RTXDI_ResamplingRuntimeParameters runtimeParams = g_RtxdiBridgeConst.runtimeParams;

    uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(GlobalIndex, runtimeParams);

    RAB_Surface surface = RAB_GetGBufferSurface(pixelPosition, false);

    if (!RAB_IsSurfaceValid(surface))
        return;

    RTXDI_GIReservoir finalReservoir = RTXDI_LoadGIReservoir(runtimeParams, pixelPosition,
        g_RtxdiBridgeConst.finalShadingReservoir);

    if (!RTXDI_IsValidGIReservoir(finalReservoir))
        return;

    const float4 secondaryPositionNormal = u_SecondarySurfacePositionNormal[pixelPosition];
    RTXDI_GIReservoir initialReservoir = RTXDI_MakeGIReservoir(
        secondaryPositionNormal.xyz,
        octToNdirUnorm32(asuint(secondaryPositionNormal.w)),
        u_SecondarySurfaceRadiance[pixelPosition].xyz,
        /* samplePdf = */ 1.0);


    float3 L = finalReservoir.position - surface.sd.posW;
    // Calculate hitDistance here in case visibility is not enabled
    float hitDistance = length(L);
    L /= hitDistance;

    float3 finalRadiance = finalReservoir.radiance * finalReservoir.weightSum;

    if (g_RtxdiBridgeConst.enableFinalVisibility)
    {
        // TODO: support partial visibility... if that is applicable with this material model.
        const RayDesc ray = setupVisibilityRay(surface, finalReservoir.position, g_RtxdiBridgeConst.rayEpsilon);

        bool visible = GetFinalVisibility(SceneBVH, ray);

        if (!visible)
            finalRadiance = 0;
    }
    
    SampleGenerator sg = (SampleGenerator)0; // Needed for bsdf.eval but not really used there

    float3 diffuseBrdf, specularBrdf;
    surface.bsdf.eval(surface.sd, L, sg, diffuseBrdf, specularBrdf);
    
    float3 diffuseContribution, specularContribution;

    if (g_RtxdiBridgeConst.enableFinalMIS)
    {
        float3 L0 = initialReservoir.position - surface.sd.posW;
        float hitDistance0 = length(L0);
        L0 /= hitDistance0;

        float3 diffuseBrdf0, specularBrdf0;
        surface.bsdf.eval(surface.sd, L0, sg, diffuseBrdf0, specularBrdf0);

        StandardBSDF roughBsdf = surface.bsdf;
        roughBsdf.data.roughness = max(roughBsdf.data.roughness, kMISRoughness);

        float3 roughDiffuseBrdf, roughSpecularBrdf;
        float3 roughDiffuseBrdf0, roughSpecularBrdf0;
        roughBsdf.eval(surface.sd, L, sg, roughDiffuseBrdf, roughSpecularBrdf);
        roughBsdf.eval(surface.sd, L0, sg, roughDiffuseBrdf0, roughSpecularBrdf0);

        const float finalWeight = 1.0 - GetMISWeight(roughDiffuseBrdf + roughSpecularBrdf, diffuseBrdf + specularBrdf);
        const float initialWeight = GetMISWeight(roughDiffuseBrdf0 + roughSpecularBrdf0, diffuseBrdf0 + specularBrdf0);

        const float3 initialRadiance = initialReservoir.radiance * initialReservoir.weightSum;

        diffuseContribution = diffuseBrdf * finalRadiance * finalWeight
                            + diffuseBrdf0 * initialRadiance * initialWeight;

        specularContribution = specularBrdf * finalRadiance * finalWeight
                             + specularBrdf0 * initialRadiance * initialWeight;

        hitDistance = hitDistance * finalWeight
                    + hitDistance0 * initialWeight;
    }
    else
    {
        diffuseContribution = diffuseBrdf * finalRadiance;
        specularContribution = specularBrdf * finalRadiance;
    }

    if (any(isinf(diffuseContribution)) || any(isnan(diffuseContribution)) ||
        any(isinf(specularContribution)) || any(isnan(specularContribution)))
    {
        diffuseContribution = 0;
        specularContribution = 0;
    }

    StablePlanesContext stablePlanes = StablePlanesContext::make(GlobalIndex, u_StablePlanesHeader, u_StablePlanesBuffer, u_PrevStablePlanesHeader, u_PrevStablePlanesBuffer, u_StableRadiance, u_SecondarySurfaceRadiance, g_Const.ptConsts);

    uint dominantStablePlaneIndex = stablePlanes.LoadDominantIndex();
    uint address = stablePlanes.PixelToAddress(GlobalIndex, dominantStablePlaneIndex);

    if (g_Const.ptConsts.denoisingEnabled)
    {
        float4 denoiserDiffRadianceHitDist;
        float4 denoiserSpecRadianceHitDist;
        UnpackTwoFp32ToFp16(u_StablePlanesBuffer[address].DenoiserPackedRadianceHitDist, denoiserDiffRadianceHitDist, denoiserSpecRadianceHitDist);

        denoiserDiffRadianceHitDist = StablePlaneCombineWithHitTCompensation(denoiserDiffRadianceHitDist, diffuseContribution, hitDistance);
        denoiserSpecRadianceHitDist = StablePlaneCombineWithHitTCompensation(denoiserSpecRadianceHitDist, specularContribution, hitDistance);

        u_StablePlanesBuffer[address].DenoiserPackedRadianceHitDist = PackTwoFp32ToFp16(denoiserDiffRadianceHitDist, denoiserSpecRadianceHitDist);
    }
    else
    {
        u_Output[GlobalIndex] += float4(diffuseContribution + specularContribution, 0);
    }


    DebugContext debug;
    debug.Init( GlobalIndex, 0, g_Const.debug, u_FeedbackBuffer, u_DebugLinesBuffer, u_DebugDeltaPathTree, u_DeltaPathSearchStack, u_DebugVizOutput );

    switch(g_Const.debug.debugViewType)
    {
    case (int)DebugViewType::SecondarySurfacePosition:
        debug.DrawDebugViz(GlobalIndex, float4(abs(finalReservoir.position) * 0.1, 1.0));
        break;
    case (int)DebugViewType::SecondarySurfaceRadiance:
        debug.DrawDebugViz(GlobalIndex, float4(finalReservoir.radiance, 1.0));
        break;
    case (int)DebugViewType::ReSTIRGIOutput:
        debug.DrawDebugViz(GlobalIndex, float4(diffuseContribution + specularContribution, 1.0));
        break;
    }
}
