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

#include <donut/engine/BindingCache.h>
#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>
#include <memory>
#include <vector>

namespace donut::engine
{
    class ShaderFactory;
    class CommonRenderPasses;
}

// Compute reduction pass to generate mipmap levels

namespace donut::render
{

    class MipMapGenPass {

    public:

        enum Mode : uint8_t {
            MODE_COLOR = 0,  // bilinear reduction of RGB channels
            MODE_MIN = 1,    // min() reduction of R channel
            MODE_MAX = 2,    // max() reduction of R channel
            MODE_MINMAX = 3, // min() and max() reductions of R channel into RG channels
        };

        // note : 'texture' must have been allocated with some mip levels
        MipMapGenPass(
            nvrhi::IDevice* device,
            std::shared_ptr<engine::ShaderFactory> shaderFactory,
            nvrhi::TextureHandle texture,
            Mode mode = Mode::MODE_MAX);

        // Dispatches reduction kernel : reads LOD 0 and populates
        // LOD 1 and up
        void Dispatch(nvrhi::ICommandList* commandList, int maxLOD=-1);

        // debug : blits mip-map levels in spiral pattern to 'target'
        // (assumes 'target' texture resolution is high enough...)
        void Display(
            std::shared_ptr<engine::CommonRenderPasses> commonPasses,
            nvrhi::ICommandList* commandList, 
            nvrhi::IFramebuffer* target);

    private:

        nvrhi::DeviceHandle m_Device;
        nvrhi::ShaderHandle m_Shader;
        nvrhi::TextureHandle m_Texture;
        nvrhi::BufferHandle m_ConstantBuffer;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        std::vector<nvrhi::BindingSetHandle> m_BindingSets;
        nvrhi::ComputePipelineHandle m_Pso;

        // Set of unique dummy textures - see details in class implementation
        struct NullTextures;
        std::shared_ptr<NullTextures> m_NullTextures;

        engine::BindingCache m_BindingCache;

    };

} // end namespace donut::render
