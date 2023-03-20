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

#include <donut/engine/SceneGraph.h>
#include <nvrhi/nvrhi.h>
#include <rtxdi/RTXDI.h>
#include <memory>
#include <unordered_map>


namespace donut::engine
{
    class CommonRenderPasses;
    class ShaderFactory;
    class ExtendedScene;
    class Light;

}

class RenderTargets;
class RtxdiResources;
class EnvironmentMap;

class PrepareLightsPass
{
private:
    nvrhi::DeviceHandle m_Device;

    nvrhi::ShaderHandle m_ComputeShader;
    nvrhi::ComputePipelineHandle m_ComputePipeline;
    nvrhi::BindingLayoutHandle m_BindingLayout;
    nvrhi::BindingSetHandle m_BindingSet;
    nvrhi::BindingLayoutHandle m_BindlessLayout;

    nvrhi::BufferHandle m_TaskBuffer;
    nvrhi::BufferHandle m_PrimitiveLightBuffer;
    nvrhi::BufferHandle m_LightIndexMappingBuffer;
    nvrhi::BufferHandle m_GeometryInstanceToLightBuffer;
    nvrhi::TextureHandle m_LocalLightPdfTexture;

    std::shared_ptr<EnvironmentMap> m_EnvironmentMap;

    uint32_t m_MaxLightsInBuffer;
    bool m_OddFrame = false;
    
    std::shared_ptr<donut::engine::ShaderFactory> m_ShaderFactory;
    std::shared_ptr<donut::engine::CommonRenderPasses> m_CommonPasses;
    std::shared_ptr<donut::engine::ExtendedScene> m_Scene;

    std::unordered_map<size_t, uint32_t> m_InstanceLightBufferOffsets; // hash(instance*, geometryIndex) -> bufferOffset
    std::unordered_map<const donut::engine::Light*, uint32_t> m_PrimitiveLightBufferOffsets;

public:
    PrepareLightsPass(
        nvrhi::IDevice* device,
        std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
        std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses,
        std::shared_ptr<donut::engine::ExtendedScene> scene,
        nvrhi::IBindingLayout* bindlessLayout);

    void SetScene(std::shared_ptr<donut::engine::ExtendedScene> scene, std::shared_ptr<EnvironmentMap> environmentMap = nullptr);
    void CreatePipeline();
    void CreateBindingSet(RtxdiResources& resources, const RenderTargets& renderTargets);
    void CountLightsInScene(uint32_t& numEmissiveMeshes, uint32_t& numEmissiveTriangles);
    
    void Process(
        nvrhi::ICommandList* commandList, 
        const rtxdi::Context& context, 
        rtxdi::FrameParameters& outFrameParameters);

    nvrhi::TextureHandle GetEnvironmentMap();
};
