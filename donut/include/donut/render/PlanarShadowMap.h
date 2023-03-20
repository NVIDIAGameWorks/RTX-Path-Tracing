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

#include <donut/engine/SceneGraph.h>
#include <donut/engine/ShadowMap.h>
#include <donut/engine/View.h>
#include <nvrhi/nvrhi.h>
#include <memory>

namespace donut::render
{
    class PlanarShadowMap : public engine::IShadowMap
    {
    private:
        nvrhi::TextureHandle m_ShadowMapTexture;
        std::shared_ptr<engine::PlanarView> m_View;
        bool m_IsLitOutOfBounds = false;
        dm::float2 m_FadeRangeTexels = 1.f;
        dm::float2 m_ShadowMapSize;
        dm::float2 m_TextureSize;
        float m_FalloffDistance = 1.f;

    public:
        PlanarShadowMap(
            nvrhi::IDevice* device,
            int resolution,
            nvrhi::Format format);

        PlanarShadowMap(
            nvrhi::IDevice* device,
            nvrhi::ITexture* texture,
            uint32_t arraySlice,
            const nvrhi::Viewport& viewport);

        bool SetupWholeSceneDirectionalLightView(
            const engine::DirectionalLight& light, 
            dm::box3_arg sceneBounds, 
            float fadeRangeWorld = 0.f);

        bool SetupDynamicDirectionalLightView(
            const engine::DirectionalLight& light, 
            dm::float3 anchor, 
            dm::float3 halfShadowBoxSize, 
            dm::float3 preViewTranslation = 0.f,
            float fadeRangeWorld = 0.f);

        void SetupProxyView();

        void Clear(nvrhi::ICommandList* commandList);

        void SetLitOutOfBounds(bool litOutOfBounds);
        void SetFalloffDistance(float distance);

        std::shared_ptr<engine::PlanarView> GetPlanarView();

        virtual dm::float4x4 GetWorldToUvzwMatrix() const override;
        virtual const engine::ICompositeView& GetView() const override;
        virtual nvrhi::ITexture* GetTexture() const override;
        virtual uint32_t GetNumberOfCascades() const override;
        virtual const IShadowMap* GetCascade(uint32_t index) const override;
        virtual uint32_t GetNumberOfPerObjectShadows() const override;
        virtual const IShadowMap* GetPerObjectShadow(uint32_t index) const override;
        virtual dm::int2 GetTextureSize() const override;
        virtual dm::box2 GetUVRange() const override;
        virtual dm::float2 GetFadeRangeInTexels() const override;
        virtual bool IsLitOutOfBounds() const override;
        virtual void FillShadowConstants(ShadowConstants& constants) const override;
    };
}