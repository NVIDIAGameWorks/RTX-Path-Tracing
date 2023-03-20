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

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>
#include <memory>
#include <unordered_map>

namespace donut::engine
{
    class BindingCache;
    class ShaderFactory;

    constexpr uint32_t c_MaxRenderPassConstantBufferVersions = 16;
        
    enum class BlitSampler
    {
        Point,
        Linear,
        Sharpen
    };

    struct BlitParameters
    {
        nvrhi::IFramebuffer* targetFramebuffer = nullptr;
        nvrhi::Viewport targetViewport;
        dm::box2 targetBox = dm::box2(0.f, 1.f);

        nvrhi::ITexture* sourceTexture = nullptr;
        uint32_t sourceArraySlice = 0;
        uint32_t sourceMip = 0;
        dm::box2 sourceBox = dm::box2(0.f, 1.f);

        BlitSampler sampler = BlitSampler::Linear;
        nvrhi::BlendState::RenderTarget blendState;
        nvrhi::Color blendConstantColor = nvrhi::Color(0.f);
    };

    class CommonRenderPasses
    {
    protected:
        nvrhi::DeviceHandle m_Device;

        struct PsoCacheKey
        {
            nvrhi::FramebufferInfo fbinfo;
            nvrhi::IShader* shader;
            nvrhi::BlendState::RenderTarget blendState;

            bool operator==(const PsoCacheKey& other) const { return fbinfo == other.fbinfo && shader == other.shader && blendState == other.blendState; }
            bool operator!=(const PsoCacheKey& other) const { return !(*this == other); }

            struct Hash
            {
                size_t operator ()(const PsoCacheKey& s) const
                {
                    size_t hash = 0;
                    nvrhi::hash_combine(hash, s.fbinfo);
                    nvrhi::hash_combine(hash, s.shader);
                    nvrhi::hash_combine(hash, s.blendState);
                    return hash;
                }
            };
        };

        std::unordered_map<PsoCacheKey, nvrhi::GraphicsPipelineHandle, PsoCacheKey::Hash> m_BlitPsoCache;
        
    public:
        nvrhi::ShaderHandle m_FullscreenVS;
        nvrhi::ShaderHandle m_FullscreenAtOneVS;
        nvrhi::ShaderHandle m_RectVS;
        nvrhi::ShaderHandle m_BlitPS;
        nvrhi::ShaderHandle m_BlitArrayPS;
        nvrhi::ShaderHandle m_SharpenPS;
        nvrhi::ShaderHandle m_SharpenArrayPS;
        
        nvrhi::TextureHandle m_BlackTexture;
        nvrhi::TextureHandle m_GrayTexture;
        nvrhi::TextureHandle m_WhiteTexture;
        nvrhi::TextureHandle m_BlackTexture2DArray;
        nvrhi::TextureHandle m_WhiteTexture2DArray;
        nvrhi::TextureHandle m_BlackCubeMapArray;

        nvrhi::SamplerHandle m_PointClampSampler;
        nvrhi::SamplerHandle m_LinearClampSampler;
        nvrhi::SamplerHandle m_LinearWrapSampler;
        nvrhi::SamplerHandle m_AnisotropicWrapSampler;

        nvrhi::BindingLayoutHandle m_BlitBindingLayout;
        
        CommonRenderPasses(nvrhi::IDevice* device, std::shared_ptr<ShaderFactory> shaderFactory);
        
        void BlitTexture(nvrhi::ICommandList* commandList, const BlitParameters& params, BindingCache* bindingCache = nullptr);

        // Simplified form of BlitTexture that blits the entire source texture, mip 0 slice 0, into the entire target framebuffer using a linear sampler.
        void BlitTexture(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* targetFramebuffer, nvrhi::ITexture* sourceTexture, BindingCache* bindingCache = nullptr);
    };

}