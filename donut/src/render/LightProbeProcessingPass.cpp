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

#include <donut/render/LightProbeProcessingPass.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/View.h>

using namespace donut::math;
#include <donut/shaders/light_probe_cb.h>

#include <assert.h>

using namespace donut::engine;
using namespace donut::render;

LightProbeProcessingPass::LightProbeProcessingPass(
    nvrhi::IDevice* device,
    std::shared_ptr<ShaderFactory> shaderFactory,
    std::shared_ptr<CommonRenderPasses> commonPasses,
    uint32_t intermediateTextureSize,
    nvrhi::Format intermediateTextureFormat)
    : m_Device(device)
    , m_CommonPasses(commonPasses)
    , m_IntermediateTextureSize(intermediateTextureSize)
{
    m_GeometryShader = shaderFactory->CreateShader("donut/passes/light_probe.hlsl", "cubemap_gs", nullptr, nvrhi::ShaderType::Geometry);
    m_MipPixelShader = shaderFactory->CreateShader("donut/passes/light_probe.hlsl", "mip_ps", nullptr, nvrhi::ShaderType::Pixel);
    m_DiffusePixelShader = shaderFactory->CreateShader("donut/passes/light_probe.hlsl", "diffuse_probe_ps", nullptr, nvrhi::ShaderType::Pixel);
    m_SpecularPixelShader = shaderFactory->CreateShader("donut/passes/light_probe.hlsl", "specular_probe_ps", nullptr, nvrhi::ShaderType::Pixel);
    m_EnvironmentBrdfPixelShader = shaderFactory->CreateShader("donut/passes/light_probe.hlsl", "environment_brdf_ps", nullptr, nvrhi::ShaderType::Pixel);

    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::Pixel;
    layoutDesc.bindings = {
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::Sampler(0),
        nvrhi::BindingLayoutItem::Texture_SRV(0),
    };
    m_BindingLayout = device->createBindingLayout(layoutDesc);

    nvrhi::BufferDesc constantBufferDesc;
    constantBufferDesc.byteSize = sizeof(LightProbeConstants);
    constantBufferDesc.debugName = "LightProbeConstants";
    constantBufferDesc.isConstantBuffer = true;
    constantBufferDesc.isVolatile = true;
    constantBufferDesc.maxVersions = 64;
    m_LightProbeCB = device->createBuffer(constantBufferDesc);

    assert(intermediateTextureSize > 0);

    nvrhi::TextureDesc cubemapDesc;
    cubemapDesc.arraySize = 6;
    cubemapDesc.width = intermediateTextureSize;
    cubemapDesc.height = intermediateTextureSize;
    cubemapDesc.mipLevels = static_cast<uint32_t>(floor(dm::log2f(static_cast<float>(intermediateTextureSize)))) + 1;
    cubemapDesc.dimension = nvrhi::TextureDimension::TextureCube;
    cubemapDesc.isRenderTarget = true;
    cubemapDesc.format = intermediateTextureFormat;
    cubemapDesc.initialState = nvrhi::ResourceStates::RenderTarget;
    cubemapDesc.keepInitialState = true;
    cubemapDesc.clearValue = nvrhi::Color(0.f);
    cubemapDesc.useClearValue = true;

    m_IntermediateTexture = m_Device->createTexture(cubemapDesc);

    m_EnvironmentBrdfTextureSize = 64;

    nvrhi::TextureDesc brdfTextureDesc;
    brdfTextureDesc.width = m_EnvironmentBrdfTextureSize;
    brdfTextureDesc.height = m_EnvironmentBrdfTextureSize;
    brdfTextureDesc.format = nvrhi::Format::RG16_FLOAT;
    brdfTextureDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    brdfTextureDesc.keepInitialState = true;
    brdfTextureDesc.isRenderTarget = true;
    brdfTextureDesc.clearValue = nvrhi::Color(0.f);
    brdfTextureDesc.useClearValue = true;
    brdfTextureDesc.debugName = "EnvironmentBrdf";

    m_EnvironmentBrdfTexture = m_Device->createTexture(brdfTextureDesc);
}

nvrhi::FramebufferHandle LightProbeProcessingPass::GetCachedFramebuffer(nvrhi::ITexture* texture, nvrhi::TextureSubresourceSet subresources)
{
    TextureSubresourcesKey key;
    key.texture = texture;
    key.subresources = subresources;

    nvrhi::FramebufferHandle& framebuffer = m_FramebufferCache[key];
    if (!framebuffer)
    {
        framebuffer = m_Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(texture, subresources));
    }

    return framebuffer;
}


nvrhi::BindingSetHandle LightProbeProcessingPass::GetCachedBindingSet(nvrhi::ITexture* texture, nvrhi::TextureSubresourceSet subresources)
{
    TextureSubresourcesKey key;
    key.texture = texture;
    key.subresources = subresources;

    nvrhi::BindingSetHandle& bindingSet = m_BindingSetCache[key];
    if (!bindingSet)
    {
        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_LightProbeCB),
            nvrhi::BindingSetItem::Sampler(0, m_CommonPasses->m_LinearWrapSampler),
            nvrhi::BindingSetItem::Texture_SRV(0, texture, nvrhi::Format::UNKNOWN, subresources),
        };

        bindingSet = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);
    }

    return bindingSet;
}

void LightProbeProcessingPass::BlitCubemap(nvrhi::ICommandList* commandList, nvrhi::ITexture* inCubeMap, uint32_t inBaseArraySlice, uint32_t inMipLevel, nvrhi::ITexture* outCubeMap, uint32_t outBaseArraySlice, uint32_t outMipLevel)
{
    const nvrhi::TextureDesc& inputDesc = inCubeMap->getDesc();
    assert(inputDesc.dimension == nvrhi::TextureDimension::TextureCube || inputDesc.dimension == nvrhi::TextureDimension::TextureCubeArray);

    const nvrhi::TextureDesc& outputDesc = outCubeMap->getDesc();
    assert(outputDesc.dimension == nvrhi::TextureDimension::TextureCube || outputDesc.dimension == nvrhi::TextureDimension::TextureCubeArray);

    
    nvrhi::FramebufferHandle framebuffer = GetCachedFramebuffer(outCubeMap, nvrhi::TextureSubresourceSet(outMipLevel, 1, outBaseArraySlice, 6));

    nvrhi::GraphicsPipelineHandle& pso = m_BlitPsoCache[framebuffer->getFramebufferInfo()];

    if (!pso)
    {
        nvrhi::GraphicsPipelineDesc psoDesc;
        psoDesc.VS = m_CommonPasses->m_FullscreenVS;
        psoDesc.GS = m_GeometryShader;
        psoDesc.PS = m_MipPixelShader;
        psoDesc.bindingLayouts = { m_BindingLayout };
        psoDesc.primType = nvrhi::PrimitiveType::TriangleStrip;
        psoDesc.renderState.rasterState.setCullNone();
        psoDesc.renderState.depthStencilState.depthTestEnable = false;
        psoDesc.renderState.depthStencilState.stencilEnable = false;
        pso = m_Device->createGraphicsPipeline(psoDesc, framebuffer);
    }

    LightProbeConstants constants = {};
    commandList->writeBuffer(m_LightProbeCB, &constants, sizeof(constants));

    nvrhi::BindingSetHandle bindingSet = GetCachedBindingSet(inCubeMap, nvrhi::TextureSubresourceSet(inMipLevel, 1, inBaseArraySlice, 6));

    float mipSize = ceilf(float(outputDesc.width) * powf(0.5f, float(outMipLevel)));

    nvrhi::GraphicsState state;
    state.pipeline = pso;
    state.framebuffer = framebuffer;
    state.bindings = { bindingSet };
    state.viewport.scissorRects = { nvrhi::Rect(int(mipSize), int(mipSize)) };
    state.viewport.viewports = { nvrhi::Viewport(mipSize, mipSize) };
    commandList->setGraphicsState(state);

    nvrhi::DrawArguments args;
    args.instanceCount = 1;
    args.vertexCount = 4;
    commandList->draw(args);
}

void LightProbeProcessingPass::GenerateCubemapMips(
    nvrhi::ICommandList* commandList, 
    nvrhi::ITexture* cubeMap,
    uint32_t baseArraySlice,
    uint32_t sourceMipLevel, 
    uint32_t levelsToGenerate)
{
    commandList->beginMarker("Cubemap Mips");

    for (uint32_t index = 0; index < levelsToGenerate; index++)
    {
        uint32_t mipLevel = sourceMipLevel + index;
        BlitCubemap(commandList, cubeMap, baseArraySlice, mipLevel, cubeMap, baseArraySlice, mipLevel + 1);
    }

    commandList->endMarker();
}

void LightProbeProcessingPass::RenderDiffuseMap(
    nvrhi::ICommandList* commandList,
    nvrhi::ITexture* inEnvironmentMap,
    nvrhi::TextureSubresourceSet inSubresources,
    nvrhi::ITexture* outDiffuseMap,
    uint32_t outBaseArraySlice,
    uint32_t outMipLevel)
{
    const nvrhi::TextureDesc& inDesc = inEnvironmentMap->getDesc();
    assert(inDesc.dimension == nvrhi::TextureDimension::TextureCube || inDesc.dimension == nvrhi::TextureDimension::TextureCubeArray);
    float inputSize = ceilf(float(inDesc.width) * powf(0.5f, float(inSubresources.baseMipLevel)));

    const nvrhi::TextureDesc& outDesc = outDiffuseMap->getDesc();
    assert(outDesc.dimension == nvrhi::TextureDimension::TextureCube || outDesc.dimension == nvrhi::TextureDimension::TextureCubeArray);
    float outputSize = ceilf(float(outDesc.width) * powf(0.5f, float(outMipLevel)));

    uint32_t intermediateMipLevel = static_cast<uint32_t>(std::max(0.f, dm::log2f(float(m_IntermediateTextureSize) / outputSize) - 2.f));
    float intermediateSize = ceilf(float(m_IntermediateTextureSize) * powf(0.5f, float(intermediateMipLevel)));

    commandList->beginMarker("Diffuse Light Probe");

    nvrhi::FramebufferHandle framebuffer = GetCachedFramebuffer(m_IntermediateTexture, nvrhi::TextureSubresourceSet(intermediateMipLevel, 1, 0, 6));

    nvrhi::GraphicsPipelineHandle& pso = m_DiffusePsoCache[framebuffer->getFramebufferInfo()];

    if (!pso)
    {
        nvrhi::GraphicsPipelineDesc psoDesc;
        psoDesc.VS = m_CommonPasses->m_FullscreenVS;
        psoDesc.GS = m_GeometryShader;
        psoDesc.PS = m_DiffusePixelShader;
        psoDesc.bindingLayouts = { m_BindingLayout };
        psoDesc.primType = nvrhi::PrimitiveType::TriangleStrip;
        psoDesc.renderState.rasterState.setCullNone();
        psoDesc.renderState.depthStencilState.depthTestEnable = false;
        psoDesc.renderState.depthStencilState.stencilEnable = false;
        pso = m_Device->createGraphicsPipeline(psoDesc, framebuffer);
    }

    LightProbeConstants constants = {};
    constants.sampleCount = 4096;
    constants.lodBias = 1.0f + 0.5f * dm::log2f((inputSize * inputSize) / constants.sampleCount);
    commandList->writeBuffer(m_LightProbeCB, &constants, sizeof(constants));

    nvrhi::BindingSetHandle bindingSet = GetCachedBindingSet(inEnvironmentMap, inSubresources);

    nvrhi::GraphicsState state;
    state.pipeline = pso;
    state.framebuffer = framebuffer;
    state.bindings = { bindingSet };
    state.viewport.scissorRects = { nvrhi::Rect(int(intermediateSize), int(intermediateSize)) };
    state.viewport.viewports = { nvrhi::Viewport(intermediateSize, intermediateSize) };
    commandList->setGraphicsState(state);

    nvrhi::DrawArguments args;
    args.instanceCount = 1;
    args.vertexCount = 4;
    commandList->draw(args);

    BlitCubemap(commandList, m_IntermediateTexture, 0, intermediateMipLevel, m_IntermediateTexture, 0, intermediateMipLevel + 1);
    BlitCubemap(commandList, m_IntermediateTexture, 0, intermediateMipLevel + 1, outDiffuseMap, outBaseArraySlice, outMipLevel);
    
    commandList->endMarker();
}

void LightProbeProcessingPass::RenderSpecularMap(nvrhi::ICommandList* commandList, float roughness, nvrhi::ITexture* inEnvironmentMap, nvrhi::TextureSubresourceSet inSubresources, nvrhi::ITexture* outDiffuseMap, uint32_t outBaseArraySlice, uint32_t outMipLevel)
{
    const nvrhi::TextureDesc& inDesc = inEnvironmentMap->getDesc();
    assert(inDesc.dimension == nvrhi::TextureDimension::TextureCube || inDesc.dimension == nvrhi::TextureDimension::TextureCubeArray);
    float inputSize = ceilf(float(inDesc.width) * powf(0.5f, float(inSubresources.baseMipLevel)));

    const nvrhi::TextureDesc& outDesc = outDiffuseMap->getDesc();
    assert(outDesc.dimension == nvrhi::TextureDimension::TextureCube || outDesc.dimension == nvrhi::TextureDimension::TextureCubeArray);
    float outputSize = ceilf(float(outDesc.width) * powf(0.5f, float(outMipLevel)));

    uint32_t intermediateMipLevel = static_cast<uint32_t>(std::max(0.f, dm::log2f(float(m_IntermediateTextureSize) / outputSize) - 2.f));
    float intermediateSize = ceilf(float(m_IntermediateTextureSize) * powf(0.5f, float(intermediateMipLevel)));

    commandList->beginMarker("Specular Light Probe");

    nvrhi::FramebufferHandle framebuffer = GetCachedFramebuffer(m_IntermediateTexture, nvrhi::TextureSubresourceSet(intermediateMipLevel, 1, 0, 6));

    nvrhi::GraphicsPipelineHandle& pso = m_SpecularPsoCache[framebuffer->getFramebufferInfo()];

    if (!pso)
    {
        nvrhi::GraphicsPipelineDesc psoDesc;
        psoDesc.VS = m_CommonPasses->m_FullscreenVS;
        psoDesc.GS = m_GeometryShader;
        psoDesc.PS = m_SpecularPixelShader;
        psoDesc.bindingLayouts = { m_BindingLayout };
        psoDesc.primType = nvrhi::PrimitiveType::TriangleStrip;
        psoDesc.renderState.rasterState.setCullNone();
        psoDesc.renderState.depthStencilState.depthTestEnable = false;
        psoDesc.renderState.depthStencilState.stencilEnable = false;
        pso = m_Device->createGraphicsPipeline(psoDesc, framebuffer);
    }

    LightProbeConstants constants = {};
    constants.sampleCount = 1024;
    constants.lodBias = 1.0f;
    constants.inputCubeSize = inputSize;
    constants.roughness = std::max(0.01f, roughness);
    commandList->writeBuffer(m_LightProbeCB, &constants, sizeof(constants));

    nvrhi::BindingSetHandle bindingSet = GetCachedBindingSet(inEnvironmentMap, inSubresources);

    nvrhi::GraphicsState state;
    state.pipeline = pso;
    state.framebuffer = framebuffer;
    state.bindings = { bindingSet };
    state.viewport.scissorRects = { nvrhi::Rect(int(intermediateSize), int(intermediateSize)) };
    state.viewport.viewports = { nvrhi::Viewport(intermediateSize, intermediateSize) };
    commandList->setGraphicsState(state);

    nvrhi::DrawArguments args;
    args.instanceCount = 1;
    args.vertexCount = 4;
    commandList->draw(args);

    BlitCubemap(commandList, m_IntermediateTexture, 0, intermediateMipLevel, m_IntermediateTexture, 0, intermediateMipLevel + 1);
    BlitCubemap(commandList, m_IntermediateTexture, 0, intermediateMipLevel + 1, outDiffuseMap, outBaseArraySlice, outMipLevel);

    commandList->endMarker();
}

void LightProbeProcessingPass::RenderEnvironmentBrdfTexture(nvrhi::ICommandList* commandList)
{
    commandList->beginMarker("Environment BRDF");

    nvrhi::FramebufferHandle framebuffer = m_Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_EnvironmentBrdfTexture));

    nvrhi::GraphicsPipelineDesc psoDesc;
    psoDesc.VS = m_CommonPasses->m_FullscreenVS;
    psoDesc.PS = m_EnvironmentBrdfPixelShader;
    psoDesc.primType = nvrhi::PrimitiveType::TriangleStrip;
    psoDesc.renderState.rasterState.setCullNone();
    psoDesc.renderState.depthStencilState.depthTestEnable = false;
    psoDesc.renderState.depthStencilState.stencilEnable = false;
    nvrhi::GraphicsPipelineHandle pso = m_Device->createGraphicsPipeline(psoDesc, framebuffer);

    nvrhi::GraphicsState state;
    state.pipeline = pso;
    state.framebuffer = framebuffer;
    state.viewport.scissorRects = { nvrhi::Rect(m_EnvironmentBrdfTextureSize, m_EnvironmentBrdfTextureSize) };
    state.viewport.viewports = { nvrhi::Viewport(float(m_EnvironmentBrdfTextureSize), float(m_EnvironmentBrdfTextureSize)) };
    commandList->setGraphicsState(state);

    nvrhi::DrawArguments args;
    args.instanceCount = 1;
    args.vertexCount = 4;
    commandList->draw(args);

    commandList->endMarker();
}

nvrhi::ITexture* LightProbeProcessingPass::GetEnvironmentBrdfTexture()
{
    return m_EnvironmentBrdfTexture;
}

void LightProbeProcessingPass::ResetCaches()
{
    m_BlitPsoCache.clear();
    m_DiffusePsoCache.clear();
    m_FramebufferCache.clear();
    m_BindingSetCache.clear();
}
