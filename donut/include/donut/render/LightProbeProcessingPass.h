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
    class ShaderFactory;
    class CommonRenderPasses;
    class FramebufferFactory;
    class ICompositeView;
}

namespace donut::render
{
    class LightProbeProcessingPass
    {
    private:
        nvrhi::DeviceHandle m_Device;

        nvrhi::ShaderHandle m_GeometryShader;
        nvrhi::ShaderHandle m_MipPixelShader;
        nvrhi::ShaderHandle m_DiffusePixelShader;
        nvrhi::ShaderHandle m_SpecularPixelShader;
        nvrhi::ShaderHandle m_EnvironmentBrdfPixelShader;
        nvrhi::BufferHandle m_LightProbeCB;

        nvrhi::BindingLayoutHandle m_BindingLayout;

        nvrhi::TextureHandle m_IntermediateTexture;
        uint32_t m_IntermediateTextureSize;

        nvrhi::TextureHandle m_EnvironmentBrdfTexture;
        uint32_t m_EnvironmentBrdfTextureSize;

        std::shared_ptr<engine::CommonRenderPasses> m_CommonPasses;

        std::unordered_map<nvrhi::FramebufferInfo, nvrhi::GraphicsPipelineHandle> m_BlitPsoCache;
        std::unordered_map<nvrhi::FramebufferInfo, nvrhi::GraphicsPipelineHandle> m_DiffusePsoCache;
        std::unordered_map<nvrhi::FramebufferInfo, nvrhi::GraphicsPipelineHandle> m_SpecularPsoCache;

        struct TextureSubresourcesKey
        {
            nvrhi::TextureHandle texture;
            nvrhi::TextureSubresourceSet subresources;

            bool operator==(const TextureSubresourcesKey& other) const { return texture == other.texture && subresources == other.subresources; }
            bool operator!=(const TextureSubresourcesKey& other) const { return !(*this == other); }

            struct Hash
            {
                size_t operator ()(const TextureSubresourcesKey& s) const {
                    return (std::hash<nvrhi::ITexture*>()(s.texture) << 1)
                        ^ std::hash<nvrhi::TextureSubresourceSet>()(s.subresources);
                }
            };
        };

        std::unordered_map<TextureSubresourcesKey, nvrhi::FramebufferHandle, TextureSubresourcesKey::Hash> m_FramebufferCache;
        nvrhi::FramebufferHandle GetCachedFramebuffer(nvrhi::ITexture* texture, nvrhi::TextureSubresourceSet subresources);

        std::unordered_map<TextureSubresourcesKey, nvrhi::BindingSetHandle, TextureSubresourcesKey::Hash> m_BindingSetCache;
        nvrhi::BindingSetHandle GetCachedBindingSet(nvrhi::ITexture* texture, nvrhi::TextureSubresourceSet subresources);


    public:
        LightProbeProcessingPass(
            nvrhi::IDevice* device,
            std::shared_ptr<engine::ShaderFactory> shaderFactory,
            std::shared_ptr<engine::CommonRenderPasses> commonPasses,
            uint32_t intermediateTextureSize = 1024,
            nvrhi::Format intermediateTextureFormat = nvrhi::Format::RGBA16_FLOAT
        );

        void BlitCubemap(
            nvrhi::ICommandList* commandList,
            nvrhi::ITexture* inCubeMap,
            uint32_t inBaseArraySlice,
            uint32_t inMipLevel,
            nvrhi::ITexture* outCubeMap,
            uint32_t outBaseArraySlice,
            uint32_t outMipLevel);

        void GenerateCubemapMips(
            nvrhi::ICommandList* commandList,
            nvrhi::ITexture* cubeMap,
            uint32_t baseArraySlice,
            uint32_t sourceMipLevel,
            uint32_t levelsToGenerate);

        void RenderDiffuseMap(
            nvrhi::ICommandList* commandList,
            nvrhi::ITexture* inEnvironmentMap,
            nvrhi::TextureSubresourceSet inSubresources,
            nvrhi::ITexture* outDiffuseMap,
            uint32_t outBaseArraySlice,
            uint32_t outMipLevel);

        void RenderSpecularMap(
            nvrhi::ICommandList* commandList,
            float roughness,
            nvrhi::ITexture* inEnvironmentMap,
            nvrhi::TextureSubresourceSet inSubresources,
            nvrhi::ITexture* outDiffuseMap,
            uint32_t outBaseArraySlice,
            uint32_t outMipLevel);

        void RenderEnvironmentBrdfTexture(
            nvrhi::ICommandList* commandList);

        nvrhi::ITexture* GetEnvironmentBrdfTexture();

        void ResetCaches();
    };
}