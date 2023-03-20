/*
* Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
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

#define CGLTF_IMPLEMENTATION
#include <cstring>

#include <cgltf.h>

#include <donut/engine/GltfImporter.h>
#include <donut/engine/TextureCache.h>
#include <donut/engine/SceneGraph.h>
#include <donut/core/vfs/VFS.h>
#include <donut/core/log.h>

#include "nvrhi/common/misc.h"

using namespace donut::math;
using namespace donut::vfs;
using namespace donut::engine;


class BufferRegionBlob : public IBlob
{
private:
    std::shared_ptr<IBlob> m_parent;
    const void* m_data;
    size_t m_size;

public:
    BufferRegionBlob(const std::shared_ptr<IBlob>& parent, size_t offset, size_t size)
        : m_parent(parent)
        , m_data(static_cast<const uint8_t*>(parent->data()) + offset)
        , m_size(size)
    {
    }

    [[nodiscard]] const void* data() const override
    {
        return m_data;
    }

    [[nodiscard]] size_t size() const override
    {
        return m_size;
    }
};



GltfImporter::GltfImporter(std::shared_ptr<vfs::IFileSystem> fs, std::shared_ptr<SceneTypeFactory> sceneTypeFactory)
    : m_fs(std::move(fs))
    , m_SceneTypeFactory(std::move(sceneTypeFactory))
{
}


struct cgltf_vfs_context
{
    std::shared_ptr<donut::vfs::IFileSystem> fs;
    std::vector<std::shared_ptr<IBlob>> blobs;
};

static cgltf_result cgltf_read_file_vfs(const struct cgltf_memory_options* memory_options,
    const struct cgltf_file_options* file_options,const char* path, cgltf_size* size, void** data)
{
    cgltf_vfs_context* context = (cgltf_vfs_context*)file_options->user_data;

    auto blob = context->fs->readFile(path);

    if (!blob)
        return cgltf_result_file_not_found;

    context->blobs.push_back(blob);

    if (size) *size = blob->size();
    if (data) *data = (void*)blob->data();  // NOLINT(clang-diagnostic-cast-qual)

    return cgltf_result_success;
}

void cgltf_release_file_vfs(const struct cgltf_memory_options*, const struct cgltf_file_options*, void*)
{
    // do nothing
}

// glTF only support DDS images through the MSFT_texture_dds extension.
// Since cgltf does not support this extension, we parse the custom extension string as json here.
// See https://github.com/KhronosGroup/glTF/tree/master/extensions/2.0/Vendor/MSFT_texture_dds 
static const cgltf_image* ParseDdsImage(const cgltf_texture* texture, const cgltf_data* objects)
{
    for (size_t i = 0; i < texture->extensions_count; i++)
    {
        const cgltf_extension& ext = texture->extensions[i];

        if (!ext.name || !ext.data)
            continue;

        if (strcmp(ext.name, "MSFT_texture_dds") != 0)
            continue;

        size_t extensionLength = strlen(ext.data);
        if (extensionLength > 1024)
            return nullptr; // safeguard against weird inputs

        jsmn_parser parser;
        jsmn_init(&parser);

        // count the tokens, normally there are 3
        int numTokens = jsmn_parse(&parser, ext.data, extensionLength, nullptr, 0);

        // allocate the tokens on the stack
        jsmntok_t* tokens = (jsmntok_t*)alloca(numTokens * sizeof(jsmntok_t));

        // reset the parser and prse
        jsmn_init(&parser);
        int numParsed = jsmn_parse(&parser, ext.data, extensionLength, tokens, numTokens);
        if (numParsed != numTokens)
            goto fail;

        if (tokens[0].type != JSMN_OBJECT)
            goto fail; // expecting that the extension is an object

        for (int k = 1; k < numTokens; k++)
        {
            if (tokens[k].type != JSMN_STRING)
                goto fail; // expecting a string key
            
            if (cgltf_json_strcmp(tokens + k, (const uint8_t*)ext.data, "source") == 0)
            {
                ++k;
                int index = cgltf_json_to_int(tokens + k, (const uint8_t*)ext.data);
                if (index < 0)
                    goto fail; // expecting a non-negative integer; non-value results in CGLTF_ERROR_JSON which is negative

                if (size_t(index) >= objects->images_count)
                {
                    donut::log::warning("Invalid image index %d specified in glTF texture definition", index);
                    return nullptr;
                }

                return objects->images + index;
            }

            // this was something else - skip it
            k = cgltf_skip_json(tokens, k);
        }

    fail:
        donut::log::warning("Failed to parse the DDS glTF extension: %s", ext.data);
        return nullptr;
    }

    return nullptr;
}

static const char* cgltf_error_to_string(cgltf_result res)
{
    switch(res)
    {
    case cgltf_result_success:
        return "Success";
    case cgltf_result_data_too_short:
        return "Data is too short";
    case cgltf_result_unknown_format:
        return "Unknown format";
    case cgltf_result_invalid_json:
        return "Invalid JSON";
    case cgltf_result_invalid_gltf:
        return "Invalid glTF";
    case cgltf_result_invalid_options:
        return "Invalid options";
    case cgltf_result_file_not_found:
        return "File not found";
    case cgltf_result_io_error:
        return "I/O error";
    case cgltf_result_out_of_memory:
        return "Out of memory";
    case cgltf_result_legacy_gltf:
        return "Legacy glTF";
    default:
        return "Unknown error";
    }
}

static std::pair<const uint8_t*, size_t> cgltf_buffer_iterator(const cgltf_accessor* accessor, size_t defaultStride)
{
    // TODO: sparse accessor support
    const cgltf_buffer_view* view = accessor->buffer_view;
    const uint8_t* data = (uint8_t*)view->buffer->data + view->offset + accessor->offset;
    const size_t stride = view->stride ? view->stride : defaultStride;
    return std::make_pair(data, stride);
}

bool GltfImporter::Load(
    const std::filesystem::path& fileName,
    TextureCache& textureCache,
    SceneLoadingStats& stats,
    tf::Executor* executor,
    SceneImportResult& result) const
{
    // Set this to 'true' if you need to fix broken tangents in a model.
    // Patched buffers will be saved alongside the gltf file, named like "<scene-name>.buffer<N>.bin"
    constexpr bool c_ForceRebuildTangents = false;

    // Search for a matching .dds file first if loading an uncompressed texture like .png,
    // even if the DDS is not specified in the glTF file.
    constexpr bool c_SearchForDds = true;

    result.rootNode.reset();

    cgltf_vfs_context vfsContext;
    vfsContext.fs = m_fs;

    cgltf_options options{};
    options.file.read = &cgltf_read_file_vfs;
    options.file.release = &cgltf_release_file_vfs;
    options.file.user_data = &vfsContext;

    std::string normalizedFileName = fileName.lexically_normal().generic_string();

    cgltf_data* objects = nullptr;
    cgltf_result res = cgltf_parse_file(&options, normalizedFileName.c_str(), &objects);
    if (res != cgltf_result_success)
    {
        log::error("Couldn't load glTF file '%s': %s", normalizedFileName.c_str(), cgltf_error_to_string(res));
        return false;
    }

    res = cgltf_load_buffers(&options, objects, normalizedFileName.c_str());
    if (res != cgltf_result_success)
    {
        log::error("Failed to load buffers for glTF file '%s': ", normalizedFileName.c_str(), cgltf_error_to_string(res));
        return false;
    }

    std::unordered_map<const cgltf_image*, std::shared_ptr<LoadedTexture>> textures;

    auto load_texture = [this, &textures, &textureCache, executor, &fileName, objects, &vfsContext, c_SearchForDds](const cgltf_texture* texture, bool sRGB)
    {
        if (!texture)
            return std::shared_ptr<LoadedTexture>(nullptr);

        // See if the extensions include a DDS image
        const cgltf_image* ddsImage = ParseDdsImage(texture, objects);

        if ((!texture->image || (!texture->image->uri && !texture->image->buffer_view)) && (!ddsImage || (!ddsImage->uri && !ddsImage->buffer_view)))
            return std::shared_ptr<LoadedTexture>(nullptr);

        // Pick either DDS or standard image, prefer DDS
        const cgltf_image* activeImage = (ddsImage && (ddsImage->uri || ddsImage->buffer_view)) ? ddsImage : texture->image;

        auto it = textures.find(activeImage);
        if (it != textures.end())
            return it->second;

        std::shared_ptr<LoadedTexture> loadedTexture;

        if (activeImage->buffer_view)
        {
            // If the image has inline data, like coming from a GLB container, use that.

            const uint8_t* dataPtr = static_cast<const uint8_t*>(activeImage->buffer_view->buffer->data) + activeImage->buffer_view->offset;
            const size_t dataSize = activeImage->buffer_view->size;

            // We need to have a managed pointer to the texture data for async decoding.
            std::shared_ptr<IBlob> textureData;

            // Try to find an existing file blob that includes our data.
            for (const auto& blob : vfsContext.blobs)
            {
                const uint8_t* blobData = static_cast<const uint8_t*>(blob->data());
                const size_t blobSize = blob->size();

                if (blobData < dataPtr && blobData + blobSize > dataPtr)
                {
                    // Found the file blob - create a range blob out of it and keep a strong reference.
                    assert(dataPtr + dataSize <= blobData + blobSize);
                    textureData = std::make_shared<BufferRegionBlob>(blob, dataPtr - blobData, dataSize);
                    break;
                }
            }

            // Didn't find a file blob - copy the data into a new container.
            if (!textureData)
            {
                void* dataCopy = malloc(dataSize);
                assert(dataCopy);
                memcpy(dataCopy, dataPtr, dataSize);
                textureData = std::make_shared<vfs::Blob>(dataCopy, dataSize);
            }

            uint64_t imageIndex = activeImage - objects->images;
            std::string name = activeImage->name ? activeImage->name : fileName.filename().generic_string() + "[" + std::to_string(imageIndex) + "]";
            std::string mimeType = activeImage->mime_type ? activeImage->mime_type : "";

#ifdef DONUT_WITH_TASKFLOW
            if (executor)
                loadedTexture = textureCache.LoadTextureFromMemoryAsync(textureData, name, mimeType, sRGB, *executor);
            else
#endif
                loadedTexture = textureCache.LoadTextureFromMemoryDeferred(textureData, name, mimeType, sRGB);
        }
        else
        {
            // No inline data - read a file.

            // Temp hack - for some reason something in cgltf replaces chars with %20 - revert it here but this should be fixed properly in the future
            std::string origPath = activeImage->uri;
            size_t index = 0; while (true) { index = origPath.find("%20", index); if (index == std::string::npos) break;
                origPath.replace(index, 3, " "); index += 3;
            }
            
            std::filesystem::path filePath = fileName.parent_path() / origPath.c_str();

            // Try to replace the texture with DDS, if enabled.
            if (c_SearchForDds && !ddsImage)
            {
                std::filesystem::path filePathDDS = filePath;

                filePathDDS.replace_extension(".dds");

                if (m_fs->fileExists(filePathDDS))
                    filePath = filePathDDS;
            }

#ifdef DONUT_WITH_TASKFLOW
            if (executor)
                loadedTexture = textureCache.LoadTextureFromFileAsync(filePath, sRGB, *executor);
            else
#endif
                loadedTexture = textureCache.LoadTextureFromFileDeferred(filePath, sRGB);
        }
        textures[activeImage] = loadedTexture;
        return loadedTexture;
    };

    std::unordered_map<const cgltf_material*, std::shared_ptr<Material>> materials;
    
    for (size_t mat_idx = 0; mat_idx < objects->materials_count; mat_idx++)
    {
        const cgltf_material& material = objects->materials[mat_idx];
        
        std::shared_ptr<Material> matinfo = m_SceneTypeFactory->CreateMaterial();
        if (material.name) matinfo->name = material.name;

        bool useTransmission = false;

        if (material.has_pbr_specular_glossiness)
        {
            matinfo->useSpecularGlossModel = true;
            matinfo->baseOrDiffuseTexture = load_texture(material.pbr_specular_glossiness.diffuse_texture.texture, true);
            matinfo->metalRoughOrSpecularTexture = load_texture(material.pbr_specular_glossiness.specular_glossiness_texture.texture, true);
            matinfo->baseOrDiffuseColor = material.pbr_specular_glossiness.diffuse_factor;
            matinfo->specularColor = material.pbr_specular_glossiness.specular_factor;
            matinfo->roughness = 1.f - material.pbr_specular_glossiness.glossiness_factor;
            matinfo->opacity = material.pbr_specular_glossiness.diffuse_factor[3];

            if (material.has_transmission)
            {
            }
        }
        else if (material.has_pbr_metallic_roughness)
        {
            matinfo->useSpecularGlossModel = false;
            matinfo->baseOrDiffuseTexture = load_texture(material.pbr_metallic_roughness.base_color_texture.texture, true);
            matinfo->metalRoughOrSpecularTexture = load_texture(material.pbr_metallic_roughness.metallic_roughness_texture.texture, false);
            matinfo->baseOrDiffuseColor = material.pbr_metallic_roughness.base_color_factor;
            matinfo->metalness = material.pbr_metallic_roughness.metallic_factor;
            matinfo->roughness = material.pbr_metallic_roughness.roughness_factor;
            matinfo->opacity = material.pbr_metallic_roughness.base_color_factor[3];

        }

        if (material.has_transmission)
        {
            if (material.has_pbr_specular_glossiness)
            {
                log::warning("Material '%s' uses the KHR_materials_transmission extension, which is undefined on materials using the "
                    "KHR_materials_pbrSpecularGlossiness extension model.", material.name ? material.name : "<Unnamed>");
            }

            matinfo->transmissionTexture = load_texture(material.transmission.transmission_texture.texture, false);
            matinfo->transmissionFactor = material.transmission.transmission_factor;
            useTransmission = true;
        }

        if (material.has_ior)
        {
            if (material.has_pbr_specular_glossiness)
            {
                log::warning("Material '%s' uses the KHR_materials_ior extension, which is undefined on materials using the "
                    "KHR_materials_pbrSpecularGlossiness extension model.", material.name ? material.name : "<Unnamed>");
            }

            matinfo->ior = material.ior.ior;
        }

        if (material.has_volume)
        {
            matinfo->thinSurface = material.volume.thickness_factor == 0;
            matinfo->volumeThicknessFactor = material.volume.thickness_factor;
            matinfo->volumeAttenuationDistance = material.volume.attenuation_distance;
            matinfo->volumeAttenuationColor = material.volume.attenuation_color;
        }
        else
        {
            matinfo->thinSurface = material.double_sided;   // makes sense to default to this for now
        }

        matinfo->emissiveTexture = load_texture(material.emissive_texture.texture, true);
        matinfo->emissiveColor = material.emissive_factor;
        matinfo->emissiveIntensity = dm::maxComponent(matinfo->emissiveColor);
        if (matinfo->emissiveIntensity > 0.f)
            matinfo->emissiveColor /= matinfo->emissiveIntensity;
        else
            matinfo->emissiveIntensity = 1.f;
        matinfo->normalTexture = load_texture(material.normal_texture.texture, false);
        matinfo->normalTextureScale = material.normal_texture.scale;
        matinfo->occlusionTexture = load_texture(material.occlusion_texture.texture, false);
        matinfo->occlusionStrength = material.occlusion_texture.scale;
        matinfo->alphaCutoff = material.alpha_cutoff;
        matinfo->doubleSided = material.double_sided;

        switch (material.alpha_mode)
        {
        case cgltf_alpha_mode_opaque: matinfo->domain = useTransmission ? MaterialDomain::Transmissive : MaterialDomain::Opaque; break;
        case cgltf_alpha_mode_mask: matinfo->domain = useTransmission ? MaterialDomain::TransmissiveAlphaTested : MaterialDomain::AlphaTested; break;
        case cgltf_alpha_mode_blend: matinfo->domain = useTransmission ? MaterialDomain::TransmissiveAlphaBlended : MaterialDomain::AlphaBlended; break;
        }

        materials[&material] = matinfo;
    }
    
    size_t totalIndices = 0;
    size_t totalVertices = 0;
    bool hasJoints = false;

    for (size_t mesh_idx = 0; mesh_idx < objects->meshes_count; mesh_idx++)
    {
        const cgltf_mesh& mesh = objects->meshes[mesh_idx];
        
        for (size_t prim_idx = 0; prim_idx < mesh.primitives_count; prim_idx++)
        {
            const cgltf_primitive& prim = mesh.primitives[prim_idx];

            if (prim.type != cgltf_primitive_type_triangles ||
                prim.attributes_count == 0)
                continue;

            if (prim.indices)
                totalIndices += prim.indices->count;
            else
                totalIndices += prim.attributes->data->count;
            totalVertices += prim.attributes->data->count;

            if (!hasJoints)
            {
                // Detect if the primitive has joints or weights attributes.
                for (size_t attr_idx = 0; attr_idx < prim.attributes_count; attr_idx++)
                {
                    const cgltf_attribute& attr = prim.attributes[attr_idx];
                    if (attr.type == cgltf_attribute_type_joints || attr.type == cgltf_attribute_type_weights)
                    {
                        hasJoints = true;
                        break;
                    }
                }
            }
        }
    }

    auto buffers = std::make_shared<BufferGroup>();

    buffers->indexData.resize(totalIndices);
    buffers->positionData.resize(totalVertices);
    buffers->normalData.resize(totalVertices);
    buffers->tangentData.resize(totalVertices);
    buffers->texcoord1Data.resize(totalVertices);
    if (hasJoints)
    {
        // Allocate joint/weight arrays for all the vertices in the model.
        // This is wasteful in case the model has both skinned and non-skinned meshes; TODO: improve.
        buffers->jointData.resize(totalVertices);
        buffers->weightData.resize(totalVertices);
    }

    totalIndices = 0;
    totalVertices = 0;

    std::unordered_map<const cgltf_mesh*, std::shared_ptr<MeshInfo>> meshMap;

    std::vector<float3> computedTangents;
    std::vector<float3> computedBitangents;
    std::vector<std::shared_ptr<MeshInfo>> meshes;

    for (size_t mesh_idx = 0; mesh_idx < objects->meshes_count; mesh_idx++)
    {
        const cgltf_mesh& mesh = objects->meshes[mesh_idx];

        std::shared_ptr<MeshInfo> minfo = m_SceneTypeFactory->CreateMesh();
        if (mesh.name) minfo->name = mesh.name;
        minfo->buffers = buffers;
        minfo->indexOffset = (uint32_t)totalIndices;
        minfo->vertexOffset = (uint32_t)totalVertices;
        meshes.push_back(minfo);

        meshMap[&mesh] = minfo;
        
        for (size_t prim_idx = 0; prim_idx < mesh.primitives_count; prim_idx++)
        {
            const cgltf_primitive& prim = mesh.primitives[prim_idx];

            if (prim.type != cgltf_primitive_type_triangles ||
                prim.attributes_count == 0)
                continue;

            if (prim.indices)
            {
                assert(prim.indices->component_type == cgltf_component_type_r_32u ||
                    prim.indices->component_type == cgltf_component_type_r_16u ||
                    prim.indices->component_type == cgltf_component_type_r_8u);
                assert(prim.indices->type == cgltf_type_scalar);
            }

            const cgltf_accessor* positions = nullptr;
            const cgltf_accessor* normals = nullptr;
            const cgltf_accessor* tangents = nullptr;
            const cgltf_accessor* texcoords = nullptr;
            const cgltf_accessor* joint_weights = nullptr;
            const cgltf_accessor* joint_indices = nullptr;
            
            for (size_t attr_idx = 0; attr_idx < prim.attributes_count; attr_idx++)
            {
                const cgltf_attribute& attr = prim.attributes[attr_idx];

                // ReSharper disable once CppIncompleteSwitchStatement
                // ReSharper disable once CppDefaultCaseNotHandledInSwitchStatement
                switch(attr.type)  // NOLINT(clang-diagnostic-switch)
                {
                case cgltf_attribute_type_position:
                    assert(attr.data->type == cgltf_type_vec3);
                    assert(attr.data->component_type == cgltf_component_type_r_32f);
                    positions = attr.data;
                    break;
                case cgltf_attribute_type_normal:
                    assert(attr.data->type == cgltf_type_vec3);
                    assert(attr.data->component_type == cgltf_component_type_r_32f);
                    normals = attr.data;
                    break;
                case cgltf_attribute_type_tangent:
                    assert(attr.data->type == cgltf_type_vec4);
                    assert(attr.data->component_type == cgltf_component_type_r_32f);
                    tangents = attr.data;
                    break;
                case cgltf_attribute_type_texcoord:
                    assert(attr.data->type == cgltf_type_vec2);
                    assert(attr.data->component_type == cgltf_component_type_r_32f);
                    if (attr.index == 0)
                        texcoords = attr.data;
                    break;
                case cgltf_attribute_type_joints:
                    assert(attr.data->type == cgltf_type_vec4);
                    assert(attr.data->component_type == cgltf_component_type_r_8u || attr.data->component_type == cgltf_component_type_r_16u);
                    joint_indices = attr.data;
                    break;
                case cgltf_attribute_type_weights:
                    assert(attr.data->type == cgltf_type_vec4);
                    assert(attr.data->component_type == cgltf_component_type_r_8u || attr.data->component_type == cgltf_component_type_r_16u || attr.data->component_type == cgltf_component_type_r_32f);
                    joint_weights = attr.data;
                    break;
                }
            }

            assert(positions);

            size_t indexCount = 0;

            if (prim.indices)
            {
                indexCount = prim.indices->count;

                // copy the indices
                auto [indexSrc, indexStride] = cgltf_buffer_iterator(prim.indices, 0);

                uint32_t* indexDst = buffers->indexData.data() + totalIndices;

                switch(prim.indices->component_type)
                {
                case cgltf_component_type_r_8u:
                    if (!indexStride) indexStride = sizeof(uint8_t);
                    for (size_t i_idx = 0; i_idx < indexCount; i_idx++)
                    {
                        *indexDst = *(const uint8_t*)indexSrc;

                        indexSrc += indexStride;
                        indexDst++;
                    }
                    break;
                case cgltf_component_type_r_16u:
                    if (!indexStride) indexStride = sizeof(uint16_t);
                    for (size_t i_idx = 0; i_idx < indexCount; i_idx++)
                    {
                        *indexDst = *(const uint16_t*)indexSrc;

                        indexSrc += indexStride;
                        indexDst++;
                    }
                    break;
                case cgltf_component_type_r_32u:
                    if (!indexStride) indexStride = sizeof(uint32_t);
                    for (size_t i_idx = 0; i_idx < indexCount; i_idx++)
                    {
                        *indexDst = *(const uint32_t*)indexSrc;

                        indexSrc += indexStride;
                        indexDst++;
                    }
                    break;
                default: 
                    assert(false);
                }
            }
            else
            {
                indexCount = positions->count;

                // generate the indices
                uint32_t* indexDst = buffers->indexData.data() + totalIndices;
                for (size_t i_idx = 0; i_idx < indexCount; i_idx++)
                {
                    *indexDst = (uint32_t)i_idx;
                    indexDst++;
                }
            }

            dm::box3 bounds = dm::box3::empty();

            if (positions)
            {
                auto [positionSrc, positionStride] = cgltf_buffer_iterator(positions, sizeof(float) * 3);
                float3* positionDst = buffers->positionData.data() + totalVertices;

                for (size_t v_idx = 0; v_idx < positions->count; v_idx++)
                {
                    *positionDst = (const float*)positionSrc;

                    bounds |= *positionDst;

                    positionSrc += positionStride;
                    ++positionDst;
                }
            }

            if (normals)
            {
                assert(normals->count == positions->count);

                auto [normalSrc, normalStride] = cgltf_buffer_iterator(normals, sizeof(float) * 3);
                uint32_t* normalDst = buffers->normalData.data() + totalVertices;

                for (size_t v_idx = 0; v_idx < normals->count; v_idx++)
                {
                    float3 normal = (const float*)normalSrc;
                    *normalDst = vectorToSnorm8(normal);

                    normalSrc += normalStride;
                    ++normalDst;
                }
            }

            if (tangents)
            {
                assert(tangents->count == positions->count);

                auto [tangentSrc, tangentStride] = cgltf_buffer_iterator(tangents, sizeof(float) * 4);
                uint32_t* tangentDst = buffers->tangentData.data() + totalVertices;
                
                for (size_t v_idx = 0; v_idx < tangents->count; v_idx++)
                {
                    float4 tangent = (const float*)tangentSrc;
                    *tangentDst = vectorToSnorm8(tangent);

                    tangentSrc += tangentStride;
                    ++tangentDst;
                }
            }

            if (texcoords)
            {
                assert(texcoords->count == positions->count);

                auto [texcoordSrc, texcoordStride] = cgltf_buffer_iterator(texcoords, sizeof(float) * 2);
                float2* texcoordDst = buffers->texcoord1Data.data() + totalVertices;

                for (size_t v_idx = 0; v_idx < texcoords->count; v_idx++)
                {
                    *texcoordDst = (const float*)texcoordSrc;

                    texcoordSrc += texcoordStride;
                    ++texcoordDst;
                }
            }
            else
            {
                float2* texcoordDst = buffers->texcoord1Data.data() + totalVertices;
                for (size_t v_idx = 0; v_idx < positions->count; v_idx++)
                {
                    *texcoordDst = float2(0.f);
                    ++texcoordDst;
                }
            }

            if (normals && texcoords && (!tangents || c_ForceRebuildTangents))
            {
                auto [positionSrc, positionStride] = cgltf_buffer_iterator(positions, sizeof(float) * 3);
                auto [texcoordSrc, texcoordStride] = cgltf_buffer_iterator(texcoords, sizeof(float) * 2);
                auto [normalSrc, normalStride] = cgltf_buffer_iterator(normals, sizeof(float) * 3);
                const uint32_t* indexSrc = buffers->indexData.data() + totalIndices;

                computedTangents.resize(positions->count);
                std::fill(computedTangents.begin(), computedTangents.end(), float3(0.f));

                computedBitangents.resize(positions->count);
                std::fill(computedBitangents.begin(), computedBitangents.end(), float3(0.f));

                for (size_t t_idx = 0; t_idx < indexCount / 3; t_idx++)
                {
                    uint3 tri = indexSrc;
                    indexSrc += 3;

                    float3 p0 = (const float*)(positionSrc + positionStride * tri.x);
                    float3 p1 = (const float*)(positionSrc + positionStride * tri.y);
                    float3 p2 = (const float*)(positionSrc + positionStride * tri.z);

                    float2 t0 = (const float*)(texcoordSrc + texcoordStride * tri.x);
                    float2 t1 = (const float*)(texcoordSrc + texcoordStride * tri.y);
                    float2 t2 = (const float*)(texcoordSrc + texcoordStride * tri.z);

                    float3 dPds = p1 - p0;
                    float3 dPdt = p2 - p0;

                    float2 dTds = t1 - t0;
                    float2 dTdt = t2 - t0;
                    float r = 1.0f / (dTds.x * dTdt.y - dTds.y * dTdt.x);
                    float3 tangent = r * (dPds * dTdt.y - dPdt * dTds.y);
                    float3 bitangent = r * (dPdt * dTds.x - dPds * dTdt.x);

                    float tangentLength = length(tangent);
                    float bitangentLength = length(bitangent);
                    if (tangentLength > 0 && bitangentLength > 0)
                    {
                        tangent /= tangentLength;
                        bitangent /= bitangentLength;

                        computedTangents[tri.x] += tangent;
                        computedTangents[tri.y] += tangent;
                        computedTangents[tri.z] += tangent;
                        computedBitangents[tri.x] += bitangent;
                        computedBitangents[tri.y] += bitangent;
                        computedBitangents[tri.z] += bitangent;
                    }
                }

                uint8_t* tangentSrc = nullptr;
                size_t tangentStride = 0;
                if (tangents)
                {
                    auto pair = cgltf_buffer_iterator(tangents, sizeof(float) * 4);
                    tangentSrc = const_cast<uint8_t*>(pair.first);
                    tangentStride = pair.second;
                }

                uint32_t* tangentDst = buffers->tangentData.data() + totalVertices;

                for (size_t v_idx = 0; v_idx < positions->count; v_idx++)
                {
                    float3 normal = (const float*)normalSrc;
                    float3 tangent = computedTangents[v_idx];
                    float3 bitangent = computedBitangents[v_idx];

                    float sign = 0;
                    float tangentLength = length(tangent);
                    float bitangentLength = length(bitangent);
                    if (tangentLength > 0 && bitangentLength > 0)
                    {
                        tangent /= tangentLength;
                        bitangent /= bitangentLength;
                        float3 cross_b = cross(normal, tangent);
                        sign = (dot(cross_b, bitangent) > 0) ? -1.f : 1.f;
                    }

                    *tangentDst = vectorToSnorm8(float4(tangent, sign));

                    if (c_ForceRebuildTangents && tangents)
                    {
                        *(float4*)tangentSrc = float4(tangent, sign);
                        tangentSrc += tangentStride;
                    }
                    
                    normalSrc += normalStride;
                    ++tangentDst;
                }
            }

            if (joint_indices)
            {
                assert(joint_indices->count == positions->count);

                auto [jointSrc, jointStride] = cgltf_buffer_iterator(joint_indices, 0);
                vector<uint16_t, 4>* jointDst = buffers->jointData.data() + totalVertices;

                if (joint_indices->component_type == cgltf_component_type_r_8u)
                {
                    for (size_t v_idx = 0; v_idx < joint_indices->count; v_idx++)
                    {
                        *jointDst = dm::vector<uint16_t, 4>(jointSrc[0], jointSrc[1], jointSrc[2], jointSrc[3]);

                        jointSrc += jointStride;
                        ++jointDst;
                    }
                }
                else
                {
                    assert(joint_indices->component_type == cgltf_component_type_r_16u);
                    for (size_t v_idx = 0; v_idx < joint_indices->count; v_idx++)
                    {
                        const uint16_t* jointSrcUshort = (const uint16_t*)jointSrc;
                        *jointDst = dm::vector<uint16_t, 4>(jointSrcUshort[0], jointSrcUshort[1], jointSrcUshort[2], jointSrcUshort[3]);

                        jointSrc += jointStride;
                        ++jointDst;
                    }
                }
            }

            if (joint_weights)
            {
                assert(joint_weights->count == positions->count);

                auto [weightSrc, weightStride] = cgltf_buffer_iterator(joint_weights, 0);
                float4* weightDst = buffers->weightData.data() + totalVertices;

                if (joint_weights->component_type == cgltf_component_type_r_8u)
                {
                    for (size_t v_idx = 0; v_idx < joint_indices->count; v_idx++)
                    {
                        *weightDst = dm::float4(
                            float(weightSrc[0]) / 255.f,
                            float(weightSrc[1]) / 255.f,
                            float(weightSrc[2]) / 255.f,
                            float(weightSrc[3]) / 255.f);

                        weightSrc += weightStride;
                        ++weightDst;
                    }
                }
                else if (joint_weights->component_type == cgltf_component_type_r_16u)
                {
                    for (size_t v_idx = 0; v_idx < joint_indices->count; v_idx++)
                    {
                        const uint16_t* weightSrcUshort = (const uint16_t*)weightSrc;
                        *weightDst = dm::float4(
                            float(weightSrcUshort[0]) / 65535.f,
                            float(weightSrcUshort[1]) / 65535.f,
                            float(weightSrcUshort[2]) / 65535.f,
                            float(weightSrcUshort[3]) / 65535.f);
                        
                        weightSrc += weightStride;
                        ++weightDst;
                    }
                }
                else
                {
                    assert(joint_weights->component_type == cgltf_component_type_r_32f);
                    for (size_t v_idx = 0; v_idx < joint_indices->count; v_idx++)
                    {
                        *weightDst = (const float*)weightSrc;

                        weightSrc += weightStride;
                        ++weightDst;
                    }
                }
            }

            auto geometry = m_SceneTypeFactory->CreateMeshGeometry();
            geometry->material = materials[prim.material];
            geometry->indexOffsetInMesh = minfo->totalIndices;
            geometry->vertexOffsetInMesh = minfo->totalVertices;
            geometry->numIndices = (uint32_t)indexCount;
            geometry->numVertices = (uint32_t)positions->count;
            geometry->objectSpaceBounds = bounds;
            minfo->objectSpaceBounds |= bounds;
            minfo->totalIndices += geometry->numIndices;
            minfo->totalVertices += geometry->numVertices;
            minfo->geometries.push_back(geometry);

            totalIndices += geometry->numIndices;
            totalVertices += geometry->numVertices;
        }
    }

    std::unordered_map<const cgltf_camera*, std::shared_ptr<SceneCamera>> cameraMap;
    for (size_t camera_idx = 0; camera_idx < objects->cameras_count; camera_idx++)
    {
        const cgltf_camera* src = &objects->cameras[camera_idx];
        std::shared_ptr<SceneCamera> dst;

        if (src->type == cgltf_camera_type_perspective)
        {
            std::shared_ptr<PerspectiveCamera> perspectiveCamera = std::make_shared<PerspectiveCamera>();

            perspectiveCamera->zNear = src->data.perspective.znear;
            if (src->data.perspective.has_zfar)
                perspectiveCamera->zFar = src->data.perspective.zfar;
            perspectiveCamera->verticalFov = src->data.perspective.yfov;
            if (src->data.perspective.has_aspect_ratio)
                perspectiveCamera->aspectRatio = src->data.perspective.aspect_ratio;

            dst = perspectiveCamera;
        }
        else
        {
            std::shared_ptr<OrthographicCamera> orthographicCamera = std::make_shared<OrthographicCamera>();
            
            orthographicCamera->zNear = src->data.orthographic.znear;
            orthographicCamera->zFar = src->data.orthographic.zfar;
            orthographicCamera->xMag = src->data.orthographic.xmag;
            orthographicCamera->yMag = src->data.orthographic.ymag;

            dst = orthographicCamera;
        }

        cameraMap[src] = dst;
    }

    std::unordered_map<const cgltf_light*, std::shared_ptr<Light>> lightMap;
    for (size_t light_idx = 0; light_idx < objects->lights_count; light_idx++)
    {
        const cgltf_light* src = &objects->lights[light_idx];
        std::shared_ptr<Light> dst;

        switch(src->type)  // NOLINT(clang-diagnostic-switch-enum)
        {
        case cgltf_light_type_directional: {
            auto directional = std::make_shared<DirectionalLight>();
            directional->irradiance = src->intensity;
            directional->color = src->color;
            dst = directional;
            break;
        }
        case cgltf_light_type_point: {
            auto point = std::make_shared<PointLight>();
            point->intensity = src->intensity;
            point->color = src->color;
            point->range = src->range;
            dst = point;
            break;
        }
        case cgltf_light_type_spot: {
            auto spot = std::make_shared<SpotLight>();
            spot->intensity = src->intensity;
            spot->color = src->color;
            spot->range = src->range;
            spot->innerAngle = dm::degrees(src->spot_inner_cone_angle);
            spot->outerAngle = dm::degrees(src->spot_outer_cone_angle);
            dst = spot;
            break;
        }
        default:
            break;
        }

        if (dst)
        {
            lightMap[src] = dst;
        }
    }

    // build the scene graph
    std::shared_ptr<SceneGraph> graph = std::make_shared<SceneGraph>();
    std::shared_ptr<SceneGraphNode> root = std::make_shared<SceneGraphNode>();
    std::unordered_map<cgltf_node*, std::shared_ptr<SceneGraphNode>> nodeMap;
    std::vector<cgltf_node*> skinnedNodes;
    
    struct StackItem
    {
        std::shared_ptr<SceneGraphNode> dstParent;
        cgltf_node** srcNodes = nullptr;
        size_t srcCount = 0;
    };
    std::vector<StackItem> stack;

    root->SetName(fileName.filename().generic_string());

    int unnamedCameraCounter = 1;

    StackItem context;
    context.dstParent = root;
    context.srcNodes = objects->scene->nodes;
    context.srcCount = objects->scene->nodes_count;
    
    while (context.srcCount > 0)
    {
        cgltf_node* src = *context.srcNodes;
        ++context.srcNodes;
        --context.srcCount;

        auto dst = std::make_shared<SceneGraphNode>();

        nodeMap[src] = dst;

        if (src->has_matrix)
        {
            // decompose the matrix into TRS
            affine3 aff = affine3(&src->matrix[0], 
                &src->matrix[4], &src->matrix[8], &src->matrix[12]);

            double3 translation;
            double3 scaling;
            dquat rotation;

            decomposeAffine(dm::daffine3(aff), &translation, &rotation, &scaling);

            dst->SetTransform(&translation, &rotation, &scaling);
        }
        else
        {
            if (src->has_scale)
                dst->SetScaling(dm::double3(dm::float3(src->scale)));
            if (src->has_rotation)
                dst->SetRotation(dm::dquat(dm::quat::fromXYZW(src->rotation)));
            if (src->has_translation)
                dst->SetTranslation(dm::double3(dm::float3(src->translation)));
        }

        if (src->name)
            dst->SetName(src->name);

        graph->Attach(context.dstParent, dst);

        if (src->skin)
        {
            // process the skinned nodes later, when the graph is constructed
            skinnedNodes.push_back(src);
        }
        else if (src->mesh)
        {
            auto found = meshMap.find(src->mesh);

            if (found != meshMap.end())
            {
                auto leaf = m_SceneTypeFactory->CreateMeshInstance(found->second);
                dst->SetLeaf(leaf);
            }
        }

        if (src->camera)
        {
            auto found = cameraMap.find(src->camera);

            if (found != cameraMap.end())
            {
                auto camera = found->second;

                if (dst->GetLeaf())
                {
                    auto node = std::make_shared<SceneGraphNode>();
                    node->SetLeaf(camera);
                    graph->Attach(dst, node);
                }
                else
                {
                    dst->SetLeaf(camera);
                }

                if (src->camera->name)
                {
                    camera->SetName(src->camera->name);
                }
                else if (camera->GetName().empty())
                {
                    camera->SetName("Camera" + std::to_string(unnamedCameraCounter));
                    ++unnamedCameraCounter;
                }
            }
        }

        if (src->light)
        {
            auto found = lightMap.find(src->light);

            if (found != lightMap.end())
            {
                auto light = found->second;

                if (dst->GetLeaf())
                {
                    auto node = std::make_shared<SceneGraphNode>();
                    node->SetLeaf(light);
                    graph->Attach(dst, node);
                }
                else
                {
                    dst->SetLeaf(light);
                }
            }
        }

        if (src->children_count)
        {
            stack.push_back(context);
            context.dstParent = dst;
            context.srcNodes = src->children;
            context.srcCount = src->children_count;
        }
        else
        {
            // go up the stack until we find a node where some nodes are left
            while (context.srcCount == 0 && !stack.empty())
            {
                context.dstParent->ReverseChildren();
                context = stack.back();
                stack.pop_back();
            }
        }
    }

    for (auto* src : skinnedNodes)
    {
        assert(src->skin);
        assert(src->mesh);

        std::shared_ptr<MeshInfo> prorotypeMesh;
        auto found = meshMap.find(src->mesh);
        if (found != meshMap.end())
        {
            prorotypeMesh = found->second;

            auto skinnedInstance = std::make_shared<SkinnedMeshInstance>(m_SceneTypeFactory, prorotypeMesh);
            skinnedInstance->joints.resize(src->skin->joints_count);

            for (size_t joint_idx = 0; joint_idx < src->skin->joints_count; joint_idx++)
            {
                SkinnedMeshJoint& joint = skinnedInstance->joints[joint_idx];
                cgltf_accessor_read_float(src->skin->inverse_bind_matrices, joint_idx, joint.inverseBindMatrix.m_data, 16);
                joint.node = nodeMap[src->skin->joints[joint_idx]];

                if (!joint.node->GetLeaf())
                {
                    joint.node->SetLeaf(std::make_shared<SkinnedMeshReference>(skinnedInstance));
                }
            }

            auto dst = nodeMap[src];
            dst->SetLeaf(skinnedInstance);
        }
    }

    result.rootNode = root;

    auto animationContainer = root;
    if (objects->animations_count > 1)
    {
        animationContainer = std::make_shared<SceneGraphNode>();
        animationContainer->SetName("Animations");
        graph->Attach(root, animationContainer);
    }

    std::unordered_map<const cgltf_animation_sampler*, std::shared_ptr<animation::Sampler>> animationSamplers;
    
    for (size_t a_idx = 0; a_idx < objects->animations_count; a_idx++)
    {
        const cgltf_animation* srcAnim = &objects->animations[a_idx];
        auto dstAnim = std::make_shared<SceneGraphAnimation>();

        animationSamplers.clear();

        for (size_t s_idx = 0; s_idx < srcAnim->samplers_count; s_idx++)
        {
            const cgltf_animation_sampler* srcSampler = &srcAnim->samplers[s_idx];
            const cgltf_animation_channel* srcChannel = &srcAnim->channels[s_idx];
            auto dstSampler = std::make_shared<animation::Sampler>();

            switch (srcSampler->interpolation)
            {
            case cgltf_interpolation_type_linear:
                if (srcChannel->target_path == cgltf_animation_path_type_rotation)
                    dstSampler->SetInterpolationMode(animation::InterpolationMode::Slerp);
                else
                    dstSampler->SetInterpolationMode(animation::InterpolationMode::Linear);
                break;
            case cgltf_interpolation_type_step:
                dstSampler->SetInterpolationMode(animation::InterpolationMode::Step);
                break;
            case cgltf_interpolation_type_cubic_spline:
                dstSampler->SetInterpolationMode(animation::InterpolationMode::HermiteSpline);
                break;
            }

            const cgltf_accessor* times = srcSampler->input;
            const cgltf_accessor* values = srcSampler->output;
            assert(times->type == cgltf_type_scalar);

            for (size_t sample_idx = 0; sample_idx < times->count; sample_idx++)
            {
                animation::Keyframe keyframe;

                bool timeRead = cgltf_accessor_read_float(times, sample_idx, &keyframe.time, 1);

                bool valueRead;
                if (srcSampler->interpolation == cgltf_interpolation_type_cubic_spline)
                {
                    valueRead = cgltf_accessor_read_float(values, sample_idx * 3 + 0, &keyframe.inTangent.x, 4);
                    valueRead = cgltf_accessor_read_float(values, sample_idx * 3 + 1, &keyframe.value.x, 4);
                    valueRead = cgltf_accessor_read_float(values, sample_idx * 3 + 2, &keyframe.outTangent.x, 4);
                }
                else
                {
                    valueRead = cgltf_accessor_read_float(values, sample_idx, &keyframe.value.x, 4);
                }

                if (timeRead && valueRead)
                    dstSampler->AddKeyframe(keyframe);
            }

            if (!dstSampler->GetKeyframes().empty())
                animationSamplers[srcSampler] = dstSampler;
            else
                log::warning("Animation channel imported with no keyframes, ignoring.");
        }

        for (size_t channel_idx = 0; channel_idx < srcAnim->channels_count; channel_idx++)
        {
            const cgltf_animation_channel* srcChannel = &srcAnim->channels[channel_idx];

            auto dstNode = nodeMap[srcChannel->target_node];
            if (!dstNode)
                continue;

            AnimationAttribute attribute;
            switch (srcChannel->target_path)
            {
                case cgltf_animation_path_type_translation:
                    attribute = AnimationAttribute::Translation;
                    break;

                case cgltf_animation_path_type_rotation:
                    attribute = AnimationAttribute::Rotation;
                    break;

                case cgltf_animation_path_type_scale:
                    attribute = AnimationAttribute::Scaling;
                    break;

                case cgltf_animation_path_type_weights:
                case cgltf_animation_path_type_invalid:
                default:
                    log::warning("Unsupported glTF animation taregt: %d", srcChannel->target_path);
                    continue;
            }

            auto dstSampler = animationSamplers[srcChannel->sampler];
            if (!dstSampler)
                continue;

            auto dstTrack = std::make_shared<SceneGraphAnimationChannel>(dstSampler, dstNode, attribute);
            
            dstAnim->AddChannel(dstTrack);
        }

        if (dstAnim->IsVald())
        {
            auto animationNode = std::make_shared<SceneGraphNode>();
            animationNode->SetName(dstAnim->GetName());
            graph->Attach(animationContainer, animationNode);
            animationNode->SetLeaf(dstAnim);
            if (srcAnim->name)
                animationNode->SetName(srcAnim->name);
        }
    }

    animationContainer->ReverseChildren();

    if (c_ForceRebuildTangents)
    {
        for (size_t buffer_idx = 0; buffer_idx < objects->buffers_count; buffer_idx++)
        {
            std::filesystem::path outputFileName = fileName.parent_path() / fileName.stem();
            outputFileName += ".buffer" + std::to_string(buffer_idx) + ".bin";

            m_fs->writeFile(outputFileName, objects->buffers[buffer_idx].data, objects->buffers[buffer_idx].size);
        }
    }

    cgltf_free(objects);

    return true;
}
