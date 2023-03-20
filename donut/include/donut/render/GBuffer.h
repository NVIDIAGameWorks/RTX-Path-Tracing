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
    class CommonRenderPasses;
    class FramebufferFactory;
}

namespace donut::render
{
    class GBufferRenderTargets
    {
    protected:
        dm::uint2 m_Size = dm::uint2::zero();
        dm::uint m_SampleCount = 0;
        bool m_UseReverseProjection = false;

    public:
        nvrhi::TextureHandle Depth;
        nvrhi::TextureHandle GBufferDiffuse;
        nvrhi::TextureHandle GBufferSpecular;
        nvrhi::TextureHandle GBufferNormals;
        nvrhi::TextureHandle GBufferEmissive;

        nvrhi::TextureHandle MotionVectors;

        std::shared_ptr<engine::FramebufferFactory> GBufferFramebuffer;

        virtual ~GBufferRenderTargets() = default;

        virtual void Init(
            nvrhi::IDevice* device,
            dm::uint2 size, 
            dm::uint sampleCount,
            bool enableMotionVectors,
            bool useReverseProjection);

        virtual void Clear(nvrhi::ICommandList* commandList);

        [[nodiscard]] dm::uint2 GetSize() const { return m_Size; }
        [[nodiscard]] dm::uint GetSampleCount() const { return m_SampleCount; }
        [[nodiscard]] bool GetUseReverseProjection() const { return m_UseReverseProjection; }
    };
}