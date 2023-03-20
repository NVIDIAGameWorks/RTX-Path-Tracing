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

#include <donut/render/SkyPass.h>
#include <donut/render/DrawStrategy.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/ShadowMap.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/View.h>

using namespace donut::math;
#include <donut/shaders/sky_cb.h>

using namespace donut::engine;
using namespace donut::render;

SkyPass::SkyPass(
    nvrhi::IDevice* device,
    const std::shared_ptr<engine::ShaderFactory>& shaderFactory,
    const std::shared_ptr<engine::CommonRenderPasses>& commonPasses,
    const std::shared_ptr<engine::FramebufferFactory>& framebufferFactory,
    const ICompositeView& compositeView)
    : m_FramebufferFactory(framebufferFactory)
{
    m_PixelShader = shaderFactory->CreateShader("donut/passes/sky_ps.hlsl", "main", nullptr, nvrhi::ShaderType::Pixel);

    nvrhi::BufferDesc constantBufferDesc;
    constantBufferDesc.byteSize = sizeof(SkyConstants);
    constantBufferDesc.debugName = "SkyConstants";
    constantBufferDesc.isConstantBuffer = true;
    constantBufferDesc.isVolatile = true;
    constantBufferDesc.maxVersions = engine::c_MaxRenderPassConstantBufferVersions;
    m_SkyCB = device->createBuffer(constantBufferDesc);

    const IView* sampleView = compositeView.GetChildView(ViewType::PLANAR, 0);
    nvrhi::IFramebuffer* sampleFramebuffer = m_FramebufferFactory->GetFramebuffer(*sampleView);

    {
        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Pixel;
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0)
        };
        m_RenderBindingLayout = device->createBindingLayout(layoutDesc);

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_SkyCB)
        };
        m_RenderBindingSet = device->createBindingSet(bindingSetDesc, m_RenderBindingLayout);

        nvrhi::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.primType = nvrhi::PrimitiveType::TriangleStrip;
        pipelineDesc.VS = sampleView->IsReverseDepth() ? commonPasses->m_FullscreenVS : commonPasses->m_FullscreenAtOneVS;
        pipelineDesc.PS = m_PixelShader;
        pipelineDesc.bindingLayouts = { m_RenderBindingLayout };

        pipelineDesc.renderState.rasterState.setCullNone();
        pipelineDesc.renderState.depthStencilState
            .enableDepthTest()
            .disableDepthWrite()
            .disableStencil()
            .setDepthFunc(sampleView->IsReverseDepth()
                ? nvrhi::ComparisonFunc::GreaterOrEqual
                : nvrhi::ComparisonFunc::LessOrEqual);

        m_RenderPso = device->createGraphicsPipeline(pipelineDesc, sampleFramebuffer);
    }
}

void SkyPass::Render(
    nvrhi::ICommandList* commandList,
    const ICompositeView& compositeView,
    const DirectionalLight& light,
    const SkyParameters& params) const
{
    commandList->beginMarker("Sky");

    for (uint viewIndex = 0; viewIndex < compositeView.GetNumChildViews(ViewType::PLANAR); viewIndex++)
    {
        const IView* view = compositeView.GetChildView(ViewType::PLANAR, viewIndex);

        nvrhi::GraphicsState state;
        state.pipeline = m_RenderPso;
        state.framebuffer = m_FramebufferFactory->GetFramebuffer(*view);
        state.bindings = { m_RenderBindingSet };
        state.viewport = view->GetViewportState();
        
        dm::affine viewToWorld = view->GetInverseViewMatrix();
        viewToWorld.m_translation = 0.f;
        dm::float4x4 clipToTranslatedWorld = view->GetInverseProjectionMatrix(true) * affineToHomogeneous(viewToWorld);

        SkyConstants skyConstants{};
        skyConstants.matClipToTranslatedWorld = clipToTranslatedWorld;
        FillShaderParameters(light, params, skyConstants.params);
        commandList->writeBuffer(m_SkyCB, &skyConstants, sizeof(skyConstants));

        commandList->setGraphicsState(state);

        nvrhi::DrawArguments args;
        args.instanceCount = 1;
        args.vertexCount = 4;
        commandList->draw(args);
    }

    commandList->endMarker();
}

void SkyPass::FillShaderParameters(const engine::DirectionalLight& light, const SkyParameters& input, ProceduralSkyShaderParameters& output)
{
    float lightAngularSize = dm::radians(clamp(light.angularSize, 0.1f, 90.f));
    float lightSolidAngle = 4 * dm::PI_f * square(sinf(lightAngularSize * 0.5f));
    float lightRadiance = light.irradiance / lightSolidAngle;
    if (input.maxLightRadiance > 0.f)
        lightRadiance = min(lightRadiance, input.maxLightRadiance);

    output.directionToLight = float3(normalize(-light.GetDirection()));
    output.angularSizeOfLight = lightAngularSize;
    output.lightColor = lightRadiance * light.color;
    output.glowSize = dm::radians(dm::clamp(input.glowSize, 0.f, 90.f));
    output.skyColor = input.skyColor * input.brightness;
    output.glowIntensity = dm::clamp(input.glowIntensity, 0.f, 1.f);
    output.horizonColor = input.horizonColor * input.brightness;
    output.horizonSize = dm::radians(dm::clamp(input.horizonSize, 0.f, 90.f));
    output.groundColor = input.groundColor * input.brightness;
    output.glowSharpness = dm::clamp(input.glowSharpness, 1.f, 10.f);
    output.directionUp = normalize(input.directionUp);
}
