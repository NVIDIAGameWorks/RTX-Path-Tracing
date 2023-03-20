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

#include <donut/render/GBufferFillPass.h>
#include <donut/render/DrawStrategy.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/ShadowMap.h>
#include <donut/engine/SceneTypes.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/MaterialBindingCache.h>
#include <donut/core/log.h>
#include <nvrhi/utils.h>
#include <utility>

using namespace donut::math;
#include <donut/shaders/gbuffer_cb.h>

using namespace donut::engine;
using namespace donut::render;

GBufferFillPass::GBufferFillPass(nvrhi::IDevice* device, std::shared_ptr<CommonRenderPasses> commonPasses)
    : m_Device(device)
    , m_CommonPasses(std::move(commonPasses))
{

}

void GBufferFillPass::Init(ShaderFactory& shaderFactory, const CreateParameters& params)
{
    m_SupportedViewTypes = ViewType::PLANAR;
    if (params.enableSinglePassCubemap)
        m_SupportedViewTypes = ViewType::Enum(m_SupportedViewTypes | ViewType::CUBEMAP);
    
    m_VertexShader = CreateVertexShader(shaderFactory, params);
    m_InputLayout = CreateInputLayout(m_VertexShader, params);
    m_GeometryShader = CreateGeometryShader(shaderFactory, params);
    m_PixelShader = CreatePixelShader(shaderFactory, params, false);
    m_PixelShaderAlphaTested = CreatePixelShader(shaderFactory, params, true);

    if (params.materialBindings)
        m_MaterialBindings = params.materialBindings;
    else
        m_MaterialBindings = CreateMaterialBindingCache(*m_CommonPasses);

    m_GBufferCB = m_Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(GBufferFillConstants), "GBufferFillConstants", params.numConstantBufferVersions));

    CreateViewBindings(m_ViewBindingLayout, m_ViewBindings, params);

    m_EnableDepthWrite = params.enableDepthWrite;
    m_StencilWriteMask = params.stencilWriteMask;
}

void GBufferFillPass::ResetBindingCache() const
{
    m_MaterialBindings->Clear();
}

nvrhi::ShaderHandle GBufferFillPass::CreateVertexShader(ShaderFactory& shaderFactory, const CreateParameters& params)
{
    std::vector<ShaderMacro> VertexShaderMacros;
    VertexShaderMacros.push_back(ShaderMacro("MOTION_VECTORS", params.enableMotionVectors ? "1" : "0"));
    return shaderFactory.CreateShader("donut/passes/gbuffer_vs.hlsl", "main", &VertexShaderMacros, nvrhi::ShaderType::Vertex);
}

nvrhi::ShaderHandle GBufferFillPass::CreateGeometryShader(ShaderFactory& shaderFactory, const CreateParameters& params)
{

    ShaderMacro MotionVectorsMacro("MOTION_VECTORS", params.enableMotionVectors ? "1" : "0");

    if (params.enableSinglePassCubemap)
    {
        // MVs will not work with cubemap views because:
        // 1. cubemap_gs does not pass through the previous position attribute;
        // 2. Computing correct MVs for a cubemap is complicated and not implemented.
        assert(!params.enableMotionVectors);

        nvrhi::ShaderDesc desc(nvrhi::ShaderType::Geometry);
        desc.fastGSFlags = nvrhi::FastGeometryShaderFlags(
            nvrhi::FastGeometryShaderFlags::ForceFastGS |
            nvrhi::FastGeometryShaderFlags::UseViewportMask |
            nvrhi::FastGeometryShaderFlags::OffsetTargetIndexByViewportIndex);

        desc.pCoordinateSwizzling = CubemapView::GetCubemapCoordinateSwizzle();

        return shaderFactory.CreateShader("donut/passes/cubemap_gs.hlsl", "main", nullptr, desc);
    }
    else
    {
        return nullptr;
    }
}

nvrhi::ShaderHandle GBufferFillPass::CreatePixelShader(ShaderFactory& shaderFactory, const CreateParameters& params, bool alphaTested)
{
    std::vector<ShaderMacro> PixelShaderMacros;
    PixelShaderMacros.push_back(ShaderMacro("MOTION_VECTORS", params.enableMotionVectors ? "1" : "0"));
    PixelShaderMacros.push_back(ShaderMacro("ALPHA_TESTED", alphaTested ? "1" : "0"));

    return shaderFactory.CreateShader("donut/passes/gbuffer_ps.hlsl", "main", &PixelShaderMacros, nvrhi::ShaderType::Pixel);
}

nvrhi::InputLayoutHandle GBufferFillPass::CreateInputLayout(nvrhi::IShader* vertexShader, const CreateParameters& params)
{
    std::vector<nvrhi::VertexAttributeDesc> inputDescs =
    {
        GetVertexAttributeDesc(VertexAttribute::Position, "POS", 0),
        GetVertexAttributeDesc(VertexAttribute::PrevPosition, "PREV_POS", 1),
        GetVertexAttributeDesc(VertexAttribute::TexCoord1, "TEXCOORD", 2),
        GetVertexAttributeDesc(VertexAttribute::Normal, "NORMAL", 3),
        GetVertexAttributeDesc(VertexAttribute::Tangent, "TANGENT", 4),
        GetVertexAttributeDesc(VertexAttribute::Transform, "TRANSFORM", 5),
    };
    if (params.enableMotionVectors)
    {
        inputDescs.push_back(GetVertexAttributeDesc(VertexAttribute::PrevTransform, "PREV_TRANSFORM", 5));
    }

    return m_Device->createInputLayout(inputDescs.data(), static_cast<uint32_t>(inputDescs.size()), vertexShader);
}

void GBufferFillPass::CreateViewBindings(nvrhi::BindingLayoutHandle& layout, nvrhi::BindingSetHandle& set, const CreateParameters& params)
{
    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::ConstantBuffer(1, m_GBufferCB)
    };

    bindingSetDesc.trackLiveness = params.trackLiveness;

    nvrhi::utils::CreateBindingSetAndLayout(m_Device, nvrhi::ShaderType::All, 0, bindingSetDesc, layout, set);
}

nvrhi::GraphicsPipelineHandle GBufferFillPass::CreateGraphicsPipeline(PipelineKey key, nvrhi::IFramebuffer* sampleFramebuffer)
{
    nvrhi::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.inputLayout = m_InputLayout;
    pipelineDesc.VS = m_VertexShader;
    pipelineDesc.GS = m_GeometryShader;
    pipelineDesc.renderState.rasterState
        .setFrontCounterClockwise(key.bits.frontCounterClockwise)
        .setCullMode(key.bits.cullMode);
    pipelineDesc.renderState.blendState.disableAlphaToCoverage();
    pipelineDesc.bindingLayouts = { m_MaterialBindings->GetLayout(), m_ViewBindingLayout };

    pipelineDesc.renderState.depthStencilState
        .setDepthWriteEnable(m_EnableDepthWrite)
        .setDepthFunc(key.bits.reverseDepth
            ? nvrhi::ComparisonFunc::GreaterOrEqual
            : nvrhi::ComparisonFunc::LessOrEqual);
        
    if (m_StencilWriteMask)
    {
        pipelineDesc.renderState.depthStencilState
            .enableStencil()
            .setStencilReadMask(0)
            .setStencilWriteMask(uint8_t(m_StencilWriteMask))
            .setStencilRefValue(uint8_t(m_StencilWriteMask))
            .setFrontFaceStencil(nvrhi::DepthStencilState::StencilOpDesc().setPassOp(nvrhi::StencilOp::Replace))
            .setBackFaceStencil(nvrhi::DepthStencilState::StencilOpDesc().setPassOp(nvrhi::StencilOp::Replace));
    }

    if (key.bits.alphaTested)
    {
        pipelineDesc.renderState.rasterState.setCullNone();

        if (m_PixelShaderAlphaTested)
        {
            pipelineDesc.PS = m_PixelShaderAlphaTested;
        }
        else
        {
            pipelineDesc.PS = m_PixelShader;
            pipelineDesc.renderState.blendState.alphaToCoverageEnable = true;
        }
    }
    else
    {
        pipelineDesc.PS = m_PixelShader;
    }

    return m_Device->createGraphicsPipeline(pipelineDesc, sampleFramebuffer);
}

std::shared_ptr<MaterialBindingCache> GBufferFillPass::CreateMaterialBindingCache(CommonRenderPasses& commonPasses)
{
    std::vector<MaterialResourceBinding> materialBindings = {
        { MaterialResource::ConstantBuffer, 0 },
        { MaterialResource::DiffuseTexture, 0 },
        { MaterialResource::SpecularTexture, 1 },
        { MaterialResource::NormalTexture, 2 },
        { MaterialResource::EmissiveTexture, 3 },
        { MaterialResource::OcclusionTexture, 4 },
        { MaterialResource::TransmissionTexture, 5 },
        { MaterialResource::Sampler, 0 },
    };

    return std::make_shared<MaterialBindingCache>(
        m_Device,
        nvrhi::ShaderType::Pixel,
        /* registerSpace = */ 0,
        materialBindings,
        commonPasses.m_AnisotropicWrapSampler,
        commonPasses.m_GrayTexture,
        commonPasses.m_BlackTexture);
}

ViewType::Enum GBufferFillPass::GetSupportedViewTypes() const
{
    return m_SupportedViewTypes;
}

void GBufferFillPass::SetupView(GeometryPassContext& abstractContext, nvrhi::ICommandList* commandList, const engine::IView* view, const engine::IView* viewPrev)
{
    auto& context = static_cast<Context&>(abstractContext);
    
    GBufferFillConstants gbufferConstants = {};
    view->FillPlanarViewConstants(gbufferConstants.view);
    viewPrev->FillPlanarViewConstants(gbufferConstants.viewPrev);
    commandList->writeBuffer(m_GBufferCB, &gbufferConstants, sizeof(gbufferConstants));

    context.keyTemplate.bits.frontCounterClockwise = view->IsMirrored();
    context.keyTemplate.bits.reverseDepth = view->IsReverseDepth();
}

bool GBufferFillPass::SetupMaterial(GeometryPassContext& abstractContext, const engine::Material* material, nvrhi::RasterCullMode cullMode, nvrhi::GraphicsState& state)
{
    auto& context = static_cast<Context&>(abstractContext);
    
    PipelineKey key = context.keyTemplate;
    key.bits.cullMode = cullMode;

    switch (material->domain)
    {
    case MaterialDomain::Opaque:
    case MaterialDomain::AlphaBlended: // Blended and transmissive domains are for the material ID pass, shouldn't be used otherwise
    case MaterialDomain::Transmissive:
    case MaterialDomain::TransmissiveAlphaTested:
    case MaterialDomain::TransmissiveAlphaBlended:
        key.bits.alphaTested = false;
        break;
    case MaterialDomain::AlphaTested:
        key.bits.alphaTested = true;
        break;
    default:
        return false;
    }

    nvrhi::IBindingSet* materialBindingSet = m_MaterialBindings->GetMaterialBindingSet(material);

    if (!materialBindingSet)
        return false;

    nvrhi::GraphicsPipelineHandle& pipeline = m_Pipelines[key.value];

    if (!pipeline)
    {
        std::lock_guard<std::mutex> lockGuard(m_Mutex);

        if (!pipeline)
            pipeline = CreateGraphicsPipeline(key, state.framebuffer);

        if (!pipeline)
            return false;
    }

    assert(pipeline->getFramebufferInfo() == state.framebuffer->getFramebufferInfo());

    state.pipeline = pipeline;
    state.bindings = { materialBindingSet, m_ViewBindings };

    return true;
}

void GBufferFillPass::SetupInputBuffers(GeometryPassContext& context, const engine::BufferGroup* buffers, nvrhi::GraphicsState& state)
{
    state.vertexBuffers = {
        { buffers->vertexBuffer, 0, buffers->getVertexBufferRange(VertexAttribute::Position).byteOffset },
        { buffers->vertexBuffer, 1, buffers->getVertexBufferRange(VertexAttribute::PrevPosition).byteOffset },
        { buffers->vertexBuffer, 2, buffers->getVertexBufferRange(VertexAttribute::TexCoord1).byteOffset },
        { buffers->vertexBuffer, 3, buffers->getVertexBufferRange(VertexAttribute::Normal).byteOffset },
        { buffers->vertexBuffer, 4, buffers->getVertexBufferRange(VertexAttribute::Tangent).byteOffset },
        { buffers->instanceBuffer, 5, 0 }
    };

    state.indexBuffer = { buffers->indexBuffer, nvrhi::Format::R32_UINT, 0 };
}

nvrhi::ShaderHandle MaterialIDPass::CreatePixelShader(engine::ShaderFactory& shaderFactory, const CreateParameters& params, bool alphaTested)
{
    std::vector<ShaderMacro> PixelShaderMacros;
    PixelShaderMacros.push_back(ShaderMacro("ALPHA_TESTED", alphaTested ? "1" : "0"));

    return shaderFactory.CreateShader("donut/passes/material_id_ps.hlsl", "main", &PixelShaderMacros, nvrhi::ShaderType::Pixel);
}

void MaterialIDPass::CreateViewBindings(nvrhi::BindingLayoutHandle& layout, nvrhi::BindingSetHandle& set, const CreateParameters& params)
{
    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::ConstantBuffer(1, m_GBufferCB),
        nvrhi::BindingSetItem::PushConstants(2, sizeof(uint32_t))
    };

    bindingSetDesc.trackLiveness = params.trackLiveness;

    nvrhi::utils::CreateBindingSetAndLayout(m_Device, nvrhi::ShaderType::All, 0, bindingSetDesc, layout, set);
}

void MaterialIDPass::SetPushConstants(GeometryPassContext& context, nvrhi::ICommandList* commandList, nvrhi::GraphicsState& state, nvrhi::DrawArguments& args)
{
    commandList->setPushConstants(&args.startInstanceLocation, sizeof(uint32_t));
}
