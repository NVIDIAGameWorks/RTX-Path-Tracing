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

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#   define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "ImNodes.h"

#include <imgui_internal.h>
#include <limits>

namespace ImNodes
{

CanvasState* gCanvas = nullptr;

bool operator ==(const ImVec2& a, const ImVec2& b)
{
    return abs(a.x - b.x) < std::numeric_limits<float>::epsilon() &&
           abs(a.y - b.y) < std::numeric_limits<float>::epsilon();
}

enum _ImNodesState
{
    State_None,
    State_Drag,
    State_Select,
};

/// Struct containing information about connection source node and slot.
struct _DragConnectionPayload
{
    /// Node id where connection started.
    void* NodeId = nullptr;
    /// Source slot name.
    const char* SlotTitle = nullptr;
    /// Source slot kind.
    int SlotKind = 0;
};

/// Node-slot combination.
struct _IgnoreSlot
{
    /// Node id.
    void* NodeId = nullptr;
    /// Slot name.
    const char* SlotName = nullptr;
    /// Slot kind. Not actual slot kind, but +1 or -1. Used to determine if slot is input or output.
    int SlotKind = 0;

    /// Equality operator, required by ImVector.
    bool operator==(const _IgnoreSlot& other) const
    {
        if (NodeId != other.NodeId || SlotKind != other.SlotKind)
            return false;

        if (SlotName != nullptr && other.SlotName != nullptr)
            return strcmp(SlotName, other.SlotName) == 0;

        return other.SlotName == SlotName;
    }
};

struct _CanvasStateImpl
{
    /// Storage for various internal node/slot attributes.
    ImGuiStorage CachedData{};
    /// Current node data.
    struct
    {
        /// User-provided unique node id.
        void* Id = nullptr;
        /// User-provided node position.
        ImVec2* Pos = nullptr;
        /// User-provided node selection status.
        bool* Selected = nullptr;
        /// Stack accumulated ImGui ID for the node item.
        ImGuiID ItemId;
    } Node;
    /// Current slot data.
    struct
    {
        int Kind = 0;
        const char* Title = nullptr;
    } slot{};
    /// Node id which will be positioned at the mouse cursor on next frame.
    void* AutoPositionNodeId = nullptr;
    /// Connection that was just created.
    struct
    {
        /// Node id of input node.
        void* InputNode = nullptr;
        /// Slot title of input node.
        const char* InputSlot = nullptr;
        /// Node id of output node.
        void* OutputNode = nullptr;
        /// Slot title of output node.
        const char* OutputSlot = nullptr;
    } NewConnection{};
    /// Starting position of node selection rect.
    ImVec2 SelectionStart{};
    /// Node id of node that is being dragged.
    void* DragNode = nullptr;
    /// Flag indicating that all selected nodes should be dragged.
    bool DragNodeSelected = false;
    /// Node id of node that should be selected on next frame, while deselecting any other nodes.
    void* SingleSelectedNode = nullptr;
    /// Frame on which selection logic should run.
    int DoSelectionsFrame = 0;
    /// Current interaction state.
    _ImNodesState State{};
    /// Flag indicating that new connection was just made.
    bool JustConnected = false;
    /// Previous canvas pointer. Used to restore proper gCanvas value when nesting canvases.
    CanvasState* PrevCanvas = nullptr;
    /// A list of node/slot combos that can not connect to current pending connection.
    ImVector<_IgnoreSlot> IgnoreConnections{};
    int PrevSelectCount = 0;
    int CurrSelectCount = 0;
    ImGuiID PendingActiveItemId = 0;
    ImGuiID PendingActiveSlotId = 0;
    /// The ID of the currently top-most hovered node as determined the last frame.
    ImGuiID HoveredNodeId = 0;
    /// The ID of the pending top-most hovered node determined thus far this frame.
    ImGuiID PendingHoveredNodeId = 0;
};

CanvasState::CanvasState() noexcept
{
    _Impl = new _CanvasStateImpl();

    auto& imgui_style = ImGui::GetStyle();
    Colors[ColCanvasLines] = imgui_style.Colors[ImGuiCol_Separator];
    Colors[ColNodeBg] = imgui_style.Colors[ImGuiCol_WindowBg];
    Colors[ColNodeActiveBg] = imgui_style.Colors[ImGuiCol_FrameBgActive];
    Colors[ColNodeBorder] = imgui_style.Colors[ImGuiCol_Border];
    Colors[ColConnection] = imgui_style.Colors[ImGuiCol_PlotLines];
    Colors[ColConnectionActive] = imgui_style.Colors[ImGuiCol_PlotLinesHovered];
    Colors[ColSelectBg] = imgui_style.Colors[ImGuiCol_FrameBgActive];
    Colors[ColSelectBg].Value.w = 0.25f;
    Colors[ColSelectBorder] = imgui_style.Colors[ImGuiCol_Border];
}

CanvasState::~CanvasState()
{
    delete _Impl;
}

ImU32 MakeSlotDataID(const char* data, const char* slot_title, void* node_id, bool input_slot)
{
    ImU32 slot_id = ImHashStr(slot_title, 0, ImHashData(&node_id, sizeof(node_id)));
    if (input_slot)
    {
        // Ensure that input and output slots with same name have different ids.
        slot_id ^= ~0U;
    }
    return ImHashStr(data, 0, slot_id);
}

// Based on http://paulbourke.net/geometry/pointlineplane/
float GetDistanceToLineSquared(const ImVec2& point, const ImVec2& a, const ImVec2& b)
{
    float tx, ty, t, u;
    tx = b.x - a.x;
    ty = b.y - a.y;
    t = (tx * tx) + (ty * ty);
    u = ((point.x - a.x) * tx + (point.y - a.y) * ty) / t;
    if (u > 1)
        u = 1;
    else if (u < 0)
        u = 0;
    tx = (a.x + u * tx) - point.x;
    ty = (a.y + u * ty) - point.y;
    return tx * tx + ty * ty;
}

bool RenderConnection(const ImVec2& input_pos, const ImVec2& output_pos, float thickness)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    CanvasState* canvas = gCanvas;
    const ImGuiStyle& style = ImGui::GetStyle();

    thickness *= canvas->Zoom;

    ImVec2 p2 = input_pos - ImVec2{canvas->Style.CurveStrength * canvas->Zoom, 0};
    ImVec2 p3 = output_pos + ImVec2{canvas->Style.CurveStrength * canvas->Zoom, 0};
#if IMGUI_VERSION_NUM < 18000
    ImVec2 closest_pt = ImBezierClosestPointCasteljau(input_pos, p2, p3, output_pos, ImGui::GetMousePos(), style.CurveTessellationTol);
#else
    ImVec2 closest_pt = ImBezierCubicClosestPointCasteljau(input_pos, p2, p3, output_pos, ImGui::GetMousePos(), style.CurveTessellationTol);
#endif
    float min_square_distance = ImFabs(ImLengthSqr(ImGui::GetMousePos() - closest_pt));
    bool is_close = min_square_distance <= thickness * thickness;
#if IMGUI_VERSION_NUM < 18000
    draw_list->AddBezierCurve(input_pos, p2, p3, output_pos, is_close ? canvas->Colors[ColConnectionActive] : canvas->Colors[ColConnection], thickness, 0);
#else
    draw_list->AddBezierCubic(input_pos, p2, p3, output_pos, is_close ? canvas->Colors[ColConnectionActive] : canvas->Colors[ColConnection], thickness, 0);
#endif
    return is_close;
}

void BeginCanvas(CanvasState* canvas)
{
    canvas->_Impl->PrevCanvas = gCanvas;
    gCanvas = canvas;
    const ImGuiWindow* w = ImGui::GetCurrentWindow();
    ImGui::PushID(canvas);

    ImGui::ItemAdd(w->ContentRegionRect, ImGui::GetID("canvas"));

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();

    if (!ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
    {
        if (ImGui::IsMouseDragging(2))
            canvas->Offset += io.MouseDelta;

        if (io.KeyShift && !io.KeyCtrl)
            canvas->Offset.x += io.MouseWheel * 16.0f;

        if (!io.KeyShift && !io.KeyCtrl)
        {
            canvas->Offset.y += io.MouseWheel * 16.0f;
            canvas->Offset.x += io.MouseWheelH * 16.0f;
        }

        if (!io.KeyShift && io.KeyCtrl)
        {
            if (io.MouseWheel != 0)
            {
                ImVec2 mouseRel = ImVec2{ ImGui::GetMousePos().x - ImGui::GetWindowPos().x, ImGui::GetMousePos().y - ImGui::GetWindowPos().y };
                float prevZoom = canvas->Zoom;
                canvas->Zoom = ImClamp(canvas->Zoom + io.MouseWheel * canvas->Zoom / 16.f, 0.3f, 3.f);
                float zoomFactor = (prevZoom - canvas->Zoom) / prevZoom;
                canvas->Offset += (mouseRel - canvas->Offset) * zoomFactor;
            }
        }
    }

    const float grid = canvas->Style.GridSpacing * canvas->Zoom;

    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();

    ImU32 grid_color = ImColor(canvas->Colors[ColCanvasLines]);
    for (float x = fmodf(canvas->Offset.x, grid); x < size.x;)
    {
        draw_list->AddLine(ImVec2(x, 0) + pos, ImVec2(x, size.y) + pos, grid_color);
        x += grid;
    }

    for (float y = fmodf(canvas->Offset.y, grid); y < size.y;)
    {
        draw_list->AddLine(ImVec2(0, y) + pos, ImVec2(size.x, y) + pos, grid_color);
        y += grid;
    }

    ImGui::SetWindowFontScale(canvas->Zoom);

    canvas->_Impl->PrevSelectCount = canvas->_Impl->CurrSelectCount;
    canvas->_Impl->CurrSelectCount = 0;
}

void EndCanvas()
{
    IM_ASSERT(gCanvas != nullptr);     // Did you forget calling BeginCanvas()?

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    auto* canvas = gCanvas;
    auto* impl = canvas->_Impl;

    // Draw pending connection
    if (const ImGuiPayload* payload = ImGui::GetDragDropPayload())
    {
        char data_type_fragment[] = "new-node-connection-";
        if (strncmp(payload->DataType, data_type_fragment, sizeof(data_type_fragment) - 1) == 0)
        {
            auto* drag_data = (_DragConnectionPayload*)payload->Data;
            ImVec2 slot_pos{
                impl->CachedData.GetFloat(MakeSlotDataID("x", drag_data->SlotTitle, drag_data->NodeId,
                                                         IsInputSlotKind(drag_data->SlotKind))),
                impl->CachedData.GetFloat(MakeSlotDataID("y", drag_data->SlotTitle, drag_data->NodeId,
                                                         IsInputSlotKind(drag_data->SlotKind))),
            };

            float connection_indent = canvas->Style.ConnectionIndent * canvas->Zoom;

            ImVec2 input_pos, output_pos;
            if (IsInputSlotKind(drag_data->SlotKind))
            {
                input_pos = slot_pos;
                input_pos.x += connection_indent;
                output_pos = ImGui::GetMousePos();
            }
            else
            {
                input_pos = ImGui::GetMousePos();
                output_pos = slot_pos;
                output_pos.x -= connection_indent;
            }

            RenderConnection(input_pos, output_pos, canvas->Style.CurveThickness);
        }
    }

    if (impl->DoSelectionsFrame <= ImGui::GetCurrentContext()->FrameCount)
        impl->SingleSelectedNode = nullptr;

    if (impl->PendingActiveItemId != 0)
    {
        ImGui::SetActiveID(impl->PendingActiveItemId, ImGui::GetCurrentWindow());
        impl->PendingActiveItemId = 0;
    }

    switch (impl->State)
    {
    case State_None:
    {
        // Set the hovered node, if there is any, for the next frame.
        impl->HoveredNodeId = impl->PendingHoveredNodeId;

        ImGuiID canvas_id = ImGui::GetID("canvas");
        if (ImGui::IsMouseDown(0) && ImGui::GetCurrentWindow()->ContentRegionRect.Contains(ImGui::GetMousePos()))
        {
            if (ImGui::IsWindowHovered())
            {
                if (!ImGui::IsWindowFocused())
                    ImGui::FocusWindow(ImGui::GetCurrentWindow());

                if (!ImGui::IsAnyItemActive())
                {
                    ImGui::SetActiveID(canvas_id, ImGui::GetCurrentWindow());
                    const ImGuiIO& io = ImGui::GetIO();
                    if (!io.KeyCtrl && !io.KeyShift)
                    {
                        impl->SingleSelectedNode = nullptr;   // unselect all
                        impl->DoSelectionsFrame = ImGui::GetCurrentContext()->FrameCount + 1;
                    }
                }
            }

            if (ImGui::GetActiveID() == canvas_id && ImGui::IsMouseDragging(0))
            {
                impl->SelectionStart = ImGui::GetMousePos();
                impl->State = State_Select;
            }
        }
        else if (ImGui::GetActiveID() == canvas_id)
            ImGui::ClearActiveID();
        break;
    }
    case State_Drag:
    {
        if (!ImGui::IsMouseDown(0))
        {
            impl->State = State_None;
            impl->DragNode = nullptr;
        }
        break;
    }
    case State_Select:
    {
        if (ImGui::IsMouseDown(0))
        {
            draw_list->AddRectFilled(impl->SelectionStart, ImGui::GetMousePos(), canvas->Colors[ColSelectBg]);
            draw_list->AddRect(impl->SelectionStart, ImGui::GetMousePos(), canvas->Colors[ColSelectBorder]);
        }
        else
        {
            ImGui::ClearActiveID();
            impl->State = State_None;
        }
        break;
    }
    }

    // Clear this in preparation for the next frame.
    impl->PendingHoveredNodeId = 0;

    ImGui::SetWindowFontScale(1.f);
    ImGui::PopID();     // canvas
    gCanvas = impl->PrevCanvas;
}

bool BeginNode(void* node_id, ImVec2* pos, bool* selected)
{
    IM_ASSERT(gCanvas != nullptr);
    IM_ASSERT(node_id != nullptr);
    IM_ASSERT(pos != nullptr);
    IM_ASSERT(selected != nullptr);
    const ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    auto* canvas = gCanvas;
    auto* impl = canvas->_Impl;

    impl->Node.Id = node_id;
    impl->Node.Pos = pos;
    impl->Node.Selected = selected;

    // 0 - node rect, curves
    // 1 - node content
    draw_list->ChannelsSplit(2);

    if (node_id == impl->AutoPositionNodeId)
    {
        // Somewhere out of view so that we dont see node flicker when it will be repositioned
        ImGui::SetCursorScreenPos(ImGui::GetWindowPos() + ImGui::GetWindowSize() + style.WindowPadding);
    }
    else
    {
        // Top-let corner of the node
        ImGui::SetCursorScreenPos(ImGui::GetWindowPos() + (*pos) * canvas->Zoom + canvas->Offset);
    }

    ImGui::PushID(node_id);

    impl->Node.ItemId = ImGui::GetID(node_id);

    ImGui::BeginGroup();    // Slots and content group
    draw_list->ChannelsSetCurrent(1);

    return true;
}

void EndNode()
{
    IM_ASSERT(gCanvas != nullptr);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    auto* canvas = gCanvas;
    auto* impl = canvas->_Impl;
    auto* node_id = impl->Node.Id;

    bool& node_selected = *impl->Node.Selected;
    ImVec2& node_pos = *impl->Node.Pos;
    bool activate = false;

    ImGui::EndGroup();    // Slots and content group

    ImRect node_rect{
        ImGui::GetItemRectMin() - canvas->Style.NodeSpacing * canvas->Zoom,
        ImGui::GetItemRectMax() + canvas->Style.NodeSpacing * canvas->Zoom
    };

    // Render frame
    draw_list->ChannelsSetCurrent(0);

    ImColor node_color = canvas->Colors[node_selected ? ColNodeActiveBg : ColNodeBg];
    draw_list->AddRectFilled(node_rect.Min, node_rect.Max, node_color, canvas->Style.NodeRounding * canvas->Zoom);
    draw_list->AddRect(node_rect.Min, node_rect.Max, canvas->Colors[ColNodeBorder], canvas->Style.NodeRounding * canvas->Zoom);

    // Create node item
    ImGuiID node_item_id = impl->Node.ItemId;
    ImGui::ItemSize(node_rect.GetSize());
    ImGui::ItemAdd(node_rect, node_item_id);

    // Save last selection state in case we are about to start dragging multiple selected nodes
    if (ImGui::IsMouseClicked(0))
    {
        ImGuiID prev_selected_id = ImHashStr("prev-selected", 0, ImHashData(&impl->Node.Id, sizeof(impl->Node.Id)));
        impl->CachedData.SetBool(prev_selected_id, node_selected);
    }

    ImGuiIO& io = ImGui::GetIO();
    switch (impl->State)
    {
    case State_None:
    {
        //
        // Nodes are drawn from back to front, but the user interaction is rather front to back. Therefore the
        // top-most of overlayed nodes will not be known until all nodes have been rendered. So we continuously
        // save the potential top-most node and thus overwrite the previous candidate. The final top-most node
        // is known once EndCanvas() is called.
        //
        // Since we do this only in this state a node is not considered hovered when another node is dragged
        // or an area selection is being made. Also, since IsItemHovered() is not called with any flags a node is
        // not considered hovered during a pending connection (the source slot is active).
        //
        if (ImGui::IsItemHovered())
            impl->PendingHoveredNodeId = node_item_id;

        // Node selection behavior. Selection can change only when no node is being dragged and connections are not being made.
        if (impl->JustConnected || ImGui::GetDragDropPayload() != nullptr)
        {
            // No selections are performed when nodes are being connected.
            impl->JustConnected = false;
        }
        else if (impl->DoSelectionsFrame == ImGui::GetCurrentContext()->FrameCount)
        {
            // Unselect other nodes when some node was left-clicked.
            if (impl->SingleSelectedNode == node_id)
            {
                // Toggle selection status unless this wasn't the only selected node on previous frame.
                if (impl->PrevSelectCount > (node_selected ? 1 : 0))
                    node_selected = true;
                else
                    node_selected ^= true;
            }
            else
                node_selected = false;
        }
        else if (ImGui::IsMouseDown(0) && !ImGui::IsAnyItemActive() && ImGui::IsItemHovered())
        {
            // Nodes are drawn from back to front, but the user interaction is rather front to back. Therefore the
            // top-most of overlayed nodes will not be known until all nodes have been rendered. So we continuously
            // save the potential top-most node and thus overwrite the previous candidate. The final top-most node
            // is known once EndCanvas() is called.
            activate = true;
        }
        else if (ImGui::IsMouseReleased(0) && ImGui::IsItemHovered() && ImGui::IsItemActive())
        {
            if (!io.KeyCtrl)
            {
                impl->SingleSelectedNode = node_id;
                impl->DoSelectionsFrame = ImGui::GetCurrentContext()->FrameCount + 1;
            }
            else
                node_selected ^= true;
        }
        else if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))
        {
            impl->State = State_Drag;
            if (impl->DragNode == nullptr)
            {
                impl->DragNode = node_id;
                impl->DragNodeSelected = node_selected;
            }
            else
                impl->SingleSelectedNode = nullptr;
        }
        else if (node_id == impl->AutoPositionNodeId)
        {
            // Upon node creation we would like it to be positioned at the center of mouse cursor. This can be done only
            // once widget dimensions are known at the end of rendering and thus on the next frame.
            node_pos = (ImGui::GetMousePos() - ImGui::GetCurrentWindow()->Pos) / canvas->Zoom - canvas->Offset - (node_rect.GetSize() / 2);
            impl->AutoPositionNodeId = nullptr;
        }
        break;
    }
    case State_Drag:
    {
        if (ImGui::IsMouseDown(0))
        {
            // Node dragging behavior. Drag node under mouse and other selected nodes if current node is selected.
            if ((ImGui::IsItemActive() || (impl->DragNode && impl->DragNodeSelected && node_selected)))
                node_pos += ImGui::GetIO().MouseDelta / canvas->Zoom;
        }
        break;
    }
    case State_Select:
    {
        ImRect selection_rect;
        selection_rect.Min.x = ImMin(impl->SelectionStart.x, ImGui::GetMousePos().x);
        selection_rect.Min.y = ImMin(impl->SelectionStart.y, ImGui::GetMousePos().y);
        selection_rect.Max.x = ImMax(impl->SelectionStart.x, ImGui::GetMousePos().x);
        selection_rect.Max.y = ImMax(impl->SelectionStart.y, ImGui::GetMousePos().y);

        ImGuiID prev_selected_id = ImHashStr("prev-selected", 0, ImHashData(&impl->Node.Id, sizeof(impl->Node.Id)));
        if (io.KeyShift)
        {
            // Append selection
            if (selection_rect.Contains(node_rect))
                node_selected = true;
            else
                node_selected = impl->CachedData.GetBool(prev_selected_id);
        }
        else if (io.KeyCtrl)
        {
            // Subtract from selection
            if (selection_rect.Contains(node_rect))
                node_selected = false;
            else
                node_selected = impl->CachedData.GetBool(prev_selected_id);
        }
        else
        {
            // Assign selection
            node_selected = selection_rect.Contains(node_rect);
        }
        break;
    }
    }

    // Set the node as pending active unless one of it's slot's is pending active.
    if (impl->PendingActiveSlotId != 0)
    {
        impl->PendingActiveItemId = impl->PendingActiveSlotId;
        impl->PendingActiveSlotId = 0;
    }
    else if (activate)
        impl->PendingActiveItemId = node_item_id;

    draw_list->ChannelsMerge();

    if (!ImGui::IsMouseDown(0) && ImGui::IsItemActive())
        ImGui::ClearActiveID();

    if (node_selected)
        impl->CurrSelectCount++;

    ImGui::PopID();     // id
}

bool IsNodeHovered()
{
    assert(gCanvas != nullptr);
    return gCanvas->_Impl->Node.ItemId == gCanvas->_Impl->HoveredNodeId;
}

bool GetNewConnection(void** input_node, const char** input_slot_title, void** output_node, const char** output_slot_title)
{
    IM_ASSERT(gCanvas != nullptr);
    IM_ASSERT(input_node != nullptr);
    IM_ASSERT(input_slot_title != nullptr);
    IM_ASSERT(output_node != nullptr);
    IM_ASSERT(output_slot_title != nullptr);

    auto* canvas = gCanvas;
    auto* impl = canvas->_Impl;

    if (impl->NewConnection.OutputNode != nullptr)
    {
        *input_node = impl->NewConnection.InputNode;
        *input_slot_title = impl->NewConnection.InputSlot;
        *output_node = impl->NewConnection.OutputNode;
        *output_slot_title = impl->NewConnection.OutputSlot;
        impl->NewConnection = {};
        return true;
    }

    return false;
}

bool GetPendingConnection(void** node_id, const char** slot_title, int* slot_kind)
{
    IM_ASSERT(gCanvas != nullptr);
    IM_ASSERT(node_id != nullptr);
    IM_ASSERT(slot_title != nullptr);
    IM_ASSERT(slot_kind != nullptr);

    if (auto* payload = ImGui::GetDragDropPayload())
    {
        char drag_id[] = "new-node-connection-";
        if (strncmp(drag_id, payload->DataType, sizeof(drag_id) - 1) == 0)
        {
            auto* drag_payload = (_DragConnectionPayload*)payload->Data;
            *node_id = drag_payload->NodeId;
            *slot_title = drag_payload->SlotTitle;
            *slot_kind = drag_payload->SlotKind;
            return true;
        }
    }

    return false;
}

bool Connection(void* input_node, const char* input_slot, void* output_node, const char* output_slot)
{
    IM_ASSERT(gCanvas != nullptr);
    IM_ASSERT(input_node != nullptr);
    IM_ASSERT(input_slot != nullptr);
    IM_ASSERT(output_node != nullptr);
    IM_ASSERT(output_slot != nullptr);

    bool is_connected = true;
    auto* canvas = gCanvas;
    auto* impl = canvas->_Impl;

    if (input_node == impl->AutoPositionNodeId || output_node == impl->AutoPositionNodeId)
        // Do not render connection to newly added output node because node is rendered outside of screen on the first frame and will be repositioned.
        return is_connected;

    ImVec2 input_slot_pos{
        impl->CachedData.GetFloat(MakeSlotDataID("x", input_slot, input_node, true)),
        impl->CachedData.GetFloat(MakeSlotDataID("y", input_slot, input_node, true)),
    };

    ImVec2 output_slot_pos{
        impl->CachedData.GetFloat(MakeSlotDataID("x", output_slot, output_node, false)),
        impl->CachedData.GetFloat(MakeSlotDataID("y", output_slot, output_node, false)),
    };

    // Indent connection a bit into slot widget.
    float connection_indent = canvas->Style.ConnectionIndent * canvas->Zoom;
    input_slot_pos.x += connection_indent;
    output_slot_pos.x -= connection_indent;

    bool curve_hovered = RenderConnection(input_slot_pos, output_slot_pos, canvas->Style.CurveThickness);
    if (curve_hovered && ImGui::IsWindowHovered())
    {
        if (ImGui::IsMouseDoubleClicked(0))
            is_connected = false;
    }

    impl->CachedData.SetFloat(MakeSlotDataID("hovered", input_slot, input_node, true), curve_hovered && is_connected);
    impl->CachedData.SetFloat(MakeSlotDataID("hovered", output_slot, output_node, false), curve_hovered && is_connected);

    void* pending_node_id;
    const char* pending_slot_title;
    int pending_slot_kind;
    if (GetPendingConnection(&pending_node_id, &pending_slot_title, &pending_slot_kind))
    {
        _IgnoreSlot ignore_connection{};
        if (IsInputSlotKind(pending_slot_kind))
        {
            if (pending_node_id == input_node && strcmp(pending_slot_title, input_slot) == 0)
            {
                ignore_connection.NodeId = output_node;
                ignore_connection.SlotName = output_slot;
                ignore_connection.SlotKind = OutputSlotKind(1);
            }
        }
        else
        {
            if (pending_node_id == output_node && strcmp(pending_slot_title, output_slot) == 0)
            {
                ignore_connection.NodeId = input_node;
                ignore_connection.SlotName = input_slot;
                ignore_connection.SlotKind = InputSlotKind(1);
            }
        }
        if (ignore_connection.NodeId)
        {
            if (!impl->IgnoreConnections.contains(ignore_connection))
                impl->IgnoreConnections.push_back(ignore_connection);
        }
    }

    return is_connected;
}

CanvasState* GetCurrentCanvas()
{
    return gCanvas;
}

bool BeginSlot(const char* title, int kind)
{
    auto* canvas = gCanvas;
    auto* impl = canvas->_Impl;

    impl->slot.Title = title;
    impl->slot.Kind = kind;

    ImGui::BeginGroup();
    return true;
}

void EndSlot()
{
    auto* canvas = gCanvas;
    auto* impl = canvas->_Impl;

    ImGui::EndGroup();

    ImGui::PushID(impl->slot.Title);
    ImGui::PushID(impl->slot.Kind);

    ImRect slot_rect{ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
    // This here adds extra line between slots because after user renders slot cursor is already past those items.
    // ImGui::ItemSize(slot_rect.GetSize());
    ImGui::ItemAdd(slot_rect, ImGui::GetID(impl->slot.Title));

    if (ImGui::IsMouseClicked(0) && ImGui::IsItemHovered())
        impl->PendingActiveSlotId = ImGui::GetID(impl->slot.Title);

    if (ImGui::IsItemActive() && !ImGui::IsMouseDown(0))
        ImGui::ClearActiveID();

    // Store slot edge positions, curves will connect there
    {
        float x;
        if (IsInputSlotKind(impl->slot.Kind))
            x = slot_rect.Min.x;
        else
            x = slot_rect.Max.x;

        impl->CachedData.SetFloat(MakeSlotDataID("x", impl->slot.Title, impl->Node.Id, IsInputSlotKind(impl->slot.Kind)), x);
        impl->CachedData.SetFloat(MakeSlotDataID("y", impl->slot.Title, impl->Node.Id, IsInputSlotKind(impl->slot.Kind)),
            slot_rect.Max.y - slot_rect.GetHeight() / 2);
    }

#if 0
    if (ImGui::BeginDragDropSource())
    {
        auto* payload = ImGui::GetDragDropPayload();
        char drag_id[32];
        snprintf(drag_id, sizeof(drag_id), "new-node-connection-%08X", impl->slot.Kind);
        if (payload == nullptr || !payload->IsDataType(drag_id))
        {
            _DragConnectionPayload drag_data{ };
            drag_data.NodeId = impl->Node.Id;
            drag_data.SlotKind = impl->slot.Kind;
            drag_data.SlotTitle = impl->slot.Title;

            ImGui::SetDragDropPayload(drag_id, &drag_data, sizeof(drag_data));

            // Clear new connection info
            impl->NewConnection.InputNode = nullptr;
            impl->NewConnection.InputSlot = nullptr;
            impl->NewConnection.OutputNode = nullptr;
            impl->NewConnection.OutputSlot = nullptr;
            canvas->_Impl->IgnoreConnections.clear();
        }
        ImGui::TextUnformatted(impl->slot.Title);
        ImGui::EndDragDropSource();
    }

    if (IsConnectingCompatibleSlot() && ImGui::BeginDragDropTarget())
    {
        // Accept drags from opposite type (input <-> output, and same kind)
        char drag_id[32];
        snprintf(drag_id, sizeof(drag_id), "new-node-connection-%08X", impl->slot.Kind * -1);

        if (auto* payload = ImGui::AcceptDragDropPayload(drag_id))
        {
            auto* drag_data = (_DragConnectionPayload*) payload->Data;

            // Store info of source slot to be queried by ImNodes::GetConnection()
            if (!IsInputSlotKind(impl->slot.Kind))
            {
                impl->NewConnection.InputNode = drag_data->NodeId;
                impl->NewConnection.InputSlot = drag_data->SlotTitle;
                impl->NewConnection.OutputNode = impl->Node.Id;
                impl->NewConnection.OutputSlot = impl->slot.Title;
            }
            else
            {
                impl->NewConnection.InputNode = impl->Node.Id;
                impl->NewConnection.InputSlot = impl->slot.Title;
                impl->NewConnection.OutputNode = drag_data->NodeId;
                impl->NewConnection.OutputSlot = drag_data->SlotTitle;
            }
            impl->JustConnected = true;
            canvas->_Impl->IgnoreConnections.clear();
        }

        ImGui::EndDragDropTarget();
    }
#endif

    ImGui::PopID(); // kind
    ImGui::PopID(); // name
}

void AutoPositionNode(void* node_id)
{
    IM_ASSERT(gCanvas != nullptr);
    gCanvas->_Impl->AutoPositionNodeId = node_id;
}

bool IsSlotCurveHovered()
{
    IM_ASSERT(gCanvas != nullptr);
    auto* canvas = gCanvas;
    auto* impl = canvas->_Impl;

    void* node_id;
    const char* slot_title;
    int slot_kind;
    if (ImNodes::GetPendingConnection(&node_id, &slot_title, &slot_kind))
    {
        // In-progress connection to current slot is hovered
        return node_id == impl->Node.Id && strcmp(slot_title, impl->slot.Title) == 0 &&
               slot_kind == impl->slot.Kind;
    }

    // Actual curve is hovered
    return impl->CachedData.GetBool(MakeSlotDataID("hovered", impl->slot.Title, impl->Node.Id,
                                                   IsInputSlotKind(impl->slot.Kind)));
}

bool IsConnectingCompatibleSlot()
{
    IM_ASSERT(gCanvas != nullptr);
    auto* canvas = gCanvas;
    auto* impl = canvas->_Impl;

    if (auto* payload = ImGui::GetDragDropPayload())
    {
        auto* drag_payload = (_DragConnectionPayload*)payload->Data;

        if (drag_payload->NodeId == impl->Node.Id)
            // Node can not connect to itself
            return false;

        char drag_id[32];
        snprintf(drag_id, sizeof(drag_id), "new-node-connection-%08X", impl->slot.Kind * -1);
        if (strcmp(drag_id, payload->DataType) != 0)
            return false;

        for (int i = 0; i < impl->IgnoreConnections.size(); i++)
        {
            const _IgnoreSlot& ignored = impl->IgnoreConnections[i];
            if (ignored.NodeId == impl->Node.Id && strcmp(ignored.SlotName, impl->slot.Title) == 0 &&
                IsInputSlotKind(ignored.SlotKind) == IsInputSlotKind(impl->slot.Kind))
                return false;
        }

        return true;
    }

    return false;
}

}
