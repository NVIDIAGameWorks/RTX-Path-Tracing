/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __PATH_TRACER_BRIDGE_DONUT_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __PATH_TRACER_BRIDGE_DONUT_HLSLI__

// easier if we let Donut do this!
#define ENABLE_METAL_ROUGH_RECONSTRUCTION 1

#include "PathTracer/PathTracerBridge.hlsli"

#include "OpacityMicroMap/OmmDebug.hlsli"

// Donut-specific (native engine - we can include before PathTracer to avoid any collisions)
#include <donut/shaders/bindless.h>
#include <donut/shaders/utils.hlsli>
#include <donut/shaders/vulkan.hlsli>
#include <donut/shaders/packing.hlsli>
#include <donut/shaders/surface.hlsli>
#include <donut/shaders/lighting.hlsli>
#include <donut/shaders/scene_material.hlsli>

#include "PathTracerBridgeResources.hlsli"

enum DonutGeometryAttributes
{
    GeomAttr_Position       = 0x01,
    GeomAttr_TexCoord       = 0x02,
    GeomAttr_Normal         = 0x04,
    GeomAttr_Tangents       = 0x08,
    GeomAttr_PrevPosition   = 0x10,

    GeomAttr_All            = 0x1F
};

struct DonutGeometrySample
{
    InstanceData instance;
    GeometryData geometry;
    GeometryDebugData geometryDebug;
    MaterialConstants material;

    float3 vertexPositions[3];
    //float3 prevVertexPositions[3]; <- not needed for anything yet so we just use local variables and compute prevObjectSpacePosition
    float2 vertexTexcoords[3];

    float3 objectSpacePosition;
    float3 prevObjectSpacePosition;
    float2 texcoord;
    float3 flatNormal;
    float3 geometryNormal;
    float4 tangent;
    bool frontFacing;
};

float3 SafeNormalize(float3 input)
{
    float lenSq = dot(input,input);
    return input * rsqrt(max( 1.175494351e-38, lenSq));
}

DonutGeometrySample getGeometryFromHit(
    uint instanceIndex,
    uint geometryIndex,
    uint triangleIndex,
    float2 rayBarycentrics,
    DonutGeometryAttributes attributes,
    StructuredBuffer<InstanceData> instanceBuffer,
    StructuredBuffer<GeometryData> geometryBuffer,
    StructuredBuffer<GeometryDebugData> geometryDebugBuffer,
    StructuredBuffer<MaterialConstants> materialBuffer, 
    float3 rayDirection, 
    DebugContext debug)
{
    DonutGeometrySample gs = (DonutGeometrySample)0;

    gs.instance = instanceBuffer[instanceIndex];
    gs.geometry = geometryBuffer[gs.instance.firstGeometryIndex + geometryIndex];
    gs.geometryDebug = geometryDebugBuffer[gs.instance.firstGeometryIndex + geometryIndex];
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

    if (attributes & GeomAttr_PrevPosition)
    {
        if( gs.geometry.prevPositionOffset != 0xFFFFFFFF )  // only present for skinned objects
        {
            float3 prevVertexPositions[3];
            /*gs.*/prevVertexPositions[0]   = asfloat(vertexBuffer.Load3(gs.geometry.prevPositionOffset + indices[0] * c_SizeOfPosition));
            /*gs.*/prevVertexPositions[1]   = asfloat(vertexBuffer.Load3(gs.geometry.prevPositionOffset + indices[1] * c_SizeOfPosition));
            /*gs.*/prevVertexPositions[2]   = asfloat(vertexBuffer.Load3(gs.geometry.prevPositionOffset + indices[2] * c_SizeOfPosition));
            gs.prevObjectSpacePosition  = interpolate(/*gs.*/prevVertexPositions, barycentrics);
        }
        else
            gs.prevObjectSpacePosition  = gs.objectSpacePosition;
    }

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
        gs.geometryNormal = SafeNormalize(gs.geometryNormal);
    }

    if ((attributes & GeomAttr_Tangents) && gs.geometry.tangentOffset != ~0u)
    {
        float4 tangents[3];
        tangents[0] = Unpack_RGBA8_SNORM(vertexBuffer.Load(gs.geometry.tangentOffset + indices[0] * c_SizeOfNormal));
        tangents[1] = Unpack_RGBA8_SNORM(vertexBuffer.Load(gs.geometry.tangentOffset + indices[1] * c_SizeOfNormal));
        tangents[2] = Unpack_RGBA8_SNORM(vertexBuffer.Load(gs.geometry.tangentOffset + indices[2] * c_SizeOfNormal));

        gs.tangent.xyz = interpolate(tangents, barycentrics).xyz;
        gs.tangent.xyz = mul(gs.instance.transform, float4(gs.tangent.xyz, 0.0)).xyz;
        gs.tangent.xyz = SafeNormalize(gs.tangent.xyz);
        gs.tangent.w = tangents[0].w;
    }

    float3 objectSpaceFlatNormal = SafeNormalize(cross(
        gs.vertexPositions[1] - gs.vertexPositions[0],
        gs.vertexPositions[2] - gs.vertexPositions[0]));

    gs.flatNormal   = SafeNormalize(mul(gs.instance.transform, float4(objectSpaceFlatNormal, 0.0)).xyz);

    gs.frontFacing  = dot( -rayDirection, gs.flatNormal ) >= 0.0;

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

float4 sampleTexture(uint textureIndexAndInfo, SamplerState samplerState, const ActiveTextureSampler textureSampler, float2 uv)
{
    uint textureIndex = textureIndexAndInfo & 0xFFFF;
    uint baseLOD = textureIndexAndInfo>>24;
    uint mipLevels = (textureIndexAndInfo>>16) & 0xFF;

    Texture2D tex2D = t_BindlessTextures[NonUniformResourceIndex(textureIndex)];

    return textureSampler.sampleTexture(tex2D, samplerState, uv, baseLOD, mipLevels);
}

MaterialSample sampleGeometryMaterial(uniform OptimizationHints optimizationHints, const DonutGeometrySample gs, const MaterialAttributes attributes, const SamplerState materialSampler, const ActiveTextureSampler textureSampler)
{
    MaterialTextureSample textures = DefaultMaterialTextures();

    if( !optimizationHints.NoTextures )
    {
        if ((attributes & MatAttr_BaseColor) && (gs.material.flags & MaterialFlags_UseBaseOrDiffuseTexture) != 0)
            textures.baseOrDiffuse = sampleTexture(gs.material.baseOrDiffuseTextureIndex, materialSampler, textureSampler, gs.texcoord);

        if ((attributes & MatAttr_Emissive) && (gs.material.flags & MaterialFlags_UseEmissiveTexture) != 0)
            textures.emissive = sampleTexture(gs.material.emissiveTextureIndex, materialSampler, textureSampler, gs.texcoord);
    
        if ((attributes & MatAttr_Normal) && (gs.material.flags & MaterialFlags_UseNormalTexture) != 0)
            textures.normal = sampleTexture(gs.material.normalTextureIndex, materialSampler, textureSampler, gs.texcoord);

        if ((attributes & MatAttr_MetalRough) && (gs.material.flags & MaterialFlags_UseMetalRoughOrSpecularTexture) != 0)
            textures.metalRoughOrSpecular = sampleTexture(gs.material.metalRoughOrSpecularTextureIndex, materialSampler, textureSampler, gs.texcoord);

        if( !optimizationHints.NoTransmission )
        {
            if ((attributes & MatAttr_Transmission) && (gs.material.flags & MaterialFlags_UseTransmissionTexture) != 0)
                textures.transmission = sampleTexture(gs.material.transmissionTextureIndex, materialSampler, textureSampler, gs.texcoord);
        }
    }

    return EvaluateSceneMaterial(gs.geometryNormal, gs.tangent, gs.material, textures);
}

static OpacityMicroMapDebugInfo loadOmmDebugInfo(const DonutGeometrySample donutGS, const uint triangleIndex, const TriangleHit triangleHit)
{
    OpacityMicroMapDebugInfo ommDebug = OpacityMicroMapDebugInfo::initDefault();

#if ENABLE_DEBUG_VIZUALISATION && !NON_PATH_TRACING_PASS
    if (donutGS.geometryDebug.ommIndexBufferIndex != -1 &&
        donutGS.geometryDebug.ommIndexBufferOffset != 0xFFFFFFFF)
    {
        ByteAddressBuffer ommIndexBuffer = t_BindlessBuffers[NonUniformResourceIndex(donutGS.geometryDebug.ommIndexBufferIndex)];
        ByteAddressBuffer ommDescArrayBuffer = t_BindlessBuffers[NonUniformResourceIndex(donutGS.geometryDebug.ommDescArrayBufferIndex)];
        ByteAddressBuffer ommArrayDataBuffer = t_BindlessBuffers[NonUniformResourceIndex(donutGS.geometryDebug.ommArrayDataBufferIndex)];

        OpacityMicroMapContext ommContext = OpacityMicroMapContext::make(
            ommIndexBuffer, donutGS.geometryDebug.ommIndexBufferOffset, donutGS.geometryDebug.ommIndexBuffer16Bit,
            ommDescArrayBuffer, donutGS.geometryDebug.ommDescArrayBufferOffset,
            ommArrayDataBuffer, donutGS.geometryDebug.ommArrayDataBufferOffset,
            triangleIndex,
            triangleHit.barycentrics.xy
        );

        ommDebug.hasOmmAttachment = true;
        ommDebug.opacityStateDebugColor = OpacityMicroMapDebugViz(ommContext);
    }
#endif

    return ommDebug;
}

static void surfaceDebugViz(const uniform OptimizationHints optimizationHints, const SurfaceData surfaceData, const TriangleHit triangleHit, const float3 rayDir, const RayCone rayCone, const PathTracerParams params, const int pathVertexIndex, const OpacityMicroMapDebugInfo ommDebug, DebugContext debug)
{
#if ENABLE_DEBUG_VIZUALISATION && !NON_PATH_TRACING_PASS
    if (g_Const.debug.debugViewType == (int)DebugViewType::Disabled || pathVertexIndex != 1)
        return;

    //const VertexData vd     = surfaceData.vd;
    const ShadingData sd = surfaceData.sd;
    const ActiveBSDF bsdf = surfaceData.bsdf;

    // these work only when ActiveBSDF is StandardBSDF - make an #ifdef if/when this becomes a problem
    StandardBSDFData bsdfData = bsdf.data;

    switch (g_Const.debug.debugViewType)
    {
    case ((int)DebugViewType::FirstHitBarycentrics):                debug.DrawDebugViz(float4(triangleHit.barycentrics, 0.0, 1.0)); break;
    case ((int)DebugViewType::FirstHitFaceNormal):                  debug.DrawDebugViz(float4(DbgShowNormalSRGB(sd.frontFacing ? sd.faceN : -sd.faceN), 1.0)); break;
    case ((int)DebugViewType::FirstHitShadingNormal):               debug.DrawDebugViz(float4(DbgShowNormalSRGB(sd.N), 1.0)); break;
    case ((int)DebugViewType::FirstHitShadingTangent):              debug.DrawDebugViz(float4(DbgShowNormalSRGB(sd.T), 1.0)); break;
    case ((int)DebugViewType::FirstHitShadingBitangent):            debug.DrawDebugViz(float4(DbgShowNormalSRGB(sd.B), 1.0)); break;
    case ((int)DebugViewType::FirstHitFrontFacing):                 debug.DrawDebugViz(float4(saturate(float3(0.15, 0.1 + sd.frontFacing, 0.15)), 1.0)); break;
    case ((int)DebugViewType::FirstHitDoubleSided):                 debug.DrawDebugViz(float4(saturate(float3(0.15, 0.1 + sd.mtl.isDoubleSided(), 0.15)), 1.0)); break;
    case ((int)DebugViewType::FirstHitThinSurface):                 debug.DrawDebugViz(float4(saturate(float3(0.15, 0.1 + sd.mtl.isThinSurface(), 0.15)), 1.0)); break;
    case ((int)DebugViewType::FirstHitShaderPermutation):           debug.DrawDebugViz(float4(optimizationHints.NoTextures, optimizationHints.NoTransmission, optimizationHints.OnlyDeltaLobes, 1.0)); break;
    case ((int)DebugViewType::FirstHitDiffuse):                     debug.DrawDebugViz(float4(bsdfData.diffuse.xyz, 1.0)); break;
    case ((int)DebugViewType::FirstHitSpecular):                    debug.DrawDebugViz(float4(bsdfData.specular.xyz, 1.0)); break;
    case ((int)DebugViewType::FirstHitRoughness):                   debug.DrawDebugViz(float4(bsdfData.roughness.xxx, 1.0)); break;
    case ((int)DebugViewType::FirstHitMetallic):                    debug.DrawDebugViz(float4(bsdfData.metallic.xxx, 1.0)); break;
    case ((int)DebugViewType::FirstHitOpacityMicroMapOverlay):      debug.DrawDebugViz(float4(ommDebug.opacityStateDebugColor, 1.0)); break;
    default: break;
    }
#endif
}

PathTracerParams Bridge::getPathTracerParams()
{
    PathTracerParams params = PathTracerParams::make( g_Const.ptConsts.camera.viewportSize );
    params.texLODBias = g_Const.ptConsts.texLODBias;
    return params;
}

uint Bridge::getSampleIndex()
{
    return g_Const.ptConsts.sampleIndex;
}

uint Bridge::getMaxBounceLimit()
{
    return g_Const.ptConsts.bounceCount;
}

uint Bridge::getMaxDiffuseBounceLimit()
{
    return g_Const.ptConsts.diffuseBounceCount;
}

Ray Bridge::computeCameraRay(const uint2 pixelPos)
{
    SampleGenerator sg = SampleGenerator::make(pixelPos, 0, getSampleIndex());

    // compute camera ray! would make sense to compile out if unused
    float2 subPixelOffset;
    if (g_Const.ptConsts.enablePerPixelJitterAA)
        subPixelOffset = sampleNext2D(sg) - 0.5.xx;
    else
        subPixelOffset = g_Const.ptConsts.camera.jitter * float2(1, -1); // conversion so that ComputeRayThinlens matches Donut offset convention in View.cpp->UpdateCache()
    const float2 cameraDoFSample = sampleNext2D(sg);
    //return ComputeRayPinhole( g_Const.ptConsts.camera, pixelPos, subPixelOffset );
    Ray ray = ComputeRayThinlens( g_Const.ptConsts.camera, pixelPos, subPixelOffset, cameraDoFSample ); 

#if 0  // fallback: use inverted matrix) useful for correctness validation; with DoF disabled (apertureRadius/focalDistance == near zero), should provide same rays as above code - otherwise something's really broken
    PlanarViewConstants view = g_Const.view;
    float2 uv = (float2(pixelPos) + 0.5) * view.viewportSizeInv;
    float4 clipPos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 1e-7, 1);
    float4 worldPos = mul(clipPos, view.matClipToWorld);
    worldPos.xyz /= worldPos.w;
        
    ray.origin  = view.cameraDirectionOrPosition.xyz;
    ray.dir     = normalize(worldPos.xyz - ray.origin);
#endif
    return ray;
}

/** Helper to create a texture sampler instance.
The method for computing texture level-of-detail depends on the configuration.
\param[in] path Path state.
\param[in] isPrimaryTriangleHit True if primary hit on a triangle.
\return Texture sampler instance.
*/
ActiveTextureSampler Bridge::createTextureSampler(const RayCone rayCone, const float3 rayDir, float coneTexLODValue, float3 normalW, bool isPrimaryHit, bool isTriangleHit, const PathTracerParams params)
{
#if ACTIVE_LOD_TEXTURE_SAMPLER == LOD_TEXTURE_SAMPLER_EXPLICIT
    return ExplicitLodTextureSampler::make(params.texLODBias);
#elif ACTIVE_LOD_TEXTURE_SAMPLER == LOD_TEXTURE_SAMPLER_RAY_CONES
    float lambda = rayCone.computeLOD(coneTexLODValue, rayDir, normalW, true);
    lambda += params.texLODBias;
    return ExplicitRayConesLodTextureSampler::make(lambda);
#endif
}

void Bridge::loadSurfacePosNormOnly(out float3 posW, out float3 faceN, const TriangleHit triangleHit, DebugContext debug)
{
    const uint instanceIndex    = triangleHit.instanceID.getInstanceIndex();
    const uint geometryIndex    = triangleHit.instanceID.getGeometryIndex();
    const uint triangleIndex    = triangleHit.primitiveIndex;
    DonutGeometrySample donutGS = getGeometryFromHit(instanceIndex, geometryIndex, triangleIndex, triangleHit.barycentrics, GeomAttr_Position,
        t_InstanceData, t_GeometryData, t_GeometryDebugData, t_MaterialConstants, float3(0,0,0), debug);
    posW    = mul(donutGS.instance.transform, float4(donutGS.objectSpacePosition, 1.0)).xyz;
    faceN   = donutGS.flatNormal;
}

SurfaceData Bridge::loadSurface(const uniform OptimizationHints optimizationHints, const TriangleHit triangleHit, const float3 rayDir, const RayCone rayCone, const PathTracerParams params, const int pathVertexIndex, DebugContext debug)
{
    const bool isPrimaryHit     = pathVertexIndex == 1;
    const uint instanceIndex    = triangleHit.instanceID.getInstanceIndex();
    const uint geometryIndex    = triangleHit.instanceID.getGeometryIndex();
    const uint triangleIndex    = triangleHit.primitiveIndex;

    DonutGeometrySample donutGS = getGeometryFromHit(instanceIndex, geometryIndex, triangleIndex, triangleHit.barycentrics, GeomAttr_TexCoord | GeomAttr_Position | GeomAttr_Normal | GeomAttr_Tangents | GeomAttr_PrevPosition,
        t_InstanceData, t_GeometryData, t_GeometryDebugData, t_MaterialConstants, rayDir, debug);

    // Convert Donut to pt_sdk vertex data
    VertexData ptVertex;
    ptVertex.posW           = mul(donutGS.instance.transform, float4(donutGS.objectSpacePosition, 1.0)).xyz;
    float3 prevPosW             = mul(donutGS.instance.prevTransform, float4(donutGS.prevObjectSpacePosition, 1.0)).xyz;
    ptVertex.normalW        = donutGS.geometryNormal;     // this normal is not guaranteed to point towards the viewer (but shading normal will get corrected below if material double-sided)
    ptVertex.tangentW       = donutGS.tangent;            // .w holds the sign/direction for the bitangent
    ptVertex.texC           = donutGS.texcoord;
    ptVertex.faceNormalW    = donutGS.flatNormal;         // this normal is not guaranteed to point towards the viewer (but shading normal will get corrected below if material double-sided)
    ptVertex.curveRadius    = 1;                          // unused for triangle meshes
        
    // transpose is to go from Donut row_major to Falcor column_major; it is likely unnecessary here since both should work the same for this specific function, but leaving in for correctness
    ptVertex.coneTexLODValue= computeRayConeTriangleLODValue( donutGS.vertexPositions, donutGS.vertexTexcoords, transpose((float3x3)donutGS.instance.transform) );

    // using flat (triangle) normal makes more sense since actual triangle surface is where the textures are sampled on (plus geometry normals are borked in some datasets)
    ActiveTextureSampler textureSampler = createTextureSampler(rayCone, rayDir, ptVertex.coneTexLODValue, donutGS.flatNormal/*donutGS.geometryNormal*/, isPrimaryHit, true, params);

    // See MaterialFactory.hlsli in Falcor
    ShadingData ptShadingData;

    ptShadingData.posW = ptVertex.posW;
    ptShadingData.uv   = ptVertex.texC;
    ptShadingData.V    = -rayDir;
    ptShadingData.N    = ptVertex.normalW;

    // after this point we have valid tangent space in ptShadingData.N/.T/.B using geometry (interpolated) normal, but without normalmap yet
    const bool validTangentSpace = computeTangentSpace(ptShadingData, ptVertex.tangentW);
    //if( !validTangentSpace )  // handled by computeTangentSpace
    //    ConstructONB( ptShadingData.N, ptShadingData.T, ptShadingData.B );

    // Primitive data
    ptShadingData.faceN = ptVertex.faceNormalW;         // must happen before adjustShadingNormal!
    ptShadingData.frontFacing = donutGS.frontFacing;        // must happen before adjustShadingNormal!
    ptShadingData.curveRadius = ptVertex.curveRadius;

    // Get donut material (normal map is evaluated here)
    MaterialSample donutMaterial = sampleGeometryMaterial(optimizationHints, donutGS, MatAttr_All, s_MaterialSampler, textureSampler);

    ptShadingData.N = donutMaterial.shadingNormal;

    // Donut -> Falcor
    const bool donutMaterialDoubleSided = (donutGS.material.flags & MaterialFlags_DoubleSided) != 0;
    const bool donutMaterialThinSurface = (donutGS.material.flags & MaterialFlags_ThinSurface) != 0;
    //const bool alphaTested = (donutGS.material.domain == MaterialDomain_AlphaTested) || (donutGS.material.domain == MaterialDomain_TransmissiveAlphaTested);
    ptShadingData.materialID = donutGS.geometry.materialIndex;
    ptShadingData.mtl = MaterialHeader::make();
    ptShadingData.mtl.setMaterialType( MaterialType::Standard );
    //ptShadingData.mtl.setAlphaMode( (AlphaMode)( (!alphaTested)?((uint)AlphaMode::Opaque):((uint)AlphaMode::Mask) ) );    // alpha testing handled on our side, Falcor stuff is unused
    //ptShadingData.mtl.setAlphaThreshold( donutGS.material.alphaCutoff );                                                  // alpha testing handled on our side, Falcor stuff is unused
    ptShadingData.mtl.setNestedPriority( min( InteriorList::kMaxNestedPriority, 1 + (uint(donutGS.material.flags) >> MaterialFlags_NestedPriorityShift)) );   // priorities are from (1, ... kMaxNestedPriority) because 0 is used to mark empty slots and remapped to kMaxNestedPriority
    ptShadingData.mtl.setDoubleSided( donutMaterialDoubleSided );
    ptShadingData.mtl.setThinSurface( donutMaterialThinSurface );
    ptShadingData.mtl.setEmissive( any(donutMaterial.emissiveColor!=0) );
    ptShadingData.mtl.setIsBasicMaterial( true );
    ptShadingData.mtl.setPSDExclude( (donutGS.material.flags & MaterialFlags_PSDExclude) != 0 );
    ptShadingData.mtl.setPSDDominantDeltaLobeP1( (donutGS.material.flags & MaterialFlags_PSDDominantDeltaLobeP1Mask) >> MaterialFlags_PSDDominantDeltaLobeP1Shift );

    // We currently flip the shading normal for back-facing hits on double-sided materials.
    // This convention will eventually go away when the material setup code handles it instead.
    if (!ptShadingData.frontFacing)
        ptShadingData.N = -ptShadingData.N;

    // Helper function to adjust the shading normal to reduce black pixels due to back-facing view direction. Note: This breaks the reciprocity of the BSDF!
    adjustShadingNormal( ptShadingData, ptVertex );

    ptShadingData.opacity = donutMaterial.opacity;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Now load the actual BSDF! Equivalent to StandardBSDF::setupBSDF
    StandardBSDFData d;

    // A.k.a. interiorIoR
    float matIoR = donutMaterial.ior;

    // from https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_transmission/README.md#refraction
    // "This microfacet lobe is exactly the same as the specular lobe except sampled along the line of sight through the surface."
    d.specularTransmission = donutMaterial.transmission * (1 - donutMaterial.metalness);    // (1 - donutMaterial.metalness) is from https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_transmission/README.md#transparent-metals
    d.diffuseTransmission = donutMaterial.diffuseTransmission * (1 - donutMaterial.metalness);    // (1 - donutMaterial.metalness) is from https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_transmission/README.md#transparent-metals
    d.transmission = donutMaterial.baseColor;

    /*LobeType*/ uint lobeType = (uint)LobeType::All;

    if (optimizationHints.NoTransmission)
    {
        d.specularTransmission = 0;
        d.diffuseTransmission = 0;
        d.transmission = float3(0,0,0);
        lobeType &= ~(uint)LobeType::Transmission;//~((uint)LobeType::DiffuseReflection | (uint)LobeType::SpecularReflection | (uint)LobeType::DeltaReflection);
    }
    //if (optimizationHints.OnlyTransmission)
    //{
    //    lobeType &= (uint)LobeType::Transmission; //~(uint)LobeType::Reflection;
    //}
    if (optimizationHints.OnlyDeltaLobes)
    {
        lobeType &= ~(uint)LobeType::NonDelta;
    }

    ptShadingData.mtl.setActiveLobes( lobeType );

    // Sample base color.
    float3 baseColor = donutMaterial.baseColor;

    // OMM Debug evaluates the OMM state at a given triangle + hit BC color codes the result for the corresonding state.
    OpacityMicroMapDebugInfo ommDebug = loadOmmDebugInfo(donutGS, triangleIndex, triangleHit);
#if ENABLE_DEBUG_VIZUALISATION && !NON_PATH_TRACING_PASS
    if (ommDebug.hasOmmAttachment && 
        g_Const.debug.debugViewType == (int)DebugViewType::FirstHitOpacityMicroMapInWorld)
    {
        baseColor = ommDebug.opacityStateDebugColor;
    }
#endif

#if ENABLE_METAL_ROUGH_RECONSTRUCTION == 0
#error we rely on Donut to do the conversion! for more info on how to do it manually search for MATERIAL_SYSTEM_HAS_SPEC_GLOSS_MATERIALS 
#endif

    // Calculate the specular reflectance for dielectrics from the IoR, as in the Disney BSDF [Burley 2015].
    // UE4 uses 0.08 multiplied by a default specular value of 0.5, hence F0=0.04 as default. The default IoR=1.5 gives the same result.
    float f = (matIoR - 1.f) / (matIoR + 1.f);
    float F0 = f * f;

    // G - Roughness; B - Metallic
    d.diffuse = lerp(baseColor, float3(0,0,0), donutMaterial.metalness);
    d.specular = lerp(float3(F0,F0,F0), baseColor, donutMaterial.metalness);
    d.roughness = donutMaterial.roughness;
    d.metallic = donutMaterial.metalness;

    // Assume the default IoR for vacuum on the front-facing side.
    // The renderer may override this for nested dielectrics (see 'handleNestedDielectrics' calling Bridge::updateOutsideIoR)
    ptShadingData.IoR = 1.f;
    d.eta = ptShadingData.frontFacing ? (ptShadingData.IoR / matIoR) : (matIoR / ptShadingData.IoR); 

    StandardBSDF bsdf; // = {}; <- not initializing to 0 but have to make sure everything's filled in
    bsdf.data = d;


    // Sample the emissive texture.
    // The standard material supports uniform emission over the hemisphere.
    // Note that the material is only emissive on the front-facing side.
    if( donutMaterialDoubleSided )
        bsdf.emission = donutMaterial.emissiveColor;
    else
        bsdf.emission = (ptShadingData.frontFacing)?(donutMaterial.emissiveColor):(0);

    // if you think tangent space is broken, test with this (won't make it correctly oriented)
    //ConstructONB( ptShadingData.N, ptShadingData.T, ptShadingData.B );

    SurfaceData ret = SurfaceData::make(/*ptVertex,*/ ptShadingData, bsdf, prevPosW, matIoR);

#if ENABLE_DEBUG_VIZUALISATION && !NON_PATH_TRACING_PASS
    if( debug.IsDebugPixel() && pathVertexIndex==1 && !debug.constants.exploreDeltaTree )
        debug.SetPickedMaterial( donutGS.geometry.materialIndex );
    surfaceDebugViz( optimizationHints, ret, triangleHit, rayDir, rayCone, params, pathVertexIndex, ommDebug, debug );
#endif
    return ret;
}

void Bridge::updateOutsideIoR(inout SurfaceData surfaceData, float outsideIoR)
{
    surfaceData.sd.IoR = outsideIoR;

    ///< Relative index of refraction (incident IoR / transmissive IoR), dependent on whether we're exiting or entering
    surfaceData.bsdf.data.eta = surfaceData.sd.frontFacing ? (surfaceData.sd.IoR / surfaceData.interiorIoR) : (surfaceData.interiorIoR / surfaceData.sd.IoR); 
}

float Bridge::loadIoR(const uint materialID)
{
    if( materialID >= g_Const.materialCount )
        return 1.0;
    else
        return t_MaterialConstants[materialID].ior;
}

HomogeneousVolumeData Bridge::loadHomogeneousVolumeData(const uint materialID)
{
    HomogeneousVolumeData ptVolume;
    ptVolume.sigmaS = float3(0,0,0); 
    ptVolume.sigmaA = float3(0,0,0); 
    ptVolume.g = 0.0;

    if( materialID >= g_Const.materialCount )
        return ptVolume;

    VolumeConstants donutVolume = t_MaterialConstants[materialID].volume;
        
    // these should be precomputed on the C++ side!!
    ptVolume.sigmaS = float3(0,0,0); // no scattering yet
    ptVolume.sigmaA = -log( clamp( donutVolume.attenuationColor, 1e-7, 1 ) ) / max( 1e-30, donutVolume.attenuationDistance.xxx );

    return ptVolume;        
}

// Get the number of analytic light sources
uint Bridge::getAnalyticLightCount()
{
    return g_Const.lightConstantsCount;
}

// Sample single analytic light source given the index (with respect to getAnalyticLightCount() )
bool Bridge::sampleAnalyticLight(const float3 shadingPosW, uint lightIndex, inout SampleGenerator sg, out AnalyticLightSample ls)
{
    // Convert from Donut to PT_SDK light
    LightConstants donutLight = g_Const.lights[lightIndex];

    AnalyticLightData light = AnalyticLightData::make();

    light.posW          = donutLight.position;       ///< World-space position of the center of a light source
    light.dirW          = donutLight.direction;      ///< World-space orientation of the light source (normalized).
    // light.openingAngle       ;                    ///< For point (spot) light: Opening half-angle of a spot light cut-off, pi by default (full sphere).
    light.intensity     = donutLight.color * donutLight.intensity;                   ///< Emitted radiance of th light source
    // light.cosOpeningAngle    ;                   ///< For point (spot) light: cos(openingAngle), -1 by default because openingAngle is pi by default
    // light.cosSubtendedAngle  ;                   ///< For distant light; cosine of the half-angle subtended by the light. Default corresponds to the sun as viewed from earth
    // light.penumbraAngle      ;                   ///< For point (spot) light: Opening half-angle of penumbra region in radians, usually does not exceed openingAngle. 0.f by default, meaning a spot light with hard cut-off

    // Extra parameters for analytic area lights
    // light.tangent            ;                   ///< Tangent vector of the light shape
    // light.surfaceArea        ;                   ///< Surface area of the light shape
    // light.bitangent          ;                   ///< Bitangent vector of the light shape
    // light.transMat           ;                   ///< Transformation matrix of the light shape, from local to world space.
    // light.transMatIT         ;                   ///< Inverse-transpose of transformation matrix of the light shape

    switch (donutLight.lightType)
    {
    case( LightType_Directional ): 
    {
#if 0
        light.type = (uint)AnalyticLightType::Directional; 
        return sampleDirectionalLight(shadingPosW, light, ls);
#else
        light.type = (uint)AnalyticLightType::Distant; 
        // AnalyticLightType::Distant requires transform in transMat, so make one up - doesn't make any difference that it's made up unless it's moving, in which case sampling could be non-consistent
        float3 T, B;
        ConstructONB( -donutLight.direction, T, B );
        light.transMat[0].xyz = T;
        light.transMat[1].xyz = B;
        light.transMat[2].xyz = -donutLight.direction;
        light.cosSubtendedAngle = cos( donutLight.angularSizeOrInvRange * 0.5);
        return sampleDistantLight(shadingPosW, light, sampleNext2D(sg), ls);
#endif
            
    } break; // AnalyticLightType::Distant?
    case( LightType_Spot ): 
    {
        light.type = (uint)AnalyticLightType::Point; 
        light.openingAngle = donutLight.outerAngle;
        light.cosOpeningAngle = cos(donutLight.outerAngle);
        light.penumbraAngle = donutLight.innerAngle;
        return samplePointLight(shadingPosW, light, ls);
    } break;
    case( LightType_Point ): 
    {
        light.type = (uint)AnalyticLightType::Point; 
        return samplePointLight(shadingPosW, light, ls);
    } break;
    default: return false;
    }
}

// 2.5D motion vectors
float3 Bridge::computeMotionVector( float3 posW, float3 prevPosW )
{
    PlanarViewConstants view = g_Const.view;
    PlanarViewConstants previousView = g_Const.previousView;

    float4 clipPos = mul(float4(posW, 1), view.matWorldToClipNoOffset);
    clipPos.xyz /= clipPos.w;
    float4 prevClipPos = mul(float4(prevPosW, 1), previousView.matWorldToClipNoOffset);
    prevClipPos.xyz /= prevClipPos.w;

    if (clipPos.w <= 0 || prevClipPos.w <= 0)
        return float3(0,0,0);

    float3 motion;
    motion.xy = (prevClipPos.xy - clipPos.xy) * view.clipToWindowScale;
    //motion.xy += (view.pixelOffset - previousView.pixelOffset); //<- no longer needed, using NoOffset matrices
    motion.z = prevClipPos.w - clipPos.w; // Use view depth

    return motion;
}
// 2.5D motion vectors
float3 Bridge::computeSkyMotionVector( const uint2 pixelPos )
{
    PlanarViewConstants view = g_Const.view;
    PlanarViewConstants previousView = g_Const.previousView;

    float4 clipPos = float4( (pixelPos + 0.5.xx)/g_Const.view.clipToWindowScale+float2(-1,1), 1e-7, 1.0);
    float4 viewPos = mul( clipPos, view.matClipToWorldNoOffset ); viewPos.xyzw /= viewPos.w;
    float4 prevClipPos = mul(viewPos, previousView.matWorldToClipNoOffset);
    prevClipPos.xyz /= prevClipPos.w;

    float3 motion;
    motion.xy = (prevClipPos.xy - clipPos.xy) * view.clipToWindowScale;
    //motion.xy += (view.pixelOffset - previousView.pixelOffset); <- no longer needed, using NoOffset matrices
    motion.z = 0; //prevClipPos.w - clipPos.w; // Use view depth

    return motion;
}

bool AlphaTestImpl(SubInstanceData subInstanceData, uint triangleIndex, float2 rayBarycentrics)
{
    bool alphaTested = (subInstanceData.FlagsAndSortKey & SubInstanceData::Flags_AlphaTested) != 0;
    if( !alphaTested ) // note: with correct use of D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE this is unnecessary, but there are cases (such as disabling texture but leaving alpha tested state) in which this isn't handled correctly
        return true;
        
    // have to do all this to figure out UVs!
    float2 texcoord;
    {
        GeometryData geometry = t_GeometryData[NonUniformResourceIndex(subInstanceData.GlobalGeometryIndex)];

        ByteAddressBuffer indexBuffer = t_BindlessBuffers[NonUniformResourceIndex(geometry.indexBufferIndex)];
        ByteAddressBuffer vertexBuffer = t_BindlessBuffers[NonUniformResourceIndex(geometry.vertexBufferIndex)];

        float3 barycentrics;
        barycentrics.yz = rayBarycentrics;
        barycentrics.x = 1.0 - (barycentrics.y + barycentrics.z);

        uint3 indices = indexBuffer.Load3(geometry.indexOffset + triangleIndex * c_SizeOfTriangleIndices);

        float2 vertexTexcoords[3];
        vertexTexcoords[0] = asfloat(vertexBuffer.Load2(geometry.texCoord1Offset + indices[0] * c_SizeOfTexcoord));
        vertexTexcoords[1] = asfloat(vertexBuffer.Load2(geometry.texCoord1Offset + indices[1] * c_SizeOfTexcoord));
        vertexTexcoords[2] = asfloat(vertexBuffer.Load2(geometry.texCoord1Offset + indices[2] * c_SizeOfTexcoord));
        texcoord = interpolate(vertexTexcoords, barycentrics);
    }
    // sample the alpha (opacity) texture and test vs the threshold
    Texture2D diffuseTexture = t_BindlessTextures[NonUniformResourceIndex(subInstanceData.AlphaTextureIndex)];
    float opacityValue = diffuseTexture.SampleLevel(s_MaterialSampler, texcoord, 0).a; // <- hard coded to .a channel but we might want a separate alpha only texture, maybe in .g of BC1
    return opacityValue >= subInstanceData.AlphaCutoff;
}

bool Bridge::AlphaTest(uint instanceID, uint instanceIndex, uint geometryIndex, uint triangleIndex, float2 rayBarycentrics)
{
    SubInstanceData subInstanceData = t_SubInstanceData[NonUniformResourceIndex(instanceID + geometryIndex)];

    return AlphaTestImpl(subInstanceData, triangleIndex, rayBarycentrics);
}

bool Bridge::AlphaTestVisibilityRay(uint instanceID, uint instanceIndex, uint geometryIndex, uint triangleIndex, float2 rayBarycentrics)
{
    SubInstanceData subInstanceData = t_SubInstanceData[NonUniformResourceIndex(instanceID + geometryIndex)];

    bool excludeFromNEE = (subInstanceData.FlagsAndSortKey & SubInstanceData::Flags_ExcludeFromNEE) != 0;
    if (excludeFromNEE)
        return false;

    return AlphaTestImpl(subInstanceData, triangleIndex, rayBarycentrics);
}

// There's a relatively high cost to this when used in large shaders just due to register allocation required for alphaTest, even if all geometries are opaque.
// Consider simplifying alpha testing - perhaps splitting it up from the main geometry path, load it with fewer indirections or something like that.
bool Bridge::traceVisibilityRay(RayDesc ray, const RayCone rayCone, const int pathVertexIndex, DebugContext debug)
{
#if 0
    #error make sure to enable specialized "visibility miss shader" for this to work
    const uint missShaderIndex = 1; // visibility miss shader
    VisibilityPayload visibilityPayload = VisibilityPayload::make();     // will be set to 1 if miss shader called
    TraceRay(SceneBVH, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xff, 0, 0, missShaderIndex, ray, visibilityPayload);
    return visibilityPayload.missed != 0;
#else
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> rayQuery;
    rayQuery.TraceRayInline(SceneBVH, RAY_FLAG_NONE, 0xff, ray);

    while (rayQuery.Proceed())
    {
        if (rayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            [branch]if (Bridge::AlphaTestVisibilityRay(
                rayQuery.CandidateInstanceID(),
                rayQuery.CandidateInstanceIndex(),
                rayQuery.CandidateGeometryIndex(),
                rayQuery.CandidatePrimitiveIndex(),
                rayQuery.CandidateTriangleBarycentrics()
                //, debug
                )
            )
            {
                rayQuery.CommitNonOpaqueTriangleHit();
                // break; <- TODO: revisit - not needed when using RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH?
            }
        }
    }

        
#if ENABLE_DEBUG_VIZUALISATION && !NON_PATH_TRACING_PASS && PATH_TRACER_MODE!=PATH_TRACER_MODE_BUILD_STABLE_PLANES
    float visible = rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        ray.TMax = rayQuery.CommittedRayT();    // <- this gets passed via NvMakeHitWithRecordIndex/NvInvokeHitObject as RayTCurrent() or similar in ubershader path

    if( debug.IsDebugPixel() )
        debug.DrawLine(ray.Origin, ray.Origin+ray.Direction*ray.TMax, float4(visible.x, visible.x, 0.8, 0.2), float4(visible.x, visible.x, 0.8, 0.2));
#endif

    return !rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
#endif
}

void Bridge::traceScatterRay(const PathState path, inout RayDesc ray, inout RayQuery<RAY_FLAG_NONE> rayQuery, inout PackedHitInfo packedHitInfo, inout int sortKey, DebugContext debug)
{
    ray = path.getScatterRay().toRayDesc();
    rayQuery.TraceRayInline(SceneBVH, RAY_FLAG_NONE, 0xff, ray);

    while (rayQuery.Proceed())
    {
        if (rayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            // A.k.a. 'Anyhit' shader!
            [branch]if (Bridge::AlphaTest(
                rayQuery.CandidateInstanceID(),
                rayQuery.CandidateInstanceIndex(),
                rayQuery.CandidateGeometryIndex(),
                rayQuery.CandidatePrimitiveIndex(),
                rayQuery.CandidateTriangleBarycentrics()
                //, auxContext.debug
                )
            )
            {
                rayQuery.CommitNonOpaqueTriangleHit();
            }
        }
    }

    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        ray.TMax = rayQuery.CommittedRayT();    // <- this gets passed via NvMakeHitWithRecordIndex/NvInvokeHitObject as RayTCurrent() or similar in ubershader path

        TriangleHit triangleHit;
        triangleHit.instanceID      = GeometryInstanceID::make( rayQuery.CommittedInstanceIndex(), rayQuery.CommittedGeometryIndex() );
        triangleHit.primitiveIndex  = rayQuery.CommittedPrimitiveIndex();
        triangleHit.barycentrics    = rayQuery.CommittedTriangleBarycentrics(); // attrib.barycentrics;
        packedHitInfo = triangleHit.pack();

        // per-instance sort key from cpp side - only needed if USE_UBERSHADER_IN_SER used
        sortKey = t_SubInstanceData[rayQuery.CommittedInstanceID()+rayQuery.CommittedGeometryIndex()].FlagsAndSortKey & 0xFFFF;
    }
    else
    {
        packedHitInfo = PACKED_HIT_INFO_ZERO; // this invokes miss shader a.k.a. sky!
        sortKey = 0;
    }
}

void Bridge::StoreSecondarySurfacePositionAndNormal(uint2 pixelCoordinate, float3 worldPos, float3 normal)
{
    const uint encodedNormal = ndirToOctUnorm32(normal);
    u_SecondarySurfacePositionNormal[pixelCoordinate] = float4(worldPos, asfloat(encodedNormal));
}

EnvMapSampler createEnvMapSampler()
{
    return EnvMapSampler::make(
        s_ImportanceSampler,
        t_ImportanceMap,
        g_Const.envMapSamplerData.importanceInvDim,
        g_Const.envMapSamplerData.importanceBaseMip,
        t_EnvironmentMap,
        s_EnvironmentMapSampler,
        g_Const.envMapData
    );
}

bool Bridge::EnvMap::HasEnvMap()
{
    return g_Const.ptConsts.hasEnvMap;
}

float3 Bridge::EnvMap::Eval(float3 dir)
{
    EnvMapSampler env = createEnvMapSampler();
    return env.eval(dir);
}

float Bridge::EnvMap::EvalPdf(float3 dir)
{
    EnvMapSampler env = createEnvMapSampler();
    return env.evalPdf(dir);
}

bool Bridge::EnvMap::Sample(const float2 rnd, out EnvMapSample result)
{
    EnvMapSampler env = createEnvMapSampler();
    return env.sample(rnd, result);
}

#endif // __PATH_TRACER_BRIDGE_DONUT_HLSLI__