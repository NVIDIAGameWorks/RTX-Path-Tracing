/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef SCENE_GEOMETRY_HLSLI
#define SCENE_GEOMETRY_HLSLI


#include <donut/shaders/bindless.h>
#include <donut/shaders/vulkan.hlsli>
#include <donut/shaders/packing.hlsli>
#include <donut/shaders/scene_material.hlsli>

#include "HelperFunctions.hlsli"

VK_BINDING(0, 1) ByteAddressBuffer t_BindlessBuffers[] : register(t0, space1);
VK_BINDING(1, 1) Texture2D t_BindlessTextures[] : register(t0, space2);
VK_BINDING(2, 1) RWTexture2D<float4> u_BindlessTexturesRW[] : register(u0, space3);

enum GeometryAttributes
{
    GeomAttr_Position       = 0x01,
    GeomAttr_TexCoord       = 0x02,
    GeomAttr_Normal         = 0x04,
    GeomAttr_Tangents       = 0x08,
    GeomAttr_PrevPosition   = 0x10,

    GeomAttr_All            = 0x1F
};

struct GeometrySample
{
    InstanceData instance;
    GeometryData geometry;
    MaterialConstants material;

    float3 vertexPositions[3];
    float2 vertexTexcoords[3];

    float3 objectSpacePosition;
    float3 prevObjectSpacePosition;
    float2 texcoord;
    float3 flatNormal;
    float3 geometryNormal;
    float4 tangent;
};

GeometrySample getGeometryFromHit(
    uint instanceIndex,
    uint geometryIndex,
    uint triangleIndex,
    float2 rayBarycentrics,
    GeometryAttributes attributes,
    StructuredBuffer<InstanceData> instanceBuffer,
    StructuredBuffer<GeometryData> geometryBuffer,
    StructuredBuffer<MaterialConstants> materialBuffer)
{
    GeometrySample gs = (GeometrySample)0;

    gs.instance = instanceBuffer[instanceIndex];
    gs.geometry = geometryBuffer[gs.instance.firstGeometryIndex + geometryIndex];
    gs.material = materialBuffer[gs.geometry.materialIndex];

    ByteAddressBuffer indexBuffer = t_BindlessBuffers[NonUniformResourceIndex(gs.geometry.indexBufferIndex)];
    ByteAddressBuffer vertexBuffer = t_BindlessBuffers[NonUniformResourceIndex(gs.geometry.vertexBufferIndex)];

    float3 barycentrics;
    barycentrics.yz = rayBarycentrics;
    barycentrics.x = 1.0 - (barycentrics.y + barycentrics.z);

    uint3 indices = indexBuffer.Load3(gs.geometry.indexOffset + triangleIndex * c_SizeOfTriangleIndices);

    if (attributes & GeomAttr_Position)
    {
        gs.vertexPositions[0] = asfloat(vertexBuffer.Load3(gs.geometry.positionOffset + indices[0] * c_SizeOfPosition));
        gs.vertexPositions[1] = asfloat(vertexBuffer.Load3(gs.geometry.positionOffset + indices[1] * c_SizeOfPosition));
        gs.vertexPositions[2] = asfloat(vertexBuffer.Load3(gs.geometry.positionOffset + indices[2] * c_SizeOfPosition));
        gs.objectSpacePosition = interpolate(gs.vertexPositions, barycentrics);
    }

    if ((attributes & GeomAttr_PrevPosition) && gs.geometry.prevPositionOffset != ~0u)
    {
        float3 prevVertexPositions[3];
        prevVertexPositions[0] = asfloat(vertexBuffer.Load3(gs.geometry.prevPositionOffset + indices[0] * c_SizeOfPosition));
        prevVertexPositions[1] = asfloat(vertexBuffer.Load3(gs.geometry.prevPositionOffset + indices[1] * c_SizeOfPosition));
        prevVertexPositions[2] = asfloat(vertexBuffer.Load3(gs.geometry.prevPositionOffset + indices[2] * c_SizeOfPosition));
        gs.prevObjectSpacePosition = interpolate(prevVertexPositions, barycentrics);
    }
    else
        gs.prevObjectSpacePosition = gs.objectSpacePosition;

    if ((attributes & GeomAttr_TexCoord) && gs.geometry.texCoord1Offset != ~0u)
    {
        gs.vertexTexcoords[0] = asfloat(vertexBuffer.Load2(gs.geometry.texCoord1Offset + indices[0] * c_SizeOfTexcoord));
        gs.vertexTexcoords[1] = asfloat(vertexBuffer.Load2(gs.geometry.texCoord1Offset + indices[1] * c_SizeOfTexcoord));
        gs.vertexTexcoords[2] = asfloat(vertexBuffer.Load2(gs.geometry.texCoord1Offset + indices[2] * c_SizeOfTexcoord));
        gs.texcoord = interpolate(gs.vertexTexcoords, barycentrics);
    }

    if ((attributes & GeomAttr_Normal) && gs.geometry.normalOffset != ~0u)
    {
        float3 normals[3];
        normals[0] = Unpack_RGB8_SNORM(vertexBuffer.Load(gs.geometry.normalOffset + indices[0] * c_SizeOfNormal));
        normals[1] = Unpack_RGB8_SNORM(vertexBuffer.Load(gs.geometry.normalOffset + indices[1] * c_SizeOfNormal));
        normals[2] = Unpack_RGB8_SNORM(vertexBuffer.Load(gs.geometry.normalOffset + indices[2] * c_SizeOfNormal));
        gs.geometryNormal = interpolate(normals, barycentrics);
        gs.geometryNormal = mul(gs.instance.transform, float4(gs.geometryNormal, 0.0)).xyz;
        gs.geometryNormal = normalize(gs.geometryNormal);
    }

    if ((attributes & GeomAttr_Tangents) && gs.geometry.tangentOffset != ~0u)
    {
        float4 tangents[3];
        tangents[0] = Unpack_RGBA8_SNORM(vertexBuffer.Load(gs.geometry.tangentOffset + indices[0] * c_SizeOfNormal));
        tangents[1] = Unpack_RGBA8_SNORM(vertexBuffer.Load(gs.geometry.tangentOffset + indices[1] * c_SizeOfNormal));
        tangents[2] = Unpack_RGBA8_SNORM(vertexBuffer.Load(gs.geometry.tangentOffset + indices[2] * c_SizeOfNormal));
        gs.tangent.xyz = interpolate(tangents, barycentrics).xyz;
        gs.tangent.xyz = mul(gs.instance.transform, float4(gs.tangent.xyz, 0.0)).xyz;
        gs.tangent.xyz = normalize(gs.tangent.xyz);
        gs.tangent.w = tangents[0].w;
    }

    float3 objectSpaceFlatNormal = normalize(cross(
        gs.vertexPositions[1] - gs.vertexPositions[0],
        gs.vertexPositions[2] - gs.vertexPositions[0]));

    gs.flatNormal = normalize(mul(gs.instance.transform, float4(objectSpaceFlatNormal, 0.0)).xyz);

    return gs;
}

enum MaterialAttributes
{
    MatAttr_BaseColor    = 0x01,
    MatAttr_Emissive     = 0x02,
    MatAttr_Normal       = 0x04,
    MatAttr_MetalRough   = 0x08,
    MatAttr_Transmission = 0x10,

    MatAttr_All          = 0x1F
};

MaterialSample sampleGeometryMaterial(
    GeometrySample gs, 
#ifndef SCENE_GEOMETRY_PIXEL_SHADER
    float2 texGrad_x, 
    float2 texGrad_y, 
    float mipLevel, // <-- Use a compile time constant for mipLevel, < 0 for aniso filtering
#else
    float lodBias,
#endif
    MaterialAttributes attributes, 
    SamplerState materialSampler,
    float normalMapScale = 1.0)
{
    gs.material.normalTextureScale *= normalMapScale;

    MaterialTextureSample textures = DefaultMaterialTextures();

    if ((attributes & MatAttr_BaseColor) && (gs.material.baseOrDiffuseTextureIndex >= 0) && (gs.material.flags & MaterialFlags_UseBaseOrDiffuseTexture) != 0)
    {
        Texture2D diffuseTexture = t_BindlessTextures[NonUniformResourceIndex(gs.material.baseOrDiffuseTextureIndex)];
        
#ifndef SCENE_GEOMETRY_PIXEL_SHADER
        if (mipLevel >= 0)
            textures.baseOrDiffuse = diffuseTexture.SampleLevel(materialSampler, gs.texcoord, mipLevel);
        else
            textures.baseOrDiffuse = diffuseTexture.SampleGrad(materialSampler, gs.texcoord, texGrad_x, texGrad_y);
#else
        textures.baseOrDiffuse = diffuseTexture.SampleBias(materialSampler, gs.texcoord, lodBias);
#endif
    }

    if ((attributes & MatAttr_Emissive) && (gs.material.emissiveTextureIndex >= 0) && (gs.material.flags & MaterialFlags_UseEmissiveTexture) != 0)
    {
        Texture2D emissiveTexture = t_BindlessTextures[NonUniformResourceIndex(gs.material.emissiveTextureIndex)];
        
#ifndef SCENE_GEOMETRY_PIXEL_SHADER
        if (mipLevel >= 0)
            textures.emissive = emissiveTexture.SampleLevel(materialSampler, gs.texcoord, mipLevel);
        else
            textures.emissive = emissiveTexture.SampleGrad(materialSampler, gs.texcoord, texGrad_x, texGrad_y);
#else
        textures.emissive = emissiveTexture.SampleBias(materialSampler, gs.texcoord, lodBias);
#endif
    }
    
    if ((attributes & MatAttr_Normal) && (gs.material.normalTextureIndex >= 0) && (gs.material.flags & MaterialFlags_UseNormalTexture) != 0 && (normalMapScale > 0))
    {
        Texture2D normalsTexture = t_BindlessTextures[NonUniformResourceIndex(gs.material.normalTextureIndex)];
        
#ifndef SCENE_GEOMETRY_PIXEL_SHADER
        if (mipLevel >= 0)
            textures.normal = normalsTexture.SampleLevel(materialSampler, gs.texcoord, mipLevel);
        else
            textures.normal = normalsTexture.SampleGrad(materialSampler, gs.texcoord, texGrad_x, texGrad_y);
#else
        textures.normal = normalsTexture.SampleBias(materialSampler, gs.texcoord, lodBias);
#endif
    }

    if ((attributes & MatAttr_MetalRough) && (gs.material.metalRoughOrSpecularTextureIndex >= 0) && (gs.material.flags & MaterialFlags_UseMetalRoughOrSpecularTexture) != 0)
    {
        Texture2D specularTexture = t_BindlessTextures[NonUniformResourceIndex(gs.material.metalRoughOrSpecularTextureIndex)];

#ifndef SCENE_GEOMETRY_PIXEL_SHADER
        if (mipLevel >= 0)
            textures.metalRoughOrSpecular = specularTexture.SampleLevel(materialSampler, gs.texcoord, mipLevel);
        else
            textures.metalRoughOrSpecular = specularTexture.SampleGrad(materialSampler, gs.texcoord, texGrad_x, texGrad_y);
#else
        textures.metalRoughOrSpecular = specularTexture.SampleBias(materialSampler, gs.texcoord, lodBias);
#endif
    }

    if ((attributes & MatAttr_Transmission) && (gs.material.transmissionTextureIndex >= 0) && (gs.material.flags & MaterialFlags_UseTransmissionTexture) != 0)
    {
        Texture2D transmissionTexture = t_BindlessTextures[NonUniformResourceIndex(gs.material.transmissionTextureIndex)];

#ifndef SCENE_GEOMETRY_PIXEL_SHADER
        if (mipLevel >= 0)
            textures.transmission = transmissionTexture.SampleLevel(materialSampler, gs.texcoord, mipLevel);
        else
            textures.transmission = transmissionTexture.SampleGrad(materialSampler, gs.texcoord, texGrad_x, texGrad_y);
#else
        textures.transmission = transmissionTexture.SampleBias(materialSampler, gs.texcoord, lodBias);
#endif
    }

    return EvaluateSceneMaterial(gs.geometryNormal, gs.tangent, gs.material, textures);
}

#endif // SCENE_GEOMETRY_HLSLI