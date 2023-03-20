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

#include <donut/render/ToneMappingPasses.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/View.h>
#include <sstream>
#include <assert.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/core/log.h>

using namespace donut::math;
#include <donut/shaders/tonemapping_cb.h>

using namespace donut::engine;
using namespace donut::render;

ToneMappingPass::ToneMappingPass(
    nvrhi::IDevice* device,
    std::shared_ptr<ShaderFactory> shaderFactory,
    std::shared_ptr<CommonRenderPasses> commonPasses,
    std::shared_ptr<FramebufferFactory> framebufferFactory,
    const ICompositeView& compositeView,
    const CreateParameters& params)
    : m_Device(device)
    , m_CommonPasses(commonPasses)
    , m_HistogramBins(params.histogramBins)
    , m_FramebufferFactory(framebufferFactory)
{
    assert(params.histogramBins <= 256);

    const IView* sampleView = compositeView.GetChildView(ViewType::PLANAR, 0);
    nvrhi::IFramebuffer* sampleFramebuffer = m_FramebufferFactory->GetFramebuffer(*sampleView);

    {
        std::vector<ShaderMacro> Macros;

        std::stringstream ss;
        ss << params.histogramBins;
        Macros.push_back(ShaderMacro("HISTOGRAM_BINS", ss.str()));
        Macros.push_back(ShaderMacro("SOURCE_ARRAY", params.isTextureArray ? "1" : "0"));

        m_HistogramComputeShader = shaderFactory->CreateShader("donut/passes/histogram_cs.hlsl", "main", &Macros, nvrhi::ShaderType::Compute);
        m_ExposureComputeShader = shaderFactory->CreateShader("donut/passes/exposure_cs.hlsl", "main", &Macros, nvrhi::ShaderType::Compute);
        m_PixelShader = shaderFactory->CreateShader("donut/passes/tonemapping_ps.hlsl", "main", &Macros, nvrhi::ShaderType::Pixel);
    }

    nvrhi::BufferDesc constantBufferDesc;
    constantBufferDesc.byteSize = sizeof(ToneMappingConstants);
    constantBufferDesc.debugName = "ToneMappingConstants";
    constantBufferDesc.isConstantBuffer = true;
    constantBufferDesc.isVolatile = true;
    constantBufferDesc.maxVersions = params.numConstantBufferVersions;
    m_ToneMappingCB = device->createBuffer(constantBufferDesc);

    nvrhi::BufferDesc storageBufferDesc;
    storageBufferDesc.byteSize = sizeof(uint) * m_HistogramBins;
    storageBufferDesc.format = nvrhi::Format::R32_UINT;
    storageBufferDesc.canHaveUAVs = true;
    storageBufferDesc.debugName = "HistogramBuffer";
    storageBufferDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
    storageBufferDesc.keepInitialState = true;
    storageBufferDesc.canHaveTypedViews = true;
    m_HistogramBuffer = device->createBuffer(storageBufferDesc);

    if (params.exposureBufferOverride)
    {
        m_ExposureBuffer = params.exposureBufferOverride;
    }
    else
    {
        storageBufferDesc.byteSize = sizeof(uint);
        storageBufferDesc.format = nvrhi::Format::R32_UINT;
        storageBufferDesc.debugName = "ExposureBuffer";
        m_ExposureBuffer = device->createBuffer(storageBufferDesc);
    }

    m_ColorLUT = commonPasses->m_BlackTexture;

    if (params.colorLUT)
    {
        const nvrhi::TextureDesc& desc = params.colorLUT->getDesc();
        m_ColorLUTSize = float(desc.height);

        if (desc.width != desc.height * desc.height)
        {
            log::error("Color LUT texture size must be: width = (n*n), height = (n)");
            m_ColorLUTSize = 0.f;
        }
        else
        {
            m_ColorLUT = params.colorLUT;
        }
    }

    {
        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Compute;
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::TypedBuffer_UAV(0)
        };
        m_HistogramBindingLayout = device->createBindingLayout(layoutDesc);

        nvrhi::ComputePipelineDesc computePipelineDesc;
        computePipelineDesc.CS = m_HistogramComputeShader;
        computePipelineDesc.bindingLayouts = { m_HistogramBindingLayout };
        m_HistogramPso = device->createComputePipeline(computePipelineDesc);
    }

    {
        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Compute;
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::TypedBuffer_SRV(0),
            nvrhi::BindingLayoutItem::TypedBuffer_UAV(0)
        };
        m_ExposureBindingLayout = device->createBindingLayout(layoutDesc);

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ToneMappingCB),
            nvrhi::BindingSetItem::TypedBuffer_SRV(0, m_HistogramBuffer),
            nvrhi::BindingSetItem::TypedBuffer_UAV(0, m_ExposureBuffer)
        };
        m_ExposureBindingSet = device->createBindingSet(bindingSetDesc, m_ExposureBindingLayout);

        nvrhi::ComputePipelineDesc computePipelineDesc;
        computePipelineDesc.CS = m_ExposureComputeShader;
        computePipelineDesc.bindingLayouts = { m_ExposureBindingLayout };
        m_ExposurePso = device->createComputePipeline(computePipelineDesc);
    }

    {
        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Pixel;
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::TypedBuffer_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Sampler(0)
        };
        m_RenderBindingLayout = device->createBindingLayout(layoutDesc);

        nvrhi::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.primType = nvrhi::PrimitiveType::TriangleStrip;
        pipelineDesc.VS = m_CommonPasses->m_FullscreenVS;
        pipelineDesc.PS = m_PixelShader;
        pipelineDesc.bindingLayouts = { m_RenderBindingLayout};

        pipelineDesc.renderState.rasterState.setCullNone();
        pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
        pipelineDesc.renderState.depthStencilState.stencilEnable = false;

        m_RenderPso = device->createGraphicsPipeline(pipelineDesc, sampleFramebuffer);
    }
}

void ToneMappingPass::Render(
    nvrhi::ICommandList* commandList, 
    const ToneMappingParameters& params,
    const ICompositeView& compositeView,
    nvrhi::ITexture* sourceTexture)
{
    nvrhi::BindingSetHandle& bindingSet = m_RenderBindingSets[sourceTexture];
    if (!bindingSet)
    {
        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ToneMappingCB),
            nvrhi::BindingSetItem::Texture_SRV(0, sourceTexture),
            nvrhi::BindingSetItem::TypedBuffer_SRV(1, m_ExposureBuffer),
            nvrhi::BindingSetItem::Texture_SRV(2, m_ColorLUT),
            nvrhi::BindingSetItem::Sampler(0, m_CommonPasses->m_LinearClampSampler)
        };
        bindingSet = m_Device->createBindingSet(bindingSetDesc, m_RenderBindingLayout);
    }

    for (uint viewIndex = 0; viewIndex < compositeView.GetNumChildViews(ViewType::PLANAR); viewIndex++)
    {
        const IView* view = compositeView.GetChildView(ViewType::PLANAR, viewIndex);

        nvrhi::GraphicsState state;
        state.pipeline = m_RenderPso;
        state.framebuffer = m_FramebufferFactory->GetFramebuffer(*view);
        state.bindings = { bindingSet };
        state.viewport = view->GetViewportState();

        bool enableColorLUT = params.enableColorLUT && m_ColorLUTSize > 0;

        ToneMappingConstants toneMappingConstants = {};
        toneMappingConstants.exposureScale = ::exp2f(params.exposureBias);
        toneMappingConstants.whitePointInvSquared = 1.f / powf(params.whitePoint, 2.f);
        toneMappingConstants.minAdaptedLuminance = params.minAdaptedLuminance;
        toneMappingConstants.maxAdaptedLuminance = params.maxAdaptedLuminance;
        toneMappingConstants.sourceSlice = view->GetSubresources().baseArraySlice;
        toneMappingConstants.colorLUTTextureSize = enableColorLUT ? float2(m_ColorLUTSize * m_ColorLUTSize, m_ColorLUTSize) : float2(0.f);
        toneMappingConstants.colorLUTTextureSizeInv = enableColorLUT ? 1.f / toneMappingConstants.colorLUTTextureSize : float2(0.f);
        commandList->writeBuffer(m_ToneMappingCB, &toneMappingConstants, sizeof(toneMappingConstants));

        commandList->setGraphicsState(state);

        nvrhi::DrawArguments args;
        args.instanceCount = 1;
        args.vertexCount = 4;
        commandList->draw(args);
    }
}

void ToneMappingPass::SimpleRender(
    nvrhi::ICommandList* commandList, 
    const ToneMappingParameters& params, 
    const ICompositeView& compositeView, nvrhi
    ::ITexture* sourceTexture)
{
    commandList->beginMarker("ToneMapping");
    ResetHistogram(commandList);
    AddFrameToHistogram(commandList, compositeView, sourceTexture);
    ComputeExposure(commandList, params);
    Render(commandList, params, compositeView, sourceTexture);
    commandList->endMarker();
}

nvrhi::BufferHandle ToneMappingPass::GetExposureBuffer()
{
    return m_ExposureBuffer;
}

void ToneMappingPass::AdvanceFrame(float frameTime)
{
    m_FrameTime = frameTime;
}

void ToneMappingPass::ResetExposure(nvrhi::ICommandList* commandList, float initialExposure)
{
    uint32_t uintValue = *(uint32_t*)&initialExposure;
    commandList->clearBufferUInt(m_ExposureBuffer, uintValue);
}

void ToneMappingPass::ResetHistogram(nvrhi::ICommandList* commandList)
{
    commandList->clearBufferUInt(m_HistogramBuffer, 0);
}

static const float g_minLogLuminance = -10; // TODO: figure out how to set these properly
static const float g_maxLogLuminamce = 4;

void ToneMappingPass::AddFrameToHistogram(nvrhi::ICommandList* commandList, const ICompositeView& compositeView, nvrhi::ITexture* sourceTexture)
{
    nvrhi::BindingSetHandle& bindingSet = m_HistogramBindingSets[sourceTexture];
    if (!bindingSet)
    {
        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ToneMappingCB),
            nvrhi::BindingSetItem::Texture_SRV(0, sourceTexture),
            nvrhi::BindingSetItem::TypedBuffer_UAV(0, m_HistogramBuffer)
        };

        bindingSet = m_Device->createBindingSet(bindingSetDesc, m_HistogramBindingLayout);
    }

    for (uint viewIndex = 0; viewIndex < compositeView.GetNumChildViews(ViewType::PLANAR); viewIndex++)
    {
        const IView* view = compositeView.GetChildView(ViewType::PLANAR, viewIndex);

        nvrhi::ViewportState viewportState = view->GetViewportState();

        for (uint viewportIndex = 0; viewportIndex < viewportState.scissorRects.size(); viewportIndex++)
        {
            ToneMappingConstants toneMappingConstants = {};
            toneMappingConstants.logLuminanceScale = 1.0f / (g_maxLogLuminamce - g_minLogLuminance);
            toneMappingConstants.logLuminanceBias = -g_minLogLuminance * toneMappingConstants.logLuminanceScale;

            nvrhi::Rect& scissor = viewportState.scissorRects[viewportIndex];
            toneMappingConstants.viewOrigin = uint2(scissor.minX, scissor.minY);
            toneMappingConstants.viewSize = uint2(scissor.maxX - scissor.minX, scissor.maxY - scissor.minY);
            toneMappingConstants.sourceSlice = view->GetSubresources().baseArraySlice;

            commandList->writeBuffer(m_ToneMappingCB, &toneMappingConstants, sizeof(toneMappingConstants));

            nvrhi::ComputeState state;
            state.pipeline = m_HistogramPso;
            state.bindings = { bindingSet };
            commandList->setComputeState(state);

            uint2 numGroups = (toneMappingConstants.viewSize + uint2(15)) / uint2(16);
            commandList->dispatch(numGroups.x, numGroups.y, 1);
        }
    }
}

void ToneMappingPass::ComputeExposure(nvrhi::ICommandList* commandList, const ToneMappingParameters& params)
{
    ToneMappingConstants toneMappingConstants = {};
    toneMappingConstants.logLuminanceScale = g_maxLogLuminamce - g_minLogLuminance;
    toneMappingConstants.logLuminanceBias = g_minLogLuminance;
    toneMappingConstants.histogramLowPercentile = std::min(0.99f, std::max(0.f, params.histogramLowPercentile));
    toneMappingConstants.histogramHighPercentile = std::min(1.f, std::max(toneMappingConstants.histogramLowPercentile, params.histogramHighPercentile));
    toneMappingConstants.eyeAdaptationSpeedUp = params.eyeAdaptationSpeedUp;
    toneMappingConstants.eyeAdaptationSpeedDown = params.eyeAdaptationSpeedDown;
    toneMappingConstants.minAdaptedLuminance = params.minAdaptedLuminance;
    toneMappingConstants.maxAdaptedLuminance = params.maxAdaptedLuminance;
    toneMappingConstants.frameTime = m_FrameTime;

    commandList->writeBuffer(m_ToneMappingCB, &toneMappingConstants, sizeof(toneMappingConstants));

    nvrhi::ComputeState state;
    state.pipeline = m_ExposurePso;
    state.bindings = { m_ExposureBindingSet };
    commandList->setComputeState(state);

    commandList->dispatch(1);
}
