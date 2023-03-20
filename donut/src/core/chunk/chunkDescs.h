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

#include <donut/core/math/affine.h>
#include <donut/core/math/basics.h>
#include <donut/core/math/box.h>
#include <donut/core/math/vector.h>

#include <cstdint>

// chunks access

namespace donut::chunk
{

enum ChunkType : uint32_t
{
    CHUNKTYPE_UNDEFINED     = 0,

    CHUNKTYPE_STREAM        = 0x100,
    CHUNKTYPE_STRINGS_TABLE = 0x110,

    CHUNKTYPE_MESHSET       = 0x200,
    CHUNKTYPE_MESH_INFOS,
    CHUNKTYPE_MESH_INSTANCES,
    CHUNKTYPE_MESH_NODES,

    CHUNKTYPE_MATERIALS     = 0x400,
    CHUNKTYPE_LIGHTS        = 0x500,
};


//
// Strings Table Chunks
//

struct StringsTable_ChunkDesc_0x100
{

    static constexpr uint32_t const version = 0x100;
    static constexpr ChunkType const chunktype = CHUNKTYPE_STRINGS_TABLE;

    StringsTable_ChunkDesc_0x100() : nstrings(0) { }

    uint32_t flags,
             nstrings;

    struct TableEntry
    {
        size_t offset,
               length;
    };

    // table starts here
};

//
// Data Stream Chunks
//

enum Type : uint8_t
{
    UINT8=1,
    UINT16,
    UINT32,
    FP16,
    FP32,
    STRING
};

enum Vary : uint8_t
{
    VARY_NONE = 1,
    VERTEX,       // 1 value for each vertex
    FACE,         // 1 value for each face
    FACE_VERTEX,  // 1 value for each vertex of each face
};

enum Semantic : uint8_t
{
    POSITION=1,
    NORMAL,
    TANGENT,
    BITANGENT,
    TEXCOORD,
    COLOR,
    INDEX,
    MESHLET_INFO,
    USER
};

struct StreamHandle
{

    inline bool isValid() const
    {
        return (type>0 && vary>0 && semantic>0
            && elemCount>0 && elemSize>0 && data);
    }

    char const * name;

    Type type;
    Vary vary;
    Semantic semantic;

    size_t elemCount,
           elemSize;
    void const * data;
};

struct Stream_ChunkDesc_0x100
{
    static constexpr uint32_t const version = 0x100;
    static constexpr ChunkType const chunktype = CHUNKTYPE_STREAM;

    Stream_ChunkDesc_0x100() : flags(0), elemCount(0), elemSize(0) {}

    Stream_ChunkDesc_0x100(StreamHandle h)
    {
        setFlags(h.type, h.vary, h.semantic);
        elemCount = h.elemCount;
        elemSize = h.elemSize;
    }

    // Field    | Bits | Content
    // ---------|:----:|-------------
    // type     |  4   | chunk data type
    // vary     |  2   | chunk data vary
    // semantic |  4   | chunk data semantic

    void setFlags(Type type, Vary vary, Semantic semantic) {
        flags = donut::math::insertBits<size_t>(type, 4, 0) |
                donut::math::insertBits<size_t>(vary, 2, 4) |
                donut::math::insertBits<size_t>(semantic, 4, 6);
    }

    Type getType() const {
        return (Type)donut::math::extractBits<size_t>(flags, 4, 0);
    }

    Vary getVary() const {
        return (Vary)donut::math::extractBits<size_t>(flags, 2, 4);
    }

    Semantic getSemantic() const {
        return (Semantic)donut::math::extractBits<size_t>(flags, 4, 6);
    }

    size_t flags,
           elemCount,
           elemSize;

    // data starts here
};

//
// Mesh info chunk
//

struct MeshInfos_ChunkDesc_0x100
{
    static constexpr uint32_t const version = 0x100;
    static constexpr ChunkType const chunktype = CHUNKTYPE_MESH_INFOS;

    MeshInfos_ChunkDesc_0x100() : flags(0), nelems(0) {}

    enum Type {
        MESH = 0,
        MESHLET
    };

    // Field    | Bits | Content
    // ---------|:----:|-------------
    // type     |  3   | chunk data type

    void setFlags(Type type) {
        flags = donut::math::insertBits<uint32_t>(type, 3, 0);
    }

    Type getType() const {
        return (Type)donut::math::extractBits<uint32_t>(flags, 4, 0);
    }

    uint32_t flags,
             nelems;

    // subsets data starts here
};

//
// Mesh nodes chunk
//

struct MeshNodes_ChunkDesc_0x100
{
    static constexpr uint32_t const version = 0x100;
    static constexpr ChunkType const chunktype = CHUNKTYPE_MESH_NODES;

    MeshNodes_ChunkDesc_0x100() : nnodes(0), rootId(0) {}

    uint32_t nnodes,
             rootId;

    // data starts here
};

//
// Mesh instances chunk
//

struct MeshInstances_ChunkDesc_0x100
{
    static constexpr uint32_t const version = 0x100;
    static constexpr ChunkType const chunktype = CHUNKTYPE_MESH_INSTANCES;

    MeshInstances_ChunkDesc_0x100() : ninstances(0) {}

    uint32_t ninstances;

    // data starts here
};

struct MeshSet_ChunkDesc_0x100
{
    static constexpr uint32_t const version = 0x100;
    static constexpr ChunkType const chunktype = CHUNKTYPE_MESHSET;

    MeshSet_ChunkDesc_0x100() : flags(0), meshletMaxVerts(0), meshletMaxPrims(0) {}

    enum Type {
        MESH = 0,
        MESHLET
    };

    // Field    | Bits | Content
    // ---------|:----:|-------------
    // type     |  4   | chunk data type

    void setFlags(Type type) {
        flags = donut::math::insertBits<uint32_t>(type, 4, 0);
    }

    Type getType() const {
        return (Type)donut::math::extractBits<uint32_t>(flags, 4, 0);
    }

    uint32_t flags,
             meshletMaxVerts,
             meshletMaxPrims;

    size_t name;

    enum Streams : uint8_t {
         POSITIONS =  0,

         TEXCOORDS0 = 1,
         TEXCOORDS1 = 2,

         NORMALS    = 3,
         TANGENTS   = 4,
         BITANGENTS = 5,

         MESH_INDICES       = 6, // meshes

         MESHLET_INDICES32  = 6, // meshlets (duped verts)
         MESHLET_INDICES8   = 7,
         MESHLET_INFO       = 8
    };

    ChunkId streamChunkIds[16];

    ChunkId minfosChunkId,
            instancesChunkId,
            nodesChunkId;

    donut::math::box3 bbox;
};

};

