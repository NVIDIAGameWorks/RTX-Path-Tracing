/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __SCENE_TYPES_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __SCENE_TYPES_HLSLI__

#include "../Config.h"    

#if defined(__cplusplus)
#error fix below first
// due to HLSL bug in one of the currently used compilers, it will still try to include these even though they're #ifdef-ed out
// #include "Utils/Math/PackedFormats.h"
#else
#include "../Utils/Math/PackedFormats.hlsli"
#endif

// pt_sdk<->donut mod - this is really engine dependent so keep as 2 separate uint-s for now for simplicity
struct GeometryInstanceID
{
    uint data;
    static GeometryInstanceID   make( uint instanceIndex, uint geometryIndex )          { GeometryInstanceID ret; ret.data = (geometryIndex << 16) | instanceIndex; return ret; }
    uint                        getInstanceIndex( )                                     { return data & 0xFFFF; }
    uint                        getGeometryIndex( )                                     { return data >> 16; }
};

/** Struct representing interpolated vertex attributes in world space.
    Note the tangent is not guaranteed to be orthogonal to the normal.
    The bitangent should be computed: cross(normal, tangent.xyz) * tangent.w.
    The tangent space is orthogonalized in prepareShadingData().
*/
struct VertexData
{
    float3 posW;            ///< Position in world space.
    float3 normalW;         ///< Shading normal in world space (normalized).
    float4 tangentW;        ///< Shading tangent in world space (normalized). The last component is guaranteed to be +-1.0 or zero if tangents are missing.
    float2 texC;            ///< Texture coordinate.
    float3 faceNormalW;     ///< Face normal in world space (normalized).
    float  curveRadius;     ///< Curve cross-sectional radius. Valid only for geometry generated from curves.
    float  coneTexLODValue; ///< Texture LOD data for cone tracing. This is zero, unless getVertexDataRayCones() is used.
};


#endif // __SCENE_TYPES_HLSLI__