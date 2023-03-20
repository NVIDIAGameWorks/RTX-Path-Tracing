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

#include <donut/render/GeometryPasses.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/render/DrawStrategy.h>

using namespace donut::math;
using namespace donut::engine;
using namespace donut::render;

void donut::render::RenderView(
    nvrhi::ICommandList* commandList, 
    const IView* view, 
    const IView* viewPrev,
    nvrhi::IFramebuffer* framebuffer,
    IDrawStrategy& drawStrategy,
    IGeometryPass& pass,
    GeometryPassContext& passContext,
    bool materialEvents)
{
    pass.SetupView(passContext, commandList, view, viewPrev);

    const Material* lastMaterial = nullptr;
    const BufferGroup* lastBuffers = nullptr;
    nvrhi::RasterCullMode lastCullMode = nvrhi::RasterCullMode::Back;

    bool drawMaterial = true;
    bool stateValid = false;

    const Material* eventMaterial = nullptr;

    nvrhi::GraphicsState graphicsState;
    graphicsState.framebuffer = framebuffer;
    graphicsState.viewport = view->GetViewportState();
    graphicsState.shadingRateState = view->GetVariableRateShadingState();

    nvrhi::DrawArguments currentDraw;
    currentDraw.instanceCount = 0;

    auto flushDraw = [commandList, materialEvents, &graphicsState, &currentDraw, &eventMaterial, &pass, &passContext](const Material* material)
    {
        if (currentDraw.instanceCount == 0)
            return;

        if (materialEvents && material != eventMaterial)
        {
            if (eventMaterial)
                commandList->endMarker();

            if (material->name.empty())
            {
                eventMaterial = nullptr;
            }
            else
            {
                commandList->beginMarker(material->name.c_str());
                eventMaterial = material;
            }
        }

        pass.SetPushConstants(passContext, commandList, graphicsState, currentDraw);

        commandList->drawIndexed(currentDraw);
        currentDraw.instanceCount = 0;
    };
    
    while (const DrawItem* item = drawStrategy.GetNextItem())
    {
        if (item->material == nullptr)
            continue;


        bool newBuffers = item->buffers != lastBuffers;
        bool newMaterial = item->material != lastMaterial || item->cullMode != lastCullMode;

        if (newBuffers || newMaterial)
        {
            flushDraw(lastMaterial);
        }

        if (newBuffers)
        {
            pass.SetupInputBuffers(passContext, item->buffers, graphicsState);

            lastBuffers = item->buffers;
            stateValid = false;
        }

        if (newMaterial)
        {
            drawMaterial = pass.SetupMaterial(passContext, item->material, item->cullMode, graphicsState);

            lastMaterial = item->material;
            lastCullMode = item->cullMode;
            stateValid = false;
        }

        if (drawMaterial)
        {
            if (!stateValid)
            {
                commandList->setGraphicsState(graphicsState);
                stateValid = true;
            }

            nvrhi::DrawArguments args;
            args.vertexCount = item->geometry->numIndices;
            args.instanceCount = 1;
            args.startVertexLocation = item->mesh->vertexOffset + item->geometry->vertexOffsetInMesh;
            args.startIndexLocation = item->mesh->indexOffset + item->geometry->indexOffsetInMesh;
            args.startInstanceLocation = item->instance->GetInstanceIndex();

            if (currentDraw.instanceCount > 0 && 
                currentDraw.startIndexLocation == args.startIndexLocation && 
                currentDraw.startInstanceLocation + currentDraw.instanceCount == args.startInstanceLocation)
            {
                currentDraw.instanceCount += 1;
            }
            else
            {
                flushDraw(item->material);

                currentDraw = args;
            }
        }
    }

    flushDraw(lastMaterial);

    if (materialEvents && eventMaterial)
        commandList->endMarker();
}

void donut::render::RenderCompositeView(
    nvrhi::ICommandList* commandList, 
    const ICompositeView* compositeView, 
    const ICompositeView* compositeViewPrev, 
    FramebufferFactory& framebufferFactory,
    const std::shared_ptr<engine::SceneGraphNode>& rootNode,
    IDrawStrategy& drawStrategy,
    IGeometryPass& pass,
    GeometryPassContext& passContext,
    const char* passEvent, 
    bool materialEvents)
{
    if (passEvent)
        commandList->beginMarker(passEvent);

    ViewType::Enum supportedViewTypes = pass.GetSupportedViewTypes();

    if (compositeViewPrev)
    {
        // the views must have the same topology
        assert(compositeView->GetNumChildViews(supportedViewTypes) == compositeViewPrev->GetNumChildViews(supportedViewTypes));
    }
    
    for (uint viewIndex = 0; viewIndex < compositeView->GetNumChildViews(supportedViewTypes); viewIndex++)
    {
        const IView* view = compositeView->GetChildView(supportedViewTypes, viewIndex);
        const IView* viewPrev = compositeViewPrev ? compositeViewPrev->GetChildView(supportedViewTypes, viewIndex) : nullptr;

        assert(view != nullptr);

        drawStrategy.PrepareForView(rootNode, *view);

        nvrhi::IFramebuffer* framebuffer = framebufferFactory.GetFramebuffer(*view);

        RenderView(commandList, view, viewPrev, framebuffer, drawStrategy, pass, passContext, materialEvents);
    }

    if (passEvent)
        commandList->endMarker();
}
