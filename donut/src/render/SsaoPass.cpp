/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include <donut/render/SsaoPass.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/render/DrawStrategy.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/ShadowMap.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/View.h>
#include <nvrhi/utils.h>

using namespace donut::math;
#include <donut/shaders/ssao_cb.h>

#include <sstream>
#include <assert.h>

using namespace donut::engine;
using namespace donut::render;


SsaoPass::SsaoPass(
    nvrhi::IDevice* device,
    std::shared_ptr<ShaderFactory> shaderFactory,
    std::shared_ptr<CommonRenderPasses> commonPasses,
    const CreateParameters& params)
    : m_CommonPasses(commonPasses)
    , m_Device(device)
{
    nvrhi::BufferDesc constantBufferDesc;
    constantBufferDesc.byteSize = sizeof(SsaoConstants);
    constantBufferDesc.debugName = "SsaoConstants";
    constantBufferDesc.isConstantBuffer = true;
    constantBufferDesc.isVolatile = true;
    constantBufferDesc.maxVersions = engine::c_MaxRenderPassConstantBufferVersions;
    m_ConstantBuffer = device->createBuffer(constantBufferDesc);

    nvrhi::TextureDesc DeinterleavedTextureDesc;
    DeinterleavedTextureDesc.width = (params.dimensions.x + 3) / 4;
    DeinterleavedTextureDesc.height = (params.dimensions.y + 3) / 4;
    DeinterleavedTextureDesc.arraySize = 16;
    DeinterleavedTextureDesc.dimension = nvrhi::TextureDimension::Texture2DArray;
    DeinterleavedTextureDesc.isUAV = true;
    DeinterleavedTextureDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    DeinterleavedTextureDesc.keepInitialState = true;
    DeinterleavedTextureDesc.debugName = "SSAO/DeinterleavedDepth";
    DeinterleavedTextureDesc.format = nvrhi::Format::R32_FLOAT;
    m_DeinterleavedDepth = device->createTexture(DeinterleavedTextureDesc);

    m_QuantizedGbufferTextureSize = float2(float(DeinterleavedTextureDesc.width), float(DeinterleavedTextureDesc.height)) * 4.f;

    DeinterleavedTextureDesc.debugName = "SSAO/DeinterleavedOcclusion";
    DeinterleavedTextureDesc.format = params.directionalOcclusion ? nvrhi::Format::RGBA16_FLOAT : nvrhi::Format::R8_UNORM;
    m_DeinterleavedOcclusion = device->createTexture(DeinterleavedTextureDesc);
    
    {
        std::vector<engine::ShaderMacro> macros = { 
            { "LINEAR_DEPTH", params.inputLinearDepth ? "1" : "0" }
        };
        m_Deinterleave.Shader = shaderFactory->CreateShader("donut/passes/ssao_deinterleave_cs.hlsl", "main", &macros, nvrhi::ShaderType::Compute);

        nvrhi::BindingLayoutDesc DeinterleaveBindings;
        DeinterleaveBindings.visibility = nvrhi::ShaderType::Compute;
        DeinterleaveBindings.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(0),
        };
        m_Deinterleave.BindingLayout = m_Device->createBindingLayout(DeinterleaveBindings);

        nvrhi::ComputePipelineDesc DeinterleavePipelineDesc;
        DeinterleavePipelineDesc.bindingLayouts = { m_Deinterleave.BindingLayout };
        DeinterleavePipelineDesc.CS = m_Deinterleave.Shader;
        m_Deinterleave.Pipeline = device->createComputePipeline(DeinterleavePipelineDesc);

        m_Deinterleave.BindingSets.resize(params.numBindingSets);
    }

    {
        std::vector<engine::ShaderMacro> macros = { 
            { "OCT_ENCODED_NORMALS", params.octEncodedNormals ? "1" : "0" },
            { "DIRECTIONAL_OCCLUSION", params.directionalOcclusion ? "1" : "0" }
        };
        m_Compute.Shader = shaderFactory->CreateShader("donut/passes/ssao_compute_cs.hlsl", "main", &macros, nvrhi::ShaderType::Compute);

        nvrhi::BindingLayoutDesc ComputeBindings;
        ComputeBindings.visibility = nvrhi::ShaderType::Compute;
        ComputeBindings.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_UAV(0),
        };
        m_Compute.BindingLayout = m_Device->createBindingLayout(ComputeBindings);

        nvrhi::ComputePipelineDesc ComputePipeline;
        ComputePipeline.bindingLayouts = { m_Compute.BindingLayout };
        ComputePipeline.CS = m_Compute.Shader;
        m_Compute.Pipeline = device->createComputePipeline(ComputePipeline);

        m_Compute.BindingSets.resize(params.numBindingSets);
    }

    {
        std::vector<engine::ShaderMacro> macros = {
            { "DIRECTIONAL_OCCLUSION", params.directionalOcclusion ? "1" : "0" }
        };
        m_Blur.Shader = shaderFactory->CreateShader("donut/passes/ssao_blur_cs.hlsl", "main", &macros, nvrhi::ShaderType::Compute);

        nvrhi::BindingLayoutDesc BlurBindings;
        BlurBindings.visibility = nvrhi::ShaderType::Compute;
        BlurBindings.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Sampler(0),
        };
        m_Blur.BindingLayout = m_Device->createBindingLayout(BlurBindings);

        nvrhi::ComputePipelineDesc BlurPipeline;
        BlurPipeline.bindingLayouts = { m_Blur.BindingLayout };
        BlurPipeline.CS = m_Blur.Shader;
        m_Blur.Pipeline = device->createComputePipeline(BlurPipeline);

        m_Blur.BindingSets.resize(params.numBindingSets);
    }
}

// Backwards compatibility constructor
SsaoPass::SsaoPass(
    nvrhi::IDevice* device,
    std::shared_ptr<ShaderFactory> shaderFactory,
    std::shared_ptr<CommonRenderPasses> commonPasses,
    nvrhi::ITexture* gbufferDepth,
    nvrhi::ITexture* gbufferNormals,
    nvrhi::ITexture* destinationTexture)
    : SsaoPass(device, shaderFactory, commonPasses, CreateParameters{ dm::int2(gbufferDepth->getDesc().width, gbufferDepth->getDesc().height), false, false, false, 1 })
{
    const nvrhi::TextureDesc& depthDesc = gbufferDepth->getDesc();
    const nvrhi::TextureDesc& normalsDesc = gbufferNormals->getDesc();
    assert(depthDesc.sampleCount == normalsDesc.sampleCount);
    assert(depthDesc.sampleCount == 1); // more is currently unsupported
    assert(depthDesc.dimension == nvrhi::TextureDimension::Texture2D); // arrays are currently unsupported

    CreateBindingSet(gbufferDepth, gbufferNormals, destinationTexture, 0);
}

void SsaoPass::CreateBindingSet(
    nvrhi::ITexture* gbufferDepth,
    nvrhi::ITexture* gbufferNormals,
    nvrhi::ITexture* destinationTexture,
    int bindingSetIndex)
{
    nvrhi::BindingSetDesc DeinterleaveBindings;
    DeinterleaveBindings.bindings = {
        nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
        nvrhi::BindingSetItem::Texture_SRV(0, gbufferDepth),
        nvrhi::BindingSetItem::Texture_UAV(0, m_DeinterleavedDepth)
    };
    m_Deinterleave.BindingSets[bindingSetIndex] = m_Device->createBindingSet(DeinterleaveBindings, m_Deinterleave.BindingLayout);
    
    nvrhi::BindingSetDesc ComputeBindings;
    ComputeBindings.bindings = {
        nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
        nvrhi::BindingSetItem::Texture_SRV(0, m_DeinterleavedDepth),
        nvrhi::BindingSetItem::Texture_SRV(1, gbufferNormals),
        nvrhi::BindingSetItem::Texture_UAV(0, m_DeinterleavedOcclusion)
    };
    m_Compute.BindingSets[bindingSetIndex] = m_Device->createBindingSet(ComputeBindings, m_Compute.BindingLayout);

    nvrhi::BindingSetDesc BlurBindings;
    BlurBindings.bindings = {
        nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
        nvrhi::BindingSetItem::Texture_SRV(0, m_DeinterleavedDepth),
        nvrhi::BindingSetItem::Texture_SRV(1, m_DeinterleavedOcclusion),
        nvrhi::BindingSetItem::Texture_UAV(0, destinationTexture),
        nvrhi::BindingSetItem::Sampler(0, m_CommonPasses->m_PointClampSampler)
    };
    m_Blur.BindingSets[bindingSetIndex] = m_Device->createBindingSet(BlurBindings, m_Blur.BindingLayout);
}

void SsaoPass::Render(
    nvrhi::ICommandList* commandList,
    const SsaoParameters& params,
    const ICompositeView& compositeView,
    int bindingSetIndex)
{
    assert(m_Deinterleave.BindingSets[bindingSetIndex]);
    assert(m_Compute.BindingSets[bindingSetIndex]);
    assert(m_Blur.BindingSets[bindingSetIndex]);

    commandList->beginMarker("SSAO");

    for (uint viewIndex = 0; viewIndex < compositeView.GetNumChildViews(ViewType::PLANAR); viewIndex++)
    {
        const IView* view = compositeView.GetChildView(ViewType::PLANAR, viewIndex);

        const float4x4 projectionMatrix = view->GetProjectionMatrix(false);

        nvrhi::Rect viewExtent = view->GetViewExtent();
        nvrhi::Rect quarterResExtent = viewExtent;
        quarterResExtent.minX /= 4;
        quarterResExtent.minY /= 4;
        quarterResExtent.maxX = (quarterResExtent.maxX + 3) / 4;
        quarterResExtent.maxY = (quarterResExtent.maxY + 3) / 4;

        SsaoConstants ssaoConstants = {};
        view->FillPlanarViewConstants(ssaoConstants.view);

        ssaoConstants.clipToView = float2(
            projectionMatrix[2].w / projectionMatrix[0].x,
            projectionMatrix[2].w / projectionMatrix[1].y);
        ssaoConstants.invQuantizedGbufferSize = 1.f / m_QuantizedGbufferTextureSize;
        ssaoConstants.quantizedViewportOrigin = int2(quarterResExtent.minX, quarterResExtent.minY) * 4;
        ssaoConstants.amount = params.amount;
        ssaoConstants.invBackgroundViewDepth = (params.backgroundViewDepth > 0.f) ? 1.f / params.backgroundViewDepth : 0.f;
        ssaoConstants.radiusWorld = params.radiusWorld;
        ssaoConstants.surfaceBias = params.surfaceBias;
        ssaoConstants.powerExponent = params.powerExponent;
        ssaoConstants.radiusToScreen = 0.5f * ssaoConstants.view.viewportSize.y * abs(projectionMatrix[1].y);
        commandList->writeBuffer(m_ConstantBuffer, &ssaoConstants, sizeof(ssaoConstants));

        uint32_t dispatchWidth = (quarterResExtent.width() + 7) / 8;
        uint32_t dispatchHeight = (quarterResExtent.height() + 7) / 8;

        nvrhi::ComputeState state;
        state.pipeline = m_Deinterleave.Pipeline;
        state.bindings = { m_Deinterleave.BindingSets[bindingSetIndex] };
        commandList->setComputeState(state);
        commandList->dispatch(dispatchWidth, dispatchHeight, 1);

        state.pipeline = m_Compute.Pipeline;
        state.bindings = { m_Compute.BindingSets[bindingSetIndex] };
        commandList->setComputeState(state);
        commandList->dispatch(dispatchWidth, dispatchHeight, 16);

        dispatchWidth = (viewExtent.width() + 15) / 16;
        dispatchHeight = (viewExtent.height() + 15) / 16;

        state.pipeline = m_Blur.Pipeline;
        state.bindings = { m_Blur.BindingSets[bindingSetIndex] };
        commandList->setComputeState(state);
        commandList->dispatch(dispatchWidth, dispatchHeight, 1);
    }

    commandList->endMarker();
}