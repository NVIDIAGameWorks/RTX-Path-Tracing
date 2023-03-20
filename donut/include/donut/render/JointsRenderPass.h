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
#include <nvrhi/nvrhi.h>

#include <memory>
#include <vector>

namespace donut::engine
{
    class ShaderFactory;
    class SceneGraph;
}

namespace donut::render
{
    // A rasterization pass that draws lines for each joint
    // of all the animated skeletons in a SceneGraph
    // (debugging feature)

    class JointsRenderPass
    {
    protected:
        nvrhi::DeviceHandle m_Device;

        nvrhi::BufferHandle m_VertexBuffer;
        nvrhi::BufferHandle m_ConstantsBuffer;

        nvrhi::ShaderHandle m_VertexShader;
        nvrhi::ShaderHandle m_PixelShader;

        nvrhi::InputLayoutHandle m_InputLayout;
        nvrhi::BindingSetHandle m_BindingSet;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        nvrhi::GraphicsPipelineHandle m_Pipeline;

        nvrhi::BufferHandle CreateVertexBuffer(uint32_t numVertices) const;

    protected:

        struct Vertex {
            dm::float3 position;
            uint32_t color;
        };

        std::vector<Vertex> m_Vertices;

        void UpdateVertices(const engine::SceneGraph& sceneGraph);

    public:

        JointsRenderPass(nvrhi::IDevice* device);

        void Init(donut::engine::ShaderFactory& shaderFactory);

        void ResetCaches();

        void RenderView(
            nvrhi::ICommandList* commandList,
            const engine::IView* view,
            nvrhi::IFramebuffer* framebuffer,
            std::shared_ptr<engine::SceneGraph const> sceneGraph);
    };
} // end namespace donut::render