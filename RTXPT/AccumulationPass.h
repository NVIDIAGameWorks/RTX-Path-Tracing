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
#include <memory>

namespace donut::engine
{
    class ShaderFactory;
    class IView;
}

class AccumulationPass
{
private:
    nvrhi::DeviceHandle m_Device;

    nvrhi::ShaderHandle m_ComputeShader;
    nvrhi::ComputePipelineHandle m_ComputePipeline;
    nvrhi::BindingLayoutHandle m_BindingLayout;
    nvrhi::BindingSetHandle m_BindingSet;
    nvrhi::SamplerHandle m_Sampler;
    nvrhi::TextureHandle m_CompositedColor;

    std::shared_ptr<donut::engine::ShaderFactory> m_ShaderFactory;

public:
    AccumulationPass(
        nvrhi::IDevice* device,
        std::shared_ptr<donut::engine::ShaderFactory> shaderFactory);

    void CreatePipeline();

    void CreateBindingSet(nvrhi::ITexture* inputTexture, nvrhi::ITexture* outputTexture);

    void Render(
        nvrhi::ICommandList* commandList,
        const donut::engine::IView& sourceView,
        const donut::engine::IView& upscaledView,
        float accumulationWeight);
};
