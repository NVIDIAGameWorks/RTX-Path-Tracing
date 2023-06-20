/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#include <nvrhi/nvrhi.h>

namespace rtxdi
{
    class Context;
}

class RtxdiResources
{
private:
    bool m_NeighborOffsetsInitialized = false;
    uint32_t m_MaxEmissiveMeshes = 0;
    uint32_t m_MaxEmissiveTriangles = 0;
    uint32_t m_MaxPrimitiveLights = 0;
    uint32_t m_MaxGeometryInstances = 0;

public:
    nvrhi::BufferHandle TaskBuffer;
    nvrhi::BufferHandle PrimitiveLightBuffer;
    nvrhi::BufferHandle LightDataBuffer;
    nvrhi::BufferHandle GeometryInstanceToLightBuffer;
    nvrhi::BufferHandle LightIndexMappingBuffer;
    nvrhi::BufferHandle RisBuffer;
    nvrhi::BufferHandle RisLightDataBuffer;
    nvrhi::BufferHandle NeighborOffsetsBuffer;
    nvrhi::BufferHandle LightReservoirBuffer;
    nvrhi::BufferHandle GIReservoirBuffer;
    nvrhi::TextureHandle EnvironmentPdfTexture;
    nvrhi::TextureHandle LocalLightPdfTexture;

    RtxdiResources(
        nvrhi::IDevice* device, 
        const rtxdi::Context& context,
        uint32_t maxEmissiveMeshes,
        uint32_t maxEmissiveTriangles,
        uint32_t maxPrimitiveLights,
        uint32_t maxGeometryInstances,
        uint32_t environmentMapWidth,
        uint32_t environmentMapHeight);

    void InitializeNeighborOffsets(nvrhi::ICommandList* commandList, const rtxdi::Context& context);

    uint32_t GetMaxEmissiveMeshes() const { return m_MaxEmissiveMeshes; }
    uint32_t GetMaxEmissiveTriangles() const { return m_MaxEmissiveTriangles; }
    uint32_t GetMaxPrimitiveLights() const { return m_MaxPrimitiveLights; }
    uint32_t GetMaxGeometryInstances() const { return m_MaxGeometryInstances; }

    static constexpr uint32_t c_NumReservoirBuffers = 3;
    static constexpr uint32_t c_NumGIReservoirBuffers = 2;

};