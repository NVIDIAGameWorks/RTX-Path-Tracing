/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "PostProcess.h"

#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/CommonRenderPasses.h>

using namespace donut;
using namespace donut::math;
using namespace donut::engine;

PostProcess::PostProcess( nvrhi::IDevice* device, std::shared_ptr<donut::engine::ShaderFactory> shaderFactory, 
    std::shared_ptr<engine::CommonRenderPasses> commonPasses//, std::shared_ptr<engine::FramebufferFactory> colorFramebufferFactory
    )
    : m_Device(device)
    , m_CommonPasses(commonPasses)
    , m_BindingCache(device)
//    , m_FramebufferFactory(colorFramebufferFactory)
{
    for( uint32_t i = 0; i < (uint32_t)RenderPassType::MaxCount; i++ )
    {
        std::vector<donut::engine::ShaderMacro> shaderMacros;
        switch((RenderPassType)i)
        {
        //case(RenderPassType::Debug_DenoiserUnfilteredRadiance): shaderMacros.push_back(donut::engine::ShaderMacro({  "NRD_SHOW_UNFILTERED_RADIANCE", "1" })); break;
        case(RenderPassType::Debug_BlendDebugViz): shaderMacros.push_back(donut::engine::ShaderMacro({              "BLEND_DEBUG_BUFFER", "1" })); break;
        default:;
        };
        m_RenderShaders[i] = shaderFactory->CreateShader("app/PostProcess.hlsl", "main", &shaderMacros, nvrhi::ShaderType::Pixel);
    }

    for (uint32_t i = 0; i < (uint32_t)ComputePassType::MaxCount; i++)
    {
        std::vector<donut::engine::ShaderMacro> shaderMacros;
        switch ((ComputePassType)i)
        {
        case(ComputePassType::StablePlanesDebugViz):
            shaderMacros.push_back(donut::engine::ShaderMacro({ "STABLE_PLANES_DEBUG_VIZ", "1" }));
            break;
        case(ComputePassType::RELAXDenoiserPrepareInputs):
            shaderMacros.push_back(donut::engine::ShaderMacro({ "DENOISER_PREPARE_INPUTS", "1" }));
            shaderMacros.push_back(donut::engine::ShaderMacro({ "USE_RELAX", "1" }));
            break;
        case(ComputePassType::REBLURDenoiserPrepareInputs):
            shaderMacros.push_back(donut::engine::ShaderMacro({ "DENOISER_PREPARE_INPUTS", "1" }));
            shaderMacros.push_back(donut::engine::ShaderMacro({ "USE_RELAX", "0" }));
            break;
        case(ComputePassType::RELAXDenoiserFinalMerge): 
            shaderMacros.push_back(donut::engine::ShaderMacro({ "DENOISER_FINAL_MERGE", "1" })); 
            shaderMacros.push_back(donut::engine::ShaderMacro({ "USE_RELAX", "1" }));
            break;
        case(ComputePassType::REBLURDenoiserFinalMerge): 
            shaderMacros.push_back(donut::engine::ShaderMacro({ "DENOISER_FINAL_MERGE", "1" }));
            shaderMacros.push_back(donut::engine::ShaderMacro({ "USE_RELAX", "0" }));
            break;
        case(ComputePassType::DummyPlaceholder): shaderMacros.push_back(donut::engine::ShaderMacro({ "DUMMY_PLACEHOLDER_EFFECT", "1" })); break;
        };
        m_ComputeShaders[i] = shaderFactory->CreateShader("app/PostProcess.hlsl", "main", &shaderMacros, nvrhi::ShaderType::Compute);
    }
    //m_MainCS = shaderFactory->CreateShader("app/PostProcess.hlsl", "main", &std::vector<donut::engine::ShaderMacro>(1, donut::engine::ShaderMacro("USE_CS", "1")), nvrhi::ShaderType::Compute);

    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::Pixel;
    layoutDesc.bindings = {
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
        nvrhi::BindingLayoutItem::Texture_SRV(0),
        nvrhi::BindingLayoutItem::Texture_SRV(4),
        nvrhi::BindingLayoutItem::Texture_SRV(5),
        nvrhi::BindingLayoutItem::Sampler(0)
    };
    m_BindingLayoutPS = m_Device->createBindingLayout(layoutDesc);

    layoutDesc.visibility = nvrhi::ShaderType::Compute | nvrhi::ShaderType::Pixel;
    layoutDesc.bindings = {
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
        nvrhi::BindingLayoutItem::Texture_SRV(0),
        nvrhi::BindingLayoutItem::Texture_UAV(0),
        nvrhi::BindingLayoutItem::Texture_UAV(1),
        nvrhi::BindingLayoutItem::Texture_SRV(2),
        nvrhi::BindingLayoutItem::Texture_SRV(3),
        nvrhi::BindingLayoutItem::Texture_SRV(4),
        nvrhi::BindingLayoutItem::Texture_SRV(5),
        nvrhi::BindingLayoutItem::Texture_SRV(6),
        nvrhi::BindingLayoutItem::Texture_SRV(7),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(10),
        nvrhi::BindingLayoutItem::Sampler(0)
    };
    m_BindingLayoutCS = m_Device->createBindingLayout(layoutDesc);

    nvrhi::SamplerDesc samplerDesc;
    samplerDesc.setBorderColor(nvrhi::Color(0.f));
    samplerDesc.setAllFilters(true);
    samplerDesc.setMipFilter(false);
    samplerDesc.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap);
    m_LinearSampler = m_Device->createSampler(samplerDesc);

    samplerDesc.setAllFilters(false);
    m_PointSampler = m_Device->createSampler(samplerDesc);
}

void PostProcess::Apply(nvrhi::ICommandList* commandList, RenderPassType passType, nvrhi::BufferHandle consts, nvrhi::BufferHandle miniConsts, nvrhi::IFramebuffer* targetFramebuffer, RenderTargets & renderTargets, nvrhi::ITexture* sourceTexture, bool pingActive)
{
    commandList->beginMarker("PostProcessPS");

    assert( (uint32_t)passType >= 0 && passType < RenderPassType::MaxCount );
    uint passIndex = (uint32_t)passType;

    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, consts),
            nvrhi::BindingSetItem::ConstantBuffer(1, miniConsts),
            nvrhi::BindingSetItem::Texture_SRV(0, (sourceTexture!=nullptr)?(sourceTexture):(m_CommonPasses->m_WhiteTexture.Get())),
            //nvrhi::BindingSetItem::StructuredBuffer_SRV(1, renderTargets.DenoiserPixelDataBuffer),
            nvrhi::BindingSetItem::Texture_SRV(4, renderTargets.OutputColor),
            nvrhi::BindingSetItem::Texture_SRV(5, renderTargets.DebugVizOutput),
            nvrhi::BindingSetItem::Sampler(0, m_LinearSampler /*m_PointSampler*/ )
    };
    
    nvrhi::BindingSetHandle bindingSet = m_BindingCache.GetOrCreateBindingSet(bindingSetDesc, m_BindingLayoutPS);

    if( m_RenderPSOs[passIndex] == nullptr)
    {
        nvrhi::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.bindingLayouts = { m_BindingLayoutPS };
        pipelineDesc.primType = nvrhi::PrimitiveType::TriangleStrip;
        pipelineDesc.VS = m_CommonPasses->m_FullscreenVS;
        pipelineDesc.PS = m_RenderShaders[passIndex];
        pipelineDesc.renderState.rasterState.setCullNone();
        pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
        pipelineDesc.renderState.depthStencilState.stencilEnable = false;
        pipelineDesc.renderState.blendState.targets[0].enableBlend()
            .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
            .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
            .setSrcBlendAlpha(nvrhi::BlendFactor::Zero)
            .setDestBlendAlpha(nvrhi::BlendFactor::One);
        m_RenderPSOs[passIndex] = m_Device->createGraphicsPipeline(pipelineDesc, targetFramebuffer);
    }

    nvrhi::GraphicsState state;
    state.pipeline = m_RenderPSOs[passIndex];
    state.framebuffer = targetFramebuffer;
    state.bindings = { bindingSet };
    nvrhi::ViewportState viewportState;
    auto desc = targetFramebuffer->getDesc().colorAttachments[0].texture->getDesc();
    viewportState.addViewport( nvrhi::Viewport( (float)desc.width, (float)desc.height ) );
    viewportState.addScissorRect( nvrhi::Rect( desc.width, desc.height ) );
    state.viewport = viewportState;
    commandList->setGraphicsState(state);

    nvrhi::DrawArguments args;
    args.instanceCount = 1;
    args.vertexCount = 4;
    commandList->draw(args);

    commandList->endMarker();
}

void PostProcess::Apply(nvrhi::ICommandList* commandList, ComputePassType passType, nvrhi::BufferHandle consts, nvrhi::BufferHandle miniConsts, nvrhi::BindingSetHandle bindingSet, nvrhi::BindingLayoutHandle bindingLayout, uint32_t width, uint32_t height, bool pingActive)
{
    uint passIndex = (uint32_t)passType;

    if (m_ComputePSOs[passIndex] == nullptr)
    {
        nvrhi::ComputePipelineDesc pipelineDesc;
        pipelineDesc.bindingLayouts = { bindingLayout };
        pipelineDesc.CS = m_ComputeShaders[passIndex];
        m_ComputePSOs[passIndex] = m_Device->createComputePipeline(pipelineDesc);
        m_BindingLayoutCSs[passIndex] = bindingLayout;
    }
    assert(m_BindingLayoutCSs[passIndex] == bindingLayout);

    nvrhi::ComputeState state;
    state.bindings = { bindingSet };
    state.pipeline = m_ComputePSOs[passIndex];

    commandList->setComputeState(state);

    const dm::uint  threads = NUM_COMPUTE_THREADS_PER_DIM;
    const dm::uint2 dispatchSize = dm::uint2((width + threads - 1) / threads, (height + threads - 1) / threads);
    commandList->dispatch(dispatchSize.x, dispatchSize.y);
}

void PostProcess::Apply( nvrhi::ICommandList* commandList, ComputePassType passType, int pass, nvrhi::BufferHandle consts, nvrhi::BufferHandle miniConsts, nvrhi::ITexture* workTexture, RenderTargets & renderTargets, nvrhi::ITexture* sourceTexture, bool pingActive)
{
    // commandList->beginMarker("PostProcessCS");

    assert((uint32_t)passType >= 0 && passType < ComputePassType::MaxCount);

    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.bindings = {
    		nvrhi::BindingSetItem::ConstantBuffer(0, consts),
            nvrhi::BindingSetItem::ConstantBuffer(1, miniConsts), 
            nvrhi::BindingSetItem::Texture_SRV(0, (sourceTexture!=nullptr)?(sourceTexture):(m_CommonPasses->m_WhiteTexture.Get())),
            nvrhi::BindingSetItem::Texture_UAV(0, workTexture),
            nvrhi::BindingSetItem::Texture_UAV(1, renderTargets.DebugVizOutput),
    		//nvrhi::BindingSetItem::StructuredBuffer_SRV(1, renderTargets.DenoiserPixelDataBuffer),
            nvrhi::BindingSetItem::Texture_SRV(2, renderTargets.DenoiserOutDiffRadianceHitDist[pass]),
            nvrhi::BindingSetItem::Texture_SRV(3, renderTargets.DenoiserOutSpecRadianceHitDist[pass]),
            nvrhi::BindingSetItem::Texture_SRV(4, m_CommonPasses->m_WhiteTexture.Get()),
            nvrhi::BindingSetItem::Texture_SRV(5, (renderTargets.DenoiserOutValidation!=nullptr)?(renderTargets.DenoiserOutValidation):((nvrhi::TextureHandle)m_CommonPasses->m_WhiteTexture.Get())),
            nvrhi::BindingSetItem::Texture_SRV(6, renderTargets.DenoiserViewspaceZ),
            nvrhi::BindingSetItem::Texture_SRV(7, renderTargets.DenoiserDisocclusionThresholdMix),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(10, (pingActive)?(renderTargets.StablePlanesBuffer):(renderTargets.PrevStablePlanesBuffer)),
            nvrhi::BindingSetItem::Sampler(0, (true) ? m_LinearSampler : m_PointSampler)
    	};

    nvrhi::BindingSetHandle bindingSet = m_BindingCache.GetOrCreateBindingSet(bindingSetDesc, m_BindingLayoutCS);

    Apply(commandList, passType, consts, miniConsts, bindingSet, m_BindingLayoutCS, workTexture->getDesc().width, workTexture->getDesc().height, pingActive);

    // commandList->endMarker();
}

