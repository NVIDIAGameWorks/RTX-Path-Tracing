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

#include <donut/render/GBuffer.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/FramebufferFactory.h>
#include <nvrhi/utils.h>

using namespace donut::math;
using namespace donut::engine;
using namespace donut::render;

// Resources

void GBufferRenderTargets::Init(
    nvrhi::IDevice* device,
    uint2 size, 
    uint sampleCount,
    bool enableMotionVectors,
    bool useReverseProjection)
{
    nvrhi::TextureDesc desc;
    desc.width = size.x;
    desc.height = size.y;
    desc.initialState = nvrhi::ResourceStates::RenderTarget;
    desc.isRenderTarget = true;
    desc.useClearValue = true;
    desc.clearValue = nvrhi::Color(0.f);
    desc.sampleCount = sampleCount;
    desc.dimension = sampleCount > 1 ? nvrhi::TextureDimension::Texture2DMS : nvrhi::TextureDimension::Texture2D;
    desc.keepInitialState = true;
    desc.isTypeless = false;
    desc.isUAV = false;
    desc.mipLevels = 1;

    desc.format = nvrhi::Format::SRGBA8_UNORM;
    desc.debugName = "GBufferDiffuse";
    GBufferDiffuse = device->createTexture(desc);

    desc.format = nvrhi::Format::SRGBA8_UNORM;
    desc.debugName = "GBufferSpecular";
    GBufferSpecular = device->createTexture(desc);

    desc.format = nvrhi::Format::RGBA16_SNORM;
    desc.debugName = "GBufferNormals";
    GBufferNormals = device->createTexture(desc);

    desc.format = nvrhi::Format::RGBA16_FLOAT;
    desc.debugName = "GBufferEmissive";
    GBufferEmissive = device->createTexture(desc);

    const nvrhi::Format depthFormats[] = {
        nvrhi::Format::D24S8,
        nvrhi::Format::D32S8,
        nvrhi::Format::D32,
        nvrhi::Format::D16 };

    const nvrhi::FormatSupport depthFeatures = 
        nvrhi::FormatSupport::Texture |
        nvrhi::FormatSupport::DepthStencil |
        nvrhi::FormatSupport::ShaderLoad;

    desc.format = nvrhi::utils::ChooseFormat(device, depthFeatures, depthFormats, std::size(depthFormats));
    desc.isTypeless = true;
    desc.initialState = nvrhi::ResourceStates::DepthWrite;
    desc.clearValue = useReverseProjection ? nvrhi::Color(0.f) : nvrhi::Color(1.f);
    desc.debugName = "GBufferDepth";
    Depth = device->createTexture(desc);

    desc.isTypeless = false;
    desc.format = nvrhi::Format::RG16_FLOAT;
    desc.initialState = nvrhi::ResourceStates::RenderTarget;
    desc.debugName = "GBufferMotionVectors";
    desc.clearValue = nvrhi::Color(0.f);
    if (!enableMotionVectors)
    {
        desc.width = 1;
        desc.height = 1;
    }
    MotionVectors = device->createTexture(desc);

    GBufferFramebuffer = std::make_shared<FramebufferFactory>(device);
    GBufferFramebuffer->RenderTargets = {
        GBufferDiffuse,
        GBufferSpecular,
        GBufferNormals,
        GBufferEmissive };

    if (enableMotionVectors)
        GBufferFramebuffer->RenderTargets.push_back(MotionVectors);

    GBufferFramebuffer->DepthTarget = Depth;

    m_Size = size;
    m_SampleCount = sampleCount;
    m_UseReverseProjection = useReverseProjection;
}

void GBufferRenderTargets::Clear(nvrhi::ICommandList* commandList)
{
    const nvrhi::FormatInfo& depthFormatInfo = nvrhi::getFormatInfo(Depth->getDesc().format);

    float depthClearValue = m_UseReverseProjection ? 0.f : 1.f;
    commandList->clearDepthStencilTexture(Depth, nvrhi::AllSubresources, true, depthClearValue, depthFormatInfo.hasStencil, 0);
    commandList->clearTextureFloat(GBufferDiffuse, nvrhi::AllSubresources, nvrhi::Color(0.f));
    commandList->clearTextureFloat(GBufferSpecular, nvrhi::AllSubresources, nvrhi::Color(0.f));
    commandList->clearTextureFloat(GBufferNormals, nvrhi::AllSubresources, nvrhi::Color(0.f));
    commandList->clearTextureFloat(GBufferEmissive, nvrhi::AllSubresources, nvrhi::Color(0.f));
    commandList->clearTextureFloat(MotionVectors, nvrhi::AllSubresources, nvrhi::Color(0.f));
}
