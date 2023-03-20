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

#pragma pack_matrix(row_major)

#include <donut/shaders/sky_cb.h>

float3 ProceduralSky(ProceduralSkyShaderParameters params, float3 direction, float angularSizeOfPixel)
{
    float elevation = asin(clamp(dot(direction, params.directionUp), -1.0, 1.0));
    float top = smoothstep(0.f, params.horizonSize, elevation);
    float bottom = smoothstep(0.f, params.horizonSize, -elevation);
    float3 environment = lerp(lerp(params.horizonColor, params.groundColor, bottom), params.skyColor, top);

    float angleToLight = acos(saturate(dot(direction, params.directionToLight)));
    float halfAngularSize = params.angularSizeOfLight * 0.5;
    float lightIntensity = saturate(1.0 - smoothstep(halfAngularSize - angularSizeOfPixel * 2, halfAngularSize + angularSizeOfPixel * 2, angleToLight));
    lightIntensity = pow(lightIntensity, 4.0);
    float glowInput = saturate(2.0 * (1.0 - smoothstep(halfAngularSize - params.glowSize, halfAngularSize + params.glowSize, angleToLight)));
    float glowIntensity = params.glowIntensity * pow(glowInput, params.glowSharpness);
    float3 light = max(lightIntensity, glowIntensity) * params.lightColor;
    
    return environment + light;
}
