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

cbuffer c_Sky : register(b0)
{
    SkyConstants g_Sky;
};

#if LATLONG_TEXTURE
Texture2D t_EnvironmentMap : register(t0);
#else
TextureCube t_EnvironmentMap : register(t0);
#endif
SamplerState s_Sampler : register(s0);

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

#if LATLONG_TEXTURE
    float elevation = asin(direction.y);
    float azimuth = 0;
    if (abs(direction.y) < 1)
        azimuth = atan2(direction.z, direction.x);

    const float PI = 3.14159265;
    float2 uv;
    uv.x = azimuth / (2 * PI) - 0.25;
    uv.y = 0.5 - elevation / PI;

    o_color = t_EnvironmentMap.SampleLevel(s_Sampler, uv, 0);
#else
    o_color = t_EnvironmentMap.Sample(s_Sampler, float3(direction.x, direction.y, -direction.z));
#endif
}
