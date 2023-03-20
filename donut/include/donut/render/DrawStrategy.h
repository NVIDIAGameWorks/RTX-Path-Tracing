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
#include <memory>
#include <vector>

namespace donut::engine
{
    class IView;
}

namespace donut::render
{
    struct DrawItem;

    class IDrawStrategy
    {
    public:
        virtual void PrepareForView(
            const std::shared_ptr<engine::SceneGraphNode>& rootNode,
            const engine::IView& view) = 0;

        virtual const DrawItem* GetNextItem() = 0;

        virtual ~IDrawStrategy() = default;
    };

    class PassthroughDrawStrategy : public IDrawStrategy
    {
    private:
        const DrawItem* m_Data = nullptr;
        size_t m_Count = 0;

    public:
        void PrepareForView(
            const std::shared_ptr<engine::SceneGraphNode>& rootNode,
            const engine::IView& view) override { }

        const DrawItem* GetNextItem() override;

        void SetData(const DrawItem* data, size_t count);
    };
    
    class InstancedOpaqueDrawStrategy : public IDrawStrategy
    {
    private:
        dm::frustum m_ViewFrustum;
        engine::SceneGraphWalker m_Walker;
        std::vector<DrawItem> m_InstanceChunk;
        std::vector<const DrawItem*> m_InstancePtrChunk;
        size_t m_ReadPtr = 0;
        size_t m_ChunkSize = 128;

        void FillChunk();

    public:

        void PrepareForView(
            const std::shared_ptr<engine::SceneGraphNode>& rootNode,
            const engine::IView& view) override;

        const DrawItem* GetNextItem() override;

        [[nodiscard]] size_t GetChunkSize() const { return m_ChunkSize; }
        void SetChunkSize(size_t size) { m_ChunkSize = std::max<size_t>(size, 1u); }
    };

    class TransparentDrawStrategy : public IDrawStrategy
    {
    private:
        std::vector<DrawItem> m_InstancesToDraw;
        std::vector<const DrawItem*> m_InstancePtrsToDraw;
        size_t m_ReadPtr = 0;

    public:
        bool DrawDoubleSidedMaterialsSeparately = true;
        
        void PrepareForView(
            const std::shared_ptr<engine::SceneGraphNode>& rootNode,
            const engine::IView& view) override;

        const DrawItem* GetNextItem() override;
    };
}