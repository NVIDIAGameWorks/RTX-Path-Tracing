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

#include <donut/engine/View.h>
#include <donut/engine/SceneTypes.h>
#include <donut/render/GeometryPasses.h>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace donut::engine
{
    class ShaderFactory;
    class Light;
    class CommonRenderPasses;
    class FramebufferFactory;
    class MaterialBindingCache;
    struct Material;
    struct LightProbe;
}

namespace std
{
    template<>
    struct hash<std::pair<nvrhi::ITexture*, nvrhi::ITexture*>>
    {
        size_t operator()(const std::pair<nvrhi::ITexture*, nvrhi::ITexture*>& v) const noexcept
        {
            auto h = hash<nvrhi::ITexture*>();
            return h(v.first) ^ (h(v.second) << 8);
        }
    };
}

namespace donut::render
{
    class ForwardShadingPass : public IGeometryPass
    {
    public:
        union PipelineKey
        {
            struct
            {
                engine::MaterialDomain domain : 3;
                nvrhi::RasterCullMode cullMode : 2;
                bool frontCounterClockwise : 1;
                bool reverseDepth : 1;
            } bits;
            uint32_t value;

            static constexpr size_t Count = 1 << 7;
        };

        class Context : public GeometryPassContext
        {
        public:
            nvrhi::BindingSetHandle lightBindingSet;
            PipelineKey keyTemplate;

            Context()
            {
                keyTemplate.value = 0;
            }
        };

        struct CreateParameters
        {
            std::shared_ptr<engine::MaterialBindingCache> materialBindings;
            bool singlePassCubemap = false;
            bool trackLiveness = true;
            uint32_t numConstantBufferVersions = 16;
        };


    protected:
        nvrhi::DeviceHandle m_Device;
        nvrhi::InputLayoutHandle m_InputLayout;
        nvrhi::ShaderHandle m_VertexShader;
        nvrhi::ShaderHandle m_PixelShader;
        nvrhi::ShaderHandle m_PixelShaderTransmissive;
        nvrhi::ShaderHandle m_GeometryShader;
        nvrhi::SamplerHandle m_ShadowSampler;
        nvrhi::BindingLayoutHandle m_ViewBindingLayout;
        nvrhi::BindingSetHandle m_ViewBindingSet;
        nvrhi::BindingLayoutHandle m_LightBindingLayout;
        engine::ViewType::Enum m_SupportedViewTypes = engine::ViewType::PLANAR;
        nvrhi::BufferHandle m_ForwardViewCB;
        nvrhi::BufferHandle m_ForwardLightCB;
        nvrhi::GraphicsPipelineHandle m_Pipelines[PipelineKey::Count];
        bool m_TrackLiveness = true;
        std::mutex m_Mutex;

        std::unordered_map<std::pair<nvrhi::ITexture*, nvrhi::ITexture*>, nvrhi::BindingSetHandle> m_LightBindingSets;
        
        std::shared_ptr<engine::CommonRenderPasses> m_CommonPasses;
        std::shared_ptr<engine::MaterialBindingCache> m_MaterialBindings;
        
        virtual nvrhi::ShaderHandle CreateVertexShader(engine::ShaderFactory& shaderFactory, const CreateParameters& params);
        virtual nvrhi::ShaderHandle CreateGeometryShader(engine::ShaderFactory& shaderFactory, const CreateParameters& params);
        virtual nvrhi::ShaderHandle CreatePixelShader(engine::ShaderFactory& shaderFactory, const CreateParameters& params, bool transmissiveMaterial);
        virtual nvrhi::InputLayoutHandle CreateInputLayout(nvrhi::IShader* vertexShader, const CreateParameters& params);
        virtual nvrhi::BindingLayoutHandle CreateViewBindingLayout();
        virtual nvrhi::BindingSetHandle CreateViewBindingSet();
        virtual nvrhi::BindingLayoutHandle CreateLightBindingLayout();
        virtual nvrhi::BindingSetHandle CreateLightBindingSet(nvrhi::ITexture* shadowMapTexture, nvrhi::ITexture* diffuse, nvrhi::ITexture* specular, nvrhi::ITexture* environmentBrdf);
        virtual std::shared_ptr<engine::MaterialBindingCache> CreateMaterialBindingCache(engine::CommonRenderPasses& commonPasses);
        virtual nvrhi::GraphicsPipelineHandle CreateGraphicsPipeline(PipelineKey key, nvrhi::IFramebuffer* framebuffer);
        
    public:
        ForwardShadingPass(
            nvrhi::IDevice* device,
            std::shared_ptr<engine::CommonRenderPasses> commonPasses);

        virtual void Init(
            engine::ShaderFactory& shaderFactory,
            const CreateParameters& params);

        void ResetBindingCache();
        
        virtual void PrepareLights(
            Context& context,
            nvrhi::ICommandList* commandList,
            const std::vector<std::shared_ptr<engine::Light>>& lights,
            dm::float3 ambientColorTop,
            dm::float3 ambientColorBottom,
            const std::vector<std::shared_ptr<engine::LightProbe>>& lightProbes);

        // IGeometryPass implementation

        [[nodiscard]] engine::ViewType::Enum GetSupportedViewTypes() const override;
        void SetupView(GeometryPassContext& context, nvrhi::ICommandList* commandList, const engine::IView* view, const engine::IView* viewPrev) override;
        bool SetupMaterial(GeometryPassContext& context, const engine::Material* material, nvrhi::RasterCullMode cullMode, nvrhi::GraphicsState& state) override;
        void SetupInputBuffers(GeometryPassContext& context, const engine::BufferGroup* buffers, nvrhi::GraphicsState& state) override;
        void SetPushConstants(GeometryPassContext& context, nvrhi::ICommandList* commandList, nvrhi::GraphicsState& state, nvrhi::DrawArguments& args) override { }
    };

}
