/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __UTILS_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __UTILS_HLSLI__

#if !defined(__cplusplus)
#include "Utils/Math/MathConstants.hlsli"

uint4   PackTwoFp32ToFp16(float4 a, float4 b)                             { return (f32tof16(clamp(a, -HLF_MAX, HLF_MAX)) << 16) | f32tof16(clamp(b, -HLF_MAX, HLF_MAX)); }
void    UnpackTwoFp32ToFp16(uint4 packed, out float4 a, out float4 b)     { a = f16tof32(packed >> 16); b = f16tof32(packed & 0xFFFF); }
uint3   PackTwoFp32ToFp16(float3 a, float3 b)                             { return (f32tof16(clamp(a, -HLF_MAX, HLF_MAX)) << 16) | f32tof16(clamp(b, -HLF_MAX, HLF_MAX)); }
void    UnpackTwoFp32ToFp16(uint3 packed, out float3 a, out float3 b)     { a = f16tof32(packed >> 16); b = f16tof32(packed & 0xFFFF); }
#endif

#endif // __UTILS_HLSLI__
