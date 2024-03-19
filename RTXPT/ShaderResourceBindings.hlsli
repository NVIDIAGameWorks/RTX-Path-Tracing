/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __SHADER_RESOURCE_BINDINGS_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __SHADER_RESOURCE_BINDINGS_HLSLI__

#include "SampleConstantBuffer.h"
#include "RTXDI/ShaderParameters.h"
#include <donut/shaders/vulkan.hlsli> // for VK_PUSH_CONSTANT

ConstantBuffer<SampleConstants>         g_Const                         : register(b0);
VK_PUSH_CONSTANT ConstantBuffer<SampleMiniConstants> g_MiniConst        : register(b1);

// All outputs are defined here
RWTexture2D<float4>                     u_Output                        : register(u0); // main HDR output

RWTexture2D<uint>                       u_Throughput                    : register(u4); // used by RTXDI, etc. Packed as R11G11B10_FLOAT
RWTexture2D<float4>                     u_MotionVectors                 : register(u5); // used by RTXDI, DLSS/TAA, etc.
RWTexture2D<float>                      u_Depth                         : register(u6); // used by RTXDI, DLSS/TAA, etc.

RWTexture2DArray<uint>                  u_StablePlanesHeader            : register(u40);
RWStructuredBuffer<StablePlane>         u_StablePlanesBuffer            : register(u42);
RWTexture2D<float4>                     u_StableRadiance                : register(u44);
RWStructuredBuffer<PackedPathTracerSurfaceData> u_SurfaceData           : register(u45);

// this is for debugging viz
RWTexture2D<float4>                     u_DebugVizOutput                : register(u50);
RWStructuredBuffer<DebugFeedbackStruct> u_FeedbackBuffer                : register(u51);
RWStructuredBuffer<DebugLineStruct>     u_DebugLinesBuffer              : register(u52);
RWStructuredBuffer<DeltaTreeVizPathVertex> u_DebugDeltaPathTree         : register(u53);
RWStructuredBuffer<PathPayload>         u_DeltaPathSearchStack          : register(u54);

// ReSTIR GI resources
RWTexture2D<float4>                     u_SecondarySurfacePositionNormal: register(u60);
RWTexture2D<float4>                     u_SecondarySurfaceRadiance      : register(u61);

// RTXDI resources (used for Local Lights sampling - some duplication with RtxdiApplicationBridge.hlsli - to be refactored/fixed in the future)
ConstantBuffer<RtxdiBridgeConstants>    g_LL_RtxdiBridgeConst           : register(b2);
RWBuffer<uint4>                         u_LL_RisLightDataBuffer         : register(u62);    // convert to texture?
RWBuffer<uint2>                         u_LL_RisBuffer                  : register(u63);    // convert to texture?
StructuredBuffer<PolymorphicLightInfo>  t_LL_LightDataBuffer            : register(t62);

#endif // #ifndef __SHADER_RESOURCE_BINDINGS_HLSLI__