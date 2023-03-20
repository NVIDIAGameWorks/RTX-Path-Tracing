/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef SET_SURFACE_DATA_HLSLI
#define SET_SURFACE_DATA_HLSLI

#include "SurfaceData.hlsli"
#include "ShaderParameters.h"

ConstantBuffer<RtxdiBridgeConstants> g_RtxdiBridgeConst     : register(b2 VK_DESCRIPTOR_SET(2));
RWStructuredBuffer<PackedSurfaceData> u_SurfaceData         : register(u21 VK_DESCRIPTOR_SET(2));

// Set the per pixel surface data required for RTXDI at valid surface hits.
void setSurfaceData(const uint2 pixel, const RtxdiSurfaceData sd)
{
    uint bufferIdx = pixel.x + (pixel.y * g_RtxdiBridgeConst.frameDim.x)
        + g_RtxdiBridgeConst.currentSurfaceBufferIdx * g_RtxdiBridgeConst.pixelCount;

    u_SurfaceData[bufferIdx] = sd.pack();
}

// Mark pixel as containing no valid surface data.
void setInvalidSurfaceData(uint2 pixel)
{
    // Compute buffer index based on pixel and currently used surface buffer index.
    uint bufferIdx = pixel.x + (pixel.y * g_RtxdiBridgeConst.frameDim.x)
        + g_RtxdiBridgeConst.currentSurfaceBufferIdx * g_RtxdiBridgeConst.pixelCount;

    // Store invalid surface data.
    u_SurfaceData[bufferIdx] = makeEmptyPackedSurface();
}

#endif // SET_SURFACE_DATA_HLSLI
