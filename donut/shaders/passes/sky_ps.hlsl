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

#include <donut/shaders/sky.hlsli>

cbuffer c_Sky : register(b0)
{
    SkyConstants g_Sky;
};

void main(
    in float4 i_position : SV_Position,
    in float2 i_uv : UV,
    out float4 o_color : SV_Target0
)
{
    float4 clipPos;
    clipPos.x = i_uv.x * 2 - 1;
    clipPos.y = 1 - i_uv.y * 2;
    clipPos.z = 0.5;
    clipPos.w = 1;
    float4 translatedWorldPos = mul(clipPos, g_Sky.matClipToTranslatedWorld);
    float3 direction = normalize(translatedWorldPos.xyz / translatedWorldPos.w);

    // length(ddx(dir)) is an approximation for acos(dot(dir, normalize(dir + ddx(dir))) for unit vectors with small derivatives
    float angularSizeOfPixel = max(length(ddx(direction)), length(ddy(direction)));

    o_color.rgb = ProceduralSky(g_Sky.params, direction, angularSizeOfPixel);
    o_color.a = 0;
}
