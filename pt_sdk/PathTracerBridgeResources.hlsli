/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __BRIDGE_RESOURCES_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __BRIDGE_RESOURCES_HLSLI__

#include <donut/shaders/bindless.h>
#include <donut/shaders/vulkan.hlsli>

#include "SampleConstantBuffer.h"
#include "ShaderResourceBindings.hlsli"

RaytracingAccelerationStructure SceneBVH : register(t0);
StructuredBuffer<SubInstanceData> t_SubInstanceData         : register(t1);
StructuredBuffer<InstanceData> t_InstanceData               : register(t2);
StructuredBuffer<GeometryData> t_GeometryData               : register(t3);
StructuredBuffer<GeometryDebugData> t_GeometryDebugData     : register(t4);
StructuredBuffer<MaterialConstants> t_MaterialConstants     : register(t5);
Texture2D<float4> t_EnvironmentMap                          : register(t6);
Texture2D<float> t_ImportanceMap                            : register(t7);

RWTexture2D<float4> u_RtxdiOutDirectionValid				: register(u2);
RWTexture2D<float4> u_RtxdiLiDistance						: register(u3);

SamplerState s_MaterialSampler                              : register(s0);
SamplerState s_EnvironmentMapSampler                        : register(s1);
SamplerState s_ImportanceSampler                            : register(s2);

VK_BINDING(0, 1) ByteAddressBuffer t_BindlessBuffers[]      : register(t0, space1);
VK_BINDING(1, 1) Texture2D t_BindlessTextures[]             : register(t0, space2);

#endif //__BRIDGE_RESOURCES_HLSLI__
