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

#include <donut/render/ForwardShadingPass.h>
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
#include <donut/shaders/forward_cb.h>


using namespace donut::engine;
using namespace donut::render;

ForwardShadingPass::ForwardShadingPass(
    nvrhi::IDevice* device,
    std::shared_ptr<CommonRenderPasses> commonPasses)
    : m_Device(device)
    , m_CommonPasses(std::move(commonPasses))
{
}

void ForwardShadingPass::Init(ShaderFactory& shaderFactory, const CreateParameters& params)
{
    m_SupportedViewTypes = ViewType::PLANAR;
    if (params.singlePassCubemap)
        m_SupportedViewTypes = ViewType::CUBEMAP;
    
    m_VertexShader = CreateVertexShader(shaderFactory, params);
    m_InputLayout = CreateInputLayout(m_VertexShader, params);
    m_GeometryShader = CreateGeometryShader(shaderFactory, params);
    m_PixelShader = CreatePixelShader(shaderFactory, params, false);
    m_PixelShaderTransmissive = CreatePixelShader(shaderFactory, params, true);

    if (params.materialBindings)
        m_MaterialBindings = params.materialBindings;
    else
        m_MaterialBindings = CreateMaterialBindingCache(*m_CommonPasses);

    auto samplerDesc = nvrhi::SamplerDesc()
        .setAllAddressModes(nvrhi::SamplerAddressMode::Border)
        .setBorderColor(1.0f);
    m_ShadowSampler = m_Device->createSampler(samplerDesc);

    m_ForwardViewCB = m_Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(ForwardShadingViewConstants), "ForwardShadingViewConstants", params.numConstantBufferVersions));
    m_ForwardLightCB = m_Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(ForwardShadingLightConstants), "ForwardShadingLightConstants", params.numConstantBufferVersions));

    m_ViewBindingLayout = CreateViewBindingLayout();
    m_ViewBindingSet = CreateViewBindingSet();
    m_LightBindingLayout = CreateLightBindingLayout();
}

void ForwardShadingPass::ResetBindingCache()
{
    m_MaterialBindings->Clear();
    m_LightBindingSets.clear();
}

nvrhi::ShaderHandle ForwardShadingPass::CreateVertexShader(ShaderFactory& shaderFactory, const CreateParameters& params)
{
    return shaderFactory.CreateShader("donut/passes/forward_vs.hlsl", "main", nullptr, nvrhi::ShaderType::Vertex);
}

nvrhi::ShaderHandle ForwardShadingPass::CreateGeometryShader(ShaderFactory& shaderFactory, const CreateParameters& params)
{
    if (params.singlePassCubemap)
    {
        nvrhi::ShaderDesc desc(nvrhi::ShaderType::Geometry);
        desc.fastGSFlags = nvrhi::FastGeometryShaderFlags(
            nvrhi::FastGeometryShaderFlags::ForceFastGS |
            nvrhi::FastGeometryShaderFlags::UseViewportMask |
            nvrhi::FastGeometryShaderFlags::OffsetTargetIndexByViewportIndex);

        desc.pCoordinateSwizzling = CubemapView::GetCubemapCoordinateSwizzle();

        return shaderFactory.CreateShader("donut/passes/cubemap_gs.hlsl", "main", nullptr, desc);
    }

    return nullptr;
}

nvrhi::ShaderHandle ForwardShadingPass::CreatePixelShader(ShaderFactory& shaderFactory, const CreateParameters& params, bool transmissiveMaterial)
{
    std::vector<ShaderMacro> Macros;
    Macros.push_back(ShaderMacro("TRANSMISSIVE_MATERIAL", transmissiveMaterial ? "1" : "0"));

    return shaderFactory.CreateShader("donut/passes/forward_ps.hlsl", "main", &Macros, nvrhi::ShaderType::Pixel);
}

nvrhi::InputLayoutHandle ForwardShadingPass::CreateInputLayout(nvrhi::IShader* vertexShader, const CreateParameters& params)
{
    const nvrhi::VertexAttributeDesc inputDescs[] =
    {
        GetVertexAttributeDesc(VertexAttribute::Position, "POS", 0),
        GetVertexAttributeDesc(VertexAttribute::PrevPosition, "PREV_POS", 1),
        GetVertexAttributeDesc(VertexAttribute::TexCoord1, "TEXCOORD", 2),
        GetVertexAttributeDesc(VertexAttribute::Normal, "NORMAL", 3),
        GetVertexAttributeDesc(VertexAttribute::Tangent, "TANGENT", 4),
        GetVertexAttributeDesc(VertexAttribute::Transform, "TRANSFORM", 5),
    };

    return m_Device->createInputLayout(inputDescs, uint32_t(std::size(inputDescs)), vertexShader);
}

nvrhi::BindingLayoutHandle ForwardShadingPass::CreateViewBindingLayout()
{
    nvrhi::BindingLayoutDesc viewLayoutDesc;
    viewLayoutDesc.visibility = nvrhi::ShaderType::All;
    viewLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(2),
        nvrhi::BindingLayoutItem::Sampler(1)
    };

    return m_Device->createBindingLayout(viewLayoutDesc);
}


nvrhi::BindingSetHandle ForwardShadingPass::CreateViewBindingSet()
{
    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::ConstantBuffer(1, m_ForwardViewCB),
        nvrhi::BindingSetItem::ConstantBuffer(2, m_ForwardLightCB),
        nvrhi::BindingSetItem::Sampler(1, m_ShadowSampler)
    };
    bindingSetDesc.trackLiveness = m_TrackLiveness;

    return m_Device->createBindingSet(bindingSetDesc, m_ViewBindingLayout);
}

nvrhi::BindingLayoutHandle ForwardShadingPass::CreateLightBindingLayout()
{
    nvrhi::BindingLayoutDesc lightProbeBindingDesc;
    lightProbeBindingDesc.visibility = nvrhi::ShaderType::Pixel;
    lightProbeBindingDesc.bindings = {
        nvrhi::BindingLayoutItem::Texture_SRV(10),
        nvrhi::BindingLayoutItem::Texture_SRV(11),
        nvrhi::BindingLayoutItem::Texture_SRV(12),
        nvrhi::BindingLayoutItem::Texture_SRV(13),
        nvrhi::BindingLayoutItem::Sampler(2),
        nvrhi::BindingLayoutItem::Sampler(3)
    };

    return m_Device->createBindingLayout(lightProbeBindingDesc);
}

nvrhi::BindingSetHandle ForwardShadingPass::CreateLightBindingSet(nvrhi::ITexture* shadowMapTexture, nvrhi::ITexture* diffuse, nvrhi::ITexture* specular, nvrhi::ITexture* environmentBrdf)
{
    nvrhi::BindingSetDesc bindingSetDesc;

    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::Texture_SRV(10, shadowMapTexture ? shadowMapTexture : m_CommonPasses->m_BlackTexture2DArray.Get()),
        nvrhi::BindingSetItem::Texture_SRV(11, diffuse ? diffuse : m_CommonPasses->m_BlackCubeMapArray.Get()),
        nvrhi::BindingSetItem::Texture_SRV(12, specular ? specular : m_CommonPasses->m_BlackCubeMapArray.Get()),
        nvrhi::BindingSetItem::Texture_SRV(13, environmentBrdf ? environmentBrdf : m_CommonPasses->m_BlackTexture.Get()),
        nvrhi::BindingSetItem::Sampler(2, m_CommonPasses->m_LinearWrapSampler),
        nvrhi::BindingSetItem::Sampler(3, m_CommonPasses->m_LinearClampSampler)
    };
    bindingSetDesc.trackLiveness = m_TrackLiveness;

    return m_Device->createBindingSet(bindingSetDesc, m_LightBindingLayout);
}

nvrhi::GraphicsPipelineHandle ForwardShadingPass::CreateGraphicsPipeline(PipelineKey key, nvrhi::IFramebuffer* framebuffer)
{
    nvrhi::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.inputLayout = m_InputLayout;
    pipelineDesc.VS = m_VertexShader;
    pipelineDesc.GS = m_GeometryShader;
    pipelineDesc.renderState.rasterState.frontCounterClockwise = key.bits.frontCounterClockwise;
    pipelineDesc.renderState.rasterState.setCullMode(key.bits.cullMode);
    pipelineDesc.renderState.blendState.alphaToCoverageEnable = false;
    pipelineDesc.bindingLayouts = { m_MaterialBindings->GetLayout(), m_ViewBindingLayout, m_LightBindingLayout };

    pipelineDesc.renderState.depthStencilState
        .setDepthFunc(key.bits.reverseDepth
            ? nvrhi::ComparisonFunc::GreaterOrEqual
            : nvrhi::ComparisonFunc::LessOrEqual);
    
    switch (key.bits.domain)  // NOLINT(clang-diagnostic-switch-enum)
    {
    case MaterialDomain::Opaque:
        pipelineDesc.PS = m_PixelShader;
        break;

    case MaterialDomain::AlphaTested:
        pipelineDesc.PS = m_PixelShader;
        pipelineDesc.renderState.blendState.alphaToCoverageEnable = true;
        break;

    case MaterialDomain::AlphaBlended: {
        pipelineDesc.PS = m_PixelShader;
        pipelineDesc.renderState.blendState.alphaToCoverageEnable = false;
        pipelineDesc.renderState.blendState.targets[0]
            .enableBlend()
            .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
            .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
            .setSrcBlendAlpha(nvrhi::BlendFactor::Zero)
            .setDestBlendAlpha(nvrhi::BlendFactor::One);
        
        pipelineDesc.renderState.depthStencilState.disableDepthWrite();
        break;
    }

    case MaterialDomain::Transmissive:
    case MaterialDomain::TransmissiveAlphaTested:
    case MaterialDomain::TransmissiveAlphaBlended: {
        pipelineDesc.PS = m_PixelShaderTransmissive;
        pipelineDesc.renderState.blendState.alphaToCoverageEnable = false;
        pipelineDesc.renderState.blendState.targets[0]
            .enableBlend()
            .setSrcBlend(nvrhi::BlendFactor::One)
            .setDestBlend(nvrhi::BlendFactor::Src1Color)
            .setSrcBlendAlpha(nvrhi::BlendFactor::Zero)
            .setDestBlendAlpha(nvrhi::BlendFactor::One);

        pipelineDesc.renderState.depthStencilState.disableDepthWrite();
        break;
    }
    default:
        return nullptr;
    }

    return m_Device->createGraphicsPipeline(pipelineDesc, framebuffer);
}

std::shared_ptr<MaterialBindingCache> ForwardShadingPass::CreateMaterialBindingCache(CommonRenderPasses& commonPasses)
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

void ForwardShadingPass::SetupView(
    GeometryPassContext& abstractContext,
    nvrhi::ICommandList* commandList,
    const IView* view,
    const IView* viewPrev)
{
    auto& context = static_cast<Context&>(abstractContext);
    
    ForwardShadingViewConstants viewConstants = {};
    view->FillPlanarViewConstants(viewConstants.view);
    commandList->writeBuffer(m_ForwardViewCB, &viewConstants, sizeof(viewConstants));

    context.keyTemplate.bits.frontCounterClockwise = view->IsMirrored();
    context.keyTemplate.bits.reverseDepth = view->IsReverseDepth();
}

void ForwardShadingPass::PrepareLights(
    Context& context,
    nvrhi::ICommandList* commandList,
    const std::vector<std::shared_ptr<Light>>& lights,
    dm::float3 ambientColorTop,
    dm::float3 ambientColorBottom,
    const std::vector<std::shared_ptr<LightProbe>>& lightProbes)
{
    nvrhi::ITexture* shadowMapTexture = nullptr;
    int2 shadowMapTextureSize = 0;
    for (const auto& light : lights)
    {
        if (light->shadowMap)
        {
            shadowMapTexture = light->shadowMap->GetTexture();
            shadowMapTextureSize = light->shadowMap->GetTextureSize();
            break;
        }
    }

    nvrhi::ITexture* lightProbeDiffuse = nullptr;
    nvrhi::ITexture* lightProbeSpecular = nullptr;
    nvrhi::ITexture* lightProbeEnvironmentBrdf = nullptr;

    for (const auto& probe : lightProbes)
    {
        if (!probe->enabled)
            continue;

        if (lightProbeDiffuse == nullptr || lightProbeSpecular == nullptr || lightProbeEnvironmentBrdf == nullptr)
        {
            lightProbeDiffuse = probe->diffuseMap;
            lightProbeSpecular = probe->specularMap;
            lightProbeEnvironmentBrdf = probe->environmentBrdf;
        }
        else
        {
            if (lightProbeDiffuse != probe->diffuseMap || lightProbeSpecular != probe->specularMap || lightProbeEnvironmentBrdf != probe->environmentBrdf)
            {
                log::error("All lights probe submitted to ForwardShadingPass::PrepareLights(...) must use the same set of textures");
                return;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lockGuard(m_Mutex);

        nvrhi::BindingSetHandle& lightBindings = m_LightBindingSets[std::make_pair(shadowMapTexture, lightProbeDiffuse)];

        if (!lightBindings)
        {
            lightBindings = CreateLightBindingSet(shadowMapTexture, lightProbeDiffuse, lightProbeSpecular, lightProbeEnvironmentBrdf);
        }

        context.lightBindingSet = lightBindings;
    }


    ForwardShadingLightConstants constants = {};

    constants.shadowMapTextureSize = float2(shadowMapTextureSize);
    constants.shadowMapTextureSizeInv = 1.f / constants.shadowMapTextureSize;

    int numShadows = 0;

    for (int nLight = 0; nLight < std::min(static_cast<int>(lights.size()), FORWARD_MAX_LIGHTS); nLight++)
    {
        const auto& light = lights[nLight];

        LightConstants& lightConstants = constants.lights[constants.numLights];
        light->FillLightConstants(lightConstants);

        if (light->shadowMap)
        {
            for (uint32_t cascade = 0; cascade < light->shadowMap->GetNumberOfCascades(); cascade++)
            {
                if (numShadows < FORWARD_MAX_SHADOWS)
                {
                    light->shadowMap->GetCascade(cascade)->FillShadowConstants(constants.shadows[numShadows]);
                    lightConstants.shadowCascades[cascade] = numShadows;
                    ++numShadows;
                }
            }

            for (uint32_t perObjectShadow = 0; perObjectShadow < light->shadowMap->GetNumberOfPerObjectShadows(); perObjectShadow++)
            {
                if (numShadows < FORWARD_MAX_SHADOWS)
                {
                    light->shadowMap->GetPerObjectShadow(perObjectShadow)->FillShadowConstants(constants.shadows[numShadows]);
                    lightConstants.perObjectShadows[perObjectShadow] = numShadows;
                    ++numShadows;
                }
            }
        }

        ++constants.numLights;
    }

    constants.ambientColorTop = float4(ambientColorTop, 0.f);
    constants.ambientColorBottom = float4(ambientColorBottom, 0.f);

    for (const auto& probe : lightProbes)
    {
        if (!probe->IsActive())
            continue;

        LightProbeConstants& lightProbeConstants = constants.lightProbes[constants.numLightProbes];
        probe->FillLightProbeConstants(lightProbeConstants);

        ++constants.numLightProbes;

        if (constants.numLightProbes >= FORWARD_MAX_LIGHT_PROBES)
            break;
    }

    commandList->writeBuffer(m_ForwardLightCB, &constants, sizeof(constants));
}

ViewType::Enum ForwardShadingPass::GetSupportedViewTypes() const
{
    return m_SupportedViewTypes;
}

bool ForwardShadingPass::SetupMaterial(GeometryPassContext& abstractContext, const Material* material, nvrhi::RasterCullMode cullMode, nvrhi::GraphicsState& state)
{
    auto& context = static_cast<Context&>(abstractContext);

    nvrhi::IBindingSet* materialBindingSet = m_MaterialBindings->GetMaterialBindingSet(material);

    if (!materialBindingSet)
        return false;

    if (material->domain >= MaterialDomain::Count || cullMode > nvrhi::RasterCullMode::None)
    {
        assert(false);
        return false;
    }

    PipelineKey key = context.keyTemplate;
    key.bits.cullMode = cullMode;
    key.bits.domain = material->domain;

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
    state.bindings = { materialBindingSet, m_ViewBindingSet, context.lightBindingSet };

    return true;
}

void ForwardShadingPass::SetupInputBuffers(GeometryPassContext& abstractContext, const BufferGroup* buffers, nvrhi::GraphicsState& state)
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
