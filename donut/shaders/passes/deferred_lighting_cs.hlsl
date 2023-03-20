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

#include <donut/shaders/gbuffer.hlsli>
#include <donut/shaders/lighting.hlsli>
#include <donut/shaders/shadows.hlsli>
#include <donut/shaders/deferred_lighting_cb.h>

cbuffer c_Deferred : register(b0)
{
    DeferredLightingConstants g_Deferred;
};

Texture2DArray t_ShadowMapArray : register(t0);
TextureCubeArray t_DiffuseLightProbe : register(t1);
TextureCubeArray t_SpecularLightProbe : register(t2);
Texture2D t_EnvironmentBrdf : register(t3);

SamplerState s_ShadowSampler : register(s0);
SamplerComparisonState s_ShadowSamplerComparison : register(s1);
SamplerState s_LightProbeSampler : register(s2);
SamplerState s_BrdfSampler : register(s3);

Texture2D t_GBufferDepth : register(t8);
Texture2D t_GBuffer0 : register(t9);
Texture2D t_GBuffer1 : register(t10);
Texture2D t_GBuffer2 : register(t11);
Texture2D t_GBuffer3 : register(t12);

Texture2D t_IndirectDiffuse : register(t14);
Texture2D t_IndirectSpecular : register(t15);
Texture2D t_ShadowBuffer : register(t16);
Texture2D t_AmbientOcclusion : register(t17);

RWTexture2D<float4> u_Output : register(u0);

float GetRandom(float2 pos)
{
    int x = int(pos.x) & 3;
    int y = int(pos.y) & 3;
    return g_Deferred.noisePattern[y][x];
}

[numthreads(16, 16, 1)]
void main(int2 i_globalIdx : SV_DispatchThreadID)
{
    if (any(i_globalIdx.xy >= int2(g_Deferred.view.viewportSize)))
        return;

    int2 pixelPosition = i_globalIdx.xy + int2(g_Deferred.view.viewportOrigin);

    float4 gbufferChannels[4];
    gbufferChannels[0] = t_GBuffer0[pixelPosition];
    gbufferChannels[1] = t_GBuffer1[pixelPosition];
    gbufferChannels[2] = t_GBuffer2[pixelPosition];
    gbufferChannels[3] = t_GBuffer3[pixelPosition];
    MaterialSample surfaceMaterial = DecodeGBuffer(gbufferChannels);

    float3 surfaceWorldPos = ReconstructWorldPosition(g_Deferred.view, float2(pixelPosition) + 0.5, t_GBufferDepth[pixelPosition].x);
        
    float3 viewIncident = GetIncidentVector(g_Deferred.view.cameraDirectionOrPosition, surfaceWorldPos);

    float3 diffuseTerm = 0;
    float3 specularTerm = 0;
    float angle = GetRandom(i_globalIdx.xy + g_Deferred.randomOffset);
    float2 sincos = float2(sin(angle), cos(angle));

    [loop]
    for (uint nLight = 0; nLight < g_Deferred.numLights; nLight++)
    {
        LightConstants light = g_Deferred.lights[nLight];

        float shadow = 1;

        if ((light.shadowChannel.x & 0xfffffffc) == 0) // check that the channel is between 0 and 3
        {
            float4 channels = t_ShadowBuffer[pixelPosition];
            shadow = channels[light.shadowChannel.x];
        }

        float2 combinedCascadeShadow = 0;

        [loop]
        for (int cascade = 0; cascade < 4; cascade++)
        {
            if (light.shadowCascades[cascade] >= 0)
            {
                float2 cascadeShadow = EvaluateShadowPoisson(t_ShadowMapArray, s_ShadowSamplerComparison, g_Deferred.shadows[light.shadowCascades[cascade]], surfaceWorldPos, sincos, 3.0);

                combinedCascadeShadow = saturate(combinedCascadeShadow + cascadeShadow * (1.0001 - combinedCascadeShadow.y));

                if (combinedCascadeShadow.y == 1)
                    break;
            }
            else
                break;
        }

        combinedCascadeShadow.x += (1 - combinedCascadeShadow.y) * light.outOfBoundsShadow;

        shadow *= combinedCascadeShadow.x;
        
        float objectShadow = 1;

        [loop]
        for (int object = 0; object < 4; object++)
        {
            if (light.perObjectShadows[object] >= 0)
            {
                float2 thisObjectShadow = EvaluateShadowPoisson(t_ShadowMapArray, s_ShadowSamplerComparison, g_Deferred.shadows[light.perObjectShadows[object]], surfaceWorldPos, sincos, 3.0);

                objectShadow *= saturate(thisObjectShadow.x + (1 - thisObjectShadow.y));
            }
        }

        shadow *= objectShadow;

        float3 diffuseRadiance, specularRadiance;
        ShadeSurface(light, surfaceMaterial, surfaceWorldPos, viewIncident, diffuseRadiance, specularRadiance);

        diffuseTerm += (shadow.x * diffuseRadiance) * light.color;
        specularTerm += (shadow.x * specularRadiance) * light.color;
    }

    float ambientOcclusion = 1;
    if (g_Deferred.enableAmbientOcclusion != 0)
    {
        ambientOcclusion = t_AmbientOcclusion[pixelPosition].x;
    }

    if (g_Deferred.numLightProbes > 0)
    {
        float3 N = surfaceMaterial.shadingNormal;
        float3 R = reflect(viewIncident, N);
        float NdotV = saturate(-dot(N, viewIncident));
        float2 environmentBrdf = t_EnvironmentBrdf.SampleLevel(s_BrdfSampler, float2(NdotV, surfaceMaterial.roughness), 0).xy;

        float lightProbeWeight = 0;
        float3 lightProbeDiffuse = 0;
        float3 lightProbeSpecular = 0;

        [loop]
        for (uint nProbe = 0; nProbe < g_Deferred.numLightProbes; nProbe++)
        {
            LightProbeConstants lightProbe = g_Deferred.lightProbes[nProbe];

            float weight = GetLightProbeWeight(lightProbe, surfaceWorldPos);

            if (weight == 0)
                continue;

            float specularMipLevel = sqrt(saturate(surfaceMaterial.roughness)) * (lightProbe.mipLevels - 1);
            float3 diffuseProbe = t_DiffuseLightProbe.SampleLevel(s_LightProbeSampler, float4(N.xyz, lightProbe.diffuseArrayIndex), 0).rgb;
            float3 specularProbe = t_SpecularLightProbe.SampleLevel(s_LightProbeSampler, float4(R.xyz, lightProbe.specularArrayIndex), specularMipLevel).rgb;

            lightProbeDiffuse += (weight * lightProbe.diffuseScale) * diffuseProbe;
            lightProbeSpecular += (weight * lightProbe.specularScale) * specularProbe;
            lightProbeWeight += weight;
        }

        if (lightProbeWeight > 1)
        {
            float invWeight = rcp(lightProbeWeight);
            lightProbeDiffuse *= invWeight;
            lightProbeSpecular *= invWeight;
        }

        diffuseTerm += lightProbeDiffuse * surfaceMaterial.diffuseAlbedo * ambientOcclusion * surfaceMaterial.occlusion;
        specularTerm += lightProbeSpecular * (surfaceMaterial.specularF0 * environmentBrdf.x + environmentBrdf.y) * ambientOcclusion * surfaceMaterial.occlusion;
    }

    {
        float3 ambientColor = lerp(g_Deferred.ambientColorBottom.rgb, g_Deferred.ambientColorTop.rgb, surfaceMaterial.shadingNormal.y * 0.5 + 0.5);

        diffuseTerm += ambientColor * surfaceMaterial.diffuseAlbedo * ambientOcclusion * surfaceMaterial.occlusion;
        specularTerm += ambientColor * surfaceMaterial.specularF0 * ambientOcclusion * surfaceMaterial.occlusion;
    }

    if (g_Deferred.indirectDiffuseScale > 0)
    {
        float4 indirectDiffuse = t_IndirectDiffuse[pixelPosition];
        diffuseTerm += indirectDiffuse.rgb * g_Deferred.indirectDiffuseScale * ambientOcclusion;
    }

    if (g_Deferred.indirectSpecularScale > 0)
    {
        float4 indirectSpecular = t_IndirectSpecular[pixelPosition];
        specularTerm += indirectSpecular.rgb * g_Deferred.indirectSpecularScale;
    }

    float3 outputColor = diffuseTerm
        + specularTerm
        + surfaceMaterial.emissiveColor;
    
    u_Output[pixelPosition] = float4(outputColor, 0);
}
