/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __SHADING_DATA_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __SHADING_DATA_HLSLI__

#include "../Config.h"    

#include "../Scene/Material/MaterialData.hlsli"

#include "../PathTracerHelpers.hlsli"

// import Rendering.Materials.IBxDF;
// import Scene.Material.MaterialData;
// import Utils.Geometry.GeometryHelpers;

/** This struct holds information needed for shading a hit point.

    This includes:
    - Geometric properties of the surface.
    - Texture coordinates.
    - Material ID and header.
    - Opacity value for alpha testing.
    - Index of refraction of the surrounding medium.

    Based on a ShadingData struct, the material system can be queried
    for a BSDF instance at the hit using `gScene.materials.getBSDF()`.
    The BSDF has interfaces for sampling and evaluation, as well as for
    querying its properties at the hit.
*/
struct ShadingData
{
    // Geometry data
    float3  posW;                   ///< Shading hit position in world space.
    float3  V;                      ///< Direction to the eye at shading hit.
    float3  N;                      ///< Shading normal at shading hit.
    float3  T;                      ///< Shading tangent at shading hit.
    float3  B;                      ///< Shading bitangent at shading hit.
    float2  uv;                     ///< Texture mapping coordinates.
#ifdef FALCOR_INTERNAL
    float   normalCurvature;        ///< Normal curvature.
#endif
    float3  faceN;                  ///< Face normal in world space, always on the front-facing side.
    float3  vertexN;                ///< Interpolated vertex normal, corrected for front-facing flag but not corrected for the ray view direction.
    bool    frontFacing;            ///< True if primitive seen from the front-facing side.
    float   curveRadius;            ///< Curve cross-sectional radius. Valid only for geometry generated from curves.

    // Material data
    MaterialHeader mtl;             ///< Material header data.
    uint    materialID;             ///< Material ID at shading location.    
    float   opacity;                ///< Opacity value in [0,1]. This is used for alpha testing.
    float   IoR;                    ///< Index of refraction for the medium on the front-facing side (i.e. "outside" the material at the hit).
    float   shadowNoLFadeout;       ///< See corresponding material setting.

    static ShadingData make()
    {
        ShadingData shadingData;
        shadingData.posW = 0;
        shadingData.V = 0;
        shadingData.N = 0;
        shadingData.T = 0;
        shadingData.B = 0;
        shadingData.uv = 0;
    #ifdef FALCOR_INTERNAL
        shadingData.ormalCurvature  = 0;
    #endif
        shadingData.faceN = 0;
        shadingData.vertexN = 0;
        shadingData.frontFacing = 0;
        shadingData.curveRadius = 0;

        shadingData.mtl = MaterialHeader::make();
        shadingData.materialID = 0;
        shadingData.opacity = 0;
        shadingData.IoR = 0;
        shadingData.shadowNoLFadeout = 0;
        return shadingData;
    }

    // Utility functions

    /** Computes new ray origin based on the hit point to avoid self-intersection.
        The method is described in Ray Tracing Gems, Chapter 6, "A Fast and Robust
        Method for Avoiding Self-Intersection" by Carsten Wächter and Nikolaus Binder.
        \param[in] viewside True if the origin should be on the view side (reflection) or false otherwise (transmission).
        \return Ray origin of the new ray.
    */
    float3 computeNewRayOrigin(bool viewside = true)
    {
        return ComputeRayOrigin(posW, (frontFacing == viewside) ? faceN : -faceN);
    }

    /** Transform vector from the local surface frame to world space.
        \param[in] v Vector in local space.
        \return Vector in world space.
    */
    float3 fromLocal(float3 v)
    {
        return T * v.x + B * v.y + N * v.z;
        //return mul( v, float3x3(T,B,N) );
    }

    /** Transform vector from world space to the local surface frame.
        \param[in] v Vector in world space.
        \return Vector in local space.
    */
    float3 toLocal(float3 v)
    {
        return float3(dot(v, T), dot(v, B), dot(v, N));
        //return mul( float3x3(T,B,N), v );
    }

    /** Returns the oriented face normal.
        \return Face normal flipped to the same side as the view vector.
    */
    float3 getOrientedFaceNormal()
    {
        return frontFacing ? faceN : -faceN;
    }
};

#endif // __SHADING_DATA_HLSLI__