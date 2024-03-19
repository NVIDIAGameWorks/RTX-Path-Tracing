/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "AccumulationPass.h"

#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <donut/core/log.h>

using namespace donut::math;
#include "RTXDI/ShaderParameters.h"

using namespace donut::engine;


AccumulationPass::AccumulationPass(
    nvrhi::IDevice* device, 
    std::shared_ptr<ShaderFactory> shaderFactory)
    : m_Device(device)
    , m_ShaderFactory(shaderFactory)
{
    nvrhi::BindingLayoutDesc bindingLayoutDesc;
    bindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
    bindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::Texture_SRV(0),
        nvrhi::BindingLayoutItem::Texture_UAV(0),
        nvrhi::BindingLayoutItem::Sampler(0),
        nvrhi::BindingLayoutItem::PushConstants(0, sizeof(AccumulationConstants))
    };

    m_BindingLayout = m_Device->createBindingLayout(bindingLayoutDesc);

    auto samplerDesc = nvrhi::SamplerDesc()
        .setAllFilters(true);

    m_Sampler = m_Device->createSampler(samplerDesc);
}

void AccumulationPass::CreatePipeline()
{
    m_ComputeShader = m_ShaderFactory->CreateShader("app/AccumulationPass.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);

    nvrhi::ComputePipelineDesc pipelineDesc;
    pipelineDesc.bindingLayouts = { m_BindingLayout };
    pipelineDesc.CS = m_ComputeShader;
    m_ComputePipeline = m_Device->createComputePipeline(pipelineDesc);
}

void AccumulationPass::CreateBindingSet(nvrhi::ITexture* inputTexture, nvrhi::ITexture* outputTexture)
{
    nvrhi::BindingSetDesc bindingSetDesc;

    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::Texture_SRV(0, inputTexture),
        nvrhi::BindingSetItem::Texture_UAV(0, outputTexture),
        nvrhi::BindingSetItem::Sampler(0, m_Sampler),
        nvrhi::BindingSetItem::PushConstants(0, sizeof(AccumulationConstants))
    };

    m_BindingSet = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);

    m_CompositedColor = outputTexture;
}

void AccumulationPass::Render(
    nvrhi::ICommandList* commandList,
    const donut::engine::IView& sourceView,
    const donut::engine::IView& upscaledView,
    float accumulationWeight)
{
    commandList->beginMarker("Accumulation");

    const auto sourceViewport = sourceView.GetViewportState().viewports[0];
    const auto upscaledViewport = upscaledView.GetViewportState().viewports[0];

    const auto& inputDesc = m_CompositedColor->getDesc();

    AccumulationConstants constants = {};
    constants.inputSize = float2(sourceViewport.width(), sourceViewport.height());
    constants.inputTextureSizeInv = float2(1.f / float(inputDesc.width), 1.f / float(inputDesc.height));
    constants.outputSize = float2(upscaledViewport.width(), upscaledViewport.height());
    constants.pixelOffset = sourceView.GetPixelOffset();
    constants.blendFactor = accumulationWeight;

    nvrhi::ComputeState state;
    state.bindings = { m_BindingSet };
    state.pipeline = m_ComputePipeline;
    commandList->setComputeState(state);

    commandList->setPushConstants(&constants, sizeof(constants));
    
    commandList->dispatch(
        dm::div_ceil(upscaledView.GetViewExtent().width(), 8), 
        dm::div_ceil(upscaledView.GetViewExtent().height(), 8), 
        1);

    commandList->endMarker();
}
