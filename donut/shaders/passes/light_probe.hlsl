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

#include <donut/shaders/brdf.hlsli>

// GS to replicate a quad into 6 cube faces

struct GSInput
{
    float4 position : SV_Position;
    float2 uv : UV;
};

struct GSOutput
{
    float4 position : SV_Position;
    float2 uv : UV;
    uint arrayIndex : SV_RenderTargetArrayIndex;
};

[maxvertexcount(3)]
[instance(6)]
void cubemap_gs(
    triangle GSInput Input[3],
    uint instanceID : SV_GSInstanceID,
    inout TriangleStream<GSOutput> Output)
{
    GSOutput OutputVertex;
    OutputVertex.arrayIndex = instanceID;

    OutputVertex.position = Input[0].position;
    OutputVertex.uv = Input[0].uv;
    Output.Append(OutputVertex);

    OutputVertex.position = Input[1].position;
    OutputVertex.uv = Input[1].uv;
    Output.Append(OutputVertex);

    OutputVertex.position = Input[2].position;
    OutputVertex.uv = Input[2].uv;
    Output.Append(OutputVertex);
}

// Helpers

float radicalInverse(uint i)
{
    i = (i & 0x55555555) << 1 | (i & 0xAAAAAAAA) >> 1;
    i = (i & 0x33333333) << 2 | (i & 0xCCCCCCCC) >> 2;
    i = (i & 0x0F0F0F0F) << 4 | (i & 0xF0F0F0F0) >> 4;
    i = (i & 0x00FF00FF) << 8 | (i & 0xFF00FF00) >> 8;
    i = (i << 16) | (i >> 16);
    return float(i) * 2.3283064365386963e-10f;
}

float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), radicalInverse(i));
}

float3 uvToDirection(float2 uv, uint face)
{
    float3 direction = float3(uv.x * 2 - 1, 1 - uv.y * 2, 1);

    // Inverse cube face view matrices
    switch (face)
    {
    case 0: direction.xyz = float3(direction.z, direction.y, -direction.x); break;
    case 1: direction.xyz = float3(-direction.z, direction.y, direction.x); break;
    case 2: direction.xyz = float3(direction.x, direction.z, -direction.y); break;
    case 3: direction.xyz = float3(direction.x, -direction.z, direction.y); break;
    case 4: direction.xyz = float3(direction.x, direction.y, direction.z);  break;
    case 5: direction.xyz = float3(-direction.x, direction.y, -direction.z); break;
    }

    return normalize(direction);
}

// Integration

// The implementation is based on the "Real Shading in Unreal Engine 4" paper by Brian Karis.

#include <donut/shaders/light_probe_cb.h>

cbuffer c_LightProbe : register(b0)
{
    LightProbeConstants g_LightProbe;
};

TextureCube t_EnvironmentMap : register(t0);
SamplerState s_EnvironmentMapSampler : register(s0);


void mip_ps(
    in GSOutput Input,
    out float4 o_color : SV_Target0)
{
    float3 direction = uvToDirection(Input.uv, Input.arrayIndex);

    o_color = t_EnvironmentMap.SampleLevel(s_EnvironmentMapSampler, direction, 0);
}

void diffuse_probe_ps(
    in GSOutput Input,
    out float4 o_color : SV_Target0)
{
    float3 N = uvToDirection(Input.uv, Input.arrayIndex);
    float3 T, B;
    ConstructONB(N, T, B);

    float4 totalRadiance = 0;
    
    for (uint i = 0; i < g_LightProbe.sampleCount; i++)
    {
        float2 random = Hammersley(i, g_LightProbe.sampleCount);
        float solidAnglePdf;
        float3 Le = SampleCosHemisphere(random, solidAnglePdf);
        float3 L = Le.x * T + Le.y * B + Le.z * N;

        float4 radiance = t_EnvironmentMap.SampleLevel(s_EnvironmentMapSampler, L, g_LightProbe.lodBias);

        totalRadiance += radiance;
    }

    totalRadiance /= K_PI * float(g_LightProbe.sampleCount);

    o_color = totalRadiance;
}

void specular_probe_ps(
    in GSOutput Input,
    out float4 o_color : SV_Target0)
{
    float3 R = uvToDirection(Input.uv, Input.arrayIndex);
    float3 N = R;
    float3 V = R;

    float3 T, B;
    ConstructONB(N, T, B);


    float4 totalRadiance = 0;
    float totalWeight = 0;

    for (uint i = 0; i < g_LightProbe.sampleCount; i++)
    {
        float2 random = Hammersley(i, g_LightProbe.sampleCount);
        float3 He = ImportanceSampleGGX(random, g_LightProbe.roughness);
        float3 H = He.x * T + He.y * B + He.z * N;

        float3 L = reflect(-N, H);
        float NdotL = saturate(dot(N, L));

        if (NdotL > 0)
        {
            float4 radiance = t_EnvironmentMap.SampleLevel(s_EnvironmentMapSampler, L, g_LightProbe.lodBias);

            totalRadiance += radiance * NdotL;
            totalWeight += NdotL;
        }
    }

    o_color = totalRadiance / totalWeight;
}

void environment_brdf_ps(
    in float4 position : SV_Position,
    in float2 uv : UV,
    out float4 o_color : SV_Target0)
{
    float NoV = uv.x;
    float roughness = uv.y;
    
    float3 V;
    V.x = sqrt( 1.0f - NoV * NoV ); // sin
    V.y = 0;
    V.z = NoV; // cos

    float A = 0;
    float B = 0;

    const uint NumSamples = 1024;
    for (uint i = 0; i < NumSamples; i++)
    {
        float2 random = Hammersley(i, NumSamples);
        float3 H = ImportanceSampleGGX(random, roughness);
        float3 L = 2 * dot( V, H ) * H - V;

        float NoL = saturate( L.z );
        float NoH = saturate( H.z );
        float VoH = saturate( dot( V, H ) );

        if( NoL > 0 )
        {
            float G_over_NdotV = G_Smith_over_NdotV(roughness, NoV, NoL);

            float G_Vis = G_over_NdotV * VoH / NoH;
            float Fc = pow( 1 - VoH, 5 );
            A += (1 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }

    o_color.rg = float2( A, B ) / NumSamples;
    o_color.ba = 0;
}