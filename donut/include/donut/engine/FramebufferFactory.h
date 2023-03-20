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
#include <vector>
#include <unordered_map>

namespace donut::engine
{
    class IView;

    class FramebufferFactory
    {
    private:
        nvrhi::DeviceHandle m_Device;
        std::unordered_map<nvrhi::TextureSubresourceSet, nvrhi::FramebufferHandle> m_FramebufferCache;

    public:
        FramebufferFactory(nvrhi::IDevice* device) : m_Device(device) {}

        std::vector<nvrhi::TextureHandle> RenderTargets;
        nvrhi::TextureHandle DepthTarget;
        nvrhi::TextureHandle ShadingRateSurface;

        virtual nvrhi::IFramebuffer* GetFramebuffer(const nvrhi::TextureSubresourceSet& subresources);
        nvrhi::IFramebuffer* GetFramebuffer(const IView& view);
    };
}