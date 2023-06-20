/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __PATH_TRACER_SHARED_H__
#define __PATH_TRACER_SHARED_H__

#ifndef __cplusplus
#pragma pack_matrix(row_major)
#endif

#define PATH_TRACER_MAX_PAYLOAD_SIZE     4*4*6    // PathPayload is 96 at the moment

// Condensed version of ..\Falcor\Source\Falcor\Scene\Camera\CameraData.hlsli
struct PathTracerCameraData
{
    float3  posW;
    float   nearZ;                      ///< Camera near plane.
    float3  directionW;                 ///< Camera world direction (same as cameraW, except normalized)
    float   pixelConeSpreadAngle;       ///< For raycones
    float3  cameraU;                    ///< Camera base vector U. Normalized it indicates the right image plane vector. The length is dependent on the FOV.
    float   farZ;                       ///< Camera far plane.
    float3  cameraV;                    ///< Camera base vector V. Normalized it indicates the up image plane vector. The length is dependent on the FOV.
    float   focalDistance;              ///< Camera focal distance in scene units.
    float3  cameraW;                    ///< Camera base vector W. Normalized it indicates the forward direction. The length is the camera focal distance.
    float   aspectRatio;                ///< viewport.w / viewport.h
    uint2   viewportSize;               ///< Viewport size
    float   apertureRadius;             ///< Camera aperture radius in scene units.
    float   _padding0;
    float2  jitter;
    float   _padding1;
    float   _padding2;
};

// path tracer main constants
struct PathTracerConstants
{
    uint    imageWidth;
    uint    imageHeight;
    uint    sampleIndex;
    int     enablePerPixelJitterAA;         // this is for future blue noise and/or similar experimentation; at the moment we use constant (per frame) jitter which is set to camera

    uint    bounceCount;
    uint    diffuseBounceCount;
    uint    enableRussianRoulette;
    float   texLODBias;

    uint    hasEnvMap;
    float   fireflyFilterThreshold;
    float   preExposedGrayLuminance;
    uint    denoisingEnabled;

    uint    frameIndex;
    uint    useReSTIRDI;
    uint    useReSTIRGI;
    uint    suppressPrimaryNEE;

    float   stablePlanesSplitStopThreshold;
    float   stablePlanesMinRoughness;
    uint    enableShaderExecutionReordering;
    float   stablePlanesSuppressPrimaryIndirectSpecularK;

    float   denoiserRadianceClampK;
    uint    padding1;
    float   stablePlanesAntiAliasingFallthrough;
    uint    activeStablePlaneCount;

    uint    maxStablePlaneVertexDepth;
    uint    allowPrimarySurfaceReplacement;
    uint    genericTSLineStride;  // used for u_SurfaceData
    uint    genericTSPlaneStride; // used for u_SurfaceData

    PathTracerCameraData camera;
    PathTracerCameraData prevCamera;
};

#ifdef __cplusplus
inline PathTracerCameraData BridgeCamera( uint viewportWidth, uint viewportHeight, float3 camPos, float3 camDir, float3 camUp, float fovY, float nearZ, float farZ, 
    float focalDistance, float apertureRadius, float2 jitter )
{
    PathTracerCameraData data;

    data.focalDistance  = focalDistance;
    data.posW           = camPos;
    data.nearZ          = nearZ;
    data.farZ           = farZ;
    data.aspectRatio    = viewportWidth / (float)viewportHeight;
    data.viewportSize   = {viewportWidth, viewportHeight};

    // Ray tracing related vectors
    data.directionW = donut::math::normalize( camDir );
    data.cameraW = donut::math::normalize( camDir ) * data.focalDistance;
    data.cameraU = donut::math::normalize( donut::math::cross( data.cameraW, camUp ) );
    data.cameraV = donut::math::normalize( donut::math::cross( data.cameraU, data.cameraW ) );
    const float ulen = data.focalDistance * std::tan( fovY * 0.5f ) * data.aspectRatio;
    data.cameraU *= ulen;
    const float vlen = data.focalDistance * std::tan( fovY * 0.5f );
    data.cameraV *= vlen;
    data.apertureRadius = apertureRadius;

    // Note: spread angle is the whole (not half) cone angle!
    data.pixelConeSpreadAngle = std::atan(2.0f * std::tan(fovY * 0.5f) / viewportHeight);

    data.jitter = jitter;
    data._padding0 = 0;
    data._padding1 = 0;
    data._padding2 = 0;

    return data;
}
#endif // __cplusplus

//Const buffer structs 
// From ..\Falcor\Source\Falcor\Scene\Lights\EnvMapData.hlsli
// default values removed 
struct EnvMapData
{
	float3x4    transform;              ///< Local to world transform.
	float3x4    invTransform;           ///< World to local transform.

    float3      tint;                   ///< Color tint
    float       intensity;              ///< Radiance scale
};

// From ..\Falcor\Source\Falcor\Rendering\Lights\EnvMapSampler.hlsli
struct EnvMapSamplerData
{
    float2      importanceInvDim;       ///< 1.0 / dimension.
    uint        importanceBaseMip;      ///< Mip level for 1x1 resolution.
    float       _padding0;
};

inline float3  DbgShowNormalSRGB(float3 normal)
{
    return pow(abs(normal * 0.5f + 0.5f), 2.2f);
}

// perhaps tile or use morton sort in the future here - see https://developer.nvidia.com/blog/optimizing-compute-shaders-for-l2-locality-using-thread-group-id-swizzling/
inline uint2    PixelCoordFromIndex(uint index, const uint imageWidth)     { return uint2(index % imageWidth, index / imageWidth); }
inline uint     PixelCoordToIndex(uint2 pixelCoord, const uint imageWidth)  { return pixelCoord.y * imageWidth + pixelCoord.x; }

#endif // __PATH_TRACER_SHARED_H__