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


namespace donut::engine
{
    class ShaderFactory;
    class ShadowMap;
    class CommonRenderPasses;
    class FramebufferFactory;
    class ICompositeView;
    class DirectionalLight;
}

struct ProceduralSkyShaderParameters;

namespace donut::render
{
    struct SkyParameters
    {
        dm::float3 skyColor{ 0.17f, 0.37f, 0.65f };
        dm::float3 horizonColor{ 0.50f, 0.70f, 0.92f };
        dm::float3 groundColor{ 0.62f, 0.59f, 0.55f };
        dm::float3 directionUp{ 0.f, 1.f, 0.f };
        float brightness = 0.1f; // scaler for sky brightness
        float horizonSize = 30.f; // +/- degrees
        float glowSize = 5.f; // degrees, starting from the edge of the light disk
        float glowIntensity = 0.1f; // [0-1] relative to light intensity
        float glowSharpness = 4.f; // [1-10] is the glow power exponent
        float maxLightRadiance = 100.f; // clamp for light radiance derived from its angular size, 0 = no clamp
    };

    class SkyPass
    {
    private:
        nvrhi::ShaderHandle m_PixelShader;
        nvrhi::BufferHandle m_SkyCB;
        nvrhi::BindingLayoutHandle m_RenderBindingLayout;
        nvrhi::BindingSetHandle m_RenderBindingSet;
        nvrhi::GraphicsPipelineHandle m_RenderPso;
        
        std::shared_ptr<engine::FramebufferFactory> m_FramebufferFactory;

    public:
        SkyPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<engine::ShaderFactory>& shaderFactory,
            const std::shared_ptr<engine::CommonRenderPasses>& commonPasses,
            const std::shared_ptr<engine::FramebufferFactory>& framebufferFactory,
            const engine::ICompositeView& compositeView);

        void Render(
            nvrhi::ICommandList* commandList,
            const engine::ICompositeView& compositeView,
            const engine::DirectionalLight& light,
            const SkyParameters& params) const;

        static void FillShaderParameters(
            const engine::DirectionalLight& light,
            const SkyParameters& input,
            ProceduralSkyShaderParameters& output);
    };
}