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

#include <donut/render/DeferredLightingPass.h>
#include <donut/render/DrawStrategy.h>
#include <donut/render/GBuffer.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/ShadowMap.h>
#include <donut/engine/SceneTypes.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/View.h>
#include <donut/core/log.h>
#include <utility>

using namespace donut::math;
#include <donut/shaders/deferred_lighting_cb.h>

using namespace donut::engine;
using namespace donut::render;

void DeferredLightingPass::Inputs::SetGBuffer(const GBufferRenderTargets& targets)
{
    depth = targets.Depth;
    gbufferNormals = targets.GBufferNormals;
    gbufferDiffuse = targets.GBufferDiffuse;
    gbufferSpecular = targets.GBufferSpecular;
    gbufferEmissive = targets.GBufferEmissive;
}

DeferredLightingPass::DeferredLightingPass(
    nvrhi::IDevice* device,
    std::shared_ptr<CommonRenderPasses> commonPasses)
    : m_Device(device)
    , m_BindingSets(device)
    , m_CommonPasses(std::move(commonPasses))
{
}

void donut::render::DeferredLightingPass::Init(const std::shared_ptr<engine::ShaderFactory>& shaderFactory)
{
    auto samplerDesc = nvrhi::SamplerDesc()
        .setAllAddressModes(nvrhi::SamplerAddressMode::Border)
        .setBorderColor(1.0f);
    m_ShadowSampler = m_Device->createSampler(samplerDesc);

    samplerDesc.setReductionType(nvrhi::SamplerReductionType::Comparison);
    m_ShadowSamplerComparison = m_Device->createSampler(samplerDesc);

    nvrhi::BufferDesc constantBufferDesc;
    constantBufferDesc.byteSize = sizeof(DeferredLightingConstants);
    constantBufferDesc.debugName = "DeferredLightingConstants";
    constantBufferDesc.isConstantBuffer = true;
    constantBufferDesc.isVolatile = true;
    constantBufferDesc.maxVersions = c_MaxRenderPassConstantBufferVersions;
    m_DeferredLightingCB = m_Device->createBuffer(constantBufferDesc);
    
    {
        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Compute;
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3),
            nvrhi::BindingLayoutItem::Texture_SRV(8),
            nvrhi::BindingLayoutItem::Texture_SRV(9),
            nvrhi::BindingLayoutItem::Texture_SRV(10),
            nvrhi::BindingLayoutItem::Texture_SRV(11),
            nvrhi::BindingLayoutItem::Texture_SRV(12),
            nvrhi::BindingLayoutItem::Texture_SRV(14),
            nvrhi::BindingLayoutItem::Texture_SRV(15),
            nvrhi::BindingLayoutItem::Texture_SRV(16),
            nvrhi::BindingLayoutItem::Texture_SRV(17),
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Sampler(0),
            nvrhi::BindingLayoutItem::Sampler(1),
            nvrhi::BindingLayoutItem::Sampler(2),
            nvrhi::BindingLayoutItem::Sampler(3)
        };
        m_BindingLayout = m_Device->createBindingLayout(layoutDesc);
        
        nvrhi::ComputePipelineDesc pipelineDesc;
        pipelineDesc.CS = CreateComputeShader(*shaderFactory);
        pipelineDesc.bindingLayouts = { m_BindingLayout };
        
        m_Pso = m_Device->createComputePipeline(pipelineDesc);
    }
}

nvrhi::ShaderHandle DeferredLightingPass::CreateComputeShader(ShaderFactory& shaderFactory)
{
    return shaderFactory.CreateShader("donut/passes/deferred_lighting_cs.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
}

void DeferredLightingPass::Render(
    nvrhi::ICommandList* commandList,
    const ICompositeView& compositeView,
    const Inputs& inputs,
    dm::float2 randomOffset)
{
    assert(inputs.depth);
    assert(inputs.gbufferNormals);
    assert(inputs.gbufferDiffuse);
    assert(inputs.gbufferSpecular);
    assert(inputs.gbufferEmissive);
    assert(inputs.output);

    commandList->beginMarker("DeferredLighting");

    DeferredLightingConstants deferredConstants = {};
    deferredConstants.randomOffset = randomOffset;
    deferredConstants.noisePattern[0] = float4(0.059f, 0.529f, 0.176f, 0.647f);
    deferredConstants.noisePattern[1] = float4(0.765f, 0.294f, 0.882f, 0.412f);
    deferredConstants.noisePattern[2] = float4(0.235f, 0.706f, 0.118f, 0.588f);
    deferredConstants.noisePattern[3] = float4(0.941f, 0.471f, 0.824f, 0.353f);
    deferredConstants.ambientColorTop = float4(inputs.ambientColorTop, 0.f);
    deferredConstants.ambientColorBottom = float4(inputs.ambientColorBottom, 0.f);
    deferredConstants.enableAmbientOcclusion = (inputs.ambientOcclusion != nullptr);
    deferredConstants.indirectDiffuseScale = 1.f;
    deferredConstants.indirectSpecularScale = 1.f;

    nvrhi::ITexture* shadowMapTexture = nullptr;

    int numShadows = 0;

    if (inputs.lights)
    {
        for (const auto& light : *inputs.lights)
        {
            if (light->shadowMap)
            {
                if (!shadowMapTexture)
                {
                    shadowMapTexture = light->shadowMap->GetTexture();
                    deferredConstants.shadowMapTextureSize = float2(light->shadowMap->GetTextureSize());
                }
                else
                {
                    if (shadowMapTexture != light->shadowMap->GetTexture())
                    {
                        log::error("All lights submitted to DeferredLightingPass::Render(...) must use the same shadow map textures");
                        return;
                    }
                }
            }

            if (deferredConstants.numLights >= DEFERRED_MAX_LIGHTS)
            {
                log::warning("Maximum number of active lights (%d) exceeded in DeferredLightingPass",
                    DEFERRED_MAX_LIGHTS);
                break;
            }

            LightConstants& lightConstants = deferredConstants.lights[deferredConstants.numLights];
            light->FillLightConstants(lightConstants);

            if (light->shadowMap)
            {
                for (uint32_t cascade = 0; cascade < light->shadowMap->GetNumberOfCascades(); cascade++)
                {
                    if (numShadows < DEFERRED_MAX_SHADOWS)
                    {
                        light->shadowMap->GetCascade(cascade)->FillShadowConstants(deferredConstants.shadows[numShadows]);
                        lightConstants.shadowCascades[cascade] = numShadows;
                        ++numShadows;
                    }
                }

                for (uint32_t perObjectShadow = 0; perObjectShadow < light->shadowMap->GetNumberOfPerObjectShadows(); perObjectShadow++)
                {
                    if (numShadows < DEFERRED_MAX_SHADOWS)
                    {
                        light->shadowMap->GetPerObjectShadow(perObjectShadow)->FillShadowConstants(deferredConstants.shadows[numShadows]);
                        lightConstants.perObjectShadows[perObjectShadow] = numShadows;
                        ++numShadows;
                    }
                }
            }

            ++deferredConstants.numLights;
        }
    }

    nvrhi::ITexture* lightProbeDiffuse = nullptr;
    nvrhi::ITexture* lightProbeSpecular = nullptr;
    nvrhi::ITexture* lightProbeEnvironmentBrdf = nullptr;

    if (inputs.lightProbes)
    {
        for (const auto& probe : *inputs.lightProbes)
        {
            if (!probe->IsActive())
                continue;

            LightProbeConstants& lightProbeConstants = deferredConstants.lightProbes[deferredConstants.numLightProbes];
            probe->FillLightProbeConstants(lightProbeConstants);

            ++deferredConstants.numLightProbes;

            if (deferredConstants.numLightProbes >= DEFERRED_MAX_LIGHT_PROBES)
            {
                log::warning("Maximum number of active light probes (%d) exceeded in DeferredLightingPass",
                    DEFERRED_MAX_LIGHT_PROBES);
                break;
            }

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
                    log::error("All light probes submitted to DeferredLightingPass::Render(...) must use the same set of textures");
                    return;
                }
            }
        }
    }


    for (uint viewIndex = 0; viewIndex < compositeView.GetNumChildViews(ViewType::PLANAR); viewIndex++)
    {
        const IView* view = compositeView.GetChildView(ViewType::PLANAR, viewIndex);
        auto viewSubresources = view->GetSubresources();

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_DeferredLightingCB),
            nvrhi::BindingSetItem::Texture_SRV(0, shadowMapTexture ? shadowMapTexture : m_CommonPasses->m_BlackTexture2DArray.Get()),
            nvrhi::BindingSetItem::Texture_SRV(1, lightProbeDiffuse ? lightProbeDiffuse : m_CommonPasses->m_BlackCubeMapArray.Get()),
            nvrhi::BindingSetItem::Texture_SRV(2, lightProbeSpecular ? lightProbeSpecular : m_CommonPasses->m_BlackCubeMapArray.Get()),
            nvrhi::BindingSetItem::Texture_SRV(3, lightProbeEnvironmentBrdf ? lightProbeEnvironmentBrdf : m_CommonPasses->m_BlackTexture.Get()),
            nvrhi::BindingSetItem::Texture_SRV(8, inputs.depth, nvrhi::Format::UNKNOWN, viewSubresources),
            nvrhi::BindingSetItem::Texture_SRV(9, inputs.gbufferDiffuse, nvrhi::Format::UNKNOWN, viewSubresources),
            nvrhi::BindingSetItem::Texture_SRV(10, inputs.gbufferSpecular, nvrhi::Format::UNKNOWN, viewSubresources),
            nvrhi::BindingSetItem::Texture_SRV(11, inputs.gbufferNormals, nvrhi::Format::UNKNOWN, viewSubresources),
            nvrhi::BindingSetItem::Texture_SRV(12, inputs.gbufferEmissive, nvrhi::Format::UNKNOWN, viewSubresources),
            nvrhi::BindingSetItem::Texture_SRV(14, inputs.indirectDiffuse ? inputs.indirectDiffuse : m_CommonPasses->m_BlackTexture.Get(), nvrhi::Format::UNKNOWN, viewSubresources),
            nvrhi::BindingSetItem::Texture_SRV(15, inputs.indirectSpecular ? inputs.indirectSpecular : m_CommonPasses->m_BlackTexture.Get(), nvrhi::Format::UNKNOWN, viewSubresources),
            nvrhi::BindingSetItem::Texture_SRV(16, inputs.shadowChannels ? inputs.shadowChannels : m_CommonPasses->m_BlackTexture.Get()),
            nvrhi::BindingSetItem::Texture_SRV(17, inputs.ambientOcclusion ? inputs.ambientOcclusion : m_CommonPasses->m_WhiteTexture.Get(), nvrhi::Format::UNKNOWN, viewSubresources),
            nvrhi::BindingSetItem::Texture_UAV(0, inputs.output, nvrhi::Format::UNKNOWN, viewSubresources),
            nvrhi::BindingSetItem::Sampler(0, m_ShadowSampler),
            nvrhi::BindingSetItem::Sampler(1, m_ShadowSamplerComparison),
            nvrhi::BindingSetItem::Sampler(2, m_CommonPasses->m_LinearWrapSampler),
            nvrhi::BindingSetItem::Sampler(3, m_CommonPasses->m_LinearClampSampler)
        };

        nvrhi::BindingSetHandle bindingSet = m_BindingSets.GetOrCreateBindingSet(bindingSetDesc, m_BindingLayout);
    
        view->FillPlanarViewConstants(deferredConstants.view);
        commandList->writeBuffer(m_DeferredLightingCB, &deferredConstants, sizeof(deferredConstants));

        nvrhi::ComputeState state;
        state.pipeline = m_Pso;
        state.bindings = { bindingSet };
        commandList->setComputeState(state);

        auto viewExtent = view->GetViewExtent();
        commandList->dispatch(
            dm::div_ceil(viewExtent.width(), 16),
            dm::div_ceil(viewExtent.height(), 16));
    }

    commandList->endMarker();
}

void DeferredLightingPass::ResetBindingCache()
{
    m_BindingSets.Clear();
}
