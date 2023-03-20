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

#include "light_cb.h"

float sharpen(float x, float sharpening)
{
    if (x < 0.5)
        return 0.5 * pow(2.0*x, sharpening);
    else
        return -0.5 * pow(-2.0*x + 2.0, sharpening) + 1.0;
}

float4 ShadowWithMaxDistance(float4 shadowMapValues, float reference, float maxDistance)
{
    return 1.0 - (reference > shadowMapValues) * (maxDistance == 0 ? 1 : saturate((shadowMapValues + maxDistance - reference) * 10));
}

float2 EvaluateShadowGather16(Texture2DArray ShadowMapArray, SamplerState ShadowSampler, ShadowConstants shadowParams, float3 worldPos, float2 shadowMapTextureSize)
{
    float4 uvzwShadow = mul(float4(worldPos, 1), shadowParams.matWorldToUvzwShadow);

    if (uvzwShadow.w <= 0)
        return 0;

    float3 uvzShadow = uvzwShadow.xyz / uvzwShadow.w;
    
    if(shadowParams.shadowFalloffDistance == 0)
        uvzShadow.z = min(uvzShadow.z, 0.999999);

    float2 fadeUV = saturate(abs(uvzShadow.xy - shadowParams.shadowMapCenterUV) * shadowParams.shadowFadeScale + shadowParams.shadowFadeBias);
    float fade = fadeUV.x * fadeUV.y;

    if (shadowParams.shadowFalloffDistance > 0)
        fade *= saturate((1.0 - uvzShadow.z) * 10);

    if (fade == 0)
        return 0;

    float3 sampleLocation = float3(uvzShadow.xy, shadowParams.shadowMapArrayIndex);

    // Do the samples - each one a 2x2 GatherCmp
    float4 samplesNW = ShadowMapArray.GatherRed(ShadowSampler, sampleLocation, int2(-1, -1));
    float4 samplesNE = ShadowMapArray.GatherRed(ShadowSampler, sampleLocation, int2(1, -1));
    float4 samplesSW = ShadowMapArray.GatherRed(ShadowSampler, sampleLocation, int2(-1, 1));
    float4 samplesSE = ShadowMapArray.GatherRed(ShadowSampler, sampleLocation, int2(1, 1));

    samplesNW = ShadowWithMaxDistance(samplesNW, uvzShadow.z, shadowParams.shadowFalloffDistance);
    samplesNE = ShadowWithMaxDistance(samplesNE, uvzShadow.z, shadowParams.shadowFalloffDistance);
    samplesSW = ShadowWithMaxDistance(samplesSW, uvzShadow.z, shadowParams.shadowFalloffDistance);
    samplesSE = ShadowWithMaxDistance(samplesSE, uvzShadow.z, shadowParams.shadowFalloffDistance);

    // Calculate fractional location relative to texel centers.  The 1/512 offset is needed to ensure
    // that frac()'s output steps from 1 to 0 at the exact same point that GatherCmp switches texels.
    float2 offset = frac(uvzShadow.xy * shadowMapTextureSize + (-0.5 + 1.0 / 512.0));

    // Calculate weights for the samples based on a 2px-radius biquadratic filter
    static const float radius = 2.0;
    float4 xOffsets = offset.x + float4(1, 0, -1, -2);
    float4 yOffsets = offset.y + float4(1, 0, -1, -2);

    // Readable version: xWeights = max(0, 1 - x^2/r^2) for x in xOffsets
    float4 xWeights = saturate(square(xOffsets) * (-1.0 / square(radius)) + 1.0);
    float4 yWeights = saturate(square(yOffsets) * (-1.0 / square(radius)) + 1.0);

    // Calculate weighted sum of samples
    float sampleSum = dot(xWeights.xyyx, yWeights.yyxx * samplesNW) +
        dot(xWeights.zwwz, yWeights.yyxx * samplesNE) +
        dot(xWeights.xyyx, yWeights.wwzz * samplesSW) +
        dot(xWeights.zwwz, yWeights.wwzz * samplesSE);

    float weightSum = dot(xWeights.xyyx, yWeights.yyxx) +
        dot(xWeights.zwwz, yWeights.yyxx) +
        dot(xWeights.xyyx, yWeights.wwzz) +
        dot(xWeights.zwwz, yWeights.wwzz);

    const float shadowSharpening = 1.0;
    float shadow = sharpen(saturate(sampleSum / weightSum), shadowSharpening);
    return float2(shadow * fade, fade);
}

float2 EvaluateShadowPCF(Texture2DArray ShadowMapArray, SamplerComparisonState ShadowSampler, ShadowConstants shadowParams, float3 worldPos)
{
    float4 uvzwShadow = mul(float4(worldPos, 1), shadowParams.matWorldToUvzwShadow);

    if (uvzwShadow.w <= 0)
        return 0;

    float3 uvzShadow = uvzwShadow.xyz / uvzwShadow.w;
    float2 fadeUV = saturate(abs(uvzShadow.xy - shadowParams.shadowMapCenterUV) * shadowParams.shadowFadeScale + shadowParams.shadowFadeBias);
    float fade = fadeUV.x * fadeUV.y;
    if (fade == 0)
        return 0;

    float3 sampleLocation = float3(uvzShadow.xy, shadowParams.shadowMapArrayIndex);

    float shadow = ShadowMapArray.SampleCmpLevelZero(ShadowSampler, sampleLocation, min(uvzShadow.z, 0.999999));
    return float2(shadow * fade, fade);
}

static const float2 g_ShadowSamplePositions[] = {

    // Poisson disk with 16 points : 0 - 15
  float2(-0.3935238f, 0.7530643f),
  float2(-0.3022015f, 0.297664f),
  float2(0.09813362f, 0.192451f),
  float2(-0.7593753f, 0.518795f),
  float2(0.2293134f, 0.7607011f),
  float2(0.6505286f, 0.6297367f),
  float2(0.5322764f, 0.2350069f),
  float2(0.8581018f, -0.01624052f),
  float2(-0.6928226f, 0.07119545f),
  float2(-0.3114384f, -0.3017288f),
  float2(0.2837671f, -0.179743f),
  float2(-0.3093514f, -0.749256f),
  float2(-0.7386893f, -0.5215692f),
  float2(0.3988827f, -0.617012f),
  float2(0.8114883f, -0.458026f),
  float2(0.08265103f, -0.8939569f),

};


float2 EvaluateShadowPoisson(Texture2DArray ShadowMapArray, SamplerComparisonState ShadowSampler, ShadowConstants shadowParams, float3 worldPos, float2 sincosRotationAngle, float diskSizeTexels)
{
    float4 uvzwShadow = mul(float4(worldPos, 1), shadowParams.matWorldToUvzwShadow);

    if (uvzwShadow.w <= 0)
        return 0;

    float3 uvzShadow = uvzwShadow.xyz / uvzwShadow.w;

    if (shadowParams.shadowFalloffDistance == 0)
        uvzShadow.z = min(uvzShadow.z, 0.999999);

    float2 fadeUV = saturate(abs(uvzShadow.xy - shadowParams.shadowMapCenterUV) * shadowParams.shadowFadeScale + shadowParams.shadowFadeBias);
    float fade = fadeUV.x * fadeUV.y;

    if (shadowParams.shadowFalloffDistance > 0)
        fade *= saturate((1.0 - uvzShadow.z) * 10);

    if (fade == 0)
        return 0;

    float shadow = 0;

    [unroll]
    for (uint nSample = 0; nSample < 16; ++nSample)
    {
        float2 offset = g_ShadowSamplePositions[nSample];
        offset = float2(
            offset.x * sincosRotationAngle.x - offset.y * sincosRotationAngle.y,
            offset.x * sincosRotationAngle.y + offset.y * sincosRotationAngle.x
            );

        offset *= shadowParams.shadowMapSizeTexelsInv * diskSizeTexels;

        float shadowSample = ShadowMapArray.SampleCmpLevelZero(
            ShadowSampler,
            float3(uvzShadow.xy + offset.xy, shadowParams.shadowMapArrayIndex),
            uvzShadow.z);
        shadow += shadowSample;
    }

    shadow /= 16;
    return float2(shadow * fade, fade);
}