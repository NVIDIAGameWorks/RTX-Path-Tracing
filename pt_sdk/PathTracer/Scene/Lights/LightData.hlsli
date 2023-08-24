/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __LIGHT_DATA_PARAMS__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __LIGHT_DATA_PARAMS__

#include "../../Config.h"

#include "../../Utils/Math/MathConstants.hlsli"

BEGIN_NAMESPACE_FALCOR

/** Types of light sources. Used in LightData structure.
*/
enum class AnalyticLightType : uint32_t
{
    Point       = 0,    ///< Point light source, can be a spot light if its opening angle is < 2pi
    Directional = 1,    ///< Directional light source
    Distant     = 2,    ///< Distant light that subtends a non-zero solid angle
    Rect        = 3,    ///< Quad shaped area light source
    Disc        = 4,    ///< Disc shaped area light source
    Sphere      = 5,    ///< Spherical area light source
};

/** This is a host/device structure that describes analytic light sources.
*/
struct AnalyticLightData
{
    float3   posW               ;                   ///< World-space position of the center of a light source
    uint32_t type               ;                   ///< Type of the light source (see above)
    float3   dirW               ;                   ///< World-space orientation of the light source (normalized).
    float    openingAngle       ;                   ///< For point (spot) light: Opening half-angle of a spot light cut-off, pi by default (full sphere).
    float3   intensity          ;                   ///< Emitted radiance of th light source
    float    cosOpeningAngle    ;                   ///< For point (spot) light: cos(openingAngle), -1 by default because openingAngle is pi by default
    float    cosSubtendedAngle  ;                   ///< For distant light; cosine of the half-angle subtended by the light. Default corresponds to the sun as viewed from earth
    float    penumbraAngle      ;                   ///< For point (spot) light: Opening half-angle of penumbra region in radians, usually does not exceed openingAngle. 0.f by default, meaning a spot light with hard cut-off
    float2   _pad0;

    // Extra parameters for analytic area lights
    float3   tangent            ;                   ///< Tangent vector of the light shape
    float    surfaceArea        ;                   ///< Surface area of the light shape
    float3   bitangent          ;                   ///< Bitangent vector of the light shape
    float    _pad1;             ;           
    float4x4 transMat           ;                   ///< Transformation matrix of the light shape, from local to world space.
    float4x4 transMatIT         ;                   ///< Inverse-transpose of transformation matrix of the light shape

    static AnalyticLightData make()
    {
        AnalyticLightData ret;
        ret.posW                = float3(0, 0, 0);
        ret.type                = (uint)AnalyticLightType::Point;
        ret.dirW                = float3(0, -1, 0); 
        ret.openingAngle        = float(M_PI);
        ret.intensity           = float3(1, 1, 1);
        ret.cosOpeningAngle     = -1.f;
        ret.cosSubtendedAngle   = 0.9999893f;
        ret.penumbraAngle       = 0.f;
        ret.tangent             = float3(0,0,0);
        ret.surfaceArea         = 0.f;
        ret.bitangent           = float3(0,0,0);
        ret._pad1               = 0;
        ret.transMat            = float4x4(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1);
        ret.transMatIT          = float4x4(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1);
        return ret;
    }
};

END_NAMESPACE_FALCOR

#endif // __LIGHT_DATA_PARAMS__