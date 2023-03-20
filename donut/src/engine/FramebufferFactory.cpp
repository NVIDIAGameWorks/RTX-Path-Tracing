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

#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/View.h>

using namespace donut::engine;

nvrhi::IFramebuffer* FramebufferFactory::GetFramebuffer(const nvrhi::TextureSubresourceSet& subresources)
{
    nvrhi::FramebufferHandle& item = m_FramebufferCache[subresources];

    if (!item)
    {
        nvrhi::FramebufferDesc desc;
        for (auto renderTarget : RenderTargets)
            desc.addColorAttachment(renderTarget, subresources);

        if (DepthTarget)
            desc.setDepthAttachment(DepthTarget, subresources);

        if (ShadingRateSurface)
            desc.setShadingRateAttachment(ShadingRateSurface, subresources);

        item = m_Device->createFramebuffer(desc);
    }
    
    return item;
}

nvrhi::IFramebuffer* FramebufferFactory::GetFramebuffer(const IView& view)
{
    return GetFramebuffer(view.GetSubresources());
}
