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

#include <donut/core/chunk/chunk.h>
#include <donut/core/chunk/chunkFile.h>
#include <donut/core/log.h>

#include "./chunkDescs.h"

#include <cassert>
#include <memory>
#include <vector>

namespace donut::chunk
{

// helper class to deserialize chunks blob
struct ChunkReader
{
    bool loadStringsTableChunk_0x100(Chunk const * chunk);

    bool loadStreamChunk_0x100(ChunkId chunkId, StreamHandle * handle);

    bool loadMeshInfosChunk_0x100(ChunkId chunkId, std::shared_ptr<MeshSetBase> mset);

    bool loadMeshInstancesChunk_0x100(ChunkId chunkId, std::shared_ptr<MeshSetBase> mset);

    bool loadMeshNodesChunk_0x100(ChunkId chunkId, std::shared_ptr<MeshSetBase> mset);

    std::shared_ptr<MeshSetBase> loadMeshSetChunk_0x100(Chunk const * chunk);

    std::shared_ptr<ChunkFile const> cfile;

    inline char const * uncacheString(size_t index)
    {
        if (index!=~size_t(0) && index<stringsmap.size())
            return stringsmap[index];

        return nullptr;
    }

    std::vector<char const *> stringsmap;
};

bool ChunkReader::loadStringsTableChunk_0x100(Chunk const * chunk)
{
    typedef StringsTable_ChunkDesc_0x100 Desc;

    if (!cfile->validateChunk<Desc>(chunk))
        return false;

    uint8_t const * data = (uint8_t const *)chunk->data;

    auto const & desc = *(Desc const *)data;

    size_t nstrings = desc.nstrings,
           descSize = sizeof(Desc),
           tableSize = nstrings * sizeof(Desc::TableEntry);

    stringsmap.resize(nstrings);

    char const * stringsData = (char *)(data + descSize + tableSize);

    Desc::TableEntry const * table = (Desc::TableEntry *)(data + descSize);

    for (size_t i=0; i<nstrings; ++i)
        stringsmap[i] = stringsData + table[i].offset;

    return true;
}

bool ChunkReader::loadMeshInfosChunk_0x100(
    ChunkId chunkId, std::shared_ptr<MeshSetBase> mset)
{
    assert(mset);

    if (!chunkId.valid())
        return false;

    typedef MeshInfos_ChunkDesc_0x100 Desc;

    if (Chunk const * chunk = cfile->getChunk<Desc>(chunkId))
    {
        uint8_t const * chunkData = (uint8_t const *)chunk->data;

        Desc const & desc = *(Desc const *)chunkData;

        switch (desc.getType())
        {
            case Desc::MESH :
                if (mset->type!=MeshSetBase::MESH)
                    return false;
                break;
            case Desc::MESHLET :
                if (mset->type!=MeshSetBase::MESHLET)
                    return false;
                break;
            default:
                log::error("incorrect meshinfo type in asset '%s'", cfile->getFilePath().c_str());
                return false;
        }

        mset->nmeshInfos = desc.nelems;

        uint8_t const * minfosData = (uint8_t *)chunkData+sizeof(Desc);

        auto setStrings = [&] (auto * minfos) {
            for (uint32_t i=0; i<mset->nmeshInfos; ++i) {
                minfos[i].name = uncacheString((size_t)minfos[i].name);
                minfos[i].materialName = uncacheString((size_t)minfos[i].materialName);
            }
        };

        switch (desc.getType())
        {
            case Desc::MESH : {
                MeshInfo * minfos = (MeshInfo *)minfosData;
                setStrings(minfos);
                std::static_pointer_cast<MeshSet>(mset)->meshInfos = minfos;
            } break;

            case Desc::MESHLET : {
                MeshletInfo * minfos = (MeshletInfo *)minfosData;
                setStrings(minfos);
                std::static_pointer_cast<MeshletSet>(mset)->meshInfos = minfos;
            } break;

            default:
                assert("cannot be reached");
        }
        return true;
    }
    else
        log::error("bad MeshInfo chunk in asset '%s'", cfile->getFilePath().c_str());

    return false;
}

bool ChunkReader::loadMeshInstancesChunk_0x100(
    ChunkId chunkId, std::shared_ptr<MeshSetBase> mset)
{
    assert(mset);

    if (!chunkId.valid())
        return false;

    typedef MeshInstances_ChunkDesc_0x100 Desc;

    if (Chunk const * chunk = cfile->getChunk<Desc>(chunkId))
    {
        uint8_t const * chunkData = (uint8_t const *)chunk->data;

        Desc const & desc = *(Desc const *)chunkData;

        uint32_t ninstances = desc.ninstances;

        MeshInstance * instancesData = (MeshInstance *)(chunkData+sizeof(Desc));
        for (uint32_t i=0; i<ninstances; ++i) {
            instancesData[i].name = uncacheString((size_t)instancesData[i].name);
        }

        mset->instances = instancesData;
        mset->ninstances = ninstances;
        return true;
    }
    else
        log::error("bad MeshInstance chunk in asset '%s'", cfile->getFilePath().c_str());

    return false;
}

bool ChunkReader::loadMeshNodesChunk_0x100(
    ChunkId chunkId, std::shared_ptr<MeshSetBase> mset)
{
    assert(mset);

    typedef MeshNodes_ChunkDesc_0x100 Desc;

    if (Chunk const * chunk = cfile->getChunk<Desc>(chunkId))
    {
        uint8_t const * chunkData = (uint8_t const *)chunk->data;

        Desc const & desc = *(Desc const *)chunkData;

        MeshNode * nodesData = (MeshNode *)(chunkData+sizeof(Desc));
        for (uint32_t i=0; i<desc.nnodes; ++i) {
            nodesData[i].name = uncacheString((size_t)(nodesData[i].name));
        }

        mset->nodes = nodesData;
        mset->nnodes = desc.nnodes;
        mset->rootId = desc.rootId;
        return true;
    }
    else
        log::error("bad MeshNode chunk in asset '%s'", cfile->getFilePath().c_str());

    return false;
}

bool ChunkReader::loadStreamChunk_0x100(ChunkId chunkId, StreamHandle * handle) {

    typedef Stream_ChunkDesc_0x100 Desc;

    if (!chunkId.valid())
        return false;

    if (Chunk const * chunk = cfile->getChunk<Desc>(chunkId))
    {
        uint8_t const * chunkData = (uint8_t const *)chunk->data;

        Desc const & desc = *(Desc const *)chunkData;

        handle->elemCount = 0;
        handle->data = nullptr;

        if (desc.getType()!=handle->type)
        {
            log::error("datastream chunk (%d) : bad type in asset '%s'",
                chunkId, cfile->getFilePath().c_str());
            return false;
        }
        if (desc.getVary()!=handle->vary)
        {
            log::error("datastream chunk (%d) : bad vertex vary in asset '%s'",
                chunkId, cfile->getFilePath().c_str());
            return false;
        }

        if (desc.getSemantic()!=handle->semantic)
        {
            log::error("datastream chunk (%d) : bad semantic in asset '%s'",
                chunkId, cfile->getFilePath().c_str());
            return false;
        }
        if (handle->elemSize!=0 && desc.elemSize!=handle->elemSize)
        {
            log::error("datastream chunk (%d) : bad elemSize in asset '%s'",
                chunkId, cfile->getFilePath().c_str());
            return false;
        }
        if (handle->elemCount!=0 && desc.elemCount!=handle->elemCount)
        {
            log::error("datastream chunk (%d) : bad elemCount in asset '%s'",
                chunkId, cfile->getFilePath().c_str());
            return false;
        }

        handle->elemCount = desc.elemCount;
        handle->elemSize = desc.elemSize;
        handle->data = chunkData+sizeof(Desc);
        return true;
    }
    else
        log::error("Chunk deserialize : invalid ChunkId for stream in asset '%s'",
            cfile->getFilePath().c_str());

    return false;
}

std::shared_ptr<MeshSetBase> ChunkReader::loadMeshSetChunk_0x100(Chunk const * chunk)
{
    typedef MeshSet_ChunkDesc_0x100 Desc;

    if (!cfile->validateChunk<Desc>(chunk))
        return nullptr;

    std::shared_ptr<MeshSetBase> mset;

    Desc const & desc = *(Desc const *)chunk->data;

    Desc::Type stype = desc.getType();

    switch (stype) {

        case Desc::MESH :
            mset = std::make_shared<MeshSet>();
            mset->type = MeshSetBase::MESH;
            break;

        case Desc::MESHLET :
            mset = std::make_shared<MeshletSet>();
            mset->type = MeshSetBase::MESHLET;
            break;

        default:
            log::error("incorrect Set type (%d)", stype);
            return nullptr;
    }

    mset->name = uncacheString(desc.name);
    mset->bbox = desc.bbox;

    StreamHandle handle;

    handle = {"Position", FP32, VERTEX, POSITION, 0, sizeof(donut::math::float3), nullptr};
    if (loadStreamChunk_0x100(desc.streamChunkIds[Desc::POSITIONS], &handle))
    {
        mset->streams.position = (donut::math::float3 const *)handle.data;
        mset->nverts = (uint32_t)handle.elemCount;
    } else
        return nullptr;

    handle = {"TexCoord0", FP32, VERTEX, TEXCOORD, mset->nverts, sizeof(donut::math::float2), nullptr};
    if (loadStreamChunk_0x100(desc.streamChunkIds[Desc::TEXCOORDS0], &handle))
        mset->streams.texcoord0 = (donut::math::float2 const *)handle.data;

    handle = {"TexCoord1", FP32, VERTEX, TEXCOORD, mset->nverts, sizeof(donut::math::float2), nullptr};
    if (loadStreamChunk_0x100(desc.streamChunkIds[Desc::TEXCOORDS1], &handle))
        mset->streams.texcoord1 = (donut::math::float2 const *)handle.data;

    handle = {"Normal", UINT32, VERTEX, NORMAL, mset->nverts, sizeof(uint32_t), nullptr};
    if (loadStreamChunk_0x100(desc.streamChunkIds[Desc::NORMALS], &handle))
        mset->streams.normal = (uint32_t const *)handle.data;

    handle = {"Tangent", UINT32, VERTEX, TANGENT, mset->nverts, sizeof(uint32_t), nullptr};
    if (loadStreamChunk_0x100(desc.streamChunkIds[Desc::TANGENTS], &handle))
        mset->streams.tangent = (uint32_t const *)handle.data;

    handle = {"Bitangent", UINT32, VERTEX, BITANGENT, mset->nverts, sizeof(uint32_t), nullptr};
    if (loadStreamChunk_0x100(desc.streamChunkIds[Desc::BITANGENTS], &handle))
        mset->streams.bitangent = (uint32_t const *)handle.data;

    if (stype==Desc::MESH)
    {
        std::shared_ptr<MeshSet> set = std::static_pointer_cast<MeshSet>(mset);

        set->meshInfos=nullptr;

        handle = {"Indices", UINT32, VARY_NONE, INDEX, 0, sizeof(uint32_t), nullptr};
        if (loadStreamChunk_0x100(desc.streamChunkIds[Desc::MESH_INDICES], &handle))
        {
            set->indices = (uint32_t *)handle.data;
            set->nindices = (uint32_t)handle.elemCount;
        } else
            return nullptr;

    }
    else if (stype==Desc::MESHLET)
    {
        std::shared_ptr<MeshletSet> set = std::static_pointer_cast<MeshletSet>(mset);

        set->meshInfos=nullptr;

        handle = {"Indices32", UINT32, VARY_NONE, INDEX, 0, sizeof(uint32_t), nullptr};
        if (loadStreamChunk_0x100(desc.streamChunkIds[Desc::MESHLET_INDICES32], &handle))
        {
            set->indices32 = (uint32_t *)handle.data;
            set->nindices32 = (uint32_t)handle.elemCount;
        } else
            return nullptr;

        handle = {"Indices8", UINT8, VARY_NONE, INDEX, 0, sizeof(uint8_t), nullptr};
        if (loadStreamChunk_0x100(desc.streamChunkIds[Desc::MESHLET_INDICES8], &handle))
        {
            set->indices8 = (uint8_t *)handle.data;
            set->nindices8 = (uint32_t)handle.elemCount;
        } else
            return nullptr;

        handle = {"Meshlet Headers", UINT32, VARY_NONE, MESHLET_INFO, 0, 0, nullptr};
        if (loadStreamChunk_0x100(desc.streamChunkIds[Desc::MESHLET_INFO], &handle))
        {
            set->meshlets = (uint32_t *)handle.data;
            set->nmeshlets = (uint32_t)handle.elemCount;
            set->meshletSize = (uint8_t)(handle.elemSize / sizeof(uint32_t));
        } else
            return nullptr;
    }

    if (!loadMeshInfosChunk_0x100(desc.minfosChunkId, mset))
        return nullptr;

    if (!loadMeshInstancesChunk_0x100(desc.instancesChunkId, mset))
        return nullptr;

    if (desc.nodesChunkId.valid())
        if (!loadMeshNodesChunk_0x100(desc.nodesChunkId, mset))
            return nullptr;

    return mset;
}

//
// implementation
//

std::shared_ptr<MeshSetBase const> deserialize(
    std::weak_ptr<donut::vfs::IBlob const> iblob, char const * assetpath)
{

    ChunkReader reader;

    if (auto const blob = iblob.lock())
    {
        if ((reader.cfile = ChunkFile::deserialize(blob, assetpath)))
        {
            std::vector<Chunk const *> chunks(1);

            // load strings table chunk
            reader.cfile->getChunks(CHUNKTYPE_STRINGS_TABLE, chunks);
            if (chunks.size()!=1)
            {
                log::error("Chunk deserialize : invalid number of"
                    " string table chunks in asset '%s'", assetpath);
                return nullptr;
            }
            if (!reader.loadStringsTableChunk_0x100(chunks[0]))
                return nullptr;

            // load meshset chunk
            reader.cfile->getChunks(CHUNKTYPE_MESHSET, chunks);
            if (chunks.size()!=1)
            {
                log::error("Chunk deserialize : invalid number of"
                    " meshset chunks in asset '%s'", assetpath);
                return nullptr;
            }

            std::shared_ptr<MeshSetBase> mset =
                reader.loadMeshSetChunk_0x100(chunks[0]);
            if (mset)
            {
                mset->blob = blob;
                return mset;
            }
        }
    }
    else
    {
        log::error("Chunk deserialize : invalid data blob in asset '%s'", assetpath);
    }
    return nullptr;
}

}
