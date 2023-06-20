/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __POST_PROCESS_HLSL__
#define __POST_PROCESS_HLSL__

#define VIEWZ_SKY_MARKER        FLT_MAX             // for 16bit use HLF_MAX but make sure it's bigger than commonSettings.denoisingRange in NRD!

#if defined(BLEND_DEBUG_BUFFER)
Texture2D<float4>                       t_DebugVizOutput    : register(t5);
float4 main( in float4 pos : SV_Position, in float2 uv : UV ) : SV_Target0
{
    return t_DebugVizOutput[uint2(pos.xy)].rgba;
}
#endif

//

#if defined(STABLE_PLANES_DEBUG_VIZ)

#include "PathTracerBridgeDonut.hlsli"
#include "PathTracer/PathTracer.hlsli"
#include "PathTracer/Utils.hlsli"

[numthreads(NUM_COMPUTE_THREADS_PER_DIM, NUM_COMPUTE_THREADS_PER_DIM, 1)]
void main( uint3 dispatchThreadID : SV_DispatchThreadID )
{
    const uint2 pixelPos = dispatchThreadID.xy;
    if( any(pixelPos >= uint2(g_Const.ptConsts.imageWidth, g_Const.ptConsts.imageHeight) ) )
        return;
    // u_DebugVizOutput[pixelPos] = float4(1,0,1,1);
    // return;

    uint sampleIndex = Bridge::getSampleIndex();
    DebugContext debug; debug.Init( pixelPos, sampleIndex, g_Const.debug, u_FeedbackBuffer, u_DebugLinesBuffer, u_DebugDeltaPathTree, u_DeltaPathSearchStack, u_DebugVizOutput );
    PathState path = PathTracer::emptyPathInitialize( pixelPos, sampleIndex, g_Const.ptConsts.camera.pixelConeSpreadAngle );
    const Ray cameraRay = Bridge::computeCameraRay( pixelPos );
    StablePlanesContext stablePlanes = StablePlanesContext::make(pixelPos, u_StablePlanesHeader, u_StablePlanesBuffer, u_StableRadiance, u_SecondarySurfaceRadiance, g_Const.ptConsts);

#if ENABLE_DEBUG_VIZUALISATION
    debug.StablePlanesDebugViz(stablePlanes);
#endif
}

#endif

//

#if defined(DENOISER_PREPARE_INPUTS)

#include "PathTracerBridgeDonut.hlsli"
#include "PathTracer/PathTracer.hlsli"
#include "NRD/DenoiserNRD.hlsli"

float ComputeNeighbourDisocclusionRelaxation(const StablePlanesContext stablePlanes, const int2 pixelPos, const int2 imageSize, const uint stablePlaneIndex, const float3 rayDirC, const int2 offset)
{
    const float kEdge = 0.02;

    uint2 pixelPosN = clamp( int2(pixelPos)+offset, 0.xx, imageSize );
    uint bidN = stablePlanes.GetBranchID(pixelPosN, stablePlaneIndex);
    if( bidN == cStablePlaneInvalidBranchID )
        return kEdge;
    uint spAddressN = stablePlanes.PixelToAddress( pixelPosN, stablePlaneIndex ); 
    float3 rayDirN = normalize( stablePlanes.StablePlanesUAV[spAddressN].RayDirSceneLength.xyz );
    return 1-dot(rayDirC, rayDirN);
}

float ComputeDisocclusionRelaxation(const StablePlanesContext stablePlanes, const uint2 pixelPos, const uint stablePlaneIndex, const uint spBranchID, const StablePlane sp)
{
    float disocclusionRelax = 0;

    const int2 imageSize = int2(g_Const.ptConsts.imageWidth, g_Const.ptConsts.imageHeight);
    const float3 rayDirC = normalize(sp.RayDirSceneLength.xyz);

    disocclusionRelax += ComputeNeighbourDisocclusionRelaxation(stablePlanes, pixelPos, imageSize, stablePlaneIndex, rayDirC, int2(-1, 0));
    disocclusionRelax += ComputeNeighbourDisocclusionRelaxation(stablePlanes, pixelPos, imageSize, stablePlaneIndex, rayDirC, int2( 1, 0));
    disocclusionRelax += ComputeNeighbourDisocclusionRelaxation(stablePlanes, pixelPos, imageSize, stablePlaneIndex, rayDirC, int2( 0,-1));
    disocclusionRelax += ComputeNeighbourDisocclusionRelaxation(stablePlanes, pixelPos, imageSize, stablePlaneIndex, rayDirC, int2( 0, 1));
#if 0 // add diagonals for more precision (at a cost!)
    disocclusionRelax += ComputeNeighbourDisocclusionRelaxation(stablePlanes, pixelPos, imageSize, stablePlaneIndex, rayDirC, int2(-1,-1));
    disocclusionRelax += ComputeNeighbourDisocclusionRelaxation(stablePlanes, pixelPos, imageSize, stablePlaneIndex, rayDirC, int2( 1,-1));
    disocclusionRelax += ComputeNeighbourDisocclusionRelaxation(stablePlanes, pixelPos, imageSize, stablePlaneIndex, rayDirC, int2(-1, 1));
    disocclusionRelax += ComputeNeighbourDisocclusionRelaxation(stablePlanes, pixelPos, imageSize, stablePlaneIndex, rayDirC, int2( 1, 1));
    disocclusionRelax *= 0.5;
#endif
    return saturate( (disocclusionRelax-0.00002) * 25 );
}

void NRDProbabilisticSamplingClamp( inout float4 radianceHitT, const float rangeK )
{
    const float kClampMin = g_Const.ptConsts.preExposedGrayLuminance/rangeK;
    const float kClampMax = g_Const.ptConsts.preExposedGrayLuminance*rangeK;

    const float lum = luminance( radianceHitT.xyz );
    if (lum > kClampMax)
        radianceHitT.xyz *= kClampMax / lum;
}



[numthreads(NUM_COMPUTE_THREADS_PER_DIM, NUM_COMPUTE_THREADS_PER_DIM, 1)]
void main( uint3 dispatchThreadID : SV_DispatchThreadID )
{
    const uint stablePlaneIndex = g_MiniConst.params[0];

    const uint2 pixelPos = dispatchThreadID.xy;
    if( any(pixelPos >= uint2(g_Const.ptConsts.imageWidth, g_Const.ptConsts.imageHeight) ) )
        return;

    uint sampleIndex = Bridge::getSampleIndex();
    DebugContext debug; debug.Init( pixelPos, sampleIndex, g_Const.debug, u_FeedbackBuffer, u_DebugLinesBuffer, u_DebugDeltaPathTree, u_DeltaPathSearchStack, u_DebugVizOutput );
    PathState path = PathTracer::emptyPathInitialize( pixelPos, sampleIndex, g_Const.ptConsts.camera.pixelConeSpreadAngle );
    const Ray cameraRay = Bridge::computeCameraRay( pixelPos );
    StablePlanesContext stablePlanes = StablePlanesContext::make(pixelPos, u_StablePlanesHeader, u_StablePlanesBuffer, u_StableRadiance, u_SecondarySurfaceRadiance, g_Const.ptConsts);

    bool hasSurface = false;
    uint spBranchID = stablePlanes.GetBranchIDCenter(stablePlaneIndex);
    if( spBranchID != cStablePlaneInvalidBranchID )
    {
        StablePlane sp = stablePlanes.LoadStablePlane(pixelPos, stablePlaneIndex);

        const HitInfo hit = HitInfo(sp.PackedHitInfo);
        bool hitSurface = hit.isValid() && hit.getType() == HitType::Triangle;
        if( hitSurface ) // skip sky!
        {
            hasSurface = true;
            float3 diffBSDFEstimate, specBSDFEstimate;
            UnpackTwoFp32ToFp16(sp.DenoiserPackedBSDFEstimate, diffBSDFEstimate, specBSDFEstimate);
            //diffBSDFEstimate = 1.xxx; specBSDFEstimate = 1.xxx;

            float3 virtualWorldPos = cameraRay.origin + cameraRay.dir * length(sp.RayDirSceneLength);
            float4 viewPos = mul(float4(/*bridgedData.sd.posW*/virtualWorldPos, 1), g_Const.view.matWorldToView);
            float virtualViewspaceZ = viewPos.z;

            float3 thp; float3 motionVectors;
            UnpackTwoFp32ToFp16(sp.PackedThpAndMVs, thp, motionVectors);

#if 0  // for testing correctness: compute first hit surface motion vector
            {
                float3 virtualWorldPos1 = cameraRay.origin + cameraRay.dir * stablePlanes.LoadFirstHitRayLength(pixelPos);
                motionVectors = Bridge::computeMotionVector(virtualWorldPos1, virtualWorldPos1);
            }
#endif

            // See if possible to get rid of these copies - or compress them better!
            u_DenoiserViewspaceZ[pixelPos]          = virtualViewspaceZ;
            u_DenoiserMotionVectors[pixelPos]       = float4(motionVectors, 0);
            float finalRoughness = sp.DenoiserNormalRoughness.w;         

            float disocclusionRelax = 0.0;
            float aliasingDampen = 0.0;

            float specularSuppressionMul = 1.0; // this applies 
            if (stablePlaneIndex == 0 && g_Const.ptConsts.stablePlanesSuppressPrimaryIndirectSpecularK != 0.0 && g_Const.ptConsts.activeStablePlaneCount > 1 )
            {   // only apply suppression on sp 0, and only if more than 1 stable plane enabled, and only if other stable planes are in use (so they captured some of specular radiance)
                bool shouldSuppress = true;
                for (int i = 1; i < g_Const.ptConsts.activeStablePlaneCount; i++ )
                    shouldSuppress &= stablePlanes.GetBranchIDCenter(i) != cStablePlaneInvalidBranchID;
                // (optional, experimental, for future: also don't apply suppression if rough specular)
                float roughnessModifiedSuppression = g_Const.ptConsts.stablePlanesSuppressPrimaryIndirectSpecularK; // * saturate(1 - (finalRoughness - g_Const.ptConsts.stablePlanesMinRoughness)*5);
                specularSuppressionMul = shouldSuppress?saturate(1-roughnessModifiedSuppression):specularSuppressionMul;
            }

            int vertexIndex = StablePlanesVertexIndexFromBranchID( spBranchID );
            if (vertexIndex > 1)
                disocclusionRelax = ComputeDisocclusionRelaxation(stablePlanes, pixelPos, stablePlaneIndex, spBranchID, sp);
            u_DenoiserDisocclusionThresholdMix[pixelPos] = disocclusionRelax;

            // adjust for thp and map to [0,1]
            u_CombinedHistoryClampRelax[pixelPos] = saturate(u_CombinedHistoryClampRelax[pixelPos] + disocclusionRelax * saturate(luminance(thp)) );
            
            finalRoughness = saturate( finalRoughness + disocclusionRelax );
            
            float4 denoiserDiffRadianceHitDist;
            float4 denoiserSpecRadianceHitDist;
            UnpackTwoFp32ToFp16(sp.DenoiserPackedRadianceHitDist, denoiserDiffRadianceHitDist, denoiserSpecRadianceHitDist);

            float fallthroughToBasePlane = saturate(disocclusionRelax-1.0+g_Const.ptConsts.stablePlanesAntiAliasingFallthrough);
            if (stablePlaneIndex > 0 && fallthroughToBasePlane > 0)
            {
                uint sp0Address = stablePlanes.PixelToAddress( pixelPos, 0 ); 

#if 0 // this will adjust hit length so that the fallthrough is added - but I couldn't notice any quality difference so leaving it out for now since it' not free
                float p0SceneLength = length(stablePlanes.StablePlanesUAV[sp0Address].RayDirSceneLength);
                float addedHitTLength = max( 0, length(sp.RayDirSceneLength) - p0SceneLength );
#else
                float addedHitTLength = 0;
#endif

                float4 currentDiff, currentSpec; 
                UnpackTwoFp32ToFp16(stablePlanes.StablePlanesUAV[sp0Address].DenoiserPackedRadianceHitDist, currentDiff, currentSpec);
                currentDiff.xyzw = StablePlaneCombineWithHitTCompensation(currentDiff, denoiserDiffRadianceHitDist.xyz * fallthroughToBasePlane, denoiserDiffRadianceHitDist.w+addedHitTLength);
                currentSpec.xyzw = StablePlaneCombineWithHitTCompensation(currentSpec, denoiserSpecRadianceHitDist.xyz * fallthroughToBasePlane, denoiserSpecRadianceHitDist.w+addedHitTLength);
                stablePlanes.StablePlanesUAV[sp0Address].DenoiserPackedRadianceHitDist = PackTwoFp32ToFp16(currentDiff, currentSpec);
                denoiserDiffRadianceHitDist.xyz *= (1-fallthroughToBasePlane);
                denoiserSpecRadianceHitDist.xyz *= (1-fallthroughToBasePlane);

#if 0   // debug viz
                u_DebugVizOutput[pixelPos].a = 1;
                if( stablePlaneIndex == 1 ) u_DebugVizOutput[pixelPos].x = fallthroughToBasePlane;
                if( stablePlaneIndex == 2 ) u_DebugVizOutput[pixelPos].y = fallthroughToBasePlane;
#endif
            }

            // demodulate
            denoiserDiffRadianceHitDist.xyz /= diffBSDFEstimate.xyz;
            denoiserSpecRadianceHitDist.xyz /= specBSDFEstimate.xyz;

            // apply suppression if any
            denoiserSpecRadianceHitDist.xyz *= specularSuppressionMul;

            u_DenoiserNormalRoughness[pixelPos]     = NRD_FrontEnd_PackNormalAndRoughness( sp.DenoiserNormalRoughness.xyz, finalRoughness );

            // When using probabilistic sampling and HitDistanceReconstructionMode::AREA_xXxwe have to clamp the inputs to be 
            // within sensible range.
            NRDProbabilisticSamplingClamp( denoiserDiffRadianceHitDist, g_Const.ptConsts.denoiserRadianceClampK*16 );
            NRDProbabilisticSamplingClamp( denoiserSpecRadianceHitDist, lerp(g_Const.ptConsts.denoiserRadianceClampK, g_Const.ptConsts.denoiserRadianceClampK*16, finalRoughness) );
    #if USE_RELAX
            u_DenoiserDiffRadianceHitDist[pixelPos] = RELAX_FrontEnd_PackRadianceAndHitDist( denoiserDiffRadianceHitDist.xyz, denoiserDiffRadianceHitDist.w );
            u_DenoiserSpecRadianceHitDist[pixelPos] = RELAX_FrontEnd_PackRadianceAndHitDist( denoiserSpecRadianceHitDist.xyz, denoiserSpecRadianceHitDist.w );
    #else
            float4 hitParams = g_Const.denoisingHitParamConsts;
            float diffNormHitDistance = REBLUR_FrontEnd_GetNormHitDist( denoiserDiffRadianceHitDist.w, virtualViewspaceZ, hitParams, 1);
            u_DenoiserDiffRadianceHitDist[pixelPos] = REBLUR_FrontEnd_PackRadianceAndNormHitDist( denoiserDiffRadianceHitDist.xyz, diffNormHitDistance );
            float specNormHitDistance = REBLUR_FrontEnd_GetNormHitDist( denoiserSpecRadianceHitDist.w, virtualViewspaceZ, hitParams, sp.DenoiserNormalRoughness.w);
            u_DenoiserSpecRadianceHitDist[pixelPos] = REBLUR_FrontEnd_PackRadianceAndNormHitDist( denoiserSpecRadianceHitDist.xyz, specNormHitDistance );
    #endif
        }
    }
    
    // if no surface (sky or no data) mark the pixel for NRD as unused; all the other inputs will be ignored
    if( !hasSurface )
        u_DenoiserViewspaceZ[pixelPos]          = VIEWZ_SKY_MARKER;

    // // manual debug viz, just in case
    if( stablePlaneIndex == 2 )
    {
    //    u_DebugVizOutput[pixelPos] = float4( 0.5 + u_DenoiserMotionVectors[pixelPos] * float3(0.2, 0.2, 10), 1 );
    //        u_DebugVizOutput[pixelPos] = float4( frac(u_DenoiserViewspaceZ[pixelPos].xxx), 1 );
    //    //u_DebugVizOutput[pixelPos] = float4( DbgShowNormalSRGB(u_DenoiserNormalRoughness[pixelPos].xyz), 1 );
    //    u_DebugVizOutput[pixelPos] = float4( u_DenoiserNormalRoughness[pixelPos].www, 1 );
    //    //u_DebugVizOutput[pixelPos] = float4( u_DenoiserDiffRadianceHitDist[pixelPos].xyz, 1 );
    //    //u_DebugVizOutput[pixelPos] = float4( u_DenoiserSpecRadianceHitDist[pixelPos].xyz, 1 );
    //    //u_DebugVizOutput[pixelPos] = float4( u_DenoiserDiffRadianceHitDist[pixelPos].www / 100.0, 1 );
    //    //u_DebugVizOutput[pixelPos] = float4( u_DenoiserSpecRadianceHitDist[pixelPos].www / 100.0, 1 );
    }
}

#endif

//

#if defined(DENOISER_FINAL_MERGE)

#pragma pack_matrix(row_major)
#include <donut/shaders/packing.hlsli>
#include "SampleConstantBuffer.h"
#include "NRD/DenoiserNRD.hlsli"
#include "PathTracer/StablePlanes.hlsli"

ConstantBuffer<SampleConstants>         g_Const             : register(b0);
ConstantBuffer<SampleMiniConstants>     g_MiniConst         : register(b1);

RWTexture2D<float4>     u_InputOutput                           : register(u0);
RWTexture2D<float4>     u_DebugVizOutput                        : register(u1);
Texture2D<float4>       t_DiffRadiance                          : register(t2);
Texture2D<float4>       t_SpecRadiance                          : register(t3);
Texture2D<float4>       t_DenoiserValidation                    : register(t5);
Texture2D<float>        t_DenoiserViewspaceZ                    : register(t6);
Texture2D<float>        t_DenoiserDisocclusionThresholdMix      : register(t7);
StructuredBuffer<StablePlane> t_StablePlanesBuffer              : register(t10);

[numthreads(NUM_COMPUTE_THREADS_PER_DIM, NUM_COMPUTE_THREADS_PER_DIM, 1)]
void main( uint3 dispatchThreadID : SV_DispatchThreadID )
{
    const uint stablePlaneIndex = g_MiniConst.params[0];

    uint2 pixelPos = dispatchThreadID.xy;
    if (any(pixelPos >= uint2(g_Const.ptConsts.imageWidth, g_Const.ptConsts.imageHeight) ))
        return;

    float4 diffRadiance = 0.0.xxxx;
    float4 specRadiance = 0.0.xxxx;
    float relaxedDisocclusion = 0; 

    bool hasSurface = t_DenoiserViewspaceZ[pixelPos] != VIEWZ_SKY_MARKER;

    // skip sky!
    if (hasSurface)
    {
        uint spAddress = GenericTSPixelToAddress(pixelPos, stablePlaneIndex, g_Const.ptConsts.genericTSLineStride, g_Const.ptConsts.genericTSPlaneStride);
        float3 diffBSDFEstimate, specBSDFEstimate;
        UnpackTwoFp32ToFp16(t_StablePlanesBuffer[spAddress].DenoiserPackedBSDFEstimate, diffBSDFEstimate, specBSDFEstimate);
        //diffBSDFEstimate = 1.xxx; specBSDFEstimate = 1.xxx;

        relaxedDisocclusion = t_DenoiserDisocclusionThresholdMix[pixelPos];
    #if 1 // classic
        diffRadiance = t_DiffRadiance[pixelPos];
        specRadiance = t_SpecRadiance[pixelPos];
    #else // re-jitter! requires edge-aware filter to actually work correctly
        float2 pixelSize = 1.0.xx / (float2)g_Const.ptConsts.camera.viewportSize;
        float2 samplingUV = (pixelPos.xy + float2(0.5, 0.5) + g_Const.ptConsts.camera.jitter) * pixelSize;
        diffRadiance = t_DiffRadiance.SampleLevel( g_Sampler, samplingUV, 0 );
        specRadiance = t_SpecRadiance.SampleLevel( g_Sampler, samplingUV, 0 );
    #endif

        DenoiserNRD::PostDenoiseProcess(diffBSDFEstimate, specBSDFEstimate, diffRadiance, specRadiance);
    }

#if ENABLE_DEBUG_VIZUALISATION
    if (g_Const.debug.debugViewType >= (int)DebugViewType::StablePlaneRelaxedDisocclusion && g_Const.debug.debugViewType <= ((int)DebugViewType::StablePlaneDenoiserValidation))
    {
        bool debugThisPlane = g_Const.debug.debugViewStablePlaneIndex == stablePlaneIndex;
        uint2 outDebugPixelPos = pixelPos;
        const uint2 screenSize = uint2(g_Const.ptConsts.imageWidth, g_Const.ptConsts.imageHeight);
        const uint2 halfSize = screenSize / 2;
        // figure out where we are in the small quad view
        if (g_Const.debug.debugViewStablePlaneIndex == -1)
        {
            const uint2 quadrant = uint2(stablePlaneIndex%2, stablePlaneIndex/2);
            debugThisPlane = true; 
            outDebugPixelPos = quadrant * halfSize + pixelPos / 2;
        }

        // draw checkerboard pattern for unused stable planes
        if (g_Const.debug.debugViewStablePlaneIndex == -1 && stablePlaneIndex == 0)
        {
            uint quadPlaneIndex = (pixelPos.x >= halfSize.x) + 2 * (pixelPos.y >= halfSize.y);
            if (quadPlaneIndex >= g_Const.ptConsts.activeStablePlaneCount)
                u_DebugVizOutput[pixelPos] = float4( ((pixelPos.x+pixelPos.y)%2).xxx, 1 );
        }
        
        if (debugThisPlane)
        {
            float viewZ = t_DenoiserViewspaceZ[pixelPos].x;
            float4 validation = t_DenoiserValidation[pixelPos].rgba;
            switch (g_Const.debug.debugViewType)
            {
            case ((int)DebugViewType::StablePlaneRelaxedDisocclusion):      u_DebugVizOutput[outDebugPixelPos] = float4( sqrt(relaxedDisocclusion), 0, 0, 1 ); break;
            case ((int)DebugViewType::StablePlaneDiffRadianceDenoised):     u_DebugVizOutput[outDebugPixelPos] = float4( diffRadiance.rgb, 1 ); break;
            case ((int)DebugViewType::StablePlaneSpecRadianceDenoised):     u_DebugVizOutput[outDebugPixelPos] = float4( specRadiance.rgb, 1 ); break;
            case ((int)DebugViewType::StablePlaneCombinedRadianceDenoised): u_DebugVizOutput[outDebugPixelPos] = float4( diffRadiance.rgb + specRadiance.rgb, 1 ); break;
            case ((int)DebugViewType::StablePlaneViewZ):                    u_DebugVizOutput[outDebugPixelPos] = float4( viewZ/10, frac(viewZ), 0, 1 ); break;
            case ((int)DebugViewType::StablePlaneDenoiserValidation):       
                if( validation.a > 0 ) 
                    u_DebugVizOutput[outDebugPixelPos] = float4( validation.rgb, 1 ); 
                else
                    u_DebugVizOutput[outDebugPixelPos] = float4( diffRadiance.rgb + specRadiance.rgb, 1 );
                break;
            default: break;
            }
        }
    }
#endif // #if ENABLE_DEBUG_VIZUALISATION

    if (hasSurface)
        u_InputOutput[pixelPos.xy].xyz += (diffRadiance.rgb + specRadiance.rgb);
    //else
    //    u_InputOutput[pixelPos.xy].xyz = float3(1,0,0);
}
#endif

//

#if defined(DUMMY_PLACEHOLDER_EFFECT) || defined(__INTELLISENSE__)
RWBuffer<float>     u_CaptureTarget         : register(u8);
Texture2D<float>    t_CaptureSource         : register(t0);

[numthreads(1, 1, 1)]
void main( uint3 dispatchThreadID : SV_DispatchThreadID )
{
    uint dummy0, dummy1, mipLevels; t_CaptureSource.GetDimensions(0,dummy0,dummy1,mipLevels); 
    float avgLum = t_CaptureSource.Load( int3(0, 0, mipLevels-1) );
    u_CaptureTarget[0] = avgLum;
}
#endif

#endif // __POST_PROCESS_HLSL__