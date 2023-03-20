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

#include <donut/core/vfs/Compression.h>
#include <donut/core/log.h>
#include <donut/core/string_utils.h>
#include <unordered_set>

#ifdef DONUT_WITH_LZ4
#include <lz4frame.h>
#endif

using namespace donut::vfs;

bool CompressionLayer::folderExists(const std::filesystem::path& name)
{
    return m_fs->folderExists(name);
}

bool CompressionLayer::fileExists(const std::filesystem::path& name)
{
    return m_fs->fileExists(name);
}

std::shared_ptr<IBlob> CompressionLayer::readFile(const std::filesystem::path& name)
{
#ifdef DONUT_WITH_LZ4
    std::filesystem::path nameWithExt = name;
    nameWithExt += ".lz4";
    auto compressedBlob = m_fs->readFile(nameWithExt);

    if (!compressedBlob)
        return m_fs->readFile(name);
    
    if (compressedBlob->size() == 0)
        return compressedBlob;

    // initialize the decompression context
    LZ4F_dctx* context = nullptr;
    LZ4F_errorCode_t err = LZ4F_createDecompressionContext(&context, LZ4F_VERSION);

    if (LZ4F_isError(err))
    {
        log::warning("Failed to create an LZ4 decompression context: %s",
            LZ4F_getErrorName(err));
        return nullptr;
    }

    const uint8_t* const compressedData = (const uint8_t*)compressedBlob->data();
    const size_t compressedSize = compressedBlob->size();

    size_t readPtr = 0;
    LZ4F_frameInfo_t frameInfo;

    // parse the frame header
    {
        size_t srcSize = compressedSize;

        err = LZ4F_getFrameInfo(context, &frameInfo, compressedData + readPtr, &srcSize);

        if (LZ4F_isError(err))
        {
            log::warning("Failed to parse LZ4 frame header for file '%s': %s",
                name.generic_string().c_str(), LZ4F_getErrorName(err));

            LZ4F_freeDecompressionContext(context);
            return nullptr;
        }

        readPtr += srcSize;
    }

    // get or guess the decompressed data size
    size_t decompressedSize = frameInfo.contentSize;
    size_t decompressionFactor;
    if (decompressedSize == 0)
    {
        // some initial estimate on the data size
        decompressionFactor = 3;
        decompressedSize = compressedSize * decompressionFactor;
    }
    else
    {
        // in the wonderful event of stored decompressed size being incorrect, don't realloc to a smaller size
        decompressionFactor = (decompressedSize + compressedSize - 1) / compressedSize;
    }

    uint8_t* decompressedData = (uint8_t*)malloc(decompressedSize);
    size_t writePtr = 0;

    // decompress the frame, potentially in multiple iterations, growing the output buffer if necessary
    while (err != 0)
    {
        size_t dstSize = decompressedSize - writePtr;
        size_t srcSize = compressedSize - readPtr;
        err = LZ4F_decompress(context, decompressedData + writePtr, &dstSize,
            compressedData + readPtr, &srcSize, nullptr);

        // decompression failed, maybe because of corrupted data
        if (LZ4F_isError(err))
        {
            log::warning("Failed to decompress LZ4 frame for file '%s': %s",
                name.generic_string().c_str(), LZ4F_getErrorName(err));

            free(decompressedData);
            LZ4F_freeDecompressionContext(context);
            return nullptr;
        }

        writePtr += dstSize;
        readPtr += srcSize;

        // see if the decopmressor has filled the entire output buffer but there is still more data to process
        if (writePtr == decompressedSize && err != 0)
        {
            decompressionFactor += 1;
            decompressedSize = compressedSize * decompressionFactor;
            uint8_t* newData = (uint8_t*)realloc(decompressedData, decompressedSize);

            // realloc failed
            if (newData == nullptr)
            {
                log::warning("Failed to decompress LZ4 frame for file '%s': couldn't allocate %llu bytes of memory",
                    name.generic_string().c_str(), decompressedSize);

                free(decompressedData);
                LZ4F_freeDecompressionContext(context);
                return nullptr;
            }

            decompressedData = newData;
        }
    }

    // shrink the output buffer if it's too large
    if (writePtr < decompressedSize)
    {
        uint8_t* newData = (uint8_t*)realloc(decompressedData, writePtr);

        if (newData != nullptr)
            decompressedData = newData;
    }

    // package and return the output
    auto blob = std::make_shared<Blob>(decompressedData, writePtr);

    return std::static_pointer_cast<IBlob>(blob);

#else // DONUT_WITH_LZ4
    return m_fs->readFile(name);
#endif
}

bool CompressionLayer::writeFile(const std::filesystem::path& name, const void* data, size_t size)
{
#ifdef DONUT_WITH_LZ4
    if (!string_utils::ends_with(name.generic_string(), ".lz4"))
        return m_fs->writeFile(name, data, size);

    if (data == nullptr || size == 0)
        return m_fs->writeFile(name, data, size);

    // initialize the decompression context
    LZ4F_cctx* context = nullptr;
    LZ4F_errorCode_t err = LZ4F_createCompressionContext(&context, LZ4F_VERSION);

    if (LZ4F_isError(err))
    {
        log::warning("Failed to create an LZ4 compression context: %s",
            LZ4F_getErrorName(err));

        return false;
    }

    const uint8_t* uncompressedData = (const uint8_t*)data;
    const size_t uncompressedSize = size;

    // fill the preferences structure
    LZ4F_preferences_t preferences{};
    preferences.frameInfo.contentSize = uncompressedSize;
    preferences.frameInfo.blockChecksumFlag = LZ4F_blockChecksumEnabled;
    preferences.compressionLevel = m_CompressionLevel;

    // get the maximum size 
    size_t compressedSizeBound = LZ4F_compressFrameBound(uncompressedSize, &preferences);
    uint8_t* compressedData = (uint8_t*)malloc(compressedSizeBound);

    if (!compressedData)
    {
        log::warning("Failed to compress file '%s': couldn't allocate %llu bytes of memory",
            name.generic_string().c_str(), compressedSizeBound);

        LZ4F_freeCompressionContext(context);
        return false;
    }

    // compress the data
    size_t compressedSize = LZ4F_compressFrame(compressedData, compressedSizeBound,
        uncompressedData, uncompressedSize, &preferences);

    // release the context now - it's no longer needed
    LZ4F_freeCompressionContext(context);

    // ...even if compression has failed
    if (LZ4F_isError(compressedSize))
    {
        log::warning("Failed to compress file '%s': %s",
            name.generic_string().c_str(), LZ4F_getErrorName(compressedSize));

        free(compressedData);
        return false;
    }

    // write out the compressed file
    bool writeSuccessful = m_fs->writeFile(name, compressedData, compressedSize);

    free(compressedData);
    compressedData = nullptr;

    return writeSuccessful;
    
#else // DONUT_WITH_LZ4
    return m_fs->writeFile(name, data, size);
#endif
}

int CompressionLayer::enumerateFiles(const std::filesystem::path& path,
    const std::vector<std::string>& extensions, enumerate_callback_t callback, bool allowDuplicates)
{
    std::vector<std::string> patchedExtensions = extensions;
    for (const auto& ext : extensions)
        patchedExtensions.push_back(ext + ".lz4");

    // use a set to de-duplicate the names in case some file exists
    // in both compressed and uncompressed versions
    std::unordered_set<std::string> resultSet;

    int numRawResults = m_fs->enumerateFiles(path, patchedExtensions,
        [&resultSet, allowDuplicates, callback](std::string_view name)
        {
            if (string_utils::ends_with(name, ".lz4"))
                name.remove_suffix(4);
            
            if (allowDuplicates)
            {
                // pass the name, maybe without the suffix, to the caller
                callback(name);
            }
            else
            {
                // store a copy
               resultSet.insert(std::string(name));
            }
        }, true);

    if (numRawResults < 0)
        return numRawResults;

    if (!allowDuplicates)
    {
        // pass the deduplicated names to the caller
        std::for_each(resultSet.begin(), resultSet.end(), callback);
        return int(resultSet.size());
    }

    return numRawResults;
}

int CompressionLayer::enumerateDirectories(const std::filesystem::path& path, enumerate_callback_t callback, bool allowDuplicates)
{
    return m_fs->enumerateDirectories(path, callback, allowDuplicates);
}
