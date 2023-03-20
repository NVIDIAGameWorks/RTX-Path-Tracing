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
#include <vector>
#include <memory>

struct PlanarViewConstants;

namespace donut::engine
{
    class IView;

    struct ViewType
    {
        enum Enum
        {
            PLANAR = 0x01,
            STEREO = 0x02,
            CUBEMAP = 0x04
        };
    };

    class ICompositeView
    {
    public:
        [[nodiscard]] virtual uint32_t GetNumChildViews(ViewType::Enum supportedTypes) const = 0;
        [[nodiscard]] virtual const IView* GetChildView(ViewType::Enum supportedTypes, uint32_t index) const = 0;

        virtual ~ICompositeView() = default;
    };

    class IView : public ICompositeView
    {
    public:
        virtual void FillPlanarViewConstants(PlanarViewConstants& constants) const;

        [[nodiscard]] virtual nvrhi::ViewportState GetViewportState() const = 0;
        [[nodiscard]] virtual nvrhi::VariableRateShadingState GetVariableRateShadingState() const = 0;
        [[nodiscard]] virtual nvrhi::TextureSubresourceSet GetSubresources() const = 0;
        [[nodiscard]] virtual bool IsReverseDepth() const = 0;
        [[nodiscard]] virtual bool IsOrthographicProjection() const = 0;
        [[nodiscard]] virtual bool IsStereoView() const = 0;
        [[nodiscard]] virtual bool IsCubemapView() const = 0;
        [[nodiscard]] virtual bool IsBoxVisible(const dm::box3& bbox) const = 0;
        [[nodiscard]] virtual bool IsMirrored() const = 0;
        [[nodiscard]] virtual dm::float3 GetViewOrigin() const = 0;
        [[nodiscard]] virtual dm::float3 GetViewDirection() const = 0;
        [[nodiscard]] virtual dm::frustum GetViewFrustum() const = 0;
        [[nodiscard]] virtual dm::frustum GetProjectionFrustum() const = 0;
        [[nodiscard]] virtual dm::affine3 GetViewMatrix() const = 0;
        [[nodiscard]] virtual dm::affine3 GetInverseViewMatrix() const = 0;
        [[nodiscard]] virtual dm::float4x4 GetProjectionMatrix(bool includeOffset = true) const = 0;
        [[nodiscard]] virtual dm::float4x4 GetInverseProjectionMatrix(bool includeOffset = true) const = 0;
        [[nodiscard]] virtual dm::float4x4 GetViewProjectionMatrix(bool includeOffset = true) const = 0;
        [[nodiscard]] virtual dm::float4x4 GetInverseViewProjectionMatrix(bool includeOffset = true) const = 0;
        [[nodiscard]] virtual nvrhi::Rect GetViewExtent() const = 0;
        [[nodiscard]] virtual dm::float2 GetPixelOffset() const = 0;

        [[nodiscard]] uint32_t GetNumChildViews(ViewType::Enum supportedTypes) const override;
        [[nodiscard]] const IView* GetChildView(ViewType::Enum supportedTypes, uint32_t index) const override;
    };


    class PlanarView : public IView
    {
    protected:
        // Directly settable parameters
        nvrhi::Viewport m_Viewport;
        nvrhi::Rect m_ScissorRect;
        nvrhi::VariableRateShadingState m_ShadingRateState;
        dm::affine3 m_ViewMatrix = dm::affine3::identity();
        dm::float4x4 m_ProjMatrix = dm::float4x4::identity();
        dm::float2 m_PixelOffset = dm::float2::zero();
        int m_ArraySlice = 0;

        // Derived matrices and other information - computed and cached on access
        dm::float4x4 m_PixelOffsetMatrix = dm::float4x4::identity();
        dm::float4x4 m_PixelOffsetMatrixInv = dm::float4x4::identity();
        dm::float4x4 m_ViewProjMatrix = dm::float4x4::identity();
        dm::float4x4 m_ViewProjOffsetMatrix = dm::float4x4::identity();
        dm::affine3 m_ViewMatrixInv = dm::affine3::identity();
        dm::float4x4 m_ProjMatrixInv = dm::float4x4::identity();
        dm::float4x4 m_ViewProjMatrixInv = dm::float4x4::identity();
        dm::float4x4 m_ViewProjOffsetMatrixInv = dm::float4x4::identity();
        dm::frustum m_ViewFrustum = dm::frustum::empty();
        dm::frustum m_ProjectionFrustum = dm::frustum::empty();
        bool m_ReverseDepth = false;
        bool m_IsMirrored = false;
        bool m_CacheValid = false;

        void EnsureCacheIsValid() const;
        
    public:
        void SetViewport(const nvrhi::Viewport& viewport);
        void SetVariableRateShadingState(const nvrhi::VariableRateShadingState& shadingRateState);
        void SetMatrices(const dm::affine3& viewMatrix, const dm::float4x4& projMatrix);
        void SetPixelOffset(dm::float2 offset);
        void SetArraySlice(int arraySlice);
        void UpdateCache();

        [[nodiscard]] const nvrhi::Viewport& GetViewport() const { return m_Viewport; }
        [[nodiscard]] const nvrhi::Rect& GetScissorRect() const { return m_ScissorRect; }

        [[nodiscard]] nvrhi::ViewportState GetViewportState() const override;
        [[nodiscard]] nvrhi::VariableRateShadingState GetVariableRateShadingState() const override;
        [[nodiscard]] nvrhi::TextureSubresourceSet GetSubresources() const override;
        [[nodiscard]] bool IsReverseDepth() const override;
        [[nodiscard]] bool IsOrthographicProjection() const override;
        [[nodiscard]] bool IsStereoView() const override;
        [[nodiscard]] bool IsCubemapView() const override;
        [[nodiscard]] bool IsBoxVisible(const dm::box3& bbox) const override;
        [[nodiscard]] bool IsMirrored() const override;
        [[nodiscard]] dm::float3 GetViewOrigin() const override;
        [[nodiscard]] dm::float3 GetViewDirection() const override;
        [[nodiscard]] dm::frustum GetViewFrustum() const override;
        [[nodiscard]] dm::frustum GetProjectionFrustum() const override;
        [[nodiscard]] dm::affine3 GetViewMatrix() const override;
        [[nodiscard]] dm::affine3 GetInverseViewMatrix() const override;
        [[nodiscard]] dm::float4x4 GetProjectionMatrix(bool includeOffset = true) const override;
        [[nodiscard]] dm::float4x4 GetInverseProjectionMatrix(bool includeOffset = true) const override;
        [[nodiscard]] dm::float4x4 GetViewProjectionMatrix(bool includeOffset = true) const override;
        [[nodiscard]] dm::float4x4 GetInverseViewProjectionMatrix(bool includeOffset = true) const override;
        [[nodiscard]] nvrhi::Rect GetViewExtent() const override;
        [[nodiscard]] dm::float2 GetPixelOffset() const override;
    };

    class CompositeView : public ICompositeView
    {
    protected:
        std::vector<std::shared_ptr<IView>> m_ChildViews;

    public:
        void AddView(std::shared_ptr<IView> view);

        [[nodiscard]] uint32_t GetNumChildViews(ViewType::Enum supportedTypes) const override;
        [[nodiscard]] const IView* GetChildView(ViewType::Enum supportedTypes, uint32_t index) const override;
    };

    template<typename ChildType>
    class StereoView : public IView
    {
    private:
        typedef IView Super;

    public:
        ChildType LeftView;
        ChildType RightView;

        [[nodiscard]] nvrhi::ViewportState GetViewportState() const override
        {
            nvrhi::ViewportState left = LeftView.GetViewportState();
            nvrhi::ViewportState right = RightView.GetViewportState();

            for (size_t i = 0; i < right.viewports.size(); i++)
                left.addViewport(right.viewports[i]);
            for (size_t i = 0; i < right.scissorRects.size(); i++)
                left.addScissorRect(right.scissorRects[i]);

            return left;
        }

        [[nodiscard]] nvrhi::VariableRateShadingState GetVariableRateShadingState() const override
        {
            return LeftView.GetVariableRateShadingState();
        }

        [[nodiscard]] nvrhi::TextureSubresourceSet GetSubresources() const override
        {
            return LeftView.GetSubresources(); // TODO: not really...
        }

        [[nodiscard]] bool IsReverseDepth() const override
        {
            return LeftView.IsReverseDepth();
        }

        [[nodiscard]] bool IsOrthographicProjection() const override
        {
            return LeftView.IsOrthographicProjection();
        }

        [[nodiscard]] bool IsStereoView() const override
        {
            return true;
        }

        [[nodiscard]] bool IsCubemapView() const override
        {
            return false;
        }

        [[nodiscard]] bool IsBoxVisible(const dm::box3& bbox) const override
        {
            return LeftView.IsBoxVisible(bbox) || RightView.IsBoxVisible(bbox);
        }

        [[nodiscard]] bool IsMirrored() const override
        {
            return LeftView.IsMirrored();
        }

        [[nodiscard]] dm::float3 GetViewOrigin() const override
        {
            return (LeftView.GetViewOrigin() + RightView.GetViewOrigin()) * 0.5f;
        }

        [[nodiscard]] dm::float3 GetViewDirection() const override
        {
            return LeftView.GetViewDirection();
        }

        [[nodiscard]] dm::frustum GetViewFrustum() const override
        {
            dm::frustum left = LeftView.GetViewFrustum();
            dm::frustum right = LeftView.GetViewFrustum();

            // not robust but should work for regular stereo views
            left.planes[dm::frustum::RIGHT_PLANE] = right.planes[dm::frustum::RIGHT_PLANE];

            return left;
        }

        [[nodiscard]] dm::frustum GetProjectionFrustum() const override
        {
            dm::frustum left = LeftView.GetProjectionFrustum();
            dm::frustum right = LeftView.GetProjectionFrustum();

            // not robust but should work for regular stereo views
            left.planes[dm::frustum::RIGHT_PLANE] = right.planes[dm::frustum::RIGHT_PLANE];

            return left;
        }

        [[nodiscard]] dm::affine3 GetViewMatrix() const override
        {
            assert(false);
            return dm::affine3::identity();
        }

        [[nodiscard]] dm::affine3 GetInverseViewMatrix() const override
        {
            assert(false);
            return dm::affine3::identity();
        }

        [[nodiscard]] dm::float4x4 GetProjectionMatrix(bool includeOffset = true) const override
        {
            assert(false);
            (void)includeOffset;
            return dm::float4x4::identity();
        }

        [[nodiscard]] dm::float4x4 GetInverseProjectionMatrix(bool includeOffset = true) const override
        {
            assert(false);
            (void)includeOffset;
            return dm::float4x4::identity();
        }

        [[nodiscard]] dm::float4x4 GetViewProjectionMatrix(bool includeOffset = true) const override
        {
            assert(false);
            (void)includeOffset;
            return dm::float4x4::identity();
        }

        [[nodiscard]] dm::float4x4 GetInverseViewProjectionMatrix(bool includeOffset = true) const override
        {
            assert(false);
            (void)includeOffset;
            return dm::float4x4::identity();
        }

        [[nodiscard]] nvrhi::Rect GetViewExtent() const override
        {
            nvrhi::Rect left = LeftView.GetViewExtent();
            nvrhi::Rect right = RightView.GetViewExtent();

            return nvrhi::Rect(
                std::min(left.minX, right.minX),
                std::max(left.maxX, right.maxX),
                std::min(left.minY, right.minY),
                std::max(left.maxY, right.maxY));
        }

        [[nodiscard]] uint32_t GetNumChildViews(ViewType::Enum supportedTypes) const override
        {
            if (supportedTypes & ViewType::STEREO)
                return 1;

            return 2;
        }

        [[nodiscard]] const IView* GetChildView(ViewType::Enum supportedTypes, uint32_t index) const override
        {
            if (supportedTypes & ViewType::STEREO)
            {
                assert(index == 0);
                return this;
            }

            assert(index < 2);
            if (index == 0)
                return &LeftView;
            return &RightView;
        }

        [[nodiscard]] dm::float2 GetPixelOffset() const override
        {
            return LeftView.GetPixelOffset();
        }
    };

    typedef StereoView<PlanarView> StereoPlanarView;

    class CubemapView : public IView
    {
    protected:
        typedef IView Super;

        PlanarView m_FaceViews[6];
        dm::affine3 m_ViewMatrix = dm::affine3::identity();
        dm::affine3 m_ViewMatrixInv = dm::affine3::identity();
        dm::float4x4 m_ProjMatrix = dm::float4x4::identity();
        dm::float4x4 m_ProjMatrixInv = dm::float4x4::identity();
        dm::float4x4 m_ViewProjMatrix = dm::float4x4::identity();
        dm::float4x4 m_ViewProjMatrixInv = dm::float4x4::identity();
        float m_CullDistance = 1.f;
        float m_NearPlane = 1.f;
        dm::float3 m_Center = dm::float3::zero();
        dm::box3 m_CullingBox = dm::box3::empty();
        int m_FirstArraySlice = 0;
        bool m_CacheValid = false;

        void EnsureCacheIsValid() const;

    public:
        void SetTransform(dm::affine3 viewMatrix, float zNear, float cullDistance, bool useReverseInfiniteProjections = true);
        void SetArrayViewports(int resolution, int firstArraySlice);
        void UpdateCache();

        [[nodiscard]] float GetNearPlane() const;
        [[nodiscard]] dm::box3 GetCullingBox() const;

        [[nodiscard]] nvrhi::ViewportState GetViewportState() const override;
        [[nodiscard]] nvrhi::VariableRateShadingState GetVariableRateShadingState() const override;
        [[nodiscard]] nvrhi::TextureSubresourceSet GetSubresources() const override;
        [[nodiscard]] bool IsReverseDepth() const override;
        [[nodiscard]] bool IsOrthographicProjection() const override;
        [[nodiscard]] bool IsStereoView() const override;
        [[nodiscard]] bool IsCubemapView() const override;
        [[nodiscard]] bool IsBoxVisible(const dm::box3& bbox) const override;
        [[nodiscard]] bool IsMirrored() const override;
        [[nodiscard]] dm::float3 GetViewOrigin() const override;
        [[nodiscard]] dm::float3 GetViewDirection() const override;
        [[nodiscard]] dm::frustum GetViewFrustum() const override;
        [[nodiscard]] dm::frustum GetProjectionFrustum() const override;
        [[nodiscard]] dm::affine3 GetViewMatrix() const override;
        [[nodiscard]] dm::affine3 GetInverseViewMatrix() const override;
        [[nodiscard]] dm::float4x4 GetProjectionMatrix(bool includeOffset = true) const override;
        [[nodiscard]] dm::float4x4 GetInverseProjectionMatrix(bool includeOffset = true) const override;
        [[nodiscard]] dm::float4x4 GetViewProjectionMatrix(bool includeOffset = true) const override;
        [[nodiscard]] dm::float4x4 GetInverseViewProjectionMatrix(bool includeOffset = true) const override;
        [[nodiscard]] nvrhi::Rect GetViewExtent() const override;
        [[nodiscard]] dm::float2 GetPixelOffset() const override;

        [[nodiscard]] uint32_t GetNumChildViews(ViewType::Enum supportedTypes) const override;
        [[nodiscard]] const IView* GetChildView(ViewType::Enum supportedTypes, uint32_t index) const override;

        static uint32_t* GetCubemapCoordinateSwizzle();
    };
}
