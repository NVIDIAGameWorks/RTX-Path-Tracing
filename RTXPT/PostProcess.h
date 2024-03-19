/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#include <donut/engine/BindingCache.h>
#include <nvrhi/nvrhi.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/core/math/math.h>
#include <memory>

using namespace donut::math;

#include "PathTracer/PathTracerShared.h"
#include "RenderTargets.h"
#include "PostProcess.hlsl"
#include "SampleConstantBuffer.h"

namespace donut::engine
{
    class FramebufferFactory;
}

class PostProcess 
{
public:
    enum class ComputePassType
    {
        StablePlanesDebugViz,
        RELAXDenoiserPrepareInputs,
        REBLURDenoiserPrepareInputs,
        RELAXDenoiserFinalMerge,
        REBLURDenoiserFinalMerge,
        DummyPlaceholder,

        MaxCount
    };

    enum class RenderPassType : uint32_t
    {
        Debug_BlendDebugViz,

        MaxCount
    };

private:

    nvrhi::DeviceHandle             m_Device;
    std::shared_ptr<donut::engine::CommonRenderPasses> m_CommonPasses;

    nvrhi::ShaderHandle             m_RenderShaders[(uint32_t)RenderPassType::MaxCount];
    nvrhi::GraphicsPipelineHandle   m_RenderPSOs[(uint32_t)RenderPassType::MaxCount];
    nvrhi::ShaderHandle             m_ComputeShaders[(uint32_t)ComputePassType::MaxCount];
    nvrhi::ComputePipelineHandle    m_ComputePSOs[(uint32_t)ComputePassType::MaxCount];

    nvrhi::SamplerHandle            m_PointSampler;
    nvrhi::SamplerHandle            m_LinearSampler;

    nvrhi::BindingLayoutHandle      m_BindingLayoutPS;
    nvrhi::BindingSetHandle         m_BindingSetPS;
    nvrhi::BindingLayoutHandle      m_BindingLayoutCS;
    nvrhi::BindingSetHandle         m_BindingSetCS;

    donut::engine::BindingCache     m_BindingCache;

public:

    PostProcess(
        nvrhi::IDevice* device,
        std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
        std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses
        //, std::shared_ptr<engine::FramebufferFactory> colorFramebufferFactory
    );

    void Apply(nvrhi::ICommandList* commandList, RenderPassType passType, nvrhi::BufferHandle consts, SampleMiniConstants & miniConsts, nvrhi::IFramebuffer* targetFramebuffer, RenderTargets & renderTargets, nvrhi::ITexture* sourceTexture);
    void Apply(nvrhi::ICommandList* commandList, ComputePassType passType, nvrhi::BufferHandle consts, SampleMiniConstants & miniConsts, nvrhi::BindingSetHandle bindingSet, nvrhi::BindingLayoutHandle bindingLayout, uint32_t width, uint32_t height);
    void Apply(nvrhi::ICommandList* commandList, ComputePassType passType, int pass, nvrhi::BufferHandle consts, SampleMiniConstants & miniConsts, nvrhi::ITexture* workTexture, RenderTargets & renderTargets, nvrhi::ITexture* sourceTexture);
};
