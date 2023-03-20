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

#ifndef DEFERRED_LIGHTING_CB_H
#define DEFERRED_LIGHTING_CB_H

#include "light_cb.h"
#include "view_cb.h"

#define DEFERRED_MAX_LIGHTS 16
#define DEFERRED_MAX_SHADOWS 16
#define DEFERRED_MAX_LIGHT_PROBES 16

struct DeferredLightingConstants
{
    PlanarViewConstants view;

    float2      shadowMapTextureSize;
    int         enableAmbientOcclusion;
    int         padding;

    float4      ambientColorTop;
    float4      ambientColorBottom;

    uint        numLights;
    uint        numLightProbes;
    float       indirectDiffuseScale;
    float       indirectSpecularScale;

    float2      randomOffset;
    float2      padding2;

    float4      noisePattern[4];

    LightConstants lights[DEFERRED_MAX_LIGHTS];
    ShadowConstants shadows[DEFERRED_MAX_SHADOWS];
    LightProbeConstants lightProbes[DEFERRED_MAX_LIGHT_PROBES];
};

#endif // DEFERRED_LIGHTING_CB_H