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

#include <donut/render/PixelReadbackPass.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/CommonRenderPasses.h>

using namespace donut::math;
#include <donut/shaders/pixel_readback_cb.h>

using namespace donut::engine;
using namespace donut::render;

PixelReadbackPass::PixelReadbackPass(
    nvrhi::IDevice* device, 
    std::shared_ptr<ShaderFactory> shaderFactory, 
    nvrhi::ITexture* inputTexture, 
    nvrhi::Format format,
    uint32_t arraySlice,
    uint32_t mipLevel)
    : m_Device(device)
{
    const char* formatName = "";
    switch (format)
    {
    case nvrhi::Format::RGBA32_FLOAT: formatName = "float4"; break;
    case nvrhi::Format::RGBA32_UINT: formatName = "uint4"; break;
    case nvrhi::Format::RGBA32_SINT: formatName = "int4"; break;
    default: assert(!"unsupported readback format");
    }

    std::vector<ShaderMacro> macros;
    macros.push_back(ShaderMacro("TYPE", formatName));
    macros.push_back(ShaderMacro("INPUT_MSAA", inputTexture->getDesc().sampleCount > 1 ? "1" : "0"));
    m_Shader = shaderFactory->CreateShader("donut/passes/pixel_readback_cs.hlsl", "main", &macros, nvrhi::ShaderType::Compute);

    nvrhi::BufferDesc bufferDesc;
    bufferDesc.byteSize = 16;
    bufferDesc.format = format;
    bufferDesc.canHaveUAVs = true;
    bufferDesc.initialState = nvrhi::ResourceStates::CopySource;
    bufferDesc.keepInitialState = true;
    bufferDesc.debugName = "PixelReadbackPass/IntermediateBuffer";
    bufferDesc.canHaveTypedViews = true;
    m_IntermediateBuffer = m_Device->createBuffer(bufferDesc);

    bufferDesc.canHaveUAVs = false;
    bufferDesc.cpuAccess = nvrhi::CpuAccessMode::Read;
    bufferDesc.debugName = "PixelReadbackPass/ReadbackBuffer";
    m_ReadbackBuffer = m_Device->createBuffer(bufferDesc);

    nvrhi::BufferDesc constantBufferDesc;
    constantBufferDesc.byteSize = sizeof(PixelReadbackConstants);
    constantBufferDesc.isConstantBuffer = true;
    constantBufferDesc.isVolatile = true;
    constantBufferDesc.debugName = "PixelReadbackPass/Constants";
    constantBufferDesc.maxVersions = engine::c_MaxRenderPassConstantBufferVersions;
    m_ConstantBuffer = m_Device->createBuffer(constantBufferDesc);

    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::Compute;
    layoutDesc.bindings = { 
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::Texture_SRV(0),
        nvrhi::BindingLayoutItem::TypedBuffer_UAV(0)
    };

    m_BindingLayout = m_Device->createBindingLayout(layoutDesc);

    nvrhi::BindingSetDesc setDesc;
    setDesc.bindings = {
        nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
        nvrhi::BindingSetItem::Texture_SRV(0, inputTexture, nvrhi::Format::UNKNOWN, nvrhi::TextureSubresourceSet(mipLevel, 1, arraySlice, 1)),
        nvrhi::BindingSetItem::TypedBuffer_UAV(0, m_IntermediateBuffer)
    };

    m_BindingSet = m_Device->createBindingSet(setDesc, m_BindingLayout);

    nvrhi::ComputePipelineDesc pipelineDesc;
    pipelineDesc.bindingLayouts = { m_BindingLayout };
    pipelineDesc.CS = m_Shader;
    m_Pipeline = m_Device->createComputePipeline(pipelineDesc);
}


void PixelReadbackPass::Capture(nvrhi::ICommandList* commandList, dm::uint2 pixelPosition)
{
    PixelReadbackConstants constants = {};
    constants.pixelPosition = dm::int2(pixelPosition);
    commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));

    nvrhi::ComputeState state;
    state.pipeline = m_Pipeline;
    state.bindings = { m_BindingSet };
    commandList->setComputeState(state);
    commandList->dispatch(1, 1, 1);

    commandList->copyBuffer(m_ReadbackBuffer, 0, m_IntermediateBuffer, 0, m_ReadbackBuffer->getDesc().byteSize);
}

dm::float4 PixelReadbackPass::ReadFloats()
{
    void* pData = m_Device->mapBuffer(m_ReadbackBuffer, nvrhi::CpuAccessMode::Read);
    assert(pData);

    float4 values = *static_cast<float4*>(pData);

    m_Device->unmapBuffer(m_ReadbackBuffer);
    return values;
}

dm::uint4 PixelReadbackPass::ReadUInts()
{
    void* pData = m_Device->mapBuffer(m_ReadbackBuffer, nvrhi::CpuAccessMode::Read);
    assert(pData);

    uint4 values = *static_cast<uint4*>(pData);

    m_Device->unmapBuffer(m_ReadbackBuffer);
    return values;
}

dm::int4 PixelReadbackPass::ReadInts()
{
    void* pData = m_Device->mapBuffer(m_ReadbackBuffer, nvrhi::CpuAccessMode::Read);
    assert(pData);

    int4 values = *static_cast<int4*>(pData);

    m_Device->unmapBuffer(m_ReadbackBuffer);
    return values;
}
