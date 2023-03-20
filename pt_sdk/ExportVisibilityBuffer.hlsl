/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#define NON_PATH_TRACING_PASS 1

#include "PathTracerBridgeDonut.hlsli"
#include "PathTracer/PathTracer.hlsli"

#include "ShaderResourceBindings.hlsli"

[numthreads(NUM_COMPUTE_THREADS_PER_DIM, NUM_COMPUTE_THREADS_PER_DIM, 1)]
void main( uint2 dispatchThreadID : SV_DispatchThreadID )
{
    const uint2 pixelPos = dispatchThreadID.xy;
    if( any(pixelPos >= uint2(g_Const.ptConsts.imageWidth, g_Const.ptConsts.imageHeight) ) )
        return;

    // Load the primary hit from the V-buffer (stable planes are now what used to be v-buffer).
    StablePlanesContext stablePlanes = StablePlanesContext::make(pixelPos, u_StablePlanesHeader, u_StablePlanesBuffer, u_PrevStablePlanesHeader, u_PrevStablePlanesBuffer, u_StableRadiance, u_SecondarySurfaceRadiance, g_Const.ptConsts);

    uint dominantStablePlaneIndex = stablePlanes.LoadDominantIndex();
    PackedHitInfo packedHitInfo; float3 rayDir; uint vertexIndex; uint SERSortKey; uint stableBranchID; float sceneLength; float3 pathThp; float3 motionVectors;
	stablePlanes.LoadStablePlane(pixelPos, dominantStablePlaneIndex, false, vertexIndex, packedHitInfo, SERSortKey, stableBranchID, rayDir, sceneLength, pathThp, motionVectors);

    u_MotionVectors[pixelPos]   = float4(motionVectors, 0);

    uint sampleIndex = Bridge::getSampleIndex();

    // Useful for debugging
    // DebugContext debug; debug.Init( pixelPos, sampleIndex, g_Const.debug, u_FeedbackBuffer, u_DebugLinesBuffer, u_DebugDeltaPathTree, u_DeltaPathSearchStack, u_DebugVizOutput );

    const Ray cameraRay = Bridge::computeCameraRay( pixelPos );
    const HitInfo hit = HitInfo(packedHitInfo);
    bool hitSurface = hit.isValid() && hit.getType() == HitType::Triangle;

#if 0  // for testing correctness: compute first hit surface motion vector
    {
        float3 virtualWorldPos = cameraRay.origin + cameraRay.dir * stablePlanes.LoadFirstHitRayLength(pixelPos);
        u_MotionVectors[pixelPos].xyz = Bridge::computeMotionVector(virtualWorldPos, virtualWorldPos);
    }
#endif

    // Prepare per-pixel surface data for denoising, RTXDI and etc.
    if (hitSurface)
    {
        float3 virtualWorldPos = cameraRay.origin + cameraRay.dir * sceneLength;
        float4 clipPos = mul(float4(/*bridgedData.sd.posW*/virtualWorldPos, 1), g_Const.view.matWorldToClip);
        u_Depth[pixelPos] = clipPos.z / clipPos.w;
    }
    else
    {
        u_Depth[pixelPos] = 0;
    }

    if (g_Const.debug.debugViewType==(int)DebugViewType::VBufferMotionVectors)
        u_DebugVizOutput[pixelPos] = float4( 0.5.xxx + u_MotionVectors[pixelPos].xyz * float3(0.2, 0.2, 10), 1);
    else if (g_Const.debug.debugViewType==(int)DebugViewType::VBufferDepth)
        u_DebugVizOutput[pixelPos] = float4( u_Depth[pixelPos].xxx * 100.0, 1 );

}