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

#pragma once


#include <imgui.h>

namespace ImNodes
{

enum StyleColor
{
    ColCanvasLines,
    ColNodeBg,
    ColNodeActiveBg,
    ColNodeBorder,
    ColConnection,
    ColConnectionActive,
    ColSelectBg,
    ColSelectBorder,
    ColMax
};

struct _CanvasStateImpl;

struct IMGUI_API CanvasState
{
    /// Current zoom of canvas.
    float Zoom = 1.0;
    /// Current scroll offset of canvas.
    ImVec2 Offset;
    /// Colors used to style elements of this canvas.
    ImColor Colors[StyleColor::ColMax];
    /// Style parameters
    struct CanvasStyle
    {
        /// Thickness of curves that connect slots together.
        float CurveThickness = 5.0f;
        /// Indent connection into slot widget a little. Useful when slot content covers connection end with some kind
        /// of icon (like a circle) and then no seam between icon and connection end is visible.
        float ConnectionIndent = 1.0f;

        float GridSpacing = 64.0f;
        float CurveStrength = 100.0f;
        float NodeRounding = 5.0f;
        ImVec2 NodeSpacing{4.0f, 4.0f};
    } Style;
    /// Implementation detail.
    _CanvasStateImpl* _Impl = nullptr;

    CanvasState() noexcept;
    ~CanvasState();
};

/// Create a node graph canvas in current window.
IMGUI_API void BeginCanvas(CanvasState* canvas);
/// Terminate a node graph canvas that was created by calling BeginCanvas().
IMGUI_API void EndCanvas();
/// Begin rendering of node in a graph. Render node content when returns `true`.
IMGUI_API bool BeginNode(void* node_id, ImVec2* pos, bool* selected);
/// Terminates current node. Should be called regardless of BeginNode() returns value.
IMGUI_API void EndNode();
/// Returns `true` if the current node is hovered. Call between `BeginNode()` and `EndNode()`.
IMGUI_API bool IsNodeHovered();
/// Specified node will be positioned at the mouse cursor on next frame. Call when new node is created.
IMGUI_API void AutoPositionNode(void* node_id);
/// Returns `true` when new connection is made. Connection information is returned into `connection` parameter. Must be
/// called at id scope created by BeginNode().
IMGUI_API bool GetNewConnection(void** input_node, const char** input_slot_title, void** output_node, const char** output_slot_title);
/// Get information of connection that is being made and has only one end connected. Returns true when pending connection exists, false otherwise.
IMGUI_API bool GetPendingConnection(void** node_id, const char** slot_title, int* slot_kind);
/// Render a connection. Returns `true` when connection is present, `false` if it is deleted.
IMGUI_API bool Connection(void* input_node, const char* input_slot, void* output_node, const char* output_slot);
/// Returns active canvas state when called between BeginCanvas() and EndCanvas(). Returns nullptr otherwise. This function is not thread-safe.
IMGUI_API CanvasState* GetCurrentCanvas();
/// Convert kind id to input type.
inline int InputSlotKind(int kind) { return kind > 0 ? -kind : kind; }
/// Convert kind id to output type.
inline int OutputSlotKind(int kind) { return kind < 0 ? -kind : kind; }
/// Returns `true` if `kind` is from input slot.
inline bool IsInputSlotKind(int kind) { return kind < 0; }
/// Returns `true` if `kind` is from output slot.
inline bool IsOutputSlotKind(int kind) { return kind >= 0; } // <-HACK
/// Begins slot region. Kind is unique value indicating slot type. Negative values mean input slots, positive - output slots.
IMGUI_API bool BeginSlot(const char* title, int kind);
/// Begins slot region. Kind is unique value whose sign is ignored.
inline bool BeginInputSlot(const char* title, int kind) { return BeginSlot(title, InputSlotKind(kind)); }
/// Begins slot region. Kind is unique value whose sign is ignored.
inline bool BeginOutputSlot(const char* title, int kind) { return BeginSlot(title, OutputSlotKind(kind)); }
/// Rends rendering of slot. Call only if Begin*Slot() returned `true`.
IMGUI_API void EndSlot();
/// Returns `true` if curve connected to current slot is hovered. Call between `Begin*Slot()` and `EndSlot()`. In-progress
/// connection is considered hovered as well.
IMGUI_API bool IsSlotCurveHovered();
/// Returns `true` when new slot is being created and current slot can be connected. Call between `Begin*Slot()` and `EndSlot()`.
IMGUI_API bool IsConnectingCompatibleSlot();

}   // namespace ImNodes
