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

#define WITH_NRD 1

#if WITH_NRD

#include <NRD.h>
#include <nvrhi/nvrhi.h>
#include <unordered_map>
#include <donut/engine/BindingCache.h>

class RenderTargets;

namespace donut::engine
{
    class PlanarView;
    class ShaderFactory;
}

class NrdIntegration
{
private:
    nvrhi::DeviceHandle m_Device;
    bool m_Initialized;
    nrd::Instance* m_Instance;
    nrd::Denoiser m_Denoiser;
    nrd::Identifier m_Identifier;

    struct NrdPipeline
    {
        nvrhi::ShaderHandle Shader;
        nvrhi::BindingLayoutHandle BindingLayout;
        nvrhi::ComputePipelineHandle Pipeline;
    };

    nvrhi::BufferHandle m_ConstantBuffer;
    std::vector<NrdPipeline> m_Pipelines;
    std::vector<nvrhi::SamplerHandle> m_Samplers;
    std::vector<nvrhi::TextureHandle> m_PermanentTextures;
    std::vector<nvrhi::TextureHandle> m_TransientTextures;
    donut::engine::BindingCache m_BindingCache;
public:
    NrdIntegration(nvrhi::IDevice* device, nrd::Denoiser method);
    ~NrdIntegration();

    bool Initialize(uint32_t width, uint32_t height, donut::engine::ShaderFactory& shaderFactory);
    bool IsAvailable() const;

    void RunDenoiserPasses(
        nvrhi::ICommandList* commandList,
        const RenderTargets& renderTargets,
        int pass,
        const donut::engine::PlanarView& view, 
        const donut::engine::PlanarView& viewPrev,
        uint32_t frameIndex,
        float disocclusionThreshold,
        float disocclusionThresholdAlternate,
        bool useDisocclusionThresholdAlternateMix,
        float timeDeltaBetweenFrames, // < 0 to track internally in NRD
        bool enableValidation,
        const void* methodSettings);

    const nrd::Denoiser GetDenoiser() const { return m_Denoiser; }
};

#endif