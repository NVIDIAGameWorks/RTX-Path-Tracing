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

#pragma once

#include <donut/core/math/math.h>
#include <donut/engine/DescriptorTableManager.h>
#include <donut/shaders/light_types.h>
#include <nvrhi/nvrhi.h>
#include <memory>

struct MaterialConstants;
struct LightConstants;
struct LightProbeConstants;

namespace Json
{
    class Value;
}

namespace donut::engine
{
    enum class TextureAlphaMode
    {
        UNKNOWN = 0,
        STRAIGHT = 1,
        PREMULTIPLIED = 2,
        OPAQUE_ = 3,
        CUSTOM = 4,
    };

    struct LoadedTexture
    {
        nvrhi::TextureHandle texture;
        TextureAlphaMode alphaMode = TextureAlphaMode::UNKNOWN;
        uint32_t originalBitsPerPixel = 0;
        DescriptorHandle bindlessDescriptor;
        std::string path;
        std::string mimeType;
    };

    enum class VertexAttribute
    {
        Position,
        PrevPosition,
        TexCoord1,
        TexCoord2,
        Normal,
        Tangent,
        Transform,
        PrevTransform,
        JointIndices,
        JointWeights,

        Count
    };

    nvrhi::VertexAttributeDesc GetVertexAttributeDesc(VertexAttribute attribute, const char* name, uint32_t bufferIndex);


    struct SceneLoadingStats
    {
        std::atomic<uint32_t> ObjectsTotal;
        std::atomic<uint32_t> ObjectsLoaded;
    };

    // NOTE regarding MaterialDomain and transparency. It may seem that the Transparent attribute
    // is orthogonal to the blending mode (opaque, alpha-tested, alpha-blended). In glTF, it is
    // indeed an independent extension, KHR_materials_transmission, that can interact with the
    // blending mode. But enabling physical transmission on an object is an important change
    // for renderers: for example, rasterizers need to render "opaque" transmissive objects in a
    // separate render pass, together with alpha bleneded materials; ray tracers also need to
    // process transmissive objects in a different way from regular opaque or alpha-tested objects.
    // Specifying the transmission option in the material domain makes these requirements explicit.

    enum class MaterialDomain : uint8_t
    {
        Opaque,
        AlphaTested,
        AlphaBlended,
        Transmissive,
        TransmissiveAlphaTested,
        TransmissiveAlphaBlended,

        Count
    };

    const char* MaterialDomainToString(MaterialDomain domain);

    struct Material
    {
        std::string name;
        MaterialDomain domain = MaterialDomain::Opaque;
        std::shared_ptr<LoadedTexture> baseOrDiffuseTexture; // metal-rough: base color; spec-gloss: diffuse color; .a = opacity (both modes)
        std::shared_ptr<LoadedTexture> metalRoughOrSpecularTexture; // metal-rough: ORM map; spec-gloss: specular color, .a = glossiness
        std::shared_ptr<LoadedTexture> normalTexture;
        std::shared_ptr<LoadedTexture> emissiveTexture;
        std::shared_ptr<LoadedTexture> occlusionTexture;
        std::shared_ptr<LoadedTexture> transmissionTexture; // see KHR_materials_transmission; undefined on specular-gloss materials
        // std::shared_ptr<LoadedTexture> thicknessTexture; // see KHR_materials_volume (not implemented yet)
        nvrhi::BufferHandle materialConstants;
        dm::float3 baseOrDiffuseColor = 1.f; // metal-rough: base color, spec-gloss: diffuse color (if no texture present)
        dm::float3 specularColor = 0.f; // spec-gloss: specular color
        dm::float3 emissiveColor = 0.f;
        float emissiveIntensity = 1.f; // additional multiplier for emissiveColor
        float metalness = 0.f; // metal-rough only
        float roughness = 0.f; // both metal-rough and spec-gloss
        float opacity = 1.f; // for transparent materials; multiplied by diffuse.a if present
        float alphaCutoff = 0.5f; // for alpha tested materials
        float transmissionFactor = 0.f; // see KHR_materials_transmission; undefined on specular-gloss materials
        float diffuseTransmissionFactor = 0.f; // like specularTransmissionFactor, except using diffuse transmission lobe (roughness ignored)
        float normalTextureScale = 1.f;
        float occlusionStrength = 1.f;
        float ior = 1.5f; // index of refraction, see KHR_materials_ior

        // Toggle between two PBR models: metal-rough and specular-gloss.
        // See the comments on the other fields here.
        bool useSpecularGlossModel = false;

        // Toggles for the textures. Only effective if the corresponding texture is non-null.
        bool enableBaseOrDiffuseTexture = true;
        bool enableMetalRoughOrSpecularTexture = true;
        bool enableNormalTexture = true;
        bool enableEmissiveTexture = true;
        bool enableOcclusionTexture = true;
        bool enableTransmissionTexture = true;

        bool doubleSided = false;

        bool thinSurface = false;   // As per Falcor/pt_sdk convention, ray hitting a material with the thin surface is assumed to enter and leave surface in the same bounce and it makes most sense when used with doubleSided; it skips all volume logic.

        bool excludeFromNEE = false; // The mesh will not be part of NEE.

        bool psdExclude = false; // will not propagate dominant stable plane when doing path space decomposition
        int psdDominantDeltaLobe = -1; // for path space decomposition: -1 means no dominant; 0 usually means transmission, 1 usually means reflection, 2 usually means clearcoat reflection - must match corresponding BSDFSample::getDeltaLobeIndex()!
        
        static constexpr int kMaterialMaxNestedPriority = 14;
        int nestedPriority = kMaterialMaxNestedPriority;     // When volume meshes overlap, will cause higher nestedPriority mesh to 'carve out' the volumes with lower nestedPriority (see https://www.sidefx.com/docs/houdini/render/nested.html)

        // KHR_materials_volume - see https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_volume#properties
        float volumeThicknessFactor = 0.0f; // in path tracing this is only used to set "thinSurface == thicknessFactor != 0" with value otherwise ignored (since we get exact thickness)
        float volumeAttenuationDistance = FLT_MAX;
        dm::float3 volumeAttenuationColor = 1.f;

        int materialID = 0;
        bool dirty = true; // set this to true to make Scene update the material data

        virtual ~Material() = default;
        void FillConstantBuffer(struct MaterialConstants& constants) const;
        bool SetProperty(const std::string& name, const dm::float4& value);
    };


    struct InputAssemblerBindings
    {
        VertexAttribute vertexBuffers[16];
        uint32_t numVertexBuffers;
    };

    struct BufferGroup
    {
        nvrhi::BufferHandle indexBuffer;
        nvrhi::BufferHandle vertexBuffer;
        nvrhi::BufferHandle instanceBuffer;
        std::shared_ptr<DescriptorHandle> indexBufferDescriptor;
        std::shared_ptr<DescriptorHandle> vertexBufferDescriptor;
        std::shared_ptr<DescriptorHandle> instnaceBufferDescriptor;
        std::array<nvrhi::BufferRange, size_t(VertexAttribute::Count)> vertexBufferRanges;
        std::vector<uint32_t> indexData;
        std::vector<dm::float3> positionData;
        std::vector<dm::float2> texcoord1Data;
        std::vector<dm::float2> texcoord2Data;
        std::vector<uint32_t> normalData;
        std::vector<uint32_t> tangentData;
        std::vector<dm::vector<uint16_t, 4>> jointData;
        std::vector<dm::float4> weightData;

        [[nodiscard]] bool hasAttribute(VertexAttribute attr) const { return vertexBufferRanges[int(attr)].byteSize != 0; }
        nvrhi::BufferRange& getVertexBufferRange(VertexAttribute attr) { return vertexBufferRanges[int(attr)]; }
        [[nodiscard]] const nvrhi::BufferRange& getVertexBufferRange(VertexAttribute attr) const { return vertexBufferRanges[int(attr)]; }
    };

	struct MeshGeometryDebugData
	{
        uint32_t ommArrayDataOffset = 0xFFFFFFFF; // for use by applications
        uint32_t ommDescBufferOffset = 0xFFFFFFFF; // for use by applications
        uint32_t ommIndexBufferOffset = 0xFFFFFFFF; // for use by applications
        nvrhi::Format ommIndexBufferFormat = nvrhi::Format::R32_UINT; // for use by applications
        uint64_t ommStatsTotalKnown = 0;
        uint64_t ommStatsTotalUnknown = 0;
	};

	struct MeshDebugData
	{
        std::shared_ptr<DescriptorHandle> ommArrayDataBufferDescriptor;
        std::shared_ptr<DescriptorHandle> ommDescBufferDescriptor;
        std::shared_ptr<DescriptorHandle> ommIndexBufferDescriptor;
        nvrhi::BufferHandle ommArrayDataBuffer; // for use by applications
        nvrhi::BufferHandle ommDescBuffer; // for use by applications
        nvrhi::BufferHandle ommIndexBuffer; // for use by applications
	};

    struct MeshGeometry
    {
        std::shared_ptr<Material> material;
        dm::box3 objectSpaceBounds;
        uint32_t indexOffsetInMesh = 0;
        uint32_t vertexOffsetInMesh = 0;
        uint32_t numIndices = 0;
        uint32_t numVertices = 0;
        int globalGeometryIndex = 0;

		// (Debug) OMM buffers.
        MeshGeometryDebugData debugData;
        virtual ~MeshGeometry() = default;
    };

    struct MeshInfo
    {
        std::string name;
        std::shared_ptr<BufferGroup> buffers;
        std::shared_ptr<MeshInfo> skinPrototype;
        std::vector<std::shared_ptr<MeshGeometry>> geometries;
        dm::box3 objectSpaceBounds;
        uint32_t indexOffset = 0;
        uint32_t vertexOffset = 0;
        uint32_t totalIndices = 0;
        uint32_t totalVertices = 0;
        int globalMeshIndex = 0;
        nvrhi::rt::AccelStructHandle accelStruct; // for use by applications
        nvrhi::rt::AccelStructHandle accelStructOMM; // for use by application
        std::vector<nvrhi::rt::OpacityMicromapHandle> opacityMicroMaps; // for use by application

		std::unique_ptr<MeshDebugData> debugData;
        bool debugDataDirty = true; // set this to true to make Scene update the debug data

        virtual ~MeshInfo() = default;
    };
    
    struct LightProbe
    {
        std::string name;
        nvrhi::TextureHandle diffuseMap;
        nvrhi::TextureHandle specularMap;
        nvrhi::TextureHandle environmentBrdf;
        uint32_t diffuseArrayIndex = 0;
        uint32_t specularArrayIndex = 0;
        float diffuseScale = 1.f;
        float specularScale = 1.f;
        bool enabled = true;
        dm::frustum bounds = dm::frustum::infinite();

        [[nodiscard]] bool IsActive() const;
        void FillLightProbeConstants(LightProbeConstants& lightProbeConstants) const;
    };

    inline nvrhi::IBuffer* BufferOrFallback(nvrhi::IBuffer* primary, nvrhi::IBuffer* secondary)
    {
        return primary ? primary : secondary;
    }
}
