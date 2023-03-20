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

// Bindings - can be overriden before including this file if necessary

#ifndef MATERIAL_CB_SLOT 
#define MATERIAL_CB_SLOT b0
#endif

#ifndef MATERIAL_DIFFUSE_SLOT 
#define MATERIAL_DIFFUSE_SLOT t0
#endif

#ifndef MATERIAL_SPECULAR_SLOT 
#define MATERIAL_SPECULAR_SLOT t1
#endif

#ifndef MATERIAL_NORMALS_SLOT 
#define MATERIAL_NORMALS_SLOT t2
#endif

#ifndef MATERIAL_EMISSIVE_SLOT 
#define MATERIAL_EMISSIVE_SLOT t3
#endif

#ifndef MATERIAL_OCCLUSION_SLOT 
#define MATERIAL_OCCLUSION_SLOT t4
#endif

#ifndef MATERIAL_TRANSMISSION_SLOT 
#define MATERIAL_TRANSMISSION_SLOT t5
#endif

#ifndef MATERIAL_SAMPLER_SLOT 
#define MATERIAL_SAMPLER_SLOT s0
#endif

cbuffer c_Material : register(MATERIAL_CB_SLOT)
{
    MaterialConstants g_Material;
};

Texture2D t_BaseOrDiffuse : register(MATERIAL_DIFFUSE_SLOT);
Texture2D t_MetalRoughOrSpecular : register(MATERIAL_SPECULAR_SLOT);
Texture2D t_Normal : register(MATERIAL_NORMALS_SLOT);
Texture2D t_Emissive : register(MATERIAL_EMISSIVE_SLOT);
Texture2D t_Occlusion : register(MATERIAL_OCCLUSION_SLOT);
Texture2D t_Transmission : register(MATERIAL_TRANSMISSION_SLOT);

SamplerState s_MaterialSampler : register(MATERIAL_SAMPLER_SLOT);

MaterialTextureSample SampleMaterialTexturesAuto(float2 texCoord)
{
    MaterialTextureSample values = DefaultMaterialTextures();

    if (g_Material.flags & MaterialFlags_UseBaseOrDiffuseTexture)
    {
        values.baseOrDiffuse = t_BaseOrDiffuse.Sample(s_MaterialSampler, texCoord);
    }

    if (g_Material.flags & MaterialFlags_UseMetalRoughOrSpecularTexture)
    {
        values.metalRoughOrSpecular = t_MetalRoughOrSpecular.Sample(s_MaterialSampler, texCoord);
    }

    if (g_Material.flags & MaterialFlags_UseEmissiveTexture)
    {
        values.emissive = t_Emissive.Sample(s_MaterialSampler, texCoord);
    }

    if (g_Material.flags & MaterialFlags_UseNormalTexture)
    {
        values.normal = t_Normal.Sample(s_MaterialSampler, texCoord);
    }

    if (g_Material.flags & MaterialFlags_UseOcclusionTexture)
    {
        values.occlusion = t_Occlusion.Sample(s_MaterialSampler, texCoord);
    }

    if (g_Material.flags & MaterialFlags_UseTransmissionTexture)
    {
        values.transmission = t_Transmission.Sample(s_MaterialSampler, texCoord);
    }

    return values;
}

MaterialTextureSample SampleMaterialTexturesLevel(float2 texCoord, float lod)
{
    MaterialTextureSample values = DefaultMaterialTextures();

    if (g_Material.flags & MaterialFlags_UseBaseOrDiffuseTexture)
    {
        values.baseOrDiffuse = t_BaseOrDiffuse.SampleLevel(s_MaterialSampler, texCoord, lod);
    }

    if (g_Material.flags & MaterialFlags_UseMetalRoughOrSpecularTexture)
    {
        values.metalRoughOrSpecular = t_MetalRoughOrSpecular.SampleLevel(s_MaterialSampler, texCoord, lod);
    }

    if (g_Material.flags & MaterialFlags_UseEmissiveTexture)
    {
        values.emissive = t_Emissive.SampleLevel(s_MaterialSampler, texCoord, lod);
    }

    if (g_Material.flags & MaterialFlags_UseNormalTexture)
    {
        values.normal = t_Normal.SampleLevel(s_MaterialSampler, texCoord, lod);
    }

    if (g_Material.flags & MaterialFlags_UseOcclusionTexture)
    {
        values.occlusion = t_Occlusion.SampleLevel(s_MaterialSampler, texCoord, lod);
    }

    if (g_Material.flags & MaterialFlags_UseTransmissionTexture)
    {
        values.transmission = t_Transmission.SampleLevel(s_MaterialSampler, texCoord, lod);
    }

    return values;
}

MaterialTextureSample SampleMaterialTexturesGrad(float2 texCoord, float2 ddx, float2 ddy)
{
    MaterialTextureSample values = DefaultMaterialTextures();

    if (g_Material.flags & MaterialFlags_UseBaseOrDiffuseTexture)
    {
        values.baseOrDiffuse = t_BaseOrDiffuse.SampleGrad(s_MaterialSampler, texCoord, ddx, ddy);
    }

    if (g_Material.flags & MaterialFlags_UseMetalRoughOrSpecularTexture)
    {
        values.metalRoughOrSpecular = t_MetalRoughOrSpecular.SampleGrad(s_MaterialSampler, texCoord, ddx, ddy);
    }

    if (g_Material.flags & MaterialFlags_UseEmissiveTexture)
    {
        values.emissive = t_Emissive.SampleGrad(s_MaterialSampler, texCoord, ddx, ddy);
    }

    if (g_Material.flags & MaterialFlags_UseNormalTexture)
    {
        values.normal = t_Normal.SampleGrad(s_MaterialSampler, texCoord, ddx, ddy);
    }
    
    if (g_Material.flags & MaterialFlags_UseOcclusionTexture)
    {
        values.occlusion = t_Occlusion.SampleGrad(s_MaterialSampler, texCoord, ddx, ddy);
    }

    if (g_Material.flags & MaterialFlags_UseTransmissionTexture)
    {
        values.transmission = t_Transmission.SampleGrad(s_MaterialSampler, texCoord, ddx, ddy);
    }

    return values;
}
