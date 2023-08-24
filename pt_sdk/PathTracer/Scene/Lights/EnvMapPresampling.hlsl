/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

/** This is a PT-SDK specific (independent from RTXDI) pass for presampling environment map.
*/

//import Utils.Math.MathHelpers;
//import Utils.Color.ColorHelpers;
#include "../../Utils/Math/MathHelpers.hlsli"
#include "../../Utils/Color/ColorHelpers.hlsli"
#include "../../../Lights/EnvironmentMapImportanceSampling_cb.h"

#include "EnvMapSampler.hlsli"
#include "../../Sampling.hlsli"
#include "../../Utils.hlsli"

ConstantBuffer<EnvironmentMapImportanceSamplingConstants> g_Const   : register(b0);
SamplerState        s_EnvSampler                                    : register(s0);
SamplerState        s_ImportanceSampler                             : register(s1);
Texture2D<float4>   t_EnvMap                                        : register(t0);
Texture2D<float>    t_ImportanceMap                                 : register(t1);

RWBuffer<uint2>     u_PresampledBuffer                              : register(u0);

//#pragma warning(disable : 4996)

[numthreads(256, 1, 1)]
void main(uint dispatchThreadID : SV_DispatchThreadID)
{
    // Each frame, this will generate 1 environment sample per thread for a total of ENVMAP_PRESAMPLED_COUNT samples
    // These samples are then used by the path tracer for a much faster sampling.

    EnvMapSampler envMapSampler = EnvMapSampler::make(
        s_ImportanceSampler, t_ImportanceMap,
        g_Const.envMapSamplerData.importanceInvDim,
        g_Const.envMapSamplerData.importanceBaseMip,
        t_EnvMap, s_EnvSampler, g_Const.envMapData );
    
    SampleGenerator sampleGenerator = SampleGenerator::make( uint2(0,0), 0, g_Const.sampleIndex * ENVMAP_PRESAMPLED_COUNT + dispatchThreadID );
    
    float2 uv = sampleNext2D(sampleGenerator);
    
    EnvMapSample result = envMapSampler.sample(uv);
    
    // More precise variant would be to store final sampling uv in equal-area-octahedral space; that requires extending EnvMapSampler interface
    // but also using oct_to_ndir_equal_area_unorm and transform to world space (envmap can have rotation) on the unpacking side. The 
    // performance cost of this is noticeable especially with multiple samples and I did not test quality difference.
    u_PresampledBuffer[dispatchThreadID] = uint2( PackTwoFp32ToFp16(result.dir.x, result.dir.y), PackTwoFp32ToFp16(result.dir.z, result.pdf) );
}
