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

#include <donut/shaders/material_cb.h>
#include <donut/shaders/brdf.hlsli>
#include <donut/shaders/surface.hlsli>

// This toggle enables the derivation of baseColor and metalness parameters
// for materials defined in the specular-glossiness model. If disabled,
// specular-gloss materials will have default values in these fields.
#ifndef ENABLE_METAL_ROUGH_RECONSTRUCTION
#define ENABLE_METAL_ROUGH_RECONSTRUCTION 0
#endif

struct MaterialTextureSample
{
    float4 baseOrDiffuse;
    float4 metalRoughOrSpecular;
    float4 normal;
    float4 emissive;
    float4 occlusion;
    float4 transmission;
};

MaterialTextureSample DefaultMaterialTextures()
{
    MaterialTextureSample values;
    values.baseOrDiffuse = 1.0;
    values.metalRoughOrSpecular = 1.0;
    values.emissive = 1.0;
    values.normal = float4(0.5, 0.5, 1.0, 0.0);
    values.occlusion = 1.0;
    values.transmission = 1.0;
    return values;
}

// PBR workflow conversions (metal-rough <-> specular-gloss) are ported from the glTF repository:
// https://github.com/KhronosGroup/glTF/tree/master/extensions/2.0/Khronos/KHR_materials_pbrSpecularGlossiness/examples 
/*
The MIT License

Copyright (c) 2016-2017 Gary Hsu

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/


float GetPerceivedBrightness(float3 color)
{
    return sqrt(0.299 * color.r * color.r + 0.587 * color.g * color.g + 0.114 * color.b * color.b);
}

static const float c_DielectricSpecular = 0.04;

float SolveMetalness(float diffuse, float specular, float oneMinusSpecularStrength)
{
    if (specular < c_DielectricSpecular)
        return 0;

    float a = c_DielectricSpecular;
    float b = diffuse * oneMinusSpecularStrength / (1 - c_DielectricSpecular) + specular - 2 * c_DielectricSpecular;
    float c = c_DielectricSpecular - specular;
    float D = max(b * b - 4 * a * c, 0);
    return clamp((-b + sqrt(D)) / (2 * a), 0, 1);
}

void ConvertSpecularGlossToMetalRough(float3 diffuseColor, float3 specularColor, out float3 baseColor, out float metalness)
{
    const float epsilon = 1e-6;

    float oneMinusSpecularStrength = 1.0 - max(specularColor.r, max(specularColor.g, specularColor.b));
    metalness = SolveMetalness(GetPerceivedBrightness(diffuseColor), GetPerceivedBrightness(specularColor), oneMinusSpecularStrength);

    float3 baseColorFromDiffuse = diffuseColor * (oneMinusSpecularStrength / (1 - c_DielectricSpecular) / max(1 - metalness, epsilon));
    float3 baseColorFromSpecular = specularColor - c_DielectricSpecular * (1 - metalness) / max(metalness, epsilon);
    baseColor = saturate(lerp(baseColorFromDiffuse, baseColorFromSpecular, metalness * metalness));
}

void ConvertMetalRoughToSpecularGloss(float3 baseColor, float metalness, out float3 diffuseColor, out float3 specularColor)
{
    const float epsilon = 1e-6;

    specularColor = lerp(c_DielectricSpecular.xxx, baseColor, metalness);

    float oneMinusSpecularStrength = 1.0 - max(specularColor.r, max(specularColor.g, specularColor.b));
    diffuseColor = baseColor * ((1.0 - c_DielectricSpecular) * (1.0 - metalness) / max(oneMinusSpecularStrength, epsilon));
}

// ----- End of PBR workflow conversion code -----


void ApplyNormalMap(inout MaterialSample result, float4 tangent, float4 normalsTextureValue, float normalTextureScale)
{
    float squareTangentLength = dot(tangent.xyz, tangent.xyz);
    if (squareTangentLength == 0)
        return;
    
    if (tangent.w == 0)
        return;

    normalsTextureValue.xy = normalsTextureValue.xy * 2.0 - 1.0;
    normalsTextureValue.xy *= normalTextureScale;

    if (normalsTextureValue.z <= 0)
        normalsTextureValue.z = sqrt(saturate(1.0 - square(normalsTextureValue.x) - square(normalsTextureValue.y)));
    else
        normalsTextureValue.z = abs(normalsTextureValue.z * 2.0 - 1.0);

    float squareNormalMapLength = dot(normalsTextureValue.xyz, normalsTextureValue.xyz);

    if (squareNormalMapLength == 0)
        return;
        
    float normalMapLen = sqrt(squareNormalMapLength);
    float3 localNormal = normalsTextureValue.xyz / normalMapLen;

    tangent.xyz *= rsqrt(squareTangentLength);
    float3 bitangent = cross(result.geometryNormal, tangent.xyz) * tangent.w;

    result.shadingNormal = normalize(tangent.xyz * localNormal.x + bitangent.xyz * localNormal.y + result.geometryNormal.xyz * localNormal.z);
}

MaterialSample EvaluateSceneMaterial(float3 normal, float4 tangent, MaterialConstants material, MaterialTextureSample textures)
{
    MaterialSample result = DefaultMaterialSample();
    result.geometryNormal = normalize(normal);
    result.shadingNormal = result.geometryNormal;
    
    if (material.flags & MaterialFlags_UseSpecularGlossModel)
    {
        float3 diffuseColor = material.baseOrDiffuseColor.rgb * textures.baseOrDiffuse.rgb;
        float3 specularColor = material.specularColor.rgb * textures.metalRoughOrSpecular.rgb;
        result.roughness = 1.0 - textures.metalRoughOrSpecular.a * (1.0 - material.roughness);

#if ENABLE_METAL_ROUGH_RECONSTRUCTION
        ConvertSpecularGlossToMetalRough(diffuseColor, specularColor, result.baseColor, result.metalness);
        result.hasMetalRoughParams = true;
#endif

        // Compute the BRDF inputs for the specular-gloss model
        // https://github.com/KhronosGroup/glTF/blob/master/extensions/2.0/Khronos/KHR_materials_pbrSpecularGlossiness/README.md#specular---glossiness
        result.diffuseAlbedo = diffuseColor * (1.0 - max(specularColor.r, max(specularColor.g, specularColor.b)));
        result.specularF0 = specularColor;
    }
    else
    {
        result.baseColor = material.baseOrDiffuseColor.rgb * textures.baseOrDiffuse.rgb;
        result.roughness = material.roughness * textures.metalRoughOrSpecular.g;
        result.metalness = material.metalness * textures.metalRoughOrSpecular.b;
        result.hasMetalRoughParams = true;

        // Compute the BRDF inputs for the metal-rough model
        // https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#metal-brdf-and-dielectric-brdf
        result.diffuseAlbedo = lerp(result.baseColor * (1.0 - c_DielectricSpecular), 0.0, result.metalness);
        result.specularF0 = lerp(c_DielectricSpecular, result.baseColor.rgb, result.metalness);
    }
    
    result.occlusion = 1.0;
    if (material.flags & MaterialFlags_UseOcclusionTexture)
    {
        result.occlusion = textures.occlusion.r;
    }

    result.occlusion = lerp(1.0, result.occlusion, material.occlusionStrength);
    
    result.opacity = material.opacity;
    if (material.flags & MaterialFlags_UseBaseOrDiffuseTexture)
        result.opacity *= textures.baseOrDiffuse.a;
    result.opacity = saturate(result.opacity);

    result.transmission = material.transmissionFactor;
    result.diffuseTransmission = material.diffuseTransmissionFactor;
    if (material.flags & MaterialFlags_UseTransmissionTexture)
    {
        result.transmission *= textures.transmission.r;
        result.diffuseTransmission *= textures.transmission.r;
    }
    
    result.emissiveColor = material.emissiveColor;
    if (material.flags & MaterialFlags_UseEmissiveTexture)
        result.emissiveColor *= textures.emissive.rgb;

    if (material.flags & MaterialFlags_UseNormalTexture)
        ApplyNormalMap(result, tangent, textures.normal, material.normalTextureScale);

    result.ior = material.ior;
    
    result.shadowNoLFadeout = material.shadowNoLFadeout;

    return result;
}
