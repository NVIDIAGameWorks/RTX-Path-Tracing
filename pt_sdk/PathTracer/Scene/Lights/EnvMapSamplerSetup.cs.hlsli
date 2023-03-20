/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

/** Compute shader for building a hierarchical importance map from an
    environment map. The result is used by EnvMapSampler.hlsli for sampling.
*/

//import Utils.Math.MathHelpers;
//import Utils.Color.ColorHelpers;
#include "../../Utils/Math/MathHelpers.hlsli"
#include "../../Utils/Color/ColorHelpers.hlsli"
#include "../../../Lights/EnvironmentMapImportanceSampling_cb.h"

/*
cbuffer CB
{
    uint2 outputDim;            // Resolution of the importance map in texels.
    uint2 outputDimInSamples;   // Resolution of the importance map in samples.
    uint2 numSamples;           // Per-texel subsamples s.xy at finest mip.
    float invSamples;           // 1 / (s.x*s.y).
    float _padding0; 
};
*/

ConstantBuffer<EnvironmentMapImportanceSamplingConstants> g_Const : register(b0);
SamplerState gEnvSampler : register(s0);
Texture2D<float4> gEnvMap  : register(t0);
RWTexture2D<float> gImportanceMap : register(u0);

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
   uint2 outputDim = g_Const.outputDim;                 
   uint2 outputDimInSamples = g_Const.outputDimInSamples;  
   uint2 numSamples = g_Const.numSamples;         
   float invSamples = g_Const.invSamples;         

    uint2 pixel = dispatchThreadID.xy;
    if (any(pixel >= outputDim)) return;

    float L = 0.f;
    for (uint y = 0; y < numSamples.y; y++)
    {
        for (uint x = 0; x < numSamples.x; x++)
        {
            // Compute sample pos p in [0,1)^2 in octahedral map.
            uint2 samplePos = pixel * numSamples + uint2(x, y);
            float2 p = ((float2)samplePos + 0.5f) / outputDimInSamples;

            // Convert p to (u,v) coordinate in latitude-longitude map.
            float3 dir = oct_to_ndir_equal_area_unorm(p);
            float2 uv = world_to_latlong_map(dir);

            // Accumulate the radiance from this sample.
            float3 radiance = gEnvMap.SampleLevel(gEnvSampler, uv, 0).rgb;
            L += luminance(radiance);
        }
    }

    // Store average radiance for this texel.
    gImportanceMap[pixel] = L * invSamples;
}
