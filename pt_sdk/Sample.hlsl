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

// HLSL extensions don't work on Vulkan, so just disable HitObject etc. for now
#ifdef SPIRV
#undef USE_HIT_OBJECT_EXTENSION
#define USE_HIT_OBJECT_EXTENSION 0
#endif

#ifndef USE_HIT_OBJECT_EXTENSION
#define USE_HIT_OBJECT_EXTENSION 0
#endif

// this sets up various approaches - different combinations are possible
// this will get significantly simplified once SER API is part of DirectX - we'll then be able to default to "TraceRayInline" version in all variants
#define USE_TRACE_RAY_INLINE USE_HIT_OBJECT_EXTENSION    // this defines whether rayQuery.TraceRayInline is used (otherwise it's TraceRay)
#if USE_HIT_OBJECT_EXTENSION
#define NV_HITOBJECT_USE_MACRO_API 1
#include "PathTracer/Config.hlsli" // for NVAPI macros
#include "../external/nvapi/nvHLSLExtns.h"
#endif

#define SER_USE_MANUAL_SORT_KEY 1 // use the SER reordering key that we generated in 'ComputeSubInstanceData'; otherwise use heuristic in NvReorderThread(hit, 0, 0) based on 'hit' properties

#if PATH_TRACER_MODE==PATH_TRACER_MODE_BUILD_STABLE_PLANES
#define SER_USE_SORTING 0
#else
#define SER_USE_SORTING 1
#endif

#include "PathTracerBridgeDonut.hlsli"
#include "PathTracer/PathTracer.hlsli"

#include "ShaderResourceBindings.hlsli"

AUXContext getAUXContext(uint2 pixelPos)
{
    AUXContext ret;
    ret.ptConsts = g_Const.ptConsts;
    ret.pixelPos = pixelPos;
    ret.pixelStorageIndex = PixelCoordToIndex(pixelPos, g_Const.ptConsts.imageWidth);
    ret.debug.Init( pixelPos, Bridge::getSampleIndex(), g_Const.debug, u_FeedbackBuffer, u_DebugLinesBuffer, u_DebugDeltaPathTree, u_DeltaPathSearchStack, u_DebugVizOutput );
    ret.stablePlanes = StablePlanesContext::make(pixelPos, u_StablePlanesHeader, u_StablePlanesBuffer, u_StableRadiance, u_SecondarySurfaceRadiance, g_Const.ptConsts);

    return ret;
}

AUXContext getAUXContext(const PathState path)
{
    return getAUXContext(PathIDToPixel(path.id));
}

#if PATH_TRACER_MODE!=PATH_TRACER_MODE_REFERENCE
void firstHitFromBasePlane(inout PathState path, const uint basePlaneIndex, const AUXContext auxContext)
{
    PackedHitInfo packedHitInfo; float3 rayDir; uint vertexIndex; uint SERSortKey; uint stableBranchID; float sceneLength; float3 thp; float3 motionVectors;
    auxContext.stablePlanes.LoadStablePlane(auxContext.pixelPos, basePlaneIndex, vertexIndex, packedHitInfo, SERSortKey, stableBranchID, rayDir, sceneLength, thp, motionVectors);

    // reconstruct ray; this is the ray we used to get to this hit, and Direction and rayTCurrent will not be identical due to compression
    RayDesc ray;
    ray.Direction   = rayDir;
    ray.Origin      = path.origin;  // initialized by 'PathTracer::pathSetupPrimaryRay' - WARNING, THIS WILL NOT BE CORRECT FOR NON-PRIMARY BOUNCES
    ray.TMin        = 0;
    ray.TMax        = sceneLength;  // total ray travel so far - used to correctly update rayCone and similar

    // this only works for primary surface replacement cases - in this case sceneLength and rayT become kind of the same
    path.setVertexIndex(vertexIndex-1); // decrement counter by 1 since we'll be processing hit (and calling PathTracer::updatePathTravelled) inside hit/miss shader

    // we're starting from the plane 0 (that's our vbuffer)
    path.setFlag(PathFlags::stablePlaneOnPlane , true);
    path.setFlag(PathFlags::stablePlaneOnBranch, true);
    path.setStablePlaneIndex(basePlaneIndex);
    path.stableBranchID = stableBranchID;
    path.thp = thp;
#if PATH_TRACER_MODE==PATH_TRACER_MODE_FILL_STABLE_PLANES
    const uint dominantSPIndex = auxContext.stablePlanes.LoadDominantIndexCenter();
    path.setFlag(PathFlags::stablePlaneOnDominantBranch, dominantSPIndex == basePlaneIndex ); // dominant plane has been determined in _BUILD_PASS; see if it's basePlaneIndex and set flag
    path.setCounter(PackedCounters::BouncesFromStablePlane, 0);
    path.denoiserSampleHitTFromPlane = 0.0;
    path.denoiserDiffRadianceHitDist = float4(0,0,0,0);
    path.denoiserSpecRadianceHitDist = float4(0,0,0,0);
#endif

    if (!IsValid(packedHitInfo))
    {
        // inline miss shader!
        PathTracer::handleMiss(path, ray.Origin, ray.Direction, ray.TMax, auxContext);
    }
    else
    {
#if USE_HIT_OBJECT_EXTENSION
        NvHitObject hit;
        if (IsValid(packedHitInfo))
        {
            const TriangleHit triangleHit = TriangleHit::make(packedHitInfo); // if valid, we know it's a triangle hit (no support for curved surfaces yet)
            const uint instanceIndex    = triangleHit.instanceID.getInstanceIndex();
            const uint geometryIndex    = triangleHit.instanceID.getGeometryIndex();
            const uint primitiveIndex   = triangleHit.primitiveIndex;
    
            BuiltInTriangleIntersectionAttributes attrib;
            attrib.barycentrics = triangleHit.barycentrics;
            NvMakeHit( SceneBVH, instanceIndex, geometryIndex, primitiveIndex, 0, 0, 1, ray, attrib, hit );

            // this is how ubershader would be handled here:
            // NvReorderThread(SERSortKey, 16);
            // HandleHitUbershader(...);

            PathPayload payload = PathPayload::pack(path);
#if SER_USE_SORTING
            if (auxContext.ptConsts.enableShaderExecutionReordering)  // there could be cost to this branch, although we haven't measured anything significant, and on/off is convenient for testing
#if SER_USE_MANUAL_SORT_KEY
                NvReorderThread(SERSortKey, 16); 
#else
                NvReorderThread(hit, 0, 0);
#endif
#endif // SER_USE_SORTING
            NvInvokeHitObject(SceneBVH, hit, payload);
            path = PathPayload::unpack(payload, PACKED_HIT_INFO_ZERO, Bridge::getSampleIndex());  // init dummy hitinfo - it's not included in the payload to minimize register pressure
        }
#else
        // All this below is one long hack to re-cast a tiny ray with a tiny step so correct shaders get called by TraceRay; using computeRayOrigin to offset back ensuring 
        // we'll hit the same triangle.
        // None of this is needed in SER pass, and will be removed once SER API becomes more widely available.
    
        float3 surfaceHitPosW; float3 surfaceHitFaceNormW; 
        Bridge::loadSurfacePosNormOnly(surfaceHitPosW, surfaceHitFaceNormW, TriangleHit::make(packedHitInfo), auxContext.debug);   // recover surface triangle position
        bool frontFacing = dot( -ray.Direction, surfaceHitFaceNormW ) >= 0.0;

        // ensure we'll hit the same triangle again (additional offset found empirially - it's still imperfect for glancing rays)
        float3 newOrigin = computeRayOrigin(surfaceHitPosW, (frontFacing)?(surfaceHitFaceNormW):(-surfaceHitFaceNormW)) - ray.Direction * 8e-5;

        // update path state as we'll skip everything up to the surface, thus we must account for the skip which is 'length(newOrigin-ray.Origin)'
        PathTracer::updatePathTravelled(path, ray.Origin, ray.Direction, length(newOrigin-ray.Origin), auxContext, false, false); // move path internal state by the unaccounted travel, but don't increment vertex index or update origin/rayDir

        ray.Origin = newOrigin; // move to a new starting point; leave ray.TMax as is as we can't reliably compute min travel to ensure hit but we know it's less than current TMax (sceneLength)
    
        PathPayload payload = PathPayload::pack(path);
        TraceRay( SceneBVH, RAY_FLAG_NONE, 0xff, 0, 1, 0, ray, payload );
        path = PathPayload::unpack(payload, PACKED_HIT_INFO_ZERO, Bridge::getSampleIndex());
#endif // USE_HIT_OBJECT_EXTENSION
    }
}
#endif

void nextHit(inout PathState path, const AUXContext auxContext, uniform bool skipStablePlaneExploration)
{
#if USE_HIT_OBJECT_EXTENSION
    RayDesc ray; RayQuery<RAY_FLAG_NONE> rayQuery;
    PackedHitInfo packedHitInfo; int sortKey;
    Bridge::traceScatterRay(path, ray, rayQuery, packedHitInfo, sortKey, auxContext.debug);   // this outputs ray and rayQuery; if there was a hit, ray.TMax is rayQuery.ComittedRayT

    NvHitObject hit;
    if (rayQuery.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    {
        // inline miss shader!
        PathTracer::handleMiss(path, ray.Origin, ray.Direction, ray.TMax, auxContext );
    }
    else
    {
        BuiltInTriangleIntersectionAttributes attrib;
        attrib.barycentrics = rayQuery.CommittedTriangleBarycentrics();
        NvMakeHitWithRecordIndex( rayQuery.CommittedInstanceContributionToHitGroupIndex()+rayQuery.CommittedGeometryIndex(), SceneBVH, rayQuery.CommittedInstanceIndex(), rayQuery.CommittedGeometryIndex(), rayQuery.CommittedPrimitiveIndex(), 0, ray, attrib, hit );
        PathPayload payload = PathPayload::pack(path);
#if SER_USE_SORTING
        if (auxContext.ptConsts.enableShaderExecutionReordering)  // there could be cost to this branch, although we haven't measured anything significant, and on/off is convenient for testing
#if SER_USE_MANUAL_SORT_KEY
            NvReorderThread(sortKey, 16);
#else
            NvReorderThread(hit, 0, 0);
#endif
#endif // SER_USE_SORTING
        NvInvokeHitObject(SceneBVH, hit, payload);
        path = PathPayload::unpack(payload, PACKED_HIT_INFO_ZERO, Bridge::getSampleIndex());  // init dummy hitinfo - it's not included in the payload to minimize register pressure
    }
#else
    // refactor...
    RayDesc ray = path.getScatterRay().toRayDesc();
    PathPayload payload = PathPayload::pack(path);
    TraceRay( SceneBVH, RAY_FLAG_NONE, 0xff, 0, 1, 0, ray, payload );
    path = PathPayload::unpack(payload, PACKED_HIT_INFO_ZERO, Bridge::getSampleIndex());  // init dummy hitinfo - it's not included in the payload to minimize register pressure
#endif

#if PATH_TRACER_MODE==PATH_TRACER_MODE_BUILD_STABLE_PLANES   // explore enqueued stable planes, if any
    int nextPlaneToExplore;
    if (!path.isActive() && (nextPlaneToExplore=auxContext.stablePlanes.FindNextToExplore(path.getStablePlaneIndex()+1))!=-1 )
    {
        float3 prevL = path.L;  // save non-noisy radiance captured so far
        PathPayload payload;
        auxContext.stablePlanes.ExplorationStart(nextPlaneToExplore, payload.packed);
        PathState newPayload = PathPayload::unpack(payload, PACKED_HIT_INFO_ZERO, Bridge::getSampleIndex());
        path = PathPayload::unpack(payload, PACKED_HIT_INFO_ZERO, Bridge::getSampleIndex());
        path.L = prevL;         // keep accumulating non-noisy radiance captured so far

        // if( auxContext.debug.IsDebugPixel() )
        //     auxContext.debug.Print( path.getStablePlaneIndex(), 42);
    }
#endif
}

#if ENABLE_DEBUG_DELTA_TREE_VIZUALISATION
// figure out where to move this so it's not in th emain path tracer code
void DeltaTreeVizExplorePixel(const AUXContext auxContext);
#endif

[shader("raygeneration")]
void RayGen()
{
    uint2 pixelPos = DispatchRaysIndex().xy;
    uint sampleIndex = Bridge::getSampleIndex();

    PathState path = PathTracer::emptyPathInitialize( pixelPos, sampleIndex, g_Const.ptConsts.camera.pixelConeSpreadAngle );
    PathTracer::pathSetupPrimaryRay( path, Bridge::computeCameraRay( pixelPos ) );
    AUXContext auxContext = getAUXContext(pixelPos);

#if PATH_TRACER_MODE!=PATH_TRACER_MODE_REFERENCE
    auxContext.stablePlanes.StartPathTracingPass();
#endif

#if PATH_TRACER_MODE!=PATH_TRACER_MODE_FILL_STABLE_PLANES    // Reset on stable planes disabled (0) or stable planes generate (1)
    auxContext.debug.Reset();   // Setups per-pixel debugging - has to happen before any other debugging stuff in the frame
#endif

#if PATH_TRACER_MODE==PATH_TRACER_MODE_FILL_STABLE_PLANES    // we're continuing from base stable plane (index 0) here to avoid unnecessary path tracing
    firstHitFromBasePlane(path, 0, auxContext);
#endif

#if PATH_TRACER_MODE==1     // BUILD
    u_SecondarySurfacePositionNormal[pixelPos] = 0;
#endif

    // Main path tracing loop
    while( path.isActive() )
        nextHit( path, auxContext, false );

#if PATH_TRACER_MODE==PATH_TRACER_MODE_REFERENCE
    float3 pathRadiance = path.L;
#elif PATH_TRACER_MODE==PATH_TRACER_MODE_BUILD_STABLE_PLANES
    float3 pathRadiance = auxContext.stablePlanes.CompletePathTracingBuild(path.L);
#elif PATH_TRACER_MODE==PATH_TRACER_MODE_FILL_STABLE_PLANES
    auxContext.stablePlanes.CommitDenoiserRadiance(path.getStablePlaneIndex(), path.denoiserSampleHitTFromPlane,
        path.denoiserDiffRadianceHitDist, path.denoiserSpecRadianceHitDist,
        path.secondaryL, path.hasFlag(PathFlags::stablePlaneBaseScatterDiff),
        path.hasFlag(PathFlags::stablePlaneOnDeltaBranch),
        path.hasFlag(PathFlags::stablePlaneOnDominantBranch));

    float3 pathRadiance = auxContext.stablePlanes.CompletePathTracingFill(g_Const.ptConsts.denoisingEnabled);
#endif

#if PATH_TRACER_MODE==PATH_TRACER_MODE_BUILD_STABLE_PLANES && ENABLE_DEBUG_DELTA_TREE_VIZUALISATION
    DeltaTreeVizExplorePixel(auxContext);
    return;
#endif
   
    bool somethingWrong = false;
    somethingWrong |= any(isnan(pathRadiance)) || !all(isfinite(pathRadiance));
    somethingWrong |= any(isnan(path.thp)) || !all(isfinite(path.thp));
    [branch] if (somethingWrong)
    {
#if 1 && ENABLE_DEBUG_VIZUALISATION
        auxContext.debug.DrawDebugViz( pixelPos, float4(0, 0, 0, 1 ) );
        for( int k = 1; k < 6; k++ )
        {
            auxContext.debug.DrawDebugViz( pixelPos+uint2(+k,+0), float4(1-(k/2)%2, (k/2)%2, k%5, 1 ) );
            auxContext.debug.DrawDebugViz( pixelPos+uint2(-k,+0), float4(1-(k/2)%2, (k/2)%2, k%5, 1 ) );
            auxContext.debug.DrawDebugViz( pixelPos+uint2(+0,+k), float4(1-(k/2)%2, (k/2)%2, k%5, 1 ) );
            auxContext.debug.DrawDebugViz( pixelPos+uint2(+0,-k), float4(1-(k/2)%2, (k/2)%2, k%5, 1 ) );
        }
#endif
        pathRadiance = 0; // sanitize
    }

#if PATH_TRACER_MODE!=PATH_TRACER_MODE_BUILD_STABLE_PLANES
     u_Output[pixelPos] = float4( pathRadiance, 1 );   // <- alpha 1 is important for screenshots
#endif

//  debugging examples:
//    if( auxContext.debug.IsDebugPixel() )
//    {
//        auxContext.debug.Print( 0, pathRadiance);
//        auxContext.debug.Print( 1, path.thp);
//        auxContext.debug.Print( 2, path.getCounter(PackedCounters::DiffuseBounces));
//        auxContext.debug.Print( 3, path.getCounter(PackedCounters::RejectedHits));
//    }
//    if (all(pixelPos > uint2(400, 400)) && all(pixelPos < uint2(600, 600)))
//        u_Output[pixelPos] = float4( g_Const.ptConsts.preExposedGrayLuminance.xxx, 1 ); 
}

#if ENABLE_DEBUG_DELTA_TREE_VIZUALISATION
void DeltaTreeVizExplorePixel(const AUXContext auxContext)
{
    if (auxContext.debug.constants.exploreDeltaTree && auxContext.debug.IsDebugPixel())
    {
        // setup path normally
        uint sampleIndex = Bridge::getSampleIndex();
        PathState path = PathTracer::emptyPathInitialize( auxContext.debug.pixelPos, sampleIndex, g_Const.ptConsts.camera.pixelConeSpreadAngle );
        PathTracer::pathSetupPrimaryRay( path, Bridge::computeCameraRay( auxContext.debug.pixelPos ) );
        // but then make delta lobes split into their own subpaths that get saved into debug stack with auxContext.debug.DeltaSearchStackPush()
        path.setFlag(PathFlags::deltaTreeExplorer);
        // start with just primary ray
        nextHit(path, auxContext, true);

        // auxContext.debug.Print( 0, path.stableBranchID);

        PathPayload statePacked; int loop = 0;
        while ( auxContext.debug.DeltaSearchStackPop(statePacked) )
        {
            loop++; 
            PathState deltaPathState = PathPayload::unpack( statePacked, PACKED_HIT_INFO_ZERO, Bridge::getSampleIndex() );
            //auxContext.debug.Print( loop, float4( deltaPathState.thp, deltaPathState.parentDeltaLobe ) );
            nextHit(deltaPathState, auxContext, true);
        }
        for (int i = 0; i < cStablePlaneCount; i++)
            auxContext.debug.DeltaTreeStoreStablePlaneID( i, auxContext.stablePlanes.GetBranchIDCenter(i) );
        auxContext.debug.DeltaTreeStoreDominantStablePlaneIndex( auxContext.stablePlanes.LoadDominantIndexCenter() );
    }
}
#endif

void HandleHitUnpacked(const uniform OptimizationHints optimizationHints, const PackedHitInfo packedHitInfo, inout PathState path, float3 worldRayOrigin, float3 worldRayDirection, float rayT, const AUXContext auxContext)
{
    // reconstruct previous origin & dir (avoids actually unpacking .origin from PathPayload); TODO: refactor this so the scatter ray (next ray) is in a separate payload
    path.origin = worldRayOrigin;
    path.dir = worldRayDirection;
    path.setHitPacked( packedHitInfo );
    PathTracer::handleHit(optimizationHints, path, worldRayOrigin, worldRayDirection, rayT, auxContext);
}

void HandleHit(const uniform OptimizationHints optimizationHints, const PackedHitInfo packedHitInfo, inout PathPayload payload)
{
    PathState path = PathPayload::unpack(payload, packedHitInfo, Bridge::getSampleIndex());
    AUXContext auxContext = getAUXContext(PathIDToPixel(path.id));
    HandleHitUnpacked(optimizationHints, packedHitInfo, path, WorldRayOrigin(), WorldRayDirection(), RayTCurrent(), auxContext);
    payload = PathPayload::pack( path );
}

#define CLOSEST_HIT_VARIANT( name, A, B, C )     \
[shader("closesthit")] void ClosestHit##name(inout PathPayload payload : SV_RayPayload, in BuiltInTriangleIntersectionAttributes attrib) \
{ \
    uint sortKey = t_SubInstanceData[InstanceID()+GeometryIndex()].FlagsAndSortKey & 0xFFFF; \
    HandleHit( OptimizationHints::make( A, B, C, sortKey ), TriangleHit::make( InstanceIndex(), GeometryIndex(), PrimitiveIndex(), attrib.barycentrics ).pack(), payload); \
}

//hints: NoTextures, NoTransmission, OnlyDeltaLobes
#if 1 // 3bit 8-variant version
CLOSEST_HIT_VARIANT( 000, false, false, false );
CLOSEST_HIT_VARIANT( 001, false, false, true );
CLOSEST_HIT_VARIANT( 010, false, true,  false );
CLOSEST_HIT_VARIANT( 011, false, true,  true );
CLOSEST_HIT_VARIANT( 100, true,  false, false );
CLOSEST_HIT_VARIANT( 101, true,  false, true );
CLOSEST_HIT_VARIANT( 110, true,  true,  false );
CLOSEST_HIT_VARIANT( 111, true,  true,  true );
#endif

// These two are required for the full TraceRay support
[shader("miss")]
void Miss(inout PathPayload payload : SV_RayPayload)
{
#if USE_HIT_OBJECT_EXTENSION
    // we inline misses in rgs, so this is a no-op.
#else
    PathState path = PathPayload::unpack(payload, PACKED_HIT_INFO_ZERO, Bridge::getSampleIndex());
    PathTracer::handleMiss(path, WorldRayOrigin(), WorldRayDirection(), RayTCurrent(), getAUXContext(PathIDToPixel(path.id)));
    payload = PathPayload::pack(path);
#endif
}

[shader("anyhit")]
void AnyHit(inout PathPayload payload, in BuiltInTriangleIntersectionAttributes attrib/* : SV_IntersectionAttributes*/)
{
    if (!Bridge::AlphaTest(InstanceID(), InstanceIndex(), GeometryIndex(), PrimitiveIndex(), attrib.barycentrics/*, getAUXContext( PathIDToPixel(path.id) ).debug*/ ))
        IgnoreHit();
}
