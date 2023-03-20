/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __STABLE_PLANES_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __STABLE_PLANES_HLSLI__

#include "Config.hlsli"    

#include "PathTracerShared.h"
#include "Rendering\Materials\IBSDF.hlsli"

#if !defined(__cplusplus) // shader only!
#include "Utils/Color/ColorHelpers.hlsli"
#endif
#include "Utils.hlsli"


// Largely based on ideas from https://www.mitsuba-renderer.org/~wenzel/papers/decomposition.pdf (Path-space Motion Estimation and Decomposition for Robust Animation Filtering),
// https://developer.nvidia.com/blog/rendering-perfect-reflections-and-refractions-in-path-traced-games/ (Rendering Perfect Reflections and Refractions in Path-Traced Games) and
// Falcor's NRD denoising (https://github.com/NVIDIAGameWorks/Falcor)

static const uint       cStablePlaneMaxVertexIndex      = 15;               // 15 is max, it's enough for denoising and will allow stableBranchID staying at 32bit-s
static const uint       cStablePlaneInvalidBranchID     = 0xFFFFFFFF;       // this means it's empty and any radiance there is invalid; within path tracer it also means one can start using it for the next exploration
static const uint       cStablePlaneEnqueuedBranchID    = 0xFFFFFFFF-1;     // this means it contains enqueued delta path exploration data; it should never be set to this value outside of path tracing passes (would indicate bug)
static const uint       cStablePlaneJustStartedID       = 0;                // this means the delta path is currently being explored; it should never be set to this value outside of path tracing passes (would indicate bug)

#define                 STABLE_PLANES_LAYOUT_TILE_SIZE  8

// Call after every scatter to update stable branch ID; deltaLobeID must be < 4, vertexIndex must be <= cStablePlaneMaxVertexIndex
uint StablePlanesAdvanceBranchID(const uint prevStableBranchID, const uint deltaLobeID);
uint StablePlanesGetParentLobeID(const uint stableBranchID);
uint StablePlanesVertexIndexFromBranchID(const uint stableBranchID);
bool StablePlaneIsOnPlane(const uint planeBranchID, const uint vertexBranchID);
bool StablePlaneIsOnStablePath(const uint planeBranchID, const uint planeVertexIndex, const uint vertexBranchID, const uint vertexIndex);
bool StablePlaneIsOnStablePath(const uint planeBranchID, const uint vertexBranchID);
float StablePlaneAccumulateSampleHitT(float currentHitT, float currentSegmentT, uint bouncesFromPlane, bool pathIsDeltaOnlyPath);
float4 StablePlaneCombineWithHitTCompensation(float4 currentRadianceHitDist, float3 newRadiance, float newHitT);
float3 StablePlaneDebugVizColor(const uint planeIndex);

uint StablePlanesComputeStorageElementCount(const uint imageWidth, const uint imageHeight);
uint StablePlanesPixelToAddress(const uint2 pixelPos, const uint planeIndex, const uint imageWidth, const uint imageHeight);
uint3 StablePlanesAddressToPixel(const uint address, const uint imageWidth, const uint imageHeight);

// multiple (cStablePlaneCount), 2x - current and history
struct StablePlane
{
    uint4   PackedHitInfo;                  // Hit surface info
    float3  RayDirSceneLength;              // Last surface hit direction, multiplied by total ray travel (PathState::sceneLength)
    uint    VertexIndexSortKey;             // 16bits for vertex index (only 8 actually needed), 16bits for sort key
    uint3   PackedThpAndMVs;                // throughput and motion vectors packed in fp16; throughput might no longer be required since it's baked into bsdfestimate
    uint    UsedOnlyForPacking0;            // empty space needed for padding and packing of explore rays
    uint3   DenoiserPackedBSDFEstimate;     // diff and spec bsdf estimates packed in fp16
    uint    UsedOnlyForPacking1;            // empty space needed for padding and packing of explore rays
    float4  DenoiserNormalRoughness;        // we could nicely pack these into at least fp16 if not less
    uint4   DenoiserPackedRadianceHitDist;  // noisy diffuse and specular radiance plus sample hit distance in .w, packed in fp16

    bool            IsEmpty()                                                                       { return VertexIndexSortKey == 0; }


#if STABLE_PLANES_MODE==STABLE_PLANES_BUILD_PASS
    // This is used only during the stable path build to start new branches - just reusing the data; payload size mismatch will trigger compile error - easy to fix!
    void            PackCustomPayload( const uint4 packed[6] );
    void            UnpackCustomPayload( inout uint4 packed[6] );
#endif
};

struct StablePlanesContext
{
#if !defined(__cplusplus) // shader only!
    RWTexture2D<float4>                     StableRadianceUAV;
    RWTexture2D<uint4>                      StablePlanesHeaderUAV;      // [0,1,2] are StableBranchIDs, [3] is asuint(FirstHitRayLength)
    RWStructuredBuffer<StablePlane>         StablePlanesUAV;
    RWTexture2D<uint4>                      PrevStablePlanesHeaderUAV;  // history
    RWStructuredBuffer<StablePlane>         PrevStablePlanesUAV;        // history
    RWTexture2D<float4>                     SecondarySurfaceRadiance;   // for ReSTIR GI

    PathTracerConstants                     PTConstants;

    uint2                                   CenterPixelPos;

    static StablePlanesContext make(uint2 pixelPos, RWTexture2D<uint4> stablePlanesHeaderUAV, RWStructuredBuffer<StablePlane> stablePlanesUAV, RWTexture2D<uint4> prevStablePlanesHeaderUAV, RWStructuredBuffer<StablePlane> prevStablePlanesUAV, RWTexture2D<float4> stableRadianceUAV, RWTexture2D<float4> secondarySurfaceRadiance, PathTracerConstants ptConstants)
    {
        StablePlanesContext ret;
        ret.StablePlanesHeaderUAV       = stablePlanesHeaderUAV;
        ret.StablePlanesUAV             = stablePlanesUAV;
        ret.PrevStablePlanesHeaderUAV   = prevStablePlanesHeaderUAV;
        ret.PrevStablePlanesUAV         = prevStablePlanesUAV;
        ret.StableRadianceUAV           = stableRadianceUAV;
        ret.SecondarySurfaceRadiance    = secondarySurfaceRadiance;
        ret.PTConstants                 = ptConstants;
        ret.CenterPixelPos              = pixelPos;
        return ret;
    }

    // TODO: currently using scanline; update to more cache friendly addressing
    uint    PixelToAddress(uint2 pixelPos, uint planeIndex)
    {
        return StablePlanesPixelToAddress(pixelPos, planeIndex, PTConstants.imageWidth, PTConstants.imageHeight);
    }
    
    uint    PixelToAddress(uint2 pixelPos)                                      { return PixelToAddress(pixelPos, 0); }
    
    StablePlane LoadStablePlane(const uint2 pixelPos, const uint planeIndex, const uniform bool prevFrame)
    {
        uint address = PixelToAddress( pixelPos, planeIndex );
        StablePlane sp;
        if (!prevFrame) 
            sp = StablePlanesUAV[address];
        else
            sp = PrevStablePlanesUAV[address];
        return sp;
    }

    uint    GetBranchID(const uint2 pixelPos, const uint planeIndex)
    {
        return uint3(StablePlanesHeaderUAV[pixelPos].xyz)[planeIndex];
    }

    uint    GetBranchID(const uint planeIndex)
    {
        return GetBranchID(CenterPixelPos, planeIndex);
    }

    void    SetBranchID(const uint planeIndex, uint stableBranchID)
    {
        if (planeIndex==0) // StablePlanesHeaderUAV[CenterPixelPos][planeIndex] does not work in HLSL :| - operator [] is read-only
            StablePlanesHeaderUAV[CenterPixelPos].x = stableBranchID;
        else if (planeIndex==1)
            StablePlanesHeaderUAV[CenterPixelPos].y = stableBranchID;
        else if (planeIndex==2)
            StablePlanesHeaderUAV[CenterPixelPos].z = stableBranchID;
    }

    void    LoadStablePlane(const uint2 pixelPos, const uint planeIndex, const uniform bool prevFrame, out uint vertexIndex, out uint4 packedHitInfo, out uint SERSortKey, out uint stableBranchID, 
                            out float3 rayDir, out float sceneLength, out float3 thp, out float3 motionVectors )
    {
        StablePlane sp = LoadStablePlane(pixelPos, planeIndex, prevFrame);
        vertexIndex     = sp.VertexIndexSortKey>>16;
        packedHitInfo   = sp.PackedHitInfo;
        SERSortKey      = sp.VertexIndexSortKey&0xFFFF;
        sceneLength     = length(sp.RayDirSceneLength.xyz);
        rayDir          = sp.RayDirSceneLength.xyz / sceneLength;
        UnpackTwoFp32ToFp16(sp.PackedThpAndMVs, thp, motionVectors);
        stableBranchID  = GetBranchID(pixelPos, planeIndex);
    }

    uint4               LoadStablePlanesHeader(uint2 pixelPos)                          { return StablePlanesHeaderUAV[pixelPos]; }

    void                StoreStableRadiance(uint2 pixelPos, float3 radiance)            { StableRadianceUAV[pixelPos].xyzw = float4(clamp( radiance, 0, HLF_MAX ), 0); }
    float3              LoadStableRadiance(uint2 pixelPos)                              { return StableRadianceUAV[pixelPos].xyz; }

    // last 2 bits are for dominant SP index
    void                StoreFirstHitRayLengthAndClearDominantToZero(float length)      { StablePlanesHeaderUAV[CenterPixelPos].w = asuint(min(kMaxRayTravel, length)) & 0xFFFFFFFC; }
    float               LoadFirstHitRayLength(uint2 pixelPos)                           { return asfloat(StablePlanesHeaderUAV[pixelPos].w & 0xFFFFFFFC); }
    void                StoreDominantIndex(uint index)                                  { StablePlanesHeaderUAV[CenterPixelPos].w = (StablePlanesHeaderUAV[CenterPixelPos].w & 0xFFFFFFFC) | (0x3 & index); }
    uint                LoadDominantIndex()                                             { return StablePlanesHeaderUAV[CenterPixelPos].w & 0x3; }


    // this stores at surface hit, with path processed in PathTracer::handleHit and decision taken to use vertex as stable plane
    void                StoreStablePlane(const uint planeIndex, const uint vertexIndex, const uint4 packedHitInfo, const uint SERSortKey, const uint stableBranchID, const float3 rayDir, const float sceneLength,
                                            const float3 thp, const float3 motionVectors, const float roughness, const float3 worldNormal, const float3 diffBSDFEstimate, const float3 specBSDFEstimate, bool dominantSP )
    {
        uint address = PixelToAddress( CenterPixelPos, planeIndex );
        StablePlane sp;
        sp.PackedHitInfo      = packedHitInfo;
        sp.RayDirSceneLength  = rayDir*clamp( sceneLength, 1e-7, kMaxRayTravel );
        sp.VertexIndexSortKey = (vertexIndex << 16) | (SERSortKey&0xFFFF);
        sp.PackedThpAndMVs    = PackTwoFp32ToFp16(thp, motionVectors);

        // add throughput and clamp to minimum/maximum reasonable
        const float kNRDMinReflectance = 0.04f; const float kNRDMaxReflectance = 6.5504e+4F; // HLF_MAX
        const float3 fullDiffBSDFEstimate = clamp( diffBSDFEstimate * thp, kNRDMinReflectance.xxx, kNRDMaxReflectance );
        const float3 fullSpecBSDFEstimate = clamp( specBSDFEstimate * thp, kNRDMinReflectance.xxx, kNRDMaxReflectance );

        sp.DenoiserPackedBSDFEstimate  = PackTwoFp32ToFp16( fullDiffBSDFEstimate, fullSpecBSDFEstimate );
        sp.UsedOnlyForPacking0  = 0;
        sp.UsedOnlyForPacking1  = 0;
        const float cMinRoughness = PTConstants.stablePlanesMinRoughness;   // Allows for a bit more blurring between samples
        sp.DenoiserNormalRoughness     = float4( worldNormal, max(cMinRoughness, roughness) );
        sp.DenoiserPackedRadianceHitDist = 0;
        StablePlanesUAV[address] = sp;
        SetBranchID(planeIndex, stableBranchID);

        if (dominantSP && planeIndex != 0) // planeIndex 0 is dominant by default
            StoreDominantIndex(planeIndex); // we assume StoreFirstHitRayLengthAndClearDominantToZero was already called
    }

#if STABLE_PLANES_MODE==STABLE_PLANES_BUILD_PASS
    // as we go on forking the delta paths, we need to store the payloads somewhere to be able to explore them later!
    void                StoreExplorationStart(uint planeIndex, const uint4 pathPayload[6])
    {
        uint address = PixelToAddress( CenterPixelPos, planeIndex );
        StablePlane sp;
        sp.PackCustomPayload(pathPayload);
        StablePlanesUAV[address] = sp;
        SetBranchID(planeIndex, cStablePlaneEnqueuedBranchID);
    }
    void                ExplorationStart(uint planeIndex, inout uint4 pathPayload[6])
    {
        uint address = PixelToAddress( CenterPixelPos, planeIndex );
        StablePlane sp = StablePlanesUAV[address];
        sp.UnpackCustomPayload(pathPayload);
        // and then clear radiance buffers and few things like that - this plane is stared now and consecutive calls to this function on this plane are incorrect
        ResetPlaneRadiance(planeIndex);
        SetBranchID(planeIndex, cStablePlaneJustStartedID);
    }
    int                 FindNextToExplore()
    {
        for( int i = 1; i < cStablePlaneCount; i++ )
            if( GetBranchID(i) == cStablePlaneEnqueuedBranchID )
                return i;
        return -1;
    }
    void                GetAvailableEmptyPlanes(inout int availableCount, inout int availablePlanes[cStablePlaneCount])
    {
        // TODO optimize this; no need for this many reads, could just store availability
        availableCount = 0;
        for( int i = 1; i < min(PTConstants.activeStablePlaneCount, cStablePlaneCount); i++ )    // we know 1st isn't available so ignore it
            if( GetBranchID(i) == cStablePlaneInvalidBranchID )
                availablePlanes[availableCount++] = i;
    }
#endif
    // below is the stuff used during path tracing (build & fill), which is not required for denoising, RTXDI or any post-process
    void StartPathTracingPass()
    {
#if STABLE_PLANES_MODE==STABLE_PLANES_BUILD_PASS // if first pass, initialize data
        StoreStableRadiance(CenterPixelPos, 0.xxx);         // assume sky
        StablePlanesHeaderUAV[CenterPixelPos].xyz = uint3(cStablePlaneInvalidBranchID,cStablePlaneInvalidBranchID,cStablePlaneInvalidBranchID);
        ResetPlaneRadiance(0);
#endif // STABLE_PLANES_MODE==STABLE_PLANES_BUILD_PASS
    }
#if STABLE_PLANES_MODE==STABLE_PLANES_BUILD_PASS // if first pass, initialize data; TODO: why is this necessary? Can't we init in StoreStablePlane?
    void ResetPlaneRadiance(uint planeIndex) 
    {
        uint address = PixelToAddress( CenterPixelPos, planeIndex );
        StablePlanesUAV[address].DenoiserPackedRadianceHitDist = PackTwoFp32ToFp16(0.xxxx, 0.xxxx);
    }
#endif

#if STABLE_PLANES_MODE==STABLE_PLANES_NOISY_PASS
    void CommitDenoiserRadiance(const uint planeIndex, float denoiserSampleHitTFromPlane, float4 denoiserDiffRadianceHitDist, float4 denoiserSpecRadianceHitDist,
        float3 secondaryL, bool baseScatterDiff, bool onDeltaBranch, bool onDominantBranch)
    {
        const bool useReSTIRGI = PTConstants.useReSTIRGI;
        uint address = PixelToAddress( CenterPixelPos, planeIndex ); 
        denoiserDiffRadianceHitDist.w = max(0, denoiserDiffRadianceHitDist.w); // note: we might want to go with negative hitT for transmission?
        denoiserSpecRadianceHitDist.w = max(0, denoiserSpecRadianceHitDist.w); // note: we might want to go with negative hitT for transmission?
        
        if (useReSTIRGI && !onDeltaBranch && onDominantBranch)
        {
            // Store the full secondary radiance for ReSTIR GI
            SecondarySurfaceRadiance[CenterPixelPos] = float4(secondaryL, 0);
        }
        else
        {
            // Don't accumulate the secondary radiance into the diffuse or specular denoiser channels just yet
            // for stable plane 0 when ReSTIR GI is active. That radiance will come from the ReSTIR GI final shading pass.
            if (baseScatterDiff)
                denoiserDiffRadianceHitDist.xyzw = StablePlaneCombineWithHitTCompensation(denoiserDiffRadianceHitDist, secondaryL, denoiserSampleHitTFromPlane);
            else
                denoiserSpecRadianceHitDist.xyzw = StablePlaneCombineWithHitTCompensation(denoiserSpecRadianceHitDist, secondaryL, denoiserSampleHitTFromPlane);
        }

        StablePlanesUAV[address].DenoiserPackedRadianceHitDist = PackTwoFp32ToFp16(denoiserDiffRadianceHitDist, denoiserSpecRadianceHitDist);
    }
#endif // #if STABLE_PLANES_MODE==STABLE_PLANES_NOISY_PASS

#if STABLE_PLANES_MODE==STABLE_PLANES_BUILD_PASS
    float3 CompletePathTracingBuild(const float3 pathL)
    {
        StoreStableRadiance(CenterPixelPos, pathL);
        return pathL;
    }
#elif STABLE_PLANES_MODE==STABLE_PLANES_NOISY_PASS
    float3 CompletePathTracingFill(bool denoisingEnabled)
    {
        float3 pathL = LoadStableRadiance(CenterPixelPos);
        if (!denoisingEnabled)
        {
            for (int i = 0; i < cStablePlaneCount; i++)
            {
                if (GetBranchID(i) == cStablePlaneInvalidBranchID)
                    continue;

                float4 diff, spec; 
                UnpackTwoFp32ToFp16(StablePlanesUAV[PixelToAddress( CenterPixelPos, i )].DenoiserPackedRadianceHitDist, diff, spec);
                pathL += diff.rgb;
                pathL += spec.rgb;
            }
        }
        return pathL;
    }
#endif

#endif // #if !defined(__cplusplus)
};

// Call after every scatter to update stable branch ID; deltaLobeID must be < 4, vertexIndex must be <= cStablePlaneMaxVertexIndex
inline uint StablePlanesAdvanceBranchID(const uint prevStableBranchID, const uint deltaLobeID)
{
    return (prevStableBranchID << 2) | deltaLobeID;
    // return prevStableBranchID | ( deltaLobeID << ( (vertexIndex-1)*2 ) );
}
inline uint StablePlanesGetParentLobeID(const uint stableBranchID)
{
    return stableBranchID & 0x3;
    // if( vertexIndex == 1 ) return 0; // parent is camera vertex, so just return 0
    // return (stableBranchID >> (vertexIndex-2)*2 ) & 0x3;
}
inline uint StablePlanesVertexIndexFromBranchID(const uint stableBranchID)
{
#if defined(__cplusplus)
    uint v = stableBranchID; unsigned r = 0; while (v >>= 1) r++; return r/2+1;
#else
    return firstbithigh(stableBranchID)/2+1;
#endif
}
inline bool StablePlaneIsOnPlane(const uint planeBranchID, const uint vertexBranchID) 
{ 
    return planeBranchID == vertexBranchID;
}
inline bool StablePlaneIsOnStablePath(const uint planeBranchID, const uint planeVertexIndex, const uint vertexBranchID, const uint vertexIndex) 
{
    if( vertexIndex > planeVertexIndex )
        return false;
    return (planeBranchID >> ((planeVertexIndex-vertexIndex)*2) ) == vertexBranchID;
}
inline bool StablePlaneIsOnStablePath(const uint planeBranchID, const uint vertexBranchID) 
{
    return StablePlaneIsOnStablePath(planeBranchID, StablePlanesVertexIndexFromBranchID(planeBranchID), vertexBranchID, StablePlanesVertexIndexFromBranchID(vertexBranchID));
}
inline float3 StablePlaneDebugVizColor(const uint planeIndex) 
{ 
    return float3( planeIndex==0 || planeIndex==3, planeIndex==1, planeIndex==2 || planeIndex==3 ); 
}

#if 1 // use linear storage (change requires cpp and shader recompile)
inline uint StablePlanesComputeStorageElementCount(const uint imageWidth, const uint imageHeight)       
{ 
    return cStablePlaneCount * imageWidth * imageHeight; 
}
inline uint StablePlanesPixelToAddress(const uint2 pixelPos, const uint planeIndex, const uint imageWidth, const uint imageHeight )
{
    uint yInPlane = pixelPos.y + planeIndex * imageHeight;
    return yInPlane * imageWidth + pixelPos.x;
}
inline uint3 StablePlanesAddressToPixel(const uint address, const uint imageWidth, const uint imageHeight)
{
    uint planeIndex = address / imageHeight;
    uint2 pixelPos;
    pixelPos.x = address % imageWidth;
    pixelPos.y = (address / imageWidth) % imageHeight;
    return uint3(pixelPos, planeIndex);
}
#else // use tiled storage - this is a prototype, it's faster than the linear above but we need something better (Morton?) and we need to bake in more constants instead of computing on the fly
inline uint StablePlanesComputeStorageElementCount(const uint imageWidth, const uint imageHeight)
{ 
    uint tileSize = STABLE_PLANES_LAYOUT_TILE_SIZE;
    uint tileCountX = (imageWidth + tileSize - 1) / tileSize;
    uint tileCountY = (imageHeight + tileSize - 1) / tileSize;
    return cStablePlaneCount * tileCountX * STABLE_PLANES_LAYOUT_TILE_SIZE * tileCountY * STABLE_PLANES_LAYOUT_TILE_SIZE; 
}
inline uint StablePlanesPixelToAddress(const uint2 pixelPos, const uint planeIndex, const uint imageWidth, const uint imageHeight ) // <- pass ptConstants or StablePlane constants in...
{
    uint yInPlane = pixelPos.y + planeIndex * imageHeight;

    uint tileSize = STABLE_PLANES_LAYOUT_TILE_SIZE;
    uint tileCountX = (imageWidth + tileSize - 1) / tileSize;

    uint tileIndex      = pixelPos.x / tileSize + tileCountX * (yInPlane / tileSize);
    uint tilePixelIndex = pixelPos.x % tileSize + tileSize * (yInPlane % tileSize);

    return tileIndex * (tileSize*tileSize) + tilePixelIndex;
}
inline uint3 StablePlanesAddressToPixel(const uint address, const uint imageWidth, const uint imageHeight) // <- pass ptConstants or StablePlane constants in...
{
    uint tileSize = STABLE_PLANES_LAYOUT_TILE_SIZE;
    uint tileCountX = (imageWidth + tileSize - 1) / tileSize;

    uint tileIndex      = address / (tileSize*tileSize);
    uint tilePixelIndex = address % (tileSize*tileSize);
    uint2 pixelPosInPlane = uint2( (tileIndex % tileCountX) * tileSize + tilePixelIndex % tileSize, (tileIndex / tileCountX) * tileSize + tilePixelIndex / tileSize );

    uint planeIndex = pixelPosInPlane.y / imageHeight;
    return uint3(pixelPosInPlane.x, pixelPosInPlane.y% imageHeight, planeIndex);
}
#endif

#if !defined(__cplusplus) // shader only!
inline float StablePlaneAccumulateSampleHitT(float currentHitT, float currentSegmentT, uint bouncesFromPlane, bool pathIsDeltaOnlyPath)
{
    if (bouncesFromPlane==1)    // first hit from stable (denoising) plane always records 
        return currentSegmentT;
    else if( bouncesFromPlane > 1 && pathIsDeltaOnlyPath ) // subsequent hits from stable plane only add to hitT if delta only
        return currentHitT+currentSegmentT;
    else
        return currentHitT;
}
inline float4 StablePlaneCombineWithHitTCompensation(float4 currentRadianceHitDist, float3 newRadiance, float newHitT)
{
    float lNew = luminance(newRadiance);
    if (lNew < 1e-5)
        return currentRadianceHitDist;
    float lOld = luminance(currentRadianceHitDist.rgb);
    float weightNew = lNew / (lOld + lNew);
    return float4( currentRadianceHitDist.rgb + newRadiance.rgb, abs(currentRadianceHitDist.w) * (1-weightNew) + newHitT * weightNew );
}
inline uint3 StablePlaneDebugVizFourWaySplitCoord(const int dbgPlaneIndex, const uint2 pixelPos, const uint2 screenSize)
{
    if( dbgPlaneIndex >= 0 )
        return uint3( pixelPos.xy, dbgPlaneIndex );
    else
    {
        const uint2 halfSize = screenSize / 2;
        const uint2 quadrant = (pixelPos >= halfSize);
        uint3 ret;
        ret.xy = (pixelPos - quadrant * halfSize) * 2.0;
        ret.z = quadrant.x + quadrant.y * 2;
        return ret;
    }
}
#endif

#if STABLE_PLANES_MODE==STABLE_PLANES_BUILD_PASS
void StablePlane::PackCustomPayload(const uint4 packed[6])
{
    // WARNING: take care when changing these - error could be subtle and very hard to track down later
    PackedHitInfo                   = packed[0];
    RayDirSceneLength               = asfloat(packed[1].xyz);
    VertexIndexSortKey              = packed[1].w;
    PackedThpAndMVs                 = packed[2].xyz;
    UsedOnlyForPacking0             = packed[2].w;
    DenoiserPackedBSDFEstimate      = packed[3].xyz;
    UsedOnlyForPacking1             = packed[3].w;
    DenoiserNormalRoughness         = asfloat(packed[4]);
    DenoiserPackedRadianceHitDist   = packed[5];
}
void StablePlane::UnpackCustomPayload(inout uint4 packed[6])
{
    // WARNING: take care when changing these - error could be subtle and very hard to track down later
    packed[0]       = PackedHitInfo;
    packed[1].xyz   = asuint(RayDirSceneLength);
    packed[1].w     = VertexIndexSortKey;
    packed[2].xyz   = PackedThpAndMVs;
    packed[2].w     = UsedOnlyForPacking0;
    packed[3].xyz   = DenoiserPackedBSDFEstimate;
    packed[3].w     = UsedOnlyForPacking1;
    packed[4]       = asuint(DenoiserNormalRoughness);
    packed[5]       = DenoiserPackedRadianceHitDist;
}
#endif


#endif // __STABLE_PLANES_HLSLI__
