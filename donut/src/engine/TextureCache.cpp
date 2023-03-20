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

/*
License for stb

Public Domain

This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this 
software, either in source code form or as a compiled binary, for any purpose, 
commercial or non-commercial, and by any means.

In jurisdictions that recognize copyright laws, the author or authors of this 
software dedicate any and all copyright interest in the software to the public 
domain. We make this dedication for the benefit of the public at large and to 
the detriment of our heirs and successors. We intend this dedication to be an 
overt act of relinquishment in perpetuity of all present and future rights to 
this software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN 
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION 
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <donut/engine/TextureCache.h>

#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ConsoleObjects.h>
#include <donut/engine/DDSFile.h>
#include <donut/core/vfs/VFS.h>
#include <donut/core/log.h>

#ifdef DONUT_WITH_TASKFLOW
#include <taskflow/taskflow.hpp>
#endif

#include <stb_image.h>
#include <stb_image_write.h>


#ifdef DONUT_WITH_MINIZ
    #ifdef DONUT_WITH_TINYEXR
        // tinyexr has its own copy of miniz, need to disable it
        // if we have a separate integration to avoid linker errors
        #define TINYEXR_USE_MINIZ 0
    #endif // DONUT_WITH_TINYEXR

    #include <miniz.h>

#endif

#ifdef DONUT_WITH_TINYEXR

    #if defined (_MSC_VER)
        #pragma warning(push)
        #pragma warning(disable:4018) // Silence warning from tinyEXR
    #endif

    #define TINYEXR_IMPLEMENTATION
    #include <tinyexr.h>

    #if defined (_MSC_VER)
        #pragma warning(pop)
    #endif

#endif // DONUT_WITH_TINYEXR

#include <algorithm>
#include <chrono>
#include <regex>

using namespace donut::math;
using namespace donut::vfs;
using namespace donut::engine;

class StbImageBlob : public IBlob
{
private:
    unsigned char* m_data = nullptr;

public:
    StbImageBlob(unsigned char* _data) : m_data(_data) 
    {
    }

    virtual ~StbImageBlob()
    {
        if (m_data)
        {
            stbi_image_free(m_data);
            m_data = nullptr;
        }
    }

    virtual const void* data() const override
    {
        return m_data;
    }

    virtual size_t size() const override
    {
        return 0; // nobody cares
    }
};


TextureCache::TextureCache(nvrhi::IDevice* device, std::shared_ptr<IFileSystem> fs, std::shared_ptr<DescriptorTableManager> descriptorTable)
    : m_Device(device)
    , m_DescriptorTable(std::move(descriptorTable))
    , m_fs(std::move(fs))
{
}

TextureCache::~TextureCache()
{
    Reset();
}

void TextureCache::Reset()
{
	std::lock_guard<std::shared_mutex> guard(m_LoadedTexturesMutex);

	m_LoadedTextures.clear();

    m_TexturesRequested = 0;
    m_TexturesLoaded = 0;
}

void TextureCache::SetGenerateMipmaps(bool generateMipmaps)
{
    m_GenerateMipmaps = generateMipmaps;
}

bool TextureCache::FindTextureInCache(const std::filesystem::path& path, std::shared_ptr<TextureData>& texture)
{
    std::lock_guard<std::shared_mutex> guard(m_LoadedTexturesMutex);

    // First see if this texture is already loaded (or being loaded).

    texture = m_LoadedTextures[path.generic_string()];
    if (texture)
    {
        return true;
    }

    // Allocate a new texture slot for this file name and return it. Load the file later in a thread pool.
    // LoadTextureFromFileAsync function for a given scene is only called from one thread, so there is no 
    // chance of loading the same texture twice.

    texture = CreateTextureData();
    m_LoadedTextures[path.generic_string()] = texture;

    ++m_TexturesRequested;

    return false;
}

std::shared_ptr<IBlob> TextureCache::ReadTextureFile(const std::filesystem::path& path) const
{
    auto fileData = m_fs->readFile(path);

    if (!fileData)
        log::message(m_ErrorLogSeverity, "Couldn't read texture file '%s'", path.generic_string().c_str());

    return fileData;
}

std::shared_ptr<TextureData> TextureCache::CreateTextureData()
{
    return std::make_shared<TextureData>();
}

bool TextureCache::FillTextureData(const std::shared_ptr<vfs::IBlob>& fileData, const std::shared_ptr<TextureData>& texture, const std::string& extension, const std::string& mimeType) const
{
    if (extension == ".dds" || extension == ".DDS" || mimeType == "image/vnd-ms.dds")
    {
        texture->data = fileData;
        if (!LoadDDSTextureFromMemory(*texture))
        {
            texture->data = nullptr;
            log::message(m_ErrorLogSeverity, "Couldn't load DDS texture '%s'", texture->path.c_str());
            return false;
        }
    }
#ifdef DONUT_WITH_TINYEXR
    else if (extension == ".exr" || extension == ".EXR" || mimeType == "image/aces")
    {
        float* data = nullptr;
        int width = 0, height = 0;
        char const* err = nullptr;

        // This reads only 1 or 4 channel images and duplicates channels
        // Should rewrite w/ lower level EXR functions
        if (LoadEXRFromMemory(&data, &width, &height, (uint8_t*)fileData->data(), fileData->size(), &err) == TINYEXR_SUCCESS)
        {
            uint32_t channels = 4;
            uint32_t bytesPerPixel = channels * 4;

            texture->data = std::make_shared<Blob>(data, bytesPerPixel * width * height);
            texture->width = static_cast<uint32_t>(width);
            texture->height = static_cast<uint32_t>(height);
            texture->format = nvrhi::Format::RGBA32_FLOAT;

            texture->originalBitsPerPixel = channels * 32;
            texture->isRenderTarget = true;
            texture->mipLevels = 1;
            texture->dimension = nvrhi::TextureDimension::Texture2D;

            texture->dataLayout.resize(1);
            texture->dataLayout[0].resize(1);
            texture->dataLayout[0][0].dataOffset = 0;
            texture->dataLayout[0][0].rowPitch = static_cast<size_t>(width * bytesPerPixel);
            texture->dataLayout[0][0].dataSize = static_cast<size_t>(width * height * bytesPerPixel);

            return true;
        }
        else
        {
            log::warning("Couldn't load EXR texture '%s'", texture->path.c_str());
            return false;
        }
    }
#endif // DONUT_WITH_TINYEXR
    else
    {
        int width = 0, height = 0, originalChannels = 0, channels = 0;

        if (!stbi_info_from_memory(
            static_cast<const stbi_uc*>(fileData->data()), 
            static_cast<int>(fileData->size()), 
            &width, &height, &originalChannels))
        {
            log::message(m_ErrorLogSeverity, "Couldn't process image header for texture '%s'", texture->path.c_str());
            return false;
        }

        bool is_hdr = stbi_is_hdr_from_memory(
            static_cast<const stbi_uc*>(fileData->data()),
            static_cast<int>(fileData->size()));

        if (originalChannels == 3)
        {
            channels = 4;
        }
        else {
            channels = originalChannels;
        }

        unsigned char* bitmap;
        int bytesPerPixel = channels * (is_hdr ? 4 : 1);
        
        if (is_hdr)
        {
            float* floatmap = stbi_loadf_from_memory(
                static_cast<const stbi_uc*>(fileData->data()),
                static_cast<int>(fileData->size()),
                &width, &height, &originalChannels, channels);

            bitmap = reinterpret_cast<unsigned char*>(floatmap);
        }
        else
        {
            bitmap = stbi_load_from_memory(
                static_cast<const stbi_uc*>(fileData->data()),
                static_cast<int>(fileData->size()),
                &width, &height, &originalChannels, channels);
        }

        if (!bitmap)
        {
            log::message(m_ErrorLogSeverity, "Couldn't load generic texture '%s'", texture->path.c_str());
            return false;
        }

        texture->originalBitsPerPixel = static_cast<uint32_t>(originalChannels) * (is_hdr ? 32 : 8);
        texture->width = static_cast<uint32_t>(width);
        texture->height = static_cast<uint32_t>(height);
        texture->isRenderTarget = true;
        texture->mipLevels = 1;
        texture->dimension = nvrhi::TextureDimension::Texture2D;

        texture->dataLayout.resize(1);
        texture->dataLayout[0].resize(1);
        texture->dataLayout[0][0].dataOffset = 0;
        texture->dataLayout[0][0].rowPitch = static_cast<size_t>(width * bytesPerPixel);
        texture->dataLayout[0][0].dataSize = static_cast<size_t>(width * height * bytesPerPixel);

        texture->data = std::make_shared<StbImageBlob>(bitmap);
        bitmap = nullptr; // ownership transferred to the blob

        switch (channels)
        {
        case 1:
            texture->format = is_hdr ? nvrhi::Format::R32_FLOAT : nvrhi::Format::R8_UNORM;
            break;
        case 2:
            texture->format = is_hdr ? nvrhi::Format::RG32_FLOAT : nvrhi::Format::RG8_UNORM;
            break;
        case 4:
            texture->format = is_hdr ? nvrhi::Format::RGBA32_FLOAT : (texture->forceSRGB ? nvrhi::Format::SRGBA8_UNORM : nvrhi::Format::RGBA8_UNORM);
            break;
        default:
            texture->data.reset(); // release the bitmap data

            log::message(m_ErrorLogSeverity, "Unsupported number of components (%d) for texture '%s'", channels, texture->path.c_str());
            return false;
        }
    }

    return true;
}

uint GetMipLevelsNum(uint width, uint height)
{
    uint size = std::min(width, height);
    uint levelsNum = (uint)(logf((float)size) / logf(2.0f)) + 1;

    return levelsNum;
}

void TextureCache::FinalizeTexture(std::shared_ptr<TextureData> texture, CommonRenderPasses* passes, nvrhi::ICommandList* commandList)
{
    assert(texture->data);
    assert(commandList);

    uint originalWidth = texture->width;
    uint originalHeight = texture->height;

    bool isBlockCompressed =
        (texture->format == nvrhi::Format::BC1_UNORM) ||
        (texture->format == nvrhi::Format::BC1_UNORM_SRGB) ||
        (texture->format == nvrhi::Format::BC2_UNORM) ||
        (texture->format == nvrhi::Format::BC2_UNORM_SRGB) ||
        (texture->format == nvrhi::Format::BC3_UNORM) ||
        (texture->format == nvrhi::Format::BC3_UNORM_SRGB) ||
        (texture->format == nvrhi::Format::BC4_SNORM) ||
        (texture->format == nvrhi::Format::BC4_UNORM) ||
        (texture->format == nvrhi::Format::BC5_SNORM) ||
        (texture->format == nvrhi::Format::BC5_UNORM) ||
        (texture->format == nvrhi::Format::BC6H_SFLOAT) ||
        (texture->format == nvrhi::Format::BC6H_UFLOAT) ||
        (texture->format == nvrhi::Format::BC7_UNORM) ||
        (texture->format == nvrhi::Format::BC7_UNORM_SRGB);

    if (isBlockCompressed)
    {
        originalWidth = (originalWidth + 3) & ~3;
        originalHeight = (originalHeight + 3) & ~3;
    }

    uint scaledWidth = originalWidth;
    uint scaledHeight = originalHeight;

    if (m_MaxTextureSize > 0 && int(std::max(originalWidth, originalHeight)) > m_MaxTextureSize && texture->isRenderTarget && texture->dimension == nvrhi::TextureDimension::Texture2D)
    {
        if (originalWidth >= originalHeight)
        {
            scaledHeight = originalHeight * m_MaxTextureSize / originalWidth;
            scaledWidth = m_MaxTextureSize;
        }
        else
        {
            scaledWidth = originalWidth * m_MaxTextureSize / originalHeight;
            scaledHeight = m_MaxTextureSize;
        }
    }

    const char* dataPointer = static_cast<const char*>(texture->data->data());

    nvrhi::TextureDesc textureDesc;
    textureDesc.format = texture->format;
    textureDesc.width = scaledWidth;
    textureDesc.height = scaledHeight;
    textureDesc.depth = texture->depth;
    textureDesc.arraySize = texture->arraySize;
    textureDesc.dimension = texture->dimension;
    textureDesc.mipLevels = m_GenerateMipmaps && texture->isRenderTarget && passes
        ? GetMipLevelsNum(textureDesc.width, textureDesc.height)
        : texture->mipLevels;
    textureDesc.debugName = texture->path;
    textureDesc.isRenderTarget = texture->isRenderTarget;
    texture->texture = m_Device->createTexture(textureDesc);

    commandList->beginTrackingTextureState(texture->texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);

    if (m_DescriptorTable)
        texture->bindlessDescriptor = m_DescriptorTable->CreateDescriptorHandle(nvrhi::BindingSetItem::Texture_SRV(0, texture->texture));
    
    if (scaledWidth != originalWidth || scaledHeight != originalHeight)
    {
        nvrhi::TextureDesc tempTextureDesc;
        tempTextureDesc.format = texture->format;
        tempTextureDesc.width = originalWidth;
        tempTextureDesc.height = originalHeight;
        tempTextureDesc.depth = textureDesc.depth;
        tempTextureDesc.arraySize = textureDesc.arraySize;
        tempTextureDesc.mipLevels = 1;
        tempTextureDesc.dimension = textureDesc.dimension;

        nvrhi::TextureHandle tempTexture = m_Device->createTexture(tempTextureDesc);
        assert(tempTexture);
        commandList->beginTrackingTextureState(tempTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);

        for (uint32_t arraySlice = 0; arraySlice < texture->arraySize; arraySlice++)
        {
            const TextureSubresourceData& layout = texture->dataLayout[arraySlice][0];

            commandList->writeTexture(tempTexture, arraySlice, 0, dataPointer + layout.dataOffset, layout.rowPitch, layout.depthPitch);
        }

        nvrhi::FramebufferHandle framebuffer = m_Device->createFramebuffer(nvrhi::FramebufferDesc()
            .addColorAttachment(texture->texture));
        
        passes->BlitTexture(commandList, framebuffer, tempTexture);
    }
    else
    {
        for (uint32_t arraySlice = 0; arraySlice < texture->arraySize; arraySlice++)
        {
            for (uint32_t mipLevel = 0; mipLevel < texture->mipLevels; mipLevel++)
            {
                const TextureSubresourceData& layout = texture->dataLayout[arraySlice][mipLevel];

                commandList->writeTexture(texture->texture, arraySlice, mipLevel, dataPointer + layout.dataOffset, layout.rowPitch, layout.depthPitch);
            }
        }
    }

    texture->data.reset();

    uint width = scaledWidth;
    uint height = scaledHeight;
    for (uint mipLevel = texture->mipLevels; mipLevel < textureDesc.mipLevels; mipLevel++)
    {
        width /= 2;
        height /= 2;

        nvrhi::FramebufferHandle framebuffer = m_Device->createFramebuffer(nvrhi::FramebufferDesc()
            .addColorAttachment(nvrhi::FramebufferAttachment()
                .setTexture(texture->texture)
                .setArraySlice(0)
                .setMipLevel(mipLevel)));
        
        BlitParameters blitParams;
        blitParams.sourceTexture = texture->texture;
        blitParams.sourceMip = mipLevel - 1;
        blitParams.targetFramebuffer = framebuffer;
        passes->BlitTexture(commandList, blitParams);
    }

    commandList->setPermanentTextureState(texture->texture, nvrhi::ResourceStates::ShaderResource);
    commandList->commitBarriers();

    ++m_TexturesFinalized;
}

void TextureCache::TextureLoaded(std::shared_ptr<TextureData> texture)
{
    std::lock_guard<std::mutex> guard(m_TexturesToFinalizeMutex);

    if (texture->mimeType.empty())
        log::message(m_InfoLogSeverity, "Loaded %d x %d, %d bpp: %s", texture->width, texture->height, texture->originalBitsPerPixel, texture->path.c_str());
    else
        log::message(m_InfoLogSeverity, "Loaded %d x %d, %d bpp: %s (%s)", texture->width, texture->height, texture->originalBitsPerPixel, texture->path.c_str(), texture->mimeType.c_str());
}

std::shared_ptr<LoadedTexture> TextureCache::LoadTextureFromFile(const std::filesystem::path& path, bool sRGB, CommonRenderPasses* passes, nvrhi::ICommandList* commandList)
{
    std::shared_ptr<TextureData> texture;

    if (FindTextureInCache(path, texture))
        return texture;

    texture->forceSRGB = sRGB;
    texture->path = path.generic_string();

    auto fileData = ReadTextureFile(path);
    if (fileData)
    {
        if (FillTextureData(fileData, texture, path.extension().generic_string(), ""))
        {
            TextureLoaded(texture);

            FinalizeTexture(texture, passes, commandList);
        }
    }

    ++m_TexturesLoaded;

    return texture;
}

std::shared_ptr<LoadedTexture> TextureCache::LoadTextureFromFileDeferred(const std::filesystem::path& path, bool sRGB)
{
    std::shared_ptr<TextureData> texture;

    if (FindTextureInCache(path, texture))
        return texture;

    texture->forceSRGB = sRGB;
    texture->path = path.generic_string();

    auto fileData = ReadTextureFile(path);
    if (fileData)
    {
        if (FillTextureData(fileData, texture, path.extension().generic_string(), ""))
        {
            TextureLoaded(texture);

            std::lock_guard<std::mutex> guard(m_TexturesToFinalizeMutex);

            m_TexturesToFinalize.push(texture);
        }
    }

    ++m_TexturesLoaded;

    return texture;
}

#ifdef DONUT_WITH_TASKFLOW
std::shared_ptr<LoadedTexture> TextureCache::LoadTextureFromFileAsync(const std::filesystem::path& path, bool sRGB, tf::Executor& executor)
{
    std::shared_ptr<TextureData> texture;

    if (FindTextureInCache(path, texture))
        return texture;

    texture->forceSRGB = sRGB;
    texture->path = path.generic_string();

    executor.async([this, sRGB, texture, path]()
    {
        auto fileData = ReadTextureFile(path);
        if (fileData)
        {
            if (FillTextureData(fileData, texture, path.extension().generic_string(), ""))
            {
                TextureLoaded(texture);

                std::lock_guard<std::mutex> guard(m_TexturesToFinalizeMutex);

                m_TexturesToFinalize.push(texture);
            }
        }

        ++m_TexturesLoaded;
    });

    return texture;
}

std::shared_ptr<LoadedTexture> TextureCache::LoadTextureFromMemory(const std::shared_ptr<vfs::IBlob>& data, const std::string& name, const std::string& mimeType, bool sRGB, CommonRenderPasses* passes, nvrhi::ICommandList* commandList)
{
    std::shared_ptr<TextureData> texture = CreateTextureData();
    
    texture->forceSRGB = sRGB;
    texture->path = name;
    texture->mimeType = mimeType;

    if (FillTextureData(data, texture, "", mimeType))
    {
        TextureLoaded(texture);

        FinalizeTexture(texture, passes, commandList);
    }

    ++m_TexturesLoaded;

    return texture;
}

std::shared_ptr<LoadedTexture> TextureCache::LoadTextureFromMemoryDeferred(const std::shared_ptr<vfs::IBlob>& data, const std::string& name, const std::string& mimeType, bool sRGB)
{
    std::shared_ptr<TextureData> texture = CreateTextureData();
    
    texture->forceSRGB = sRGB;
    texture->path = name;
    texture->mimeType = mimeType;

    if (FillTextureData(data, texture, "", mimeType))
    {
        TextureLoaded(texture);

        std::lock_guard<std::mutex> guard(m_TexturesToFinalizeMutex);

        m_TexturesToFinalize.push(texture);
    }
    
    ++m_TexturesLoaded;

    return texture;
}

std::shared_ptr<LoadedTexture> TextureCache::LoadTextureFromMemoryAsync(const std::shared_ptr<vfs::IBlob>& data, const std::string& name, const std::string& mimeType, bool sRGB, tf::Executor& executor)
{
    std::shared_ptr<TextureData> texture = CreateTextureData();
    
    texture->forceSRGB = sRGB;
    texture->path = name;
    texture->mimeType = mimeType;

    executor.async([this, sRGB, texture, data, mimeType]()
        {
            if (FillTextureData(data, texture, "", mimeType))
            {
                TextureLoaded(texture);

                std::lock_guard<std::mutex> guard(m_TexturesToFinalizeMutex);

                m_TexturesToFinalize.push(texture);
            }

            ++m_TexturesLoaded;
        });

    return texture;
}
#endif

std::shared_ptr<TextureData> TextureCache::GetLoadedTexture(std::filesystem::path const& path)
{
	std::lock_guard<std::shared_mutex> guard(m_LoadedTexturesMutex);
	return m_LoadedTextures[path.generic_string()];
}

bool TextureCache::ProcessRenderingThreadCommands(CommonRenderPasses& passes, float timeLimitMilliseconds)
{
    using namespace std::chrono;

    time_point<high_resolution_clock> startTime = high_resolution_clock::now();

    uint commandsExecuted = 0;
    while (true)
    {
        std::shared_ptr<TextureData> pTexture;

        if (timeLimitMilliseconds > 0 && commandsExecuted > 0)
        {
            time_point<high_resolution_clock> now = high_resolution_clock::now();

            if (float(duration_cast<microseconds>(now - startTime).count()) > timeLimitMilliseconds * 1e3f)
                break;
        }

        {
            std::lock_guard<std::mutex> guard(m_TexturesToFinalizeMutex);

            if (m_TexturesToFinalize.empty())
                break;

            pTexture = m_TexturesToFinalize.front();
            m_TexturesToFinalize.pop();
        }

        if (pTexture->data)
        {
            //LOG("Finalizing texture %s", pTexture->fileName.c_str());
            commandsExecuted += 1;

            if (!m_CommandList)
            {
                m_CommandList = m_Device->createCommandList();
            }

            m_CommandList->open();

            FinalizeTexture(pTexture, &passes, m_CommandList);

            m_CommandList->close();
            m_Device->executeCommandList(m_CommandList);
            m_Device->runGarbageCollection();
        }
    }

    return (commandsExecuted > 0);
}

void TextureCache::LoadingFinished()
{
    m_CommandList = nullptr;
}

void TextureCache::SetMaxTextureSize(uint32_t size)
{
	m_MaxTextureSize = size;
}

namespace donut::engine
{
    bool WriteBMPToFile(
        const uint * pPixels,
        const int2& dims,
        const char* path)
    {
        assert(pPixels);
        assert(all(dims > 0));
        return stbi_write_bmp(path, dims.x, dims.y, 4, pPixels) != 0;
    }

    bool SaveTextureToFile(nvrhi::IDevice* device, CommonRenderPasses* pPasses, nvrhi::ITexture* texture, nvrhi::ResourceStates textureState, const char* fileName)
    {
        nvrhi::TextureDesc desc = texture->getDesc();
        nvrhi::TextureHandle tempTexture;
        nvrhi::FramebufferHandle tempFramebuffer;

        nvrhi::CommandListHandle commandList = device->createCommandList();
        commandList->open();

        if (textureState != nvrhi::ResourceStates::Unknown)
        {
            commandList->beginTrackingTextureState(texture, nvrhi::TextureSubresourceSet(0, 1, 0, 1), textureState);
        }

        switch (desc.format)
        {
        case nvrhi::Format::RGBA8_UNORM:
        case nvrhi::Format::SRGBA8_UNORM:
            tempTexture = texture;
            break;
        default:
            desc.format = nvrhi::Format::SRGBA8_UNORM;
            desc.isRenderTarget = true;
            desc.initialState = nvrhi::ResourceStates::RenderTarget;
            desc.keepInitialState = true;

            tempTexture = device->createTexture(desc);
            tempFramebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(tempTexture));
            
            pPasses->BlitTexture(commandList, tempFramebuffer, texture);
        }

        nvrhi::StagingTextureHandle stagingTexture = device->createStagingTexture(desc, nvrhi::CpuAccessMode::Read);
        commandList->copyTexture(stagingTexture, nvrhi::TextureSlice(), tempTexture, nvrhi::TextureSlice());

        if (textureState != nvrhi::ResourceStates::Unknown)
        {
            commandList->setTextureState(texture, nvrhi::TextureSubresourceSet(0, 1, 0, 1), textureState);
            commandList->commitBarriers();
        }

        commandList->close();
        device->executeCommandList(commandList);

        size_t rowPitch = 0;
        void* pData = device->mapStagingTexture(stagingTexture, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch);

        if (!pData)
            return false;

        uint32_t* newData = nullptr;

        if (rowPitch != desc.width * 4)
        {
            newData = new uint32_t[desc.width * desc.height];

            for (uint32_t row = 0; row < desc.height; row++)
            {
                memcpy(newData + row * desc.width, static_cast<char*>(pData) + row * rowPitch, desc.width * sizeof(uint32_t));
            }

            pData = newData;
        }

        bool writeSuccess = WriteBMPToFile(static_cast<uint*>(pData), int2(desc.width, desc.height), fileName);

        if (newData)
        {
            delete[] newData;
            newData = nullptr;
        }

        device->unmapStagingTexture(stagingTexture);

        return writeSuccess;
    }

    bool TextureCache::IsTextureLoaded(const std::shared_ptr<LoadedTexture>& _texture)
    {
        TextureData* texture = static_cast<TextureData*>(_texture.get());

        return texture && texture->data;
    }

    bool TextureCache::IsTextureFinalized(const std::shared_ptr<LoadedTexture>& texture)
    {
        return texture->texture != nullptr;
    }

    bool TextureCache::UnloadTexture(const std::shared_ptr<LoadedTexture>& texture)
    {
        const auto& it = m_LoadedTextures.find(texture->path);

        if (it == m_LoadedTextures.end())
            return false;

        m_LoadedTextures.erase(it);

        return true;
    }

}
