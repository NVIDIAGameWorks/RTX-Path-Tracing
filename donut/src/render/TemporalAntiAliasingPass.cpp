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

#include <donut/render/TemporalAntiAliasingPass.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/View.h>

using namespace donut::math;
#include <donut/shaders/taa_cb.h>

#include <nvrhi/utils.h>

#include <assert.h>
#include <random>

using namespace donut::engine;
using namespace donut::render;

TemporalAntiAliasingPass::TemporalAntiAliasingPass(
    nvrhi::IDevice* device,
    std::shared_ptr<ShaderFactory> shaderFactory, 
    std::shared_ptr<CommonRenderPasses> commonPasses,
    const ICompositeView& compositeView,
    const CreateParameters& params)
    : m_CommonPasses(commonPasses)
    , m_FrameIndex(0)
    , m_StencilMask(params.motionVectorStencilMask)
    , m_R2Jitter(0.0f, 0.0f)
    , m_Jitter(TemporalAntiAliasingJitter::MSAA)
{
    const IView* sampleView = compositeView.GetChildView(ViewType::PLANAR, 0);

    const nvrhi::TextureDesc& unresolvedColorDesc = params.unresolvedColor->getDesc();
    const nvrhi::TextureDesc& resolvedColorDesc = params.resolvedColor->getDesc();
    const nvrhi::TextureDesc& feedback1Desc = params.feedback1->getDesc();
    const nvrhi::TextureDesc& feedback2Desc = params.feedback2->getDesc();

    assert(feedback1Desc.width == feedback2Desc.width);
    assert(feedback1Desc.height == feedback2Desc.height);
    assert(feedback1Desc.format == feedback2Desc.format);
    assert(feedback1Desc.isUAV);
    assert(feedback2Desc.isUAV);
    assert(resolvedColorDesc.isUAV);

    bool useStencil = false;
    nvrhi::Format stencilFormat = nvrhi::Format::UNKNOWN;
    if (params.motionVectorStencilMask)
    {
        useStencil = true;

        nvrhi::Format depthFormat = params.sourceDepth->getDesc().format;

        if (depthFormat == nvrhi::Format::D24S8)
            stencilFormat = nvrhi::Format::X24G8_UINT;
        else if (depthFormat == nvrhi::Format::D32S8)
            stencilFormat = nvrhi::Format::X32G8_UINT;
        else
            assert(!"the format of sourceDepth texture doesn't have a stencil plane");
    }

    std::vector<ShaderMacro> MotionVectorMacros;
    MotionVectorMacros.push_back(ShaderMacro("USE_STENCIL", useStencil ? "1" : "0"));
    m_MotionVectorPS = shaderFactory->CreateShader("donut/passes/motion_vectors_ps.hlsl", "main", &MotionVectorMacros, nvrhi::ShaderType::Pixel);
    
    std::vector<ShaderMacro> ResolveMacros;
    ResolveMacros.push_back(ShaderMacro("SAMPLE_COUNT", std::to_string(unresolvedColorDesc.sampleCount)));
    ResolveMacros.push_back(ShaderMacro("USE_CATMULL_ROM_FILTER", params.useCatmullRomFilter ? "1" : "0"));
    m_TemporalAntiAliasingCS = shaderFactory->CreateShader("donut/passes/taa_cs.hlsl", "main", &ResolveMacros, nvrhi::ShaderType::Compute);

    nvrhi::SamplerDesc samplerDesc;
    samplerDesc.addressU = samplerDesc.addressV = samplerDesc.addressW = nvrhi::SamplerAddressMode::Border;
    samplerDesc.borderColor = nvrhi::Color(0.0f);
    m_BilinearSampler = device->createSampler(samplerDesc);

    m_ResolvedColorSize = float2(float(resolvedColorDesc.width), float(resolvedColorDesc.height));

    nvrhi::BufferDesc constantBufferDesc;
    constantBufferDesc.byteSize = sizeof(TemporalAntiAliasingConstants);
    constantBufferDesc.debugName = "TemporalAntiAliasingConstants";
    constantBufferDesc.isConstantBuffer = true;
    constantBufferDesc.isVolatile = true;
    constantBufferDesc.maxVersions = params.numConstantBufferVersions;
    m_TemporalAntiAliasingCB = device->createBuffer(constantBufferDesc);

    if(params.sourceDepth)
    {
        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Pixel;
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0)
        };

        if (useStencil)
        {
            layoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::Texture_SRV(1));
        }

        m_MotionVectorsBindingLayout = device->createBindingLayout(layoutDesc);

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_TemporalAntiAliasingCB),
            nvrhi::BindingSetItem::Texture_SRV(0, params.sourceDepth),
        };
        if (useStencil)
        {
            bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(1, params.sourceDepth, stencilFormat));
        }
        m_MotionVectorsBindingSet = device->createBindingSet(bindingSetDesc, m_MotionVectorsBindingLayout);

        m_MotionVectorsFramebufferFactory = std::make_unique<FramebufferFactory>(device);
        m_MotionVectorsFramebufferFactory->RenderTargets = { params.motionVectors };

        nvrhi::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.primType = nvrhi::PrimitiveType::TriangleStrip;
        pipelineDesc.VS = m_CommonPasses->m_FullscreenVS;
        pipelineDesc.PS = m_MotionVectorPS;
        pipelineDesc.bindingLayouts = { m_MotionVectorsBindingLayout };

        pipelineDesc.renderState.rasterState.setCullNone();
        pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
        pipelineDesc.renderState.depthStencilState.stencilEnable = false;

        nvrhi::IFramebuffer* sampleFramebuffer = m_MotionVectorsFramebufferFactory->GetFramebuffer(*sampleView);
        m_MotionVectorsPso = device->createGraphicsPipeline(pipelineDesc, sampleFramebuffer);
    }

    {
        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_TemporalAntiAliasingCB),
            nvrhi::BindingSetItem::Sampler(0, m_BilinearSampler),
            nvrhi::BindingSetItem::Texture_SRV(0, params.unresolvedColor),
            nvrhi::BindingSetItem::Texture_SRV(1, params.motionVectors),
            nvrhi::BindingSetItem::Texture_SRV(2, params.feedback1),
            nvrhi::BindingSetItem::Texture_UAV(0, params.resolvedColor),
            nvrhi::BindingSetItem::Texture_UAV(1, params.feedback2)
        };
        
        m_HasHistoryClampRelaxTexture = params.historyClampRelax!=nullptr;
        if (params.historyClampRelax!=nullptr)
            bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(3, params.historyClampRelax));

        nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::Compute, 0, bindingSetDesc, m_ResolveBindingLayout, m_ResolveBindingSet);
     
        // Swap resolvedColor and resolvedColorPrevious (t2 and u0)
        bindingSetDesc.bindings[4].resourceHandle = params.feedback2;
        bindingSetDesc.bindings[6].resourceHandle = params.feedback1;
        m_ResolveBindingSetPrevious = device->createBindingSet(bindingSetDesc, m_ResolveBindingLayout);

        nvrhi::ComputePipelineDesc pipelineDesc;
        pipelineDesc.CS = m_TemporalAntiAliasingCS;
        pipelineDesc.bindingLayouts = { m_ResolveBindingLayout };

        m_ResolvePso = device->createComputePipeline(pipelineDesc);
    }
}

void TemporalAntiAliasingPass::RenderMotionVectors(
    nvrhi::ICommandList* commandList,
    const ICompositeView& compositeView,
    const ICompositeView& compositeViewPrevious,
    dm::float3 preViewTranslationDifference)
{
    assert(compositeView.GetNumChildViews(ViewType::PLANAR) == compositeViewPrevious.GetNumChildViews(ViewType::PLANAR));
    assert(m_MotionVectorsPso);

    commandList->beginMarker("MotionVectors");

    for (uint viewIndex = 0; viewIndex < compositeView.GetNumChildViews(ViewType::PLANAR); viewIndex++)
    {
        const IView* view = compositeView.GetChildView(ViewType::PLANAR, viewIndex);
        const IView* viewPrevious = compositeViewPrevious.GetChildView(ViewType::PLANAR, viewIndex);

        const nvrhi::ViewportState viewportState = view->GetViewportState();
        
        // This pass only works for planar, single-viewport views
        assert(viewportState.viewports.size() == 1);

        const nvrhi::Viewport& inputViewport = viewportState.viewports[0];

        TemporalAntiAliasingConstants taaConstants = {};
        affine3 viewReprojection = inverse(view->GetViewMatrix()) * translation(-preViewTranslationDifference) * viewPrevious->GetViewMatrix();
        taaConstants.reprojectionMatrix = inverse(view->GetProjectionMatrix(false)) * affineToHomogeneous(viewReprojection) * viewPrevious->GetProjectionMatrix(false);
        taaConstants.inputViewOrigin = float2(inputViewport.minX, inputViewport.minY);
        taaConstants.inputViewSize = float2(inputViewport.width(), inputViewport.height());
        taaConstants.stencilMask = m_StencilMask;
        commandList->writeBuffer(m_TemporalAntiAliasingCB, &taaConstants, sizeof(taaConstants));

        nvrhi::GraphicsState state;
        state.pipeline = m_MotionVectorsPso;
        state.framebuffer = m_MotionVectorsFramebufferFactory->GetFramebuffer(*view);
        state.bindings = { m_MotionVectorsBindingSet};
        state.viewport = viewportState;
        commandList->setGraphicsState(state);

        nvrhi::DrawArguments args;
        args.instanceCount = 1;
        args.vertexCount = 4;
        commandList->draw(args);
    }

    commandList->endMarker();
}

void TemporalAntiAliasingPass::TemporalResolve(
    nvrhi::ICommandList* commandList,
    const TemporalAntiAliasingParameters& params,
    bool feedbackIsValid,
    const ICompositeView& compositeViewInput,
    const ICompositeView& compositeViewOutput)
{
    assert(compositeViewInput.GetNumChildViews(ViewType::PLANAR) == compositeViewOutput.GetNumChildViews(ViewType::PLANAR));
    
    commandList->beginMarker("TemporalAA");

    for (uint viewIndex = 0; viewIndex < compositeViewInput.GetNumChildViews(ViewType::PLANAR); viewIndex++)
    {
        const IView* viewInput = compositeViewInput.GetChildView(ViewType::PLANAR, viewIndex);
        const IView* viewOutput = compositeViewOutput.GetChildView(ViewType::PLANAR, viewIndex);

        const nvrhi::Viewport viewportInput = viewInput->GetViewportState().viewports[0];
        const nvrhi::Viewport viewportOutput = viewOutput->GetViewportState().viewports[0];
        
        TemporalAntiAliasingConstants taaConstants = {};
        const float marginSize = 1.f;
        taaConstants.inputViewOrigin = float2(viewportInput.minX, viewportInput.minY);
        taaConstants.inputViewSize = float2(viewportInput.width(), viewportInput.height());
        taaConstants.outputViewOrigin = float2(viewportOutput.minX, viewportOutput.minY);
        taaConstants.outputViewSize = float2(viewportOutput.width(), viewportOutput.height());
        taaConstants.inputPixelOffset = viewInput->GetPixelOffset();
        taaConstants.outputTextureSizeInv = 1.0f / m_ResolvedColorSize;
        taaConstants.inputOverOutputViewSize = taaConstants.inputViewSize / taaConstants.outputViewSize;
        taaConstants.outputOverInputViewSize = taaConstants.outputViewSize / taaConstants.inputViewSize;
        taaConstants.clampingFactor = params.enableHistoryClamping ? params.clampingFactor : -1.f;
        taaConstants.newFrameWeight = feedbackIsValid ? params.newFrameWeight : 1.f;
        taaConstants.pqC = dm::clamp(params.maxRadiance, 1e-4f, 1e8f);
        taaConstants.invPqC = 1.f / taaConstants.pqC;
        taaConstants.useHistoryClampRelax = (params.useHistoryClampRelax && m_HasHistoryClampRelaxTexture)?1:0;
        commandList->writeBuffer(m_TemporalAntiAliasingCB, &taaConstants, sizeof(taaConstants));

        int2 viewportSize = int2(taaConstants.outputViewSize);
        int2 gridSize = (viewportSize + 15) / 16;

        nvrhi::ComputeState state;
        state.pipeline = m_ResolvePso;
        state.bindings = { m_ResolveBindingSet };
        commandList->setComputeState(state);

        commandList->dispatch(gridSize.x, gridSize.y, 1);
    }

    commandList->endMarker();
}

void TemporalAntiAliasingPass::AdvanceFrame()
{
    m_FrameIndex++;

    std::swap(m_ResolveBindingSet, m_ResolveBindingSetPrevious);

    if (m_Jitter == TemporalAntiAliasingJitter::R2)
    {
        // Advance R2 jitter sequence
        // http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/

        static const float g = 1.32471795724474602596f;
        static const float a1 = 1.0f / g;
        static const float a2 = 1.0f / (g * g);
        m_R2Jitter[0] = fmodf(m_R2Jitter[0] + a1, 1.0f);
        m_R2Jitter[1] = fmodf(m_R2Jitter[1] + a2, 1.0f);
    }
}

static float VanDerCorput(size_t base, size_t index)
{
    float ret = 0.0f;
    float denominator = float(base);
    while (index > 0)
    {
        size_t multiplier = index % base;
        ret += float(multiplier) / denominator;
        index = index / base;
        denominator *= base;
    }
    return ret;
}

dm::float2 TemporalAntiAliasingPass::GetCurrentPixelOffset()
{
    switch (m_Jitter)
    {
        default:
        case TemporalAntiAliasingJitter::MSAA:
        {
            const float2 offsets[] = {
                float2(0.0625f, -0.1875f), float2(-0.0625f, 0.1875f), float2(0.3125f, 0.0625f), float2(-0.1875f, -0.3125f),
                float2(-0.3125f, 0.3125f), float2(-0.4375f, 0.0625f), float2(0.1875f, 0.4375f), float2(0.4375f, -0.4375f)
            };

            return offsets[m_FrameIndex % 8];
        }
        case TemporalAntiAliasingJitter::Halton:
        {
            uint32_t index = (m_FrameIndex % 16) + 1;
            return float2{ VanDerCorput(2, index), VanDerCorput(3, index) } - 0.5f;
        }
        case TemporalAntiAliasingJitter::R2:
        {
            return m_R2Jitter - 0.5f;
        }
        case TemporalAntiAliasingJitter::WhiteNoise:
        {
            std::mt19937 rng(m_FrameIndex);
            std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
            return float2{ dist(rng), dist(rng) };
        }
    }
}

void donut::render::TemporalAntiAliasingPass::SetJitter(TemporalAntiAliasingJitter jitter)
{
    m_Jitter = jitter;
}
