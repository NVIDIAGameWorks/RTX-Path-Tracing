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

#include <donut/shaders/bloom_cb.h>

cbuffer c_Bloom : register(b0)
{
    BloomConstants g_Bloom;
};

SamplerState s_Sampler : register(s0);
Texture2D<float4> t_Src : register(t0);

float square(float x) { return x * x; }

void main(
    in float4 pos : SV_Position,
    in float2 uv : UV,
    out float4 o_rgba : SV_Target
)
{
    float3 result = t_Src[pos.xy].rgb;

    for (float x = 1; x < g_Bloom.numSamples; x += 2)
    {
        float w1 = exp(square(x) * g_Bloom.argumentScale);
        float w2 = exp(square(x + 1) * g_Bloom.argumentScale);

        float w12 = w1 + w2;
        float p = w2 / w12;
        float2 offset = g_Bloom.pixstep * (x + p);

        result += t_Src.SampleLevel(s_Sampler, uv + offset, 0).rgb * w12;
        result += t_Src.SampleLevel(s_Sampler, uv - offset, 0).rgb * w12;
    }

    result *= g_Bloom.normalizationScale;

    o_rgba = float4(result, 1.0);
}
