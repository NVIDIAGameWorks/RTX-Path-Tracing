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

#include <map>

namespace donut::chunk
{

class ChunkWriter
{
public:
    ChunkFile cfile;

    // strings cache : accumulate all the strings used in an asset
    // and index them in a map - the map is saved in a special strings
    // table chunk (see chunkStringsTable())
    size_t cacheString(char const * str);

    ChunkId createStringsTableChunk();

private:
    std::map<std::string, size_t> m_stringsmap;
};

size_t ChunkWriter::cacheString(char const * str)
{
    if (str)
    {
        auto const it = m_stringsmap.find(str);
        if (it!=m_stringsmap.end())
            return it->second;
        else
        {
            size_t id = m_stringsmap.size();
            m_stringsmap[str] = id;
            return id;
        }
    }
    else
    {
        return ~size_t(0);
    }
}

ChunkId ChunkWriter::createStringsTableChunk()
{
    typedef StringsTable_ChunkDesc_0x100 Desc;

    size_t descSize = sizeof(Desc),
           nstrings = m_stringsmap.size(),
           tableSize = nstrings * sizeof(Desc::TableEntry),
           stringsSize = 0;

    // create a table of strings using the indices from the
    // mapped strings

    std::vector<Desc::TableEntry> table(nstrings);
    for (auto const & it : m_stringsmap) {

        size_t len = it.first.size() + 1;

        Desc::TableEntry & entry = table[it.second];
        entry.length = len;

        stringsSize += len;
    }

    // compute offsets of strings in chunk

    for (size_t i=0, offset=0; i<table.size(); ++i) {
        table[i].offset = offset;
        offset += table[i].length;
    }

    // populate chunk data

    size_t chunkSize = descSize + tableSize + stringsSize;

    uint8_t * chunkData = new uint8_t[chunkSize];

    Desc * desc = (Desc *)chunkData;
    desc->flags = 0;
    desc->nstrings = (uint32_t)nstrings;

    memcpy(chunkData+descSize, table.data(), tableSize);

    uint8_t * stringsData = chunkData + descSize + tableSize;

    for (auto const it : m_stringsmap) {

        Desc::TableEntry & e = table[it.second];

        memcpy(stringsData + e.offset, it.first.c_str(), e.length-1);

        stringsData[e.offset + e.length -1] = '\0';
    }

    return cfile.addChunk<Desc>(chunkData, chunkSize);
}

//
// Serialization implementation
//

// serialize data streams
static ChunkId chunkStream(StreamHandle const & handle, ChunkWriter & writer)
{
    typedef Stream_ChunkDesc_0x100 Desc;

    if (!handle.isValid())
    {
        log::error("invalid stream : %s", handle.name);
        return ChunkId();
    }

    size_t descSize = sizeof(Desc),
           dataSize = handle.elemSize * handle.elemCount,
           chunkSize = descSize + dataSize;

    uint8_t * chunkData = new uint8_t[chunkSize];

    // fill descriptor

    Desc * desc = (Desc *)chunkData;

    desc->setFlags(handle.type, handle.vary, handle.semantic);

    desc->elemCount = handle.elemCount;
    desc->elemSize = handle.elemSize;

    // copy stream data into chunk

    memcpy(chunkData+descSize, handle.data, dataSize);

    return writer.cfile.addChunk<Desc>(chunkData, chunkSize);
}

// serialize MeshInfos
template <typename T> static ChunkId chunkMeshInfos(
    T const * minfos, uint32_t nminfos, ChunkWriter & writer)
{

    if (nminfos==0)
        return ChunkId();

    typedef MeshInfos_ChunkDesc_0x100 Desc;

    size_t descSize = sizeof(Desc),
           dataSize = nminfos * sizeof(T),
           chunkSize = descSize + dataSize;

    Desc::Type type =
        std::is_same<T, MeshletInfo>::value ? Desc::MESHLET : Desc::MESH;

    uint8_t * chunkData = new uint8_t[chunkSize];

    // fill descriptor

    Desc * desc = (Desc *)chunkData;
    desc->setFlags(type);
    desc->nelems = nminfos;

    // process minfo entries

    T * minfoData = (T *)(chunkData + descSize);
    memcpy(minfoData, minfos, nminfos * sizeof(T));

    for (uint32_t i=0; i<nminfos; ++i) {
        T & minfo = minfoData[i];
        minfo.name = (char *)writer.cacheString(minfo.name);
        minfo.materialName = (char *)writer.cacheString(minfo.materialName);
    }

    return writer.cfile.addChunk<Desc>(chunkData, chunkSize);
}

// serialize MeshInstances
static ChunkId chunkMeshInstances(
    MeshInstance const * instances, uint32_t ninstances, ChunkWriter & writer)
{
    if (ninstances==0)
        return ChunkId();

    typedef MeshInstances_ChunkDesc_0x100 Desc;

    size_t descSize = sizeof(Desc),
           dataSize = ninstances * sizeof(MeshInstance),
           chunkSize = descSize + dataSize;

    uint8_t * chunkData = new uint8_t[chunkSize];

    // fill descriptor

    Desc * desc = (Desc *)chunkData;
    desc->ninstances = ninstances;

    // process instances entries

    MeshInstance * instanceData = (MeshInstance *)(chunkData + descSize);

    memcpy(instanceData, instances, ninstances * sizeof(MeshInstance));

    for (uint32_t i=0; i<ninstances; ++i)
    {
        MeshInstance & instance = instanceData[i];
        instance.name = (char *)writer.cacheString(instance.name);
    }
    return writer.cfile.addChunk<Desc>(chunkData, chunkSize);
}

static ChunkId chunkMeshNodes(
    MeshNode const * nodes, uint32_t nnodes, uint32_t rootId, ChunkWriter & writer)
{
    if (nnodes==0)
        return ChunkId();

    typedef MeshNodes_ChunkDesc_0x100 Desc;

    size_t descSize = sizeof(Desc),
           dataSize = nnodes * sizeof(MeshNode),
           chunkSize = descSize + dataSize;

    uint8_t * chunkData = new uint8_t[chunkSize];

    // fill descriptor

    Desc * desc = (Desc *)chunkData;
    desc->nnodes = nnodes;
    desc->rootId = rootId;

    // process nodes entries

    MeshNode * nodesData = (MeshNode *)(chunkData + descSize);

    memcpy(nodesData, nodes, nnodes * sizeof(MeshNode));

    for (uint32_t i=0; i<nnodes; ++i)
    {
        MeshNode & node = nodesData[i];
        node.name = (char *)writer.cacheString(node.name);
    }

    return writer.cfile.addChunk<Desc>(chunkData, chunkSize);
}

// serialize MeshSets
std::shared_ptr<donut::vfs::IBlob const> serialize(MeshSetBase const & mset)
{

    ChunkWriter writer;

    typedef MeshSet_ChunkDesc_0x100 Desc;

    Desc::Type type;
    switch (mset.type) {
        case MeshSetBase::MESH : type = Desc::MESH; break;
        case MeshSetBase::MESHLET : type = Desc::MESHLET; break;
        default:
            log::error("unsupported set type (%d)", mset.type);
            return nullptr;
    }

    Desc desc;

    desc.setFlags(type);

    desc.name = writer.cacheString(mset.name);

    //  attribute streams

    StreamHandle handle;

    if (mset.streams.position)
    {
        handle = {"Position", FP32, VERTEX, POSITION, mset.nverts, sizeof(donut::math::float3), mset.streams.position};
        desc.streamChunkIds[Desc::POSITIONS] = chunkStream(handle, writer);
    }

    if (mset.streams.texcoord0)
    {
        handle = {"TexCoord0", FP32, VERTEX, TEXCOORD, mset.nverts, sizeof(donut::math::float2), mset.streams.texcoord0};
        desc.streamChunkIds[Desc::TEXCOORDS0] = chunkStream(handle, writer);
    }

    if (mset.streams.texcoord1)
    {
        handle = {"TexCoord1", FP32, VERTEX, TEXCOORD, mset.nverts, sizeof(donut::math::float2), mset.streams.texcoord1};
        desc.streamChunkIds[Desc::TEXCOORDS1] = chunkStream(handle, writer);
    }

    if (mset.streams.normal)
    {
        handle = {"Normal", UINT32, VERTEX, NORMAL, mset.nverts, sizeof(uint32_t), mset.streams.normal};
        desc.streamChunkIds[Desc::NORMALS] = chunkStream(handle, writer);
    }

    if (mset.streams.tangent)
    {
        handle = {"Tangent", UINT32, VERTEX, TANGENT, mset.nverts, sizeof(uint32_t), mset.streams.tangent};
        desc.streamChunkIds[Desc::TANGENTS] = chunkStream(handle, writer);
    }

    if (mset.streams.bitangent)
    {
        handle = {"Bitangent", UINT32, VERTEX, BITANGENT, mset.nverts, sizeof(uint32_t), mset.streams.bitangent};
        desc.streamChunkIds[Desc::BITANGENTS] = chunkStream(handle, writer);
    }

    // topology indices streams

    if (type==Desc::MESH)
    {
        MeshSet const & set = (MeshSet const &)mset;

        handle = {"Indices", UINT32, VARY_NONE, INDEX, set.nindices, sizeof(uint32_t), set.indices};
        desc.streamChunkIds[Desc::MESH_INDICES] = chunkStream(handle, writer);

        desc.minfosChunkId = chunkMeshInfos(set.meshInfos, set.nmeshInfos, writer);
    }
    else if (type==Desc::MESHLET)
    {
        MeshletSet const & set = (MeshletSet const &)mset;

        if (set.meshletSize>255)
        {
            log::error("meshlet info size too big : %d (max 255)", set.meshletSize);
            return nullptr;
        }

        desc.meshletMaxVerts = set.maxVerts;
        desc.meshletMaxPrims = set.maxPrims;

        handle = {"Meshlet Indices32", UINT32, VARY_NONE, INDEX, set.nindices32, sizeof(uint32_t), set.indices32};
        desc.streamChunkIds[Desc::MESHLET_INDICES32] = chunkStream(handle, writer);

        handle = {"Meshlet Indices8", UINT8, VARY_NONE, INDEX, set.nindices8, sizeof(uint8_t), set.indices8};
        desc.streamChunkIds[Desc::MESHLET_INDICES8] = chunkStream(handle, writer);

        handle = {"Meshlet Headers", UINT32, VARY_NONE, MESHLET_INFO, set.nmeshlets, set.meshletSize * sizeof(uint32_t), set.meshlets};
        desc.streamChunkIds[Desc::MESHLET_INFO] = chunkStream(handle, writer);

        desc.minfosChunkId = chunkMeshInfos(set.meshInfos, set.nmeshInfos, writer);
    }
    else
    {
        log::error("Unknown type of MeshSet");
        return nullptr;
    }

    desc.instancesChunkId = chunkMeshInstances(mset.instances, mset.ninstances, writer);

    desc.nodesChunkId = chunkMeshNodes(mset.nodes, mset.nnodes, mset.rootId, writer);

    desc.bbox = mset.bbox;

    size_t chunkSize = sizeof(Desc);

    uint8_t * chunkData = new uint8_t[chunkSize];

    memcpy(chunkData, &desc, chunkSize);

    if (!writer.cfile.addChunk<Desc>(chunkData, chunkSize).valid())
        return nullptr;

    if (!writer.createStringsTableChunk().valid())
        return nullptr;

    return writer.cfile.serialize();
}

}
