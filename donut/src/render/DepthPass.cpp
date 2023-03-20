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

#include <donut/render/DepthPass.h>
#include <donut/render/DrawStrategy.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/SceneTypes.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/View.h>
#include <donut/engine/MaterialBindingCache.h>
#include <nvrhi/utils.h>
#include <utility>

using namespace donut::math;
#include <donut/shaders/depth_cb.h>


using namespace donut::engine;
using namespace donut::render;

DepthPass::DepthPass(
    nvrhi::IDevice* device,
    std::shared_ptr<CommonRenderPasses> commonPasses)
    : m_Device(device)
    , m_CommonPasses(std::move(commonPasses))
{
}

void DepthPass::Init(ShaderFactory& shaderFactory, const CreateParameters& params)
{
    m_VertexShader = CreateVertexShader(shaderFactory, params);
    m_PixelShader = CreatePixelShader(shaderFactory, params);
    m_InputLayout = CreateInputLayout(m_VertexShader, params);

    if (params.materialBindings)
        m_MaterialBindings = params.materialBindings;
    else
        m_MaterialBindings = CreateMaterialBindingCache(*m_CommonPasses);

    m_DepthCB = m_Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(DepthPassConstants),
        "DepthPassConstants", params.numConstantBufferVersions));

    CreateViewBindings(m_ViewBindingLayout, m_ViewBindingSet, params);

    m_DepthBias = params.depthBias;
    m_DepthBiasClamp = params.depthBiasClamp;
    m_SlopeScaledDepthBias = params.slopeScaledDepthBias;
}

void DepthPass::ResetBindingCache() const
{
    m_MaterialBindings->Clear();
}

nvrhi::ShaderHandle DepthPass::CreateVertexShader(ShaderFactory& shaderFactory, const CreateParameters& params)
{
    return shaderFactory.CreateShader("donut/passes/depth_vs.hlsl", "main", nullptr, nvrhi::ShaderType::Vertex);
}

nvrhi::ShaderHandle DepthPass::CreatePixelShader(ShaderFactory& shaderFactory, const CreateParameters& params)
{
    return shaderFactory.CreateShader("donut/passes/depth_ps.hlsl", "main", nullptr, nvrhi::ShaderType::Pixel);
}

nvrhi::InputLayoutHandle DepthPass::CreateInputLayout(nvrhi::IShader* vertexShader, const CreateParameters& params)
{
    nvrhi::VertexAttributeDesc aInputDescs[] =
    {
        GetVertexAttributeDesc(VertexAttribute::Position, "POSITION", 0),
        GetVertexAttributeDesc(VertexAttribute::TexCoord1, "TEXCOORD", 1),
        GetVertexAttributeDesc(VertexAttribute::Transform, "TRANSFORM", 2)
    };

    return m_Device->createInputLayout(aInputDescs, dim(aInputDescs), vertexShader);
}

void DepthPass::CreateViewBindings(nvrhi::BindingLayoutHandle& layout, nvrhi::BindingSetHandle& set, const CreateParameters& params)
{
    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::ConstantBuffer(0, m_DepthCB)
    };
    bindingSetDesc.trackLiveness = m_TrackLiveness;

    nvrhi::utils::CreateBindingSetAndLayout(m_Device, nvrhi::ShaderType::Vertex, 0, bindingSetDesc, layout, set);
}

std::shared_ptr<MaterialBindingCache> DepthPass::CreateMaterialBindingCache(CommonRenderPasses& commonPasses)
{
    std::vector<MaterialResourceBinding> materialBindings = {
        { MaterialResource::DiffuseTexture, 0 },
        { MaterialResource::Sampler, 0 },
        { MaterialResource::ConstantBuffer, 1 }
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

nvrhi::GraphicsPipelineHandle DepthPass::CreateGraphicsPipeline(PipelineKey key, nvrhi::IFramebuffer* framebuffer)
{
    nvrhi::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.inputLayout = m_InputLayout;
    pipelineDesc.VS = m_VertexShader;
    pipelineDesc.renderState.rasterState.depthBias = m_DepthBias;
    pipelineDesc.renderState.rasterState.depthBiasClamp = m_DepthBiasClamp;
    pipelineDesc.renderState.rasterState.slopeScaledDepthBias = m_SlopeScaledDepthBias;
    pipelineDesc.renderState.rasterState.frontCounterClockwise = key.bits.frontCounterClockwise;
    pipelineDesc.renderState.rasterState.cullMode = key.bits.cullMode;
    pipelineDesc.renderState.depthStencilState.depthFunc = key.bits.reverseDepth
        ? nvrhi::ComparisonFunc::GreaterOrEqual
        : nvrhi::ComparisonFunc::LessOrEqual;

    if (key.bits.alphaTested)
    {
        pipelineDesc.PS = m_PixelShader;
        pipelineDesc.bindingLayouts = { m_ViewBindingLayout, m_MaterialBindings->GetLayout() };
    }
    else
    {
        pipelineDesc.PS = nullptr;
        pipelineDesc.bindingLayouts = { m_ViewBindingLayout };
    }

    return m_Device->createGraphicsPipeline(pipelineDesc, framebuffer);
}

ViewType::Enum DepthPass::GetSupportedViewTypes() const
{
    return ViewType::PLANAR;
}

void DepthPass::SetupView(GeometryPassContext& abstractContext, nvrhi::ICommandList* commandList, const engine::IView* view, const engine::IView* viewPrev)
{
    auto& context = static_cast<Context&>(abstractContext);
    
    DepthPassConstants depthConstants = {};
    depthConstants.matWorldToClip = view->GetViewProjectionMatrix();
    commandList->writeBuffer(m_DepthCB, &depthConstants, sizeof(depthConstants));

    context.keyTemplate.bits.frontCounterClockwise = view->IsMirrored();
    context.keyTemplate.bits.reverseDepth = view->IsReverseDepth();
}

bool DepthPass::SetupMaterial(GeometryPassContext& abstractContext, const engine::Material* material, nvrhi::RasterCullMode cullMode, nvrhi::GraphicsState& state)
{
    auto& context = static_cast<Context&>(abstractContext);

    PipelineKey key = context.keyTemplate;
    key.bits.cullMode = cullMode;

    if (material->domain == MaterialDomain::AlphaTested && material->baseOrDiffuseTexture && material->baseOrDiffuseTexture->texture)
    {
        nvrhi::IBindingSet* materialBindingSet = m_MaterialBindings->GetMaterialBindingSet(material);

        if (!materialBindingSet)
            return false;
        
        state.bindings = { m_ViewBindingSet, materialBindingSet };
        key.bits.alphaTested = true;
    }
    else if (material->domain == MaterialDomain::Opaque)
    {
        state.bindings = { m_ViewBindingSet };
        key.bits.alphaTested = false;
    }
    else
    {
        return false;
    }

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
    return true;
}

void DepthPass::SetupInputBuffers(GeometryPassContext& context, const engine::BufferGroup* buffers, nvrhi::GraphicsState& state)
{
    state.vertexBuffers = {
        { buffers->vertexBuffer, 0, buffers->getVertexBufferRange(VertexAttribute::Position).byteOffset },
        { buffers->vertexBuffer, 1, buffers->getVertexBufferRange(VertexAttribute::TexCoord1).byteOffset },
        { buffers->instanceBuffer, 2, 0 }
    };

    state.indexBuffer = { buffers->indexBuffer, nvrhi::Format::R32_UINT, 0 };
}
