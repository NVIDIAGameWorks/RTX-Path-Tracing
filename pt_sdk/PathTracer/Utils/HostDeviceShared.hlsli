/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __HOST_DEVICE_SHARED_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __HOST_DEVICE_SHARED_HLSLI__

/*******************************************************************
                    Glue code for CPU/GPU compilation
*******************************************************************/

#if (defined(__STDC_HOSTED__) || defined(__cplusplus)) && !defined(__INTELLISENSE__)   // we're in C-compliant compiler, probably host
#define HOST_CODE 1
#error not yet supported in Donut<->pt_sdk
#endif

// TODO: Replace by bit packing functions
#define EXTRACT_BITS(bits, offset, value) (((value) >> (offset)) & ((1u << (bits)) - 1u))
#define PACK_BITS(bits, offset, flags, value) ((((value) & ((1u << (bits)) - 1u)) << (offset)) | ((flags) & (~(((1u << (bits)) - 1u) << (offset)))))
#define PACK_BITS_UNSAFE(bits, offset, flags, value) (((value) << (offset)) | ((flags) & (~(((1u << (bits)) - 1u) << (offset)))))

#ifdef HOST_CODE
/*******************************************************************
                    CPU declarations
*******************************************************************/
#define BEGIN_NAMESPACE_FALCOR namespace Falcor{
#define END_NAMESPACE_FALCOR }
#define SETTER_DECL
#define CONST_FUNCTION const

#ifndef OV_BRIDGE

#error fix below first
// due to HLSL bug in one of the currently used compilers, it will still try to include these even though they're #ifdef-ed out
// #include "Utils/Math/Vector.h"
// #include "Utils/Math/Float16.h"
// #include "glm/packing.hpp"
// #include <algorithm>

namespace Falcor
{
    using uint = uint32_t;
    using float3x3 = glm::float3x3;
    using float3x4 = glm::float3x4;
    using float4x4 = glm::float4x4;

    inline float f16tof32(uint v) { return glm::unpackHalf2x16(v).x; }
    inline float2 f16tof32(uint2 v) { return { f16tof32(v.x), f16tof32(v.y) }; };
    inline float3 f16tof32(uint3 v) { return { f16tof32(v.x), f16tof32(v.y), f16tof32(v.z) }; };
    inline float4 f16tof32(uint4 v) { return { f16tof32(v.x), f16tof32(v.y), f16tof32(v.z), f16tof32(v.w) }; };

    inline uint f32tof16(float v) { return glm::packHalf2x16({ v, 0.f }); }
    inline uint2 f32tof16(float2 v) { return { f32tof16(v.x), f32tof16(v.y) }; }
    inline uint3 f32tof16(float3 v) { return { f32tof16(v.x), f32tof16(v.y), f32tof16(v.z) }; }
    inline uint4 f32tof16(float4 v) { return { f32tof16(v.x), f32tof16(v.y), f32tof16(v.z), f32tof16(v.w) }; }

    inline float saturate(float v) { return std::max(0.0f, std::min(1.0f, v)); }
    inline float2 saturate(float2 v) { return float2(saturate(v.x), saturate(v.y)); }
    inline float3 saturate(float3 v) { return float3(saturate(v.x), saturate(v.y), saturate(v.z)); }
    inline float4 saturate(float4 v) { return float4(saturate(v.x), saturate(v.y), saturate(v.z), saturate(v.w)); }

    /** Generic reinterpret/bit cast.
        TODO: Replace by std::bit_cast<T> when it is available in Visual Studio.
    */
    template<typename T, typename F>
    const T bit_cast(const F& from) noexcept
    {
        static_assert(sizeof(T) == sizeof(F));
        T result;
        memcpy(&result, &from, sizeof(from));
        return result;
    }

    inline float asfloat(uint32_t i) { return bit_cast<float>(i); }
    inline float asfloat(int32_t i) { return bit_cast<float>(i); }
    inline float16_t asfloat16(uint16_t i) { return bit_cast<float16_t>(i); }
    inline uint32_t asuint(float f) { return bit_cast<uint32_t>(f); }
    inline int32_t asint(float f) { return bit_cast<int32_t>(f); }
    inline uint16_t asuint16(float16_t f) { return bit_cast<uint16_t>(f); }
}
#endif // OV_BRIDGE

#else
/*******************************************************************
                    HLSL declarations
*******************************************************************/
#define inline
#define SETTER_DECL // [mutating]
#define BEGIN_NAMESPACE_FALCOR
#define END_NAMESPACE_FALCOR
#define CONST_FUNCTION

#endif

#endif // __HOST_DEVICE_SHARED_HLSLI__