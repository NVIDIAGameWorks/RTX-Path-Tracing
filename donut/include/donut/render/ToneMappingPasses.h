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

#pragma once

#include <nvrhi/nvrhi.h>
#include <memory>
#include <unordered_map>

namespace donut::engine
{
    class ShaderFactory;
    class CommonRenderPasses;
    class FramebufferFactory;
    class ICompositeView;
}

namespace donut::render
{
    struct ToneMappingParameters
    {
        float histogramLowPercentile = 0.8f;
        float histogramHighPercentile = 0.95f;
        float eyeAdaptationSpeedUp = 1.f;
        float eyeAdaptationSpeedDown = 0.5f;
        float minAdaptedLuminance = 0.02f;
        float maxAdaptedLuminance = 0.5f;
        float exposureBias = -0.5f;
        float whitePoint = 3.f;
        bool enableColorLUT = true;
    };

    class ToneMappingPass
    {
    private:

        nvrhi::DeviceHandle m_Device;
        nvrhi::ShaderHandle m_PixelShader;
        nvrhi::ShaderHandle m_HistogramComputeShader;
        nvrhi::ShaderHandle m_ExposureComputeShader;
        uint32_t m_HistogramBins;

        nvrhi::BufferHandle m_ToneMappingCB;
        nvrhi::BufferHandle m_HistogramBuffer;
        nvrhi::BufferHandle m_ExposureBuffer;
        float m_FrameTime = 0.f;

        nvrhi::TextureHandle m_ColorLUT;
        float m_ColorLUTSize = 0.f;

        nvrhi::BindingLayoutHandle m_HistogramBindingLayout;
        nvrhi::ComputePipelineHandle m_HistogramPso;

        nvrhi::BindingLayoutHandle m_ExposureBindingLayout;
        nvrhi::BindingSetHandle m_ExposureBindingSet;
        nvrhi::ComputePipelineHandle m_ExposurePso;

        nvrhi::BindingLayoutHandle m_RenderBindingLayout;
        nvrhi::GraphicsPipelineHandle m_RenderPso;

        std::shared_ptr<engine::CommonRenderPasses> m_CommonPasses;
        std::shared_ptr<engine::FramebufferFactory> m_FramebufferFactory;

        std::unordered_map<nvrhi::ITexture*, nvrhi::BindingSetHandle> m_HistogramBindingSets;
        std::unordered_map<nvrhi::ITexture*, nvrhi::BindingSetHandle> m_RenderBindingSets;

    public:
        struct CreateParameters
        {
            bool isTextureArray = false;
            uint32_t histogramBins = 256;
            uint32_t numConstantBufferVersions = 16;
            nvrhi::IBuffer* exposureBufferOverride = nullptr;
            nvrhi::ITexture* colorLUT = nullptr;
        };

        ToneMappingPass(
            nvrhi::IDevice* device,
            std::shared_ptr<engine::ShaderFactory> shaderFactory,
            std::shared_ptr<engine::CommonRenderPasses> commonPasses,
            std::shared_ptr<engine::FramebufferFactory> framebufferFactory,
            const engine::ICompositeView& compositeView,
            const CreateParameters& params);

        void Render(
            nvrhi::ICommandList* commandList,
            const ToneMappingParameters& params,
            const engine::ICompositeView& compositeView,
            nvrhi::ITexture* sourceTexture);

        void SimpleRender(
            nvrhi::ICommandList* commandList,
            const ToneMappingParameters& params,
            const engine::ICompositeView& compositeView,
            nvrhi::ITexture* sourceTexture);

        nvrhi::BufferHandle GetExposureBuffer();
        
        void AdvanceFrame(float frameTime);

        void ResetExposure(nvrhi::ICommandList* commandList, float initialExposure = 0.f);
        void ResetHistogram(nvrhi::ICommandList* commandList);
        void AddFrameToHistogram(nvrhi::ICommandList* commandList, const engine::ICompositeView& compositeView, nvrhi::ITexture* sourceTexture);
        void ComputeExposure(nvrhi::ICommandList* commandList, const ToneMappingParameters& params);
    };
}