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

#include <donut/core/log.h>

#include <cstdint>
#include <memory>
#include <vector>
#include <string>

namespace donut::vfs
{
    class IBlob;
}

//
// Low-level Chunk file API
//

namespace donut::chunk
{



//
// chunkId : unique chunk identifier in a file
//

class ChunkId {

public:

    ChunkId() : _chunkId(INVALID_CHUNK_ID) {}

    bool valid() const { return _chunkId!=INVALID_CHUNK_ID; }

    bool operator == (ChunkId const & other) const { return _chunkId==other._chunkId; }

private:

    friend class ChunkFile;

    ChunkId(uint32_t id) : _chunkId(id) {}

    static constexpr uint32_t const INVALID_CHUNK_ID = ~uint32_t(0);

    uint32_t _chunkId;
};



//
// Chunk : individual chunk descriptor
//
struct Chunk
{
    ChunkId chunkId;        // chunk unique ID in file/blob

    uint32_t chunkType,     // note : chunkType is not enum typed because ChunkFile
             chunkVersion;  // is agnostic to actual chunk types

    size_t offset,          // offset of chunk in file/blob
           size;            // size of chunk user data (in bytes)

    void const * data;      // chunk user data
};

//
// ChunkFile
//

class ChunkFile
{

public:

    // deserialization interface

    static std::shared_ptr<ChunkFile const> deserialize(
        std::weak_ptr<donut::vfs::IBlob const> blobPtr, char const * filepath);

    std::string const & getFilePath() const { return _filepath; }

public:

    // serialization interface

    std::shared_ptr<donut::vfs::IBlob const> serialize() const;

    template <typename ChunkDesc> ChunkId addChunk(void const * data, size_t size);

    void reset();

public:

    // general chunk access interface

    auto const & getChunks() const { return _chunks; }

    Chunk const * getChunk(ChunkId chunkId) const;

    void getChunks(uint32_t chunkType, std::vector<Chunk const *> & result) const;

    template <typename ChunkDesc> Chunk const * getChunk(ChunkId chunkId) const;

    template <typename ChunkDesc> bool validateChunk(Chunk const * chunk) const;

private:

    struct Header;

    struct ChunkTableEntry;

    ChunkId addChunk(uint32_t type, uint32_t version, void const * data, size_t size);

    std::string _filepath;

    std::vector<std::unique_ptr<Chunk const>> _chunks;

    std::shared_ptr<donut::vfs::IBlob const> _data;
};



//
// Implementation
//

template <typename ChunkDesc> ChunkId ChunkFile::addChunk(void const * data, size_t size)
{
    return addChunk(ChunkDesc::chunktype, ChunkDesc::version, data, size);
}

template <typename ChunkDesc> Chunk const * ChunkFile::getChunk(ChunkId chunkId) const
{
    if (!chunkId.valid())
    {
        log::error("chunkId (%d) not valid");
        return nullptr;
    }
    if (auto const chunk = getChunk(chunkId))
        return validateChunk<ChunkDesc>(chunk) ? chunk : nullptr;
    else
        log::error("chunk (%d) not found");
    return nullptr;
}

template <typename ChunkDesc> bool ChunkFile::validateChunk(Chunk const * chunk) const
{
    if (!chunk)
        return false;

    if (chunk->chunkType!=ChunkDesc::chunktype)
    {
        log::error("chunk (%d) : wrong type %d (expected %d)",
            chunk->chunkId, chunk->chunkType, ChunkDesc::chunktype);
        return false;
    }
    if (chunk->chunkVersion!=ChunkDesc::version) {
        log::error("chunk (%d) : wrong version %d (expected %d)",
            chunk->chunkId, chunk->chunkVersion, ChunkDesc::version);
        return false;
    }
    if (chunk->size==0 || chunk->data==nullptr)
    {
        log::error("no data in chunk (%d)", chunk->chunkId);
        return false;
    }
    return true;
}

}
