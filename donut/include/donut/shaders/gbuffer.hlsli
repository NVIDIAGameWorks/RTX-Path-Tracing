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

#ifndef GBUFFER_HLSLI
#define GBUFFER_HLSLI

#include <donut/shaders/view_cb.h>
#include <donut/shaders/surface.hlsli>

MaterialSample DecodeGBuffer(float4 channels[4])
{
    MaterialSample surface = DefaultMaterialSample();
    
    surface.diffuseAlbedo = channels[0].xyz;
    surface.opacity = channels[0].w;
    surface.specularF0 = channels[1].xyz;
    surface.occlusion = channels[1].w;
    surface.shadingNormal = channels[2].xyz;
    surface.roughness = channels[2].w;
    surface.emissiveColor = channels[3].xyz;
    surface.geometryNormal = surface.shadingNormal;

    return surface;
}

MaterialSample DecodeGBuffer(int2 pixelPosition, Texture2D tex0, Texture2D tex1, Texture2D tex2, Texture2D tex3)
{
    float4 gbufferChannels[4];
    gbufferChannels[0] = tex0[pixelPosition.xy];
    gbufferChannels[1] = tex1[pixelPosition.xy];
    gbufferChannels[2] = tex2[pixelPosition.xy];
    gbufferChannels[3] = tex3[pixelPosition.xy];
    return DecodeGBuffer(gbufferChannels);
}

float4 ReconstructClipPosition(PlanarViewConstants view, float2 pixelPosition, float depth)
{
    float2 uv = (pixelPosition - view.viewportOrigin) * view.viewportSizeInv;
    return float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
}

float3 ReconstructViewPosition(PlanarViewConstants view, float2 pixelPosition, float depth)
{
    float4 clipPos = ReconstructClipPosition(view, pixelPosition, depth);
    float4 worldPos = mul(clipPos, view.matClipToView);
    return worldPos.xyz / worldPos.w;
}

float3 ReconstructWorldPosition(PlanarViewConstants view, float2 pixelPosition, float depth)
{
    float4 clipPos = ReconstructClipPosition(view, pixelPosition, depth);
    float4 worldPos = mul(clipPos, view.matClipToWorld);
    return worldPos.xyz / worldPos.w;
}

float3 GetIncidentVector(float4 cameraDirectionOrPosition, float3 surfacePos)
{
    if (cameraDirectionOrPosition.w > 0)
        return normalize(surfacePos.xyz - cameraDirectionOrPosition.xyz);
    else
        return cameraDirectionOrPosition.xyz;
}

#endif // GBUFFER_HLSLI