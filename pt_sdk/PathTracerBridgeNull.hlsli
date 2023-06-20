/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __PATH_TRACER_BRIDGE_NULL_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __PATH_TRACER_BRIDGE_NULL_HLSLI__

#include "PathTracer/PathTracerBridge.hlsli"

PathTracerParams Bridge::getPathTracerParams()
{
    return PathTracerParams::make( float2(0, 0) );
}

uint Bridge::getSampleIndex()
{
    return 0;
}

uint Bridge::getMaxBounceLimit()
{
    return 1;
}

uint Bridge::getMaxDiffuseBounceLimit()
{
    return 1;
}

Ray Bridge::computeCameraRay(const uint2 pixelPos)
{
    Ray ray; 
    ray.origin = float3(0,0,0);
    ray.dir = float3(1, 0, 0);
    return ray;
}

ActiveTextureSampler Bridge::createTextureSampler(const RayCone rayCone, const float3 rayDir, float coneTexLODValue, float3 normalW, bool isPrimaryHit, bool isTriangleHit, const PathTracerParams params)
{
#if ACTIVE_LOD_TEXTURE_SAMPLER == LOD_TEXTURE_SAMPLER_EXPLICIT
    return ExplicitLodTextureSampler::make(params.texLODBias);
#elif ACTIVE_LOD_TEXTURE_SAMPLER == LOD_TEXTURE_SAMPLER_RAY_CONES
    float lambda = rayCone.computeLOD(coneTexLODValue, rayDir, normalW, true);
    lambda += params.texLODBias;
    return ExplicitRayConesLodTextureSampler::make(lambda);
#endif
}

void Bridge::loadSurfacePosNormOnly(out float3 posW, out float3 faceN, const TriangleHit triangleHit, DebugContext debug)
{
    posW = 0.xxx; 
    faceN = 0.xxx;
}

SurfaceData Bridge::loadSurface(const uniform OptimizationHints optimizationHints, const TriangleHit triangleHit, const float3 rayDir, const RayCone rayCone, const PathTracerParams params, const int pathVertexIndex, DebugContext debug)
{
    return SurfaceData::make();
}

void Bridge::updateOutsideIoR(inout SurfaceData surfaceData, float outsideIoR)
{
}

float Bridge::loadIoR(const uint materialID)
{
    return 1.0;
}

HomogeneousVolumeData Bridge::loadHomogeneousVolumeData(const uint materialID)
{
    HomogeneousVolumeData ptVolume;
    ptVolume.sigmaS = float3(0,0,0); 
    ptVolume.sigmaA = float3(0,0,0); 
    ptVolume.g = 0.0;
    return ptVolume;
}

uint Bridge::getAnalyticLightCount()
{
    return 1;
}

bool Bridge::sampleAnalyticLight(const float3 shadingPosW, uint lightIndex, inout SampleGenerator sg, out AnalyticLightSample ls)
{
    AnalyticLightData light = AnalyticLightData::make();
    light.posW              = float3(0, 0, 0);
    light.dirW              = float3(0, 1, 0);
    light.intensity         = 1.f;
    light.type              = (uint)AnalyticLightType::Point;
    light.openingAngle      = 0.1f;
    light.cosOpeningAngle   = cos(0.1f);
    light.penumbraAngle     = 0.1f;
    return samplePointLight(shadingPosW, light, ls);
}

float3 Bridge::computeMotionVector( float3 posW, float3 prevPosW )
{
    return float3(0, 0, 0);
}

float3 Bridge::computeSkyMotionVector( const uint2 pixelPos )
{
    return float3(0, 0, 0);
}

bool Bridge::AlphaTest(uint instanceID, uint instanceIndex, uint geometryIndex, uint triangleIndex, float2 rayBarycentrics)
{
    return true;
}

bool Bridge::AlphaTestVisibilityRay(uint instanceID, uint instanceIndex, uint geometryIndex, uint triangleIndex, float2 rayBarycentrics)
{
    return true;
}

bool Bridge::traceVisibilityRay(RayDesc ray, const RayCone rayCone, const int pathVertexIndex, DebugContext debug)
{
    return false;
}

void Bridge::traceScatterRay(const PathState path, inout RayDesc ray, inout RayQuery<RAY_FLAG_NONE> rayQuery, inout PackedHitInfo packedHitInfo, inout int sortKey, DebugContext debug)
{
}

void Bridge::StoreSecondarySurfacePositionAndNormal(uint2 pixelCoordinate, float3 worldPos, float3 normal)
{
}

bool Bridge::EnvMap::HasEnvMap()
{
    return false;
}

float3 Bridge::EnvMap::Eval(float3 dir)
{
    return 0;
}

float Bridge::EnvMap::EvalPdf(float3 dir)
{
    return 0;
}

bool Bridge::EnvMap::Sample(const float2 rnd, out EnvMapSample result)
{
    EnvMapSample s;
    s.dir = 0;
    s.pdf = 0;
    s.Le = 0;

    result = s;
    return false;
}

#endif // __BRIDGE_NULL_HLSLI__