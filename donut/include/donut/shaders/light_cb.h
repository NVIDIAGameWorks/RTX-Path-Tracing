/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#ifndef LIGHT_CB_H
#define LIGHT_CB_H

#include "light_types.h"

struct ShadowConstants
{
    float4x4 matWorldToUvzwShadow;

    float2 shadowFadeScale;
    float2 shadowFadeBias;

    float2 shadowMapCenterUV;
    float shadowFalloffDistance;
    int shadowMapArrayIndex;

    float2 shadowMapSizeTexels;
    float2 shadowMapSizeTexelsInv;
};

struct LightConstants
{
    float3 direction;
    int lightType;

    float3 position;
    float radius;

    float3 color;
    float intensity; // illuminance (lm/m2) for directional lights, luminous intensity (lm/sr) for positional lights

    float angularSizeOrInvRange;   // angular size for directional lights, 1/range for spot and point lights
    float innerAngle;
    float outerAngle;
    float outOfBoundsShadow;

    int4 shadowCascades;
    int4 perObjectShadows;

    int4 shadowChannel;
};

struct LightProbeConstants
{
    float diffuseScale;
    float specularScale;
    float mipLevels;
    float padding1;

    uint diffuseArrayIndex;
    uint specularArrayIndex;
    uint2 padding2;

    float4 frustumPlanes[6];
};

#endif // LIGHT_CB_H