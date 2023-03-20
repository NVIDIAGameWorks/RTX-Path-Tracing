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

// This file is loosely based on DDSTextureLoader.cpp from Microsoft DirectXTK library.

//--------------------------------------------------------------------------------------
// File: DDSTextureLoader.cpp
//
// Functions for loading a DDS texture and creating a Direct3D runtime resource for it
//
// Note these functions are useful as a light-weight runtime loader for DDS files. For
// a full-featured DDS file reader, writer, and texture processing pipeline see
// the 'Texconv' sample and the 'DirectXTex' library.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkId=248926
// http://go.microsoft.com/fwlink/?LinkId=248929
//--------------------------------------------------------------------------------------

/*
    The MIT License (MIT)

    Copyright (c) 2018 Microsoft Corp

    Permission is hereby granted, free of charge, to any person obtaining a copy of this
    software and associated documentation files (the "Software"), to deal in the Software
    without restriction, including without limitation the rights to use, copy, modify,
    merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
    permit persons to whom the Software is furnished to do so, subject to the following
    conditions:

    The above copyright notice and this permission notice shall be included in all copies
    or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
    INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
    PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
    HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
    CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
    OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <donut/engine/DDSFile.h>

#include "dds.h"

#include <iterator>

#include <donut/engine/TextureCache.h>
#include <donut/core/vfs/VFS.h>

#define D3D11_RESOURCE_MISC_TEXTURECUBE 0x4

using namespace donut::vfs;
using namespace donut::engine::dds;

namespace donut::engine
{

    struct FormatMapping
    {
        nvrhi::Format nvrhiFormat;
        DXGI_FORMAT dxgiFormat;
        uint32_t bitsPerPixel;
    };

    const FormatMapping g_FormatMappings[] = {
        { nvrhi::Format::UNKNOWN,              DXGI_FORMAT_UNKNOWN,                0 },
        { nvrhi::Format::R8_UINT,              DXGI_FORMAT_R8_UINT,                8 },
        { nvrhi::Format::R8_SINT,              DXGI_FORMAT_R8_SINT,                8 },
        { nvrhi::Format::R8_UNORM,             DXGI_FORMAT_R8_UNORM,               8 },
        { nvrhi::Format::R8_SNORM,             DXGI_FORMAT_R8_SNORM,               8 },
        { nvrhi::Format::RG8_UINT,             DXGI_FORMAT_R8G8_UINT,              16 },
        { nvrhi::Format::RG8_SINT,             DXGI_FORMAT_R8G8_SINT,              16 },
        { nvrhi::Format::RG8_UNORM,            DXGI_FORMAT_R8G8_UNORM,             16 },
        { nvrhi::Format::RG8_SNORM,            DXGI_FORMAT_R8G8_SNORM,             16 },
        { nvrhi::Format::R16_UINT,             DXGI_FORMAT_R16_UINT,               16 },
        { nvrhi::Format::R16_SINT,             DXGI_FORMAT_R16_SINT,               16 },
        { nvrhi::Format::R16_UNORM,            DXGI_FORMAT_R16_UNORM,              16 },
        { nvrhi::Format::R16_SNORM,            DXGI_FORMAT_R16_SNORM,              16 },
        { nvrhi::Format::R16_FLOAT,            DXGI_FORMAT_R16_FLOAT,              16 },
        { nvrhi::Format::BGRA4_UNORM,          DXGI_FORMAT_B4G4R4A4_UNORM,         16 },
        { nvrhi::Format::B5G6R5_UNORM,         DXGI_FORMAT_B5G6R5_UNORM,           16 },
        { nvrhi::Format::B5G5R5A1_UNORM,       DXGI_FORMAT_B5G5R5A1_UNORM,         16 },
        { nvrhi::Format::RGBA8_UINT,           DXGI_FORMAT_R8G8B8A8_UINT,          32 },
        { nvrhi::Format::RGBA8_SINT,           DXGI_FORMAT_R8G8B8A8_SINT,          32 },
        { nvrhi::Format::RGBA8_UNORM,          DXGI_FORMAT_R8G8B8A8_UNORM,         32 },
        { nvrhi::Format::RGBA8_SNORM,          DXGI_FORMAT_R8G8B8A8_SNORM,         32 },
        { nvrhi::Format::BGRA8_UNORM,          DXGI_FORMAT_B8G8R8A8_UNORM,         32 },
        { nvrhi::Format::SRGBA8_UNORM,         DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,    32 },
        { nvrhi::Format::SBGRA8_UNORM,         DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,    32 },
        { nvrhi::Format::R10G10B10A2_UNORM,    DXGI_FORMAT_R10G10B10A2_UNORM,      32 },
        { nvrhi::Format::R11G11B10_FLOAT,      DXGI_FORMAT_R11G11B10_FLOAT,        32 },
        { nvrhi::Format::RG16_UINT,            DXGI_FORMAT_R16G16_UINT,            32 },
        { nvrhi::Format::RG16_SINT,            DXGI_FORMAT_R16G16_SINT,            32 },
        { nvrhi::Format::RG16_UNORM,           DXGI_FORMAT_R16G16_UNORM,           32 },
        { nvrhi::Format::RG16_SNORM,           DXGI_FORMAT_R16G16_SNORM,           32 },
        { nvrhi::Format::RG16_FLOAT,           DXGI_FORMAT_R16G16_FLOAT,           32 },
        { nvrhi::Format::R32_UINT,             DXGI_FORMAT_R32_UINT,               32 },
        { nvrhi::Format::R32_SINT,             DXGI_FORMAT_R32_SINT,               32 },
        { nvrhi::Format::R32_FLOAT,            DXGI_FORMAT_R32_FLOAT,              32 },
        { nvrhi::Format::RGBA16_UINT,          DXGI_FORMAT_R16G16B16A16_UINT,      64 },
        { nvrhi::Format::RGBA16_SINT,          DXGI_FORMAT_R16G16B16A16_SINT,      64 },
        { nvrhi::Format::RGBA16_FLOAT,         DXGI_FORMAT_R16G16B16A16_FLOAT,     64 },
        { nvrhi::Format::RGBA16_UNORM,         DXGI_FORMAT_R16G16B16A16_UNORM,     64 },
        { nvrhi::Format::RGBA16_SNORM,         DXGI_FORMAT_R16G16B16A16_SNORM,     64 },
        { nvrhi::Format::RG32_UINT,            DXGI_FORMAT_R32G32_UINT,            64 },
        { nvrhi::Format::RG32_SINT,            DXGI_FORMAT_R32G32_SINT,            64 },
        { nvrhi::Format::RG32_FLOAT,           DXGI_FORMAT_R32G32_FLOAT,           64 },
        { nvrhi::Format::RGB32_UINT,           DXGI_FORMAT_R32G32B32_UINT,         96 },
        { nvrhi::Format::RGB32_SINT,           DXGI_FORMAT_R32G32B32_SINT,         96 },
        { nvrhi::Format::RGB32_FLOAT,          DXGI_FORMAT_R32G32B32_FLOAT,        96 },
        { nvrhi::Format::RGBA32_UINT,          DXGI_FORMAT_R32G32B32A32_UINT,      128 },
        { nvrhi::Format::RGBA32_SINT,          DXGI_FORMAT_R32G32B32A32_SINT,      128 },
        { nvrhi::Format::RGBA32_FLOAT,         DXGI_FORMAT_R32G32B32A32_FLOAT,     128 },
        { nvrhi::Format::D16,                  DXGI_FORMAT_R16_UNORM,              16 },
        { nvrhi::Format::D24S8,                DXGI_FORMAT_R24_UNORM_X8_TYPELESS,  32 },
        { nvrhi::Format::X24G8_UINT,           DXGI_FORMAT_X24_TYPELESS_G8_UINT,   32 },
        { nvrhi::Format::D32,                  DXGI_FORMAT_R32_FLOAT,              32 },
        { nvrhi::Format::D32S8,                DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, 64 },
        { nvrhi::Format::X32G8_UINT,           DXGI_FORMAT_X32_TYPELESS_G8X24_UINT,  64 },
        { nvrhi::Format::BC1_UNORM,            DXGI_FORMAT_BC1_UNORM,              4 },
        { nvrhi::Format::BC1_UNORM_SRGB,       DXGI_FORMAT_BC1_UNORM_SRGB,         4 },
        { nvrhi::Format::BC2_UNORM,            DXGI_FORMAT_BC2_UNORM,              8 },
        { nvrhi::Format::BC2_UNORM_SRGB,       DXGI_FORMAT_BC2_UNORM_SRGB,         8 },
        { nvrhi::Format::BC3_UNORM,            DXGI_FORMAT_BC3_UNORM,              8 },
        { nvrhi::Format::BC3_UNORM_SRGB,       DXGI_FORMAT_BC3_UNORM_SRGB,         8 },
        { nvrhi::Format::BC4_UNORM,            DXGI_FORMAT_BC4_UNORM,              4 },
        { nvrhi::Format::BC4_SNORM,            DXGI_FORMAT_BC4_SNORM,              4 },
        { nvrhi::Format::BC5_UNORM,            DXGI_FORMAT_BC5_UNORM,              8 },
        { nvrhi::Format::BC5_SNORM,            DXGI_FORMAT_BC5_SNORM,              8 },
        { nvrhi::Format::BC6H_UFLOAT,          DXGI_FORMAT_BC6H_UF16,              8 },
        { nvrhi::Format::BC6H_SFLOAT,          DXGI_FORMAT_BC6H_SF16,              8 },
        { nvrhi::Format::BC7_UNORM,            DXGI_FORMAT_BC7_UNORM,              8 },
        { nvrhi::Format::BC7_UNORM_SRGB,       DXGI_FORMAT_BC7_UNORM_SRGB,         8 },
    };

#define ISBITMASK( r,g,b,a ) ( ddpf.RBitMask == r && ddpf.GBitMask == g && ddpf.BBitMask == b && ddpf.ABitMask == a )

    static nvrhi::Format ConvertDDSFormat(const DDS_PIXELFORMAT& ddpf, bool forceSRGB)
    {
        if (ddpf.flags & DDS_RGB)
        {
            // Note that sRGB formats are written using the "DX10" extended header

            switch (ddpf.RGBBitCount)
            {
            case 32:
                if (ISBITMASK(0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000))
                {
                    return forceSRGB ? nvrhi::Format::RGBA8_UNORM : nvrhi::Format::SRGBA8_UNORM;
                }

                if (ISBITMASK(0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000))
                {
                    return forceSRGB ? nvrhi::Format::BGRA8_UNORM : nvrhi::Format::SBGRA8_UNORM;
                }

                if (ISBITMASK(0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000))
                {
                    return forceSRGB ? nvrhi::Format::BGRA8_UNORM : nvrhi::Format::SBGRA8_UNORM; // actually BGRX8, but there's no such format in NVRHI
                }

                // No DXGI format maps to ISBITMASK(0x000000ff,0x0000ff00,0x00ff0000,0x00000000) aka D3DFMT_X8B8G8R8

                // Note that many common DDS reader/writers (including D3DX) swap the
                // the RED/BLUE masks for 10:10:10:2 formats. We assume
                // below that the 'backwards' header mask is being used since it is most
                // likely written by D3DX. The more robust solution is to use the 'DX10'
                // header extension and specify the DXGI_FORMAT_R10G10B10A2_UNORM format directly

                // For 'correct' writers, this should be 0x000003ff,0x000ffc00,0x3ff00000 for RGB data
                if (ISBITMASK(0x3ff00000, 0x000ffc00, 0x000003ff, 0xc0000000))
                {
                    return nvrhi::Format::R10G10B10A2_UNORM;
                }

                // No DXGI format maps to ISBITMASK(0x000003ff,0x000ffc00,0x3ff00000,0xc0000000) aka D3DFMT_A2R10G10B10

                if (ISBITMASK(0x0000ffff, 0xffff0000, 0x00000000, 0x00000000))
                {
                    return nvrhi::Format::RG16_UNORM;
                }

                if (ISBITMASK(0xffffffff, 0x00000000, 0x00000000, 0x00000000))
                {
                    // Only 32-bit color channel format in D3D9 was R32F
                    return nvrhi::Format::R32_FLOAT;
                }
                break;

            case 24:
                // No 24bpp DXGI formats aka D3DFMT_R8G8B8
                break;

            case 16:
                if (ISBITMASK(0x7c00, 0x03e0, 0x001f, 0x8000))
                {
                    return nvrhi::Format::B5G5R5A1_UNORM;
                }
                if (ISBITMASK(0xf800, 0x07e0, 0x001f, 0x0000))
                {
                    return nvrhi::Format::B5G6R5_UNORM;
                }

                // No DXGI format maps to ISBITMASK(0x7c00,0x03e0,0x001f,0x0000) aka D3DFMT_X1R5G5B5

                if (ISBITMASK(0x0f00, 0x00f0, 0x000f, 0xf000))
                {
                    return nvrhi::Format::BGRA4_UNORM;
                }

                // No DXGI format maps to ISBITMASK(0x0f00,0x00f0,0x000f,0x0000) aka D3DFMT_X4R4G4B4

                // No 3:3:2, 3:3:2:8, or paletted DXGI formats aka D3DFMT_A8R3G3B2, D3DFMT_R3G3B2, D3DFMT_P8, D3DFMT_A8P8, etc.
                break;
            }
        }
        else if (ddpf.flags & DDS_LUMINANCE)
        {
            if (8 == ddpf.RGBBitCount)
            {
                if (ISBITMASK(0x000000ff, 0x00000000, 0x00000000, 0x00000000))
                {
                    return nvrhi::Format::R8_UNORM;
                }

                // No DXGI format maps to ISBITMASK(0x0f,0x00,0x00,0xf0) aka D3DFMT_A4L4

                if (ISBITMASK(0x000000ff, 0x00000000, 0x00000000, 0x0000ff00))
                {
                    return nvrhi::Format::RG8_UNORM;
                }
            }

            if (16 == ddpf.RGBBitCount)
            {
                if (ISBITMASK(0x0000ffff, 0x00000000, 0x00000000, 0x00000000))
                {
                    return nvrhi::Format::R16_UNORM;
                }
                if (ISBITMASK(0x000000ff, 0x00000000, 0x00000000, 0x0000ff00))
                {
                    return nvrhi::Format::RG8_UNORM;
                }
            }
        }
        else if (ddpf.flags & DDS_ALPHA)
        {
            if (8 == ddpf.RGBBitCount)
            {
                return nvrhi::Format::R8_UNORM; // we don't support A8 in NVRHI
            }
        }
        else if (ddpf.flags & DDS_BUMPDUDV)
        {
            if (16 == ddpf.RGBBitCount)
            {
                if (ISBITMASK(0x00ff, 0xff00, 0x0000, 0x0000))
                {
                    return nvrhi::Format::RG8_SNORM;
                }
            }

            if (32 == ddpf.RGBBitCount)
            {
                if (ISBITMASK(0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000))
                {
                    return nvrhi::Format::RGBA8_SNORM;
                }
                if (ISBITMASK(0x0000ffff, 0xffff0000, 0x00000000, 0x00000000))
                {
                    return nvrhi::Format::RG16_SNORM;
                }

                // No DXGI format maps to ISBITMASK(0x3ff00000, 0x000ffc00, 0x000003ff, 0xc0000000) aka D3DFMT_A2W10V10U10
            }
        }
        else if (ddpf.flags & DDS_FOURCC)
        {
            if (MAKEFOURCC('D', 'X', 'T', '1') == ddpf.fourCC)
            {
                return forceSRGB ? nvrhi::Format::BC1_UNORM_SRGB : nvrhi::Format::BC1_UNORM;
            }
            if (MAKEFOURCC('D', 'X', 'T', '3') == ddpf.fourCC)
            {
                return forceSRGB ? nvrhi::Format::BC2_UNORM_SRGB : nvrhi::Format::BC2_UNORM;
            }
            if (MAKEFOURCC('D', 'X', 'T', '5') == ddpf.fourCC)
            {
                return forceSRGB ? nvrhi::Format::BC3_UNORM_SRGB : nvrhi::Format::BC3_UNORM;
            }

            // While pre-multiplied alpha isn't directly supported by the DXGI formats,
            // they are basically the same as these BC formats so they can be mapped
            if (MAKEFOURCC('D', 'X', 'T', '2') == ddpf.fourCC)
            {
                return nvrhi::Format::BC2_UNORM;
            }
            if (MAKEFOURCC('D', 'X', 'T', '4') == ddpf.fourCC)
            {
                return nvrhi::Format::BC3_UNORM;
            }

            if (MAKEFOURCC('A', 'T', 'I', '1') == ddpf.fourCC)
            {
                return nvrhi::Format::BC4_UNORM;
            }
            if (MAKEFOURCC('B', 'C', '4', 'U') == ddpf.fourCC)
            {
                return nvrhi::Format::BC4_UNORM;
            }
            if (MAKEFOURCC('B', 'C', '4', 'S') == ddpf.fourCC)
            {
                return nvrhi::Format::BC4_SNORM;
            }

            if (MAKEFOURCC('A', 'T', 'I', '2') == ddpf.fourCC)
            {
                return nvrhi::Format::BC5_UNORM;
            }
            if (MAKEFOURCC('B', 'C', '5', 'U') == ddpf.fourCC)
            {
                return nvrhi::Format::BC5_UNORM;
            }
            if (MAKEFOURCC('B', 'C', '5', 'S') == ddpf.fourCC)
            {
                return nvrhi::Format::BC5_SNORM;
            }

            // BC6H and BC7 are written using the "DX10" extended header
            /*
            if (MAKEFOURCC('R', 'G', 'B', 'G') == ddpf.fourCC)
            {
                return DXGI_FORMAT_R8G8_B8G8_UNORM;
            }
            if (MAKEFOURCC('G', 'R', 'G', 'B') == ddpf.fourCC)
            {
                return DXGI_FORMAT_G8R8_G8B8_UNORM;
            }

            if (MAKEFOURCC('Y', 'U', 'Y', '2') == ddpf.fourCC)
            {
                return DXGI_FORMAT_YUY2;
            }
            */

            // Check for D3DFORMAT enums being set here
            switch (ddpf.fourCC)
            {
            case 36: // D3DFMT_A16B16G16R16
                return nvrhi::Format::RGBA16_UNORM;

            case 110: // D3DFMT_Q16W16V16U16
                return nvrhi::Format::RGBA16_SNORM;

            case 111: // D3DFMT_R16F
                return nvrhi::Format::R16_FLOAT;

            case 112: // D3DFMT_G16R16F
                return nvrhi::Format::RG16_FLOAT;

            case 113: // D3DFMT_A16B16G16R16F
                return nvrhi::Format::RGBA16_FLOAT;

            case 114: // D3DFMT_R32F
                return nvrhi::Format::R32_FLOAT;

            case 115: // D3DFMT_G32R32F
                return nvrhi::Format::RG32_FLOAT;

            case 116: // D3DFMT_A32B32G32R32F
                return nvrhi::Format::RGBA32_FLOAT;
            }
        }

        return nvrhi::Format::UNKNOWN;
    }

    static uint32_t BitsPerPixel(nvrhi::Format format)
    {
        const FormatMapping& mapping = g_FormatMappings[static_cast<uint32_t>(format)];
        assert(mapping.nvrhiFormat == format);

        return mapping.bitsPerPixel;
    }

    static void GetSurfaceInfo(size_t width,
        size_t height,
        nvrhi::Format fmt,
        uint32_t bitsPerPixel,
        size_t* outNumBytes,
        size_t* outRowBytes,
        size_t* outNumRows)
    {
        size_t numBytes = 0;
        size_t rowBytes = 0;
        size_t numRows = 0;

        bool bc = false;
        size_t bpe = 0;
        switch (fmt)
        {
        case nvrhi::Format::BC1_UNORM:
        case nvrhi::Format::BC1_UNORM_SRGB:
        case nvrhi::Format::BC4_UNORM:
        case nvrhi::Format::BC4_SNORM:
            bc = true;
            bpe = 8;
            break;

        case nvrhi::Format::BC2_UNORM:
        case nvrhi::Format::BC2_UNORM_SRGB:
        case nvrhi::Format::BC3_UNORM:
        case nvrhi::Format::BC3_UNORM_SRGB:
        case nvrhi::Format::BC5_UNORM:
        case nvrhi::Format::BC5_SNORM:
        case nvrhi::Format::BC6H_UFLOAT:
        case nvrhi::Format::BC6H_SFLOAT:
        case nvrhi::Format::BC7_UNORM:
        case nvrhi::Format::BC7_UNORM_SRGB:
            bc = true;
            bpe = 16;
            break;

        default:
            break;
        }

        if (bc)
        {
            size_t numBlocksWide = 0;
            if (width > 0)
            {
                numBlocksWide = std::max<size_t>(1, (width + 3) / 4);
            }
            size_t numBlocksHigh = 0;
            if (height > 0)
            {
                numBlocksHigh = std::max<size_t>(1, (height + 3) / 4);
            }
            rowBytes = numBlocksWide * bpe;
            numRows = numBlocksHigh;
            numBytes = rowBytes * numBlocksHigh;
        }
        else
        {
            rowBytes = (width * bitsPerPixel + 7) / 8; // round up to nearest byte
            numRows = height;
            numBytes = rowBytes * height;
        }

        if (outNumBytes)
        {
            *outNumBytes = numBytes;
        }
        if (outRowBytes)
        {
            *outRowBytes = rowBytes;
        }
        if (outNumRows)
        {
            *outNumRows = numRows;
        }
    }

#undef ISBITMASK

    static TextureAlphaMode GetAlphaMode(const DDS_HEADER* header)
    {
        if (header->ddspf.flags & DDS_FOURCC)
        {
            if (MAKEFOURCC('D', 'X', '1', '0') == header->ddspf.fourCC)
            {
                auto d3d10ext = reinterpret_cast<const DDS_HEADER_DXT10*>(reinterpret_cast<const uint8_t*>(header) + sizeof(DDS_HEADER));
                auto mode = static_cast<TextureAlphaMode>(d3d10ext->miscFlags2 & DDS_MISC_FLAGS2_ALPHA_MODE_MASK);
                switch (mode)
                {
                case TextureAlphaMode::STRAIGHT:
                case TextureAlphaMode::PREMULTIPLIED:
                case TextureAlphaMode::OPAQUE_:
                case TextureAlphaMode::CUSTOM:
                    return mode;

                default:
                    break;
                }
            }
            else if ((MAKEFOURCC('D', 'X', 'T', '2') == header->ddspf.fourCC)
                || (MAKEFOURCC('D', 'X', 'T', '4') == header->ddspf.fourCC))
            {
                return TextureAlphaMode::PREMULTIPLIED;
            }
        }

        return TextureAlphaMode::UNKNOWN;
    }

    static size_t FillTextureInfoOffsets(TextureData& textureInfo, size_t dataSize, ptrdiff_t dataOffset)
    {
        textureInfo.originalBitsPerPixel = BitsPerPixel(textureInfo.format);

        textureInfo.dataLayout.resize(textureInfo.arraySize);
        for (uint32_t arraySlice = 0; arraySlice < textureInfo.arraySize; arraySlice++)
        {
            size_t w = textureInfo.width;
            size_t h = textureInfo.height;
            size_t d = textureInfo.depth;

            std::vector<TextureSubresourceData>& sliceData = textureInfo.dataLayout[arraySlice];
            sliceData.resize(textureInfo.mipLevels);

            for (uint32_t mipLevel = 0; mipLevel < textureInfo.mipLevels; mipLevel++)
            {
                size_t NumBytes = 0;
                size_t RowBytes = 0;
                size_t NumRows = 0;
                GetSurfaceInfo(w, h, textureInfo.format, textureInfo.originalBitsPerPixel, &NumBytes, &RowBytes, &NumRows);

                TextureSubresourceData& levelData = sliceData[mipLevel];
                levelData.dataOffset = dataOffset;
                levelData.dataSize = NumBytes;
                levelData.rowPitch = RowBytes;
                levelData.depthPitch = RowBytes * NumRows;

                dataOffset += NumBytes * d;

                if (dataSize > 0 && dataOffset > static_cast<ptrdiff_t>(dataSize))
                    return 0;

                w = w >> 1;
                h = h >> 1;
                d = d >> 1;

                if (w == 0) w = 1;
                if (h == 0) h = 1;
                if (d == 0) d = 1;
            }
        }

        return dataOffset;
    }

    bool LoadDDSTextureFromMemory(TextureData& textureInfo)
    {
        if (textureInfo.data->size() < sizeof(uint32_t) + sizeof(DDS_HEADER))
        {
            return false;
        }

        auto dwMagicNumber = *reinterpret_cast<const uint32_t*>(textureInfo.data->data());
        if (dwMagicNumber != DDS_MAGIC)
        {
            return false;
        }

        auto header = reinterpret_cast<const DDS_HEADER*>(static_cast<const char*>(textureInfo.data->data()) + sizeof(uint32_t));

        // Verify header to validate DDS file
        if (header->size != sizeof(DDS_HEADER) ||
            header->ddspf.size != sizeof(DDS_PIXELFORMAT))
        {
            return false;
        }

        // Check for DX10 extension
        bool bDXT10Header = false;
        if ((header->ddspf.flags & DDS_FOURCC) &&
            (MAKEFOURCC('D', 'X', '1', '0') == header->ddspf.fourCC))
        {
            // Must be long enough for both headers and magic value
            if (textureInfo.data->size() < (sizeof(DDS_HEADER) + sizeof(uint32_t) + sizeof(DDS_HEADER_DXT10)))
            {
                return false;
            }

            bDXT10Header = true;
        }

        ptrdiff_t dataOffset = sizeof(uint32_t)
            + sizeof(DDS_HEADER)
            + (bDXT10Header ? sizeof(DDS_HEADER_DXT10) : 0);

        textureInfo.width = header->width;
        textureInfo.height = header->height;
        textureInfo.mipLevels = header->mipMapCount ? header->mipMapCount : 1;
        textureInfo.depth = 1;
        textureInfo.arraySize = 1;
        textureInfo.alphaMode = GetAlphaMode(header);

        if ((header->ddspf.flags & DDS_FOURCC) &&
            (MAKEFOURCC('D', 'X', '1', '0') == header->ddspf.fourCC))
        {
            auto d3d10ext = reinterpret_cast<const DDS_HEADER_DXT10*>(reinterpret_cast<const char*>(header) + sizeof(DDS_HEADER));

            if (d3d10ext->arraySize == 0)
            {
                return false;
            }

            for (const FormatMapping& mapping : g_FormatMappings)
            {
                if (mapping.dxgiFormat == d3d10ext->dxgiFormat)
                {
                    textureInfo.format = mapping.nvrhiFormat;
                    break;
                }
            }

            if (textureInfo.format == nvrhi::Format::UNKNOWN)
            {
                return false;
            }

            // Apply the forceSRGB flag and promote various compatible formats to sRGB
            if (textureInfo.forceSRGB)
            {
                switch (textureInfo.format)  // NOLINT(clang-diagnostic-switch-enum)
                {
                case(nvrhi::Format::RGBA8_UNORM):
                    textureInfo.format = nvrhi::Format::SRGBA8_UNORM;
                    break;

                case(nvrhi::Format::BGRA8_UNORM):
                    textureInfo.format = nvrhi::Format::SBGRA8_UNORM;
                    break;

                case(nvrhi::Format::BC1_UNORM):
                    textureInfo.format = nvrhi::Format::BC1_UNORM_SRGB;
                    break;

                case(nvrhi::Format::BC2_UNORM):
                    textureInfo.format = nvrhi::Format::BC2_UNORM_SRGB;
                    break;

                case(nvrhi::Format::BC3_UNORM):
                    textureInfo.format = nvrhi::Format::BC3_UNORM_SRGB;
                    break;

                case(nvrhi::Format::BC7_UNORM):
                    textureInfo.format = nvrhi::Format::BC7_UNORM_SRGB;
                    break;

                default:
                    break;
                }
            }

            switch (d3d10ext->resourceDimension)
            {
            case DDS_DIMENSION_TEXTURE1D:
                // D3DX writes 1D textures with a fixed Height of 1
                if ((header->flags & DDS_HEIGHT) && textureInfo.height != 1)
                {
                    return false;
                }
                textureInfo.height = 1;
                textureInfo.dimension = d3d10ext->arraySize > 1 ? nvrhi::TextureDimension::Texture1DArray : nvrhi::TextureDimension::Texture1D;
                break;

            case DDS_DIMENSION_TEXTURE2D:
                if (d3d10ext->miscFlag & D3D11_RESOURCE_MISC_TEXTURECUBE)
                {
                    textureInfo.arraySize = d3d10ext->arraySize * 6;
                    textureInfo.dimension = d3d10ext->arraySize > 1 ? nvrhi::TextureDimension::TextureCubeArray : nvrhi::TextureDimension::TextureCube;
                }
                else
                {
                    textureInfo.arraySize = d3d10ext->arraySize;
                    textureInfo.dimension = d3d10ext->arraySize > 1 ? nvrhi::TextureDimension::Texture2DArray : nvrhi::TextureDimension::Texture2D;
                }

                break;

            case DDS_DIMENSION_TEXTURE3D:
                if (!(header->flags & DDS_HEADER_FLAGS_VOLUME))
                {
                    return false;
                }
                textureInfo.depth = header->depth;
                textureInfo.dimension = nvrhi::TextureDimension::Texture3D;
                break;

            default:
                return false;
            }
        }
        else
        {
            textureInfo.format = ConvertDDSFormat(header->ddspf, textureInfo.forceSRGB);

            if (textureInfo.format == nvrhi::Format::UNKNOWN)
            {
                return false;
            }

            if (header->flags & DDS_HEADER_FLAGS_VOLUME)
            {
                textureInfo.depth = header->depth;
                textureInfo.dimension = nvrhi::TextureDimension::Texture3D;
            }
            else
            {
                if (header->caps2 & DDS_CUBEMAP)
                {
                    // We require all six faces to be defined
                    if ((header->caps2 & DDS_CUBEMAP_ALLFACES) != DDS_CUBEMAP_ALLFACES)
                    {
                        return false;
                    }

                    textureInfo.arraySize = 6;
                    textureInfo.dimension = nvrhi::TextureDimension::TextureCube;
                }
                else
                {
                    textureInfo.dimension = nvrhi::TextureDimension::Texture2D;
                }
            }
        }

        if (FillTextureInfoOffsets(textureInfo, textureInfo.data->size(), dataOffset) == 0)
            return false;

        return true;
    }

    static nvrhi::TextureHandle CreateDDSTextureInternal(nvrhi::IDevice* device, nvrhi::ICommandList* commandList, TextureData& info, const char* debugName)
    {
        if (!LoadDDSTextureFromMemory(info))
            return nullptr;

        nvrhi::TextureDesc desc;
        desc.width = info.width;
        desc.height = info.height;
        desc.depth = info.depth;
        desc.arraySize = info.arraySize;
        desc.dimension = info.dimension;
        desc.mipLevels = info.mipLevels;
        desc.format = info.format;
        desc.debugName = debugName;

        nvrhi::TextureHandle texture = device->createTexture(desc);

        if (!texture)
            return nullptr;

        commandList->beginTrackingTextureState(texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);

        for (uint32_t arraySlice = 0; arraySlice < info.arraySize; arraySlice++)
        {
            for (uint32_t mipLevel = 0; mipLevel < info.mipLevels; mipLevel++)
            {
                const TextureSubresourceData& layout = info.dataLayout[arraySlice][mipLevel];

                commandList->writeTexture(texture, arraySlice, mipLevel, static_cast<const char*>(info.data->data()) + layout.dataOffset, layout.rowPitch);
            }
        }

        commandList->setPermanentTextureState(texture, nvrhi::ResourceStates::ShaderResource);
        commandList->commitBarriers();

        return texture;
    }

    nvrhi::TextureHandle CreateDDSTextureFromMemory(nvrhi::IDevice* device, nvrhi::ICommandList* commandList, std::shared_ptr<IBlob> data, const char* debugName /*= nullptr*/, bool forceSRGB /*= false*/)
    {
        if (!data)
            return nullptr;

        TextureData info;
        info.data = data;
        info.forceSRGB = forceSRGB;

        return CreateDDSTextureInternal(device, commandList, info, debugName);
    }

    std::shared_ptr<IBlob> SaveStagingTextureAsDDS(nvrhi::IDevice* device, nvrhi::IStagingTexture* stagingTexture)
    {
        DDS_HEADER header = {};
        DDS_HEADER_DXT10 dx10header = {};
        const nvrhi::TextureDesc& textureDesc = stagingTexture->getDesc();

        header.size = sizeof(DDS_HEADER);
        header.flags = DDS_HEADER_FLAGS_TEXTURE;
        header.width = textureDesc.width;
        header.height = textureDesc.height;
        header.depth = textureDesc.depth;
        header.mipMapCount = textureDesc.mipLevels;
        header.ddspf.size = sizeof(DDS_PIXELFORMAT);
        header.ddspf.flags = DDS_FOURCC;
        header.ddspf.fourCC = MAKEFOURCC('D', 'X', '1', '0');

        switch (textureDesc.dimension)
        {
        case nvrhi::TextureDimension::Texture1D:
        case nvrhi::TextureDimension::Texture1DArray:
            dx10header.resourceDimension = DDS_DIMENSION_TEXTURE1D;
            break;

        case nvrhi::TextureDimension::Texture2D:
        case nvrhi::TextureDimension::Texture2DArray:
        case nvrhi::TextureDimension::TextureCube:
        case nvrhi::TextureDimension::TextureCubeArray:
            dx10header.resourceDimension = DDS_DIMENSION_TEXTURE2D;
            break;

        case nvrhi::TextureDimension::Texture3D:
            // Unsupported
            return nullptr;
            /*header.flags |= DDS_HEADER_FLAGS_VOLUME;
            dx10header.resourceDimension = DDS_DIMENSION_TEXTURE3D;
            break;*/

        case nvrhi::TextureDimension::Texture2DMS:
        case nvrhi::TextureDimension::Texture2DMSArray:
        case nvrhi::TextureDimension::Unknown:
            // Unsupported
            return nullptr;
        }

        dx10header.arraySize = textureDesc.arraySize;
        if (textureDesc.dimension == nvrhi::TextureDimension::TextureCube || textureDesc.dimension == nvrhi::TextureDimension::TextureCubeArray)
        {
            dx10header.arraySize /= 6;
            dx10header.miscFlag |= D3D11_RESOURCE_MISC_TEXTURECUBE;
        }

        for (const FormatMapping& mapping : g_FormatMappings)
        {
            if (mapping.nvrhiFormat == textureDesc.format)
            {
                dx10header.dxgiFormat = mapping.dxgiFormat;
                break;
            }
        }

        if (dx10header.dxgiFormat == DXGI_FORMAT_UNKNOWN)
        {
            // Unsupported
            return nullptr;
        }

        TextureData textureInfo = {};
        textureInfo.format = textureDesc.format;
        textureInfo.arraySize = textureDesc.arraySize;
        textureInfo.width = textureDesc.width;
        textureInfo.height = textureDesc.height;
        textureInfo.depth = textureDesc.depth;
        textureInfo.dimension = textureDesc.dimension;
        textureInfo.mipLevels = textureDesc.mipLevels;

        ptrdiff_t dataOffset = sizeof(uint32_t)
            + sizeof(DDS_HEADER)
            + sizeof(DDS_HEADER_DXT10);

        size_t dataSize = FillTextureInfoOffsets(textureInfo, 0, dataOffset);

        char* data = reinterpret_cast<char*>(malloc(dataSize));
        *reinterpret_cast<uint32_t*>(data) = DDS_MAGIC;
        *reinterpret_cast<DDS_HEADER*>(data + sizeof(uint32_t)) = header;
        *reinterpret_cast<DDS_HEADER_DXT10*>(data + sizeof(uint32_t) + sizeof(DDS_HEADER)) = dx10header;


        for (uint32_t arraySlice = 0; arraySlice < textureDesc.arraySize; arraySlice++)
        {
            uint32_t width = textureInfo.width;
            uint32_t height = textureInfo.height;

            for (uint32_t mipLevel = 0; mipLevel < textureDesc.mipLevels; mipLevel++)
            {
                nvrhi::TextureSlice slice;
                slice.arraySlice = arraySlice;
                slice.mipLevel = mipLevel;

                size_t rowPitch = 0;
                const char* sliceData = reinterpret_cast<const char*>(device->mapStagingTexture(stagingTexture, slice, nvrhi::CpuAccessMode::Read, &rowPitch));

                const TextureSubresourceData& subresourceData = textureInfo.dataLayout[arraySlice][mipLevel];

                for (uint32_t row = 0; row < height; row++)
                {
                    ptrdiff_t destOffset = subresourceData.dataOffset + subresourceData.rowPitch * row;
                    ptrdiff_t srcOffset = rowPitch * row;

                    assert(destOffset + subresourceData.rowPitch <= dataSize);

                    memcpy(data + destOffset, sliceData + srcOffset, subresourceData.rowPitch);
                }

                device->unmapStagingTexture(stagingTexture);

                width = width >> 1;
                height = height >> 1;

                if (width == 0) width = 1;
                if (height == 0) height = 1;
            }
        }

        return std::make_shared<Blob>(data, dataSize);
    }
}