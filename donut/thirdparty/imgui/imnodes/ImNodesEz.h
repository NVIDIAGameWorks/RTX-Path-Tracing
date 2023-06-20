//
// Copyright (c) 2019 Rokas Kupstys.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
//
#pragma once

#include "ImNodes.h"

#include <string>

//
// Appearance can be styled mid-frame by ImNodes::Ez::PushStyleVar()/ImNodes::Ez::PopStyleVar().
//
// Defined outside the ImNodes namespace similar to enums in ImGui.
//
enum ImNodesStyleVar
{
    ImNodesStyleVar_GridSpacing,        // float
    ImNodesStyleVar_CurveThickness,     // float
    ImNodesStyleVar_CurveStrength,      // float
    ImNodesStyleVar_SlotRadius,         // float
    ImNodesStyleVar_NodeRounding,       // float
    ImNodesStyleVar_NodeSpacing,        // ImVec2
    ImNodesStyleVar_ItemSpacing,        // ImVec2
    ImNodesStyleVar_COUNT,
};

enum ImNodesStyleCol
{
    ImNodesStyleCol_GridLines,
    ImNodesStyleCol_NodeBodyBg,
    ImNodesStyleCol_NodeBodyBgHovered,
    ImNodesStyleCol_NodeBodyBgActive,
    ImNodesStyleCol_NodeBorder,
    ImNodesStyleCol_Connection,
    ImNodesStyleCol_ConnectionActive,
    ImNodesStyleCol_SelectBg,
    ImNodesStyleCol_SelectBorder,
    ImNodesStyleCol_NodeTitleBarBg,
    ImNodesStyleCol_NodeTitleBarBgHovered,
    ImNodesStyleCol_NodeTitleBarBgActive,
    ImNodesStyleCol_COUNT,
};

namespace ImNodes
{

/// This namespace includes functions for easily creating nodes. They implement a somewhat nice node layout. If you need
/// a quick solution - use easy nodes. If you want to customize node look - use lower level node functions from ImNodes.h
namespace Ez
{

struct SlotInfo
{
    /// Slot title, will be displayed on the node.
    std::string title;
    /// Slot kind, will be used for matching connections to slots of same kind.
    int kind;
    int row;
};

// Style which holds the extended variables and colors not already stored in ImNodes::CanvasState.
struct StyleVars
{
    float SlotRadius = 5.0f;
    ImVec2 ItemSpacing{8.0f, 4.0f};
    struct
    {
        ImVec4 NodeBodyBg{0.12f, 0.12f, 0.12f, 1.0f};
        ImVec4 NodeBodyBgHovered{0.16f, 0.16f, 0.16f, 1.0f};
        ImVec4 NodeBodyBgActive{0.25f, 0.25f, 0.25f, 1.0f};
        ImVec4 NodeBorder{0.4f, 0.4f, 0.4f, 1.0f};
        ImVec4 NodeTitleBarBg{0.22f, 0.22f, 0.22f, 1.0f};
        ImVec4 NodeTitleBarBgHovered{0.32f, 0.32f, 0.32f, 1.0f};
        ImVec4 NodeTitleBarBgActive{0.5f, 0.5f, 0.5f, 1.0f};
    } Colors;
};

struct Context;

IMGUI_API Context* CreateContext();
IMGUI_API void FreeContext(Context *ctx);
IMGUI_API void SetContext(Context *ctx);

IMGUI_API ImNodes::CanvasState& GetState();

IMGUI_API void BeginCanvas();
IMGUI_API void EndCanvas();

/// Begin rendering of node in a graph. Render node content when returns `true`.
IMGUI_API bool BeginNode(void* node_id, const char* title, ImVec2* pos, bool* selected);
/// Terminates current node. Should be called regardless of BeginNode() returns value.
IMGUI_API void EndNode();
/// Renders input slot region. Kind is unique value whose sign is ignored.
/// This function must always be called after BeginNode() and before OutputSlots().
/// When no input slots are rendered call InputSlots(nullptr, 0);
IMGUI_API void InputSlots(const SlotInfo* slots, int snum);
/// Renders output slot region. Kind is unique value whose sign is ignored. This function must always be called after InputSlots() and function call is required (not optional).
/// This function must always be called after InputSlots() and before EndNode().
/// When no input slots are rendered call OutputSlots(nullptr, 0);
IMGUI_API void OutputSlots(const SlotInfo* slots, int snum);

bool Connection(void* input_node, const char* input_slot, void* output_node, const char* output_slot);

IMGUI_API void PushStyleVar(ImNodesStyleVar idx, float val);
IMGUI_API void PushStyleVar(ImNodesStyleVar idx, const ImVec2 &val);
IMGUI_API void PopStyleVar(int count = 1);

IMGUI_API void PushStyleColor(ImNodesStyleCol idx, ImU32 col);
IMGUI_API void PushStyleColor(ImNodesStyleCol idx, const ImVec4& col);
IMGUI_API void PopStyleColor(int count);

}

}
