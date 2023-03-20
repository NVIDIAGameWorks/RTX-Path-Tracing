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

#ifndef PACKING_HLSLI
#define PACKING_HLSLI

// Pack [0.0, 1.0] float to a uint of a given bit depth
#define PACK_UFLOAT_TEMPLATE(size)                      \
uint Pack_R ## size ## _UFLOAT(float r, float d = 0.5f) \
{                                                       \
    const uint mask = (1U << size) - 1U;                \
                                                        \
    return (uint)floor(r * mask + d) & mask;            \
}                                                       \
                                                        \
float Unpack_R ## size ## _UFLOAT(uint r)               \
{                                                       \
    const uint mask = (1U << size) - 1U;                \
                                                        \
    return (float)(r & mask) / (float)mask;             \
}

PACK_UFLOAT_TEMPLATE(8)
PACK_UFLOAT_TEMPLATE(10)
PACK_UFLOAT_TEMPLATE(11)
PACK_UFLOAT_TEMPLATE(16)

uint Pack_R8G8B8_UFLOAT(float3 rgb, float3 d = float3(0.5f, 0.5f, 0.5f))
{
    uint r = Pack_R8_UFLOAT(rgb.r, d.r);
    uint g = Pack_R8_UFLOAT(rgb.g, d.g) << 8;
    uint b = Pack_R8_UFLOAT(rgb.b, d.b) << 16;
    return r | g | b;
}

float3 Unpack_R8G8B8_UFLOAT(uint rgb)
{
    float r = Unpack_R8_UFLOAT(rgb);
    float g = Unpack_R8_UFLOAT(rgb >> 8);
    float b = Unpack_R8_UFLOAT(rgb >> 16);
    return float3(r, g, b);
}

uint Pack_R8G8B8A8_Gamma_UFLOAT(float4 rgba, float gamma = 2.2, float4 d = float4(0.5f, 0.5f, 0.5f, 0.5f))
{
    rgba = pow(saturate(rgba), 1.0 / gamma);
    uint r = Pack_R8_UFLOAT(rgba.r, d.r);
    uint g = Pack_R8_UFLOAT(rgba.g, d.g) << 8;
    uint b = Pack_R8_UFLOAT(rgba.b, d.b) << 16;
    uint a = Pack_R8_UFLOAT(rgba.a, d.a) << 24;
    return r | g | b | a;
}

float4 Unpack_R8G8B8A8_Gamma_UFLOAT(uint rgba, float gamma = 2.2)
{
    float r = Unpack_R8_UFLOAT(rgba);
    float g = Unpack_R8_UFLOAT(rgba >> 8);
    float b = Unpack_R8_UFLOAT(rgba >> 16);
    float a = Unpack_R8_UFLOAT(rgba >> 24);
    float4 v = float4(r, g, b, a);
    v = pow(saturate(v), gamma);
    return v;
}


uint Pack_R11G11B10_UFLOAT(float3 rgb, float3 d = float3(0.5f, 0.5f, 0.5f))
{
    uint r = Pack_R11_UFLOAT(rgb.r, d.r);
    uint g = Pack_R11_UFLOAT(rgb.g, d.g) << 11;
    uint b = Pack_R10_UFLOAT(rgb.b, d.b) << 22;
    return r | g | b;
}

float3 Unpack_R11G11B10_UFLOAT(uint rgb)
{
    float r = Unpack_R11_UFLOAT(rgb);
    float g = Unpack_R11_UFLOAT(rgb >> 11);
    float b = Unpack_R10_UFLOAT(rgb >> 22);
    return float3(r, g, b);
}

uint Pack_R8G8B8A8_UFLOAT(float4 rgba, float4 d = float4(0.5f, 0.5f, 0.5f, 0.5f))
{
    uint r = Pack_R8_UFLOAT(rgba.r, d.r);
    uint g = Pack_R8_UFLOAT(rgba.g, d.g) << 8;
    uint b = Pack_R8_UFLOAT(rgba.b, d.b) << 16;
    uint a = Pack_R8_UFLOAT(rgba.a, d.a) << 24;
    return r | g | b | a;
}

float4 Unpack_R8G8B8A8_UFLOAT(uint rgba)
{
    float r = Unpack_R8_UFLOAT(rgba);
    float g = Unpack_R8_UFLOAT(rgba >> 8);
    float b = Unpack_R8_UFLOAT(rgba >> 16);
    float a = Unpack_R8_UFLOAT(rgba >> 24);
    return float4(r, g, b, a);
}

uint Pack_R16G16_UFLOAT(float2 rg, float2 d = float2(0.5f, 0.5f))
{
    uint r = Pack_R16_UFLOAT(rg.r, d.r);
    uint g = Pack_R16_UFLOAT(rg.g, d.g) << 16;
    return r | g;
}

float2 Unpack_R16G16_UFLOAT(uint rg)
{
    float r = Unpack_R16_UFLOAT(rg);
    float g = Unpack_R16_UFLOAT(rg >> 16);
    return float2(r, g);
}

// Todo: FLOAT is not consistent with the rest of the naming here, they should be changed
// to UNORM as they do not actually decode into full floats but are rather normalized unsigned
// floats, whereas this should be a SFLOAT.
uint Pack_R16G16_FLOAT(float2 rg)
{
    uint r = f32tof16(rg.r);
    uint g = f32tof16(rg.g) << 16;
    return r | g;
}

uint2 Pack_R16G16B16A16_FLOAT(float4 rgba)
{
    return uint2(Pack_R16G16_FLOAT(rgba.rg), Pack_R16G16_FLOAT(rgba.ba));
}

float2 Unpack_R16G16_FLOAT(uint rg)
{
    uint2 d = uint2(rg, rg >> 16);
    return f16tof32(d);
}

float4 Unpack_R16G16B16A16_FLOAT(uint2 rgba)
{
    return float4(Unpack_R16G16_FLOAT(rgba.x), Unpack_R16G16_FLOAT(rgba.y));
}

uint Pack_R8_SNORM(float value)
{
    return int(clamp(value, -1.0, 1.0) * 127.0) & 0xff;
}

float Unpack_R8_SNORM(uint value)
{
    int signedValue = int(value << 24) >> 24;
    return clamp(float(signedValue) / 127.0, -1.0, 1.0);
}

uint Pack_RGB8_SNORM(float3 rgb)
{
    uint r = Pack_R8_SNORM(rgb.r);
    uint g = Pack_R8_SNORM(rgb.g) << 8;
    uint b = Pack_R8_SNORM(rgb.b) << 16;
    return r | g | b;
}

float3 Unpack_RGB8_SNORM(uint value)
{
    return float3(
        Unpack_R8_SNORM(value),
        Unpack_R8_SNORM(value >> 8),
        Unpack_R8_SNORM(value >> 16)
    );
}

uint Pack_RGBA8_SNORM(float4 rgb)
{
    uint r = Pack_R8_SNORM(rgb.r);
    uint g = Pack_R8_SNORM(rgb.g) << 8;
    uint b = Pack_R8_SNORM(rgb.b) << 16;
    uint a = Pack_R8_SNORM(rgb.a) << 24;
    return r | g | b | a;
}

float4 Unpack_RGBA8_SNORM(uint value)
{
    return float4(
        Unpack_R8_SNORM(value),
        Unpack_R8_SNORM(value >> 8),
        Unpack_R8_SNORM(value >> 16),
        Unpack_R8_SNORM(value >> 24)
    );
}

#endif // PACKING_HLSLI