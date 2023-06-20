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
#include "ImNodesEz.h"

#include <imgui_internal.h>

namespace ImNodes
{

namespace Ez
{

static ImVec4& GetStyleColorRef(ImNodesStyleCol idx);
static ImU32 GetStyleColorU32(ImNodesStyleCol idx);

struct StyleVarMod
{
    ImNodesStyleVar Index;
    float Value[2];
    StyleVarMod(ImNodesStyleVar idx, float val)          { Index = idx; Value[0] = val; }
    StyleVarMod(ImNodesStyleVar idx, const ImVec2 &val)  { Index = idx; Value[0] = val.x; Value[1] = val.y; }
};

struct StyleColMod
{
    ImNodesStyleCol Index;
    ImVec4 Value;
};

struct Context
{
    StyleVars Style;
    ImVector<StyleVarMod> StyleVarStack;
    ImVector<StyleColMod> StyleColStack;
    ImDrawListSplitter NodeSplitter;
    ImDrawListSplitter CanvasSplitter;
    float BodyPosY;
    bool *NodeSelected;
    CanvasState State;
};

static Context *GContext = nullptr;


Context* CreateContext()
{
    Context *ctx = new Context();
    if (GContext == nullptr)
        GContext = ctx;
    return ctx;
}

void FreeContext(Context *ctx)
{
    if (GContext == ctx)
        GContext = nullptr;
    delete ctx;
}

void SetContext(Context *ctx)
{
    GContext = ctx;
}


ImNodes::CanvasState& GetState()
{
    return GContext->State;
}


void BeginCanvas()
{
    IM_ASSERT(GContext != nullptr);
    Context &g = *GContext;
    auto draw_list = ImGui::GetWindowDrawList();

    //
    // Setup and use this splitter to separate nodes and connections into layers. The connections should
    // not be rendered until after the nodes to get correct positions in relation to the nodes' slots on
    // the same frame, but be rendered behind the nodes.
    //
    g.CanvasSplitter.Clear();
    g.CanvasSplitter.Split(draw_list, 2);

    ImNodes::BeginCanvas(&GContext->State);
}

void EndCanvas()
{
    IM_ASSERT(GContext != nullptr);
    Context &g = *GContext;
    auto draw_list = ImGui::GetWindowDrawList();

    ImNodes::EndCanvas();

    g.CanvasSplitter.Merge(draw_list);    
}


bool BeginNode(void* node_id, const char* title, ImVec2* pos, bool* selected)
{
    IM_ASSERT(GContext != nullptr);
    Context &g = *GContext;
    ImGuiStorage *storage = ImGui::GetStateStorage();
    auto draw_list = ImGui::GetWindowDrawList();

    g.NodeSelected = selected;

    g.CanvasSplitter.SetCurrentChannel(draw_list, 1);   // Node layer.

    g.NodeSplitter.Clear();
    g.NodeSplitter.Split(draw_list, 2);
    g.NodeSplitter.SetCurrentChannel(draw_list, 1);     // Front layer.

    bool result = ImNodes::BeginNode(node_id, pos, selected);

    ImVec2 title_size = ImGui::CalcTextSize(title);
    ImVec2 title_pos = ImGui::GetCursorScreenPos();
    g.BodyPosY = title_pos.y + title_size.y + g.State.Style.NodeSpacing.y * g.State.Zoom;
    ImVec2 input_pos = ImVec2{title_pos.x, g.BodyPosY + g.State.Style.NodeSpacing.y * g.State.Zoom};

    // Get widths from previous frame rendering.
    float input_width = storage->GetFloat(ImGui::GetID("input-width"));
    float content_width = storage->GetFloat(ImGui::GetID("content-width"));
    float output_width = storage->GetFloat(ImGui::GetID("output-width"));
    float body_width = input_width + content_width + output_width;

    // Ignore this the first time the node is rendered since we don't know any widths yet.
    if (body_width > 0)
    {
        float output_max_title_width_next = storage->GetFloat(ImGui::GetID("output-max-title-width-next"));
        storage->SetFloat(ImGui::GetID("output-max-title-width"), output_max_title_width_next);
        storage->SetFloat(ImGui::GetID("output-max-title-width-next"), 0);

        body_width += 2*g.Style.ItemSpacing.x * g.State.Zoom;
        float body_spacing = 0;

        if (body_width > title_size.x)
        {
            // Center node title
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + body_width*0.5f - title_size.x*0.5f);
        }
        else
        {
            // Calculate extra node body spacing, i.e. around content, needed due to wider title.
            body_spacing = ((title_size.x - body_width)*0.5f);
        }

        float content_x = input_pos.x + input_width + g.Style.ItemSpacing.x * g.State.Zoom + body_spacing;
        float output_x = content_x + content_width + g.Style.ItemSpacing.x * g.State.Zoom + body_spacing;
        storage->SetFloat(ImGui::GetID("content-x"), content_x);
        storage->SetFloat(ImGui::GetID("output-x"), output_x);
        storage->SetFloat(ImGui::GetID("body-y"), input_pos.y);
    }

    // Render node title
    ImGui::TextUnformatted(title);

    ImGui::SetCursorScreenPos(input_pos);

    return result;
}

void EndNode()
{
    IM_ASSERT(GContext != nullptr);
    Context &g = *GContext;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    bool hovered = IsNodeHovered();

    // Inhibit node rendering in ImNodes::EndNode() by setting colors with alpha as 0.
    ImColor activebg = g.State.Colors[ColNodeActiveBg];
    ImColor inactivebg = g.State.Colors[ColNodeBg];
    ImColor border = g.State.Colors[ColNodeBorder];
    g.State.Colors[ColNodeActiveBg] = IM_COL32(0,0,0,0);
    g.State.Colors[ColNodeBg] = IM_COL32(0,0,0,0);
    g.State.Colors[ColNodeBorder] = IM_COL32(0,0,0,0);

    ImNodes::EndNode();

    // Restore colors.
    g.State.Colors[ColNodeActiveBg] = activebg;
    g.State.Colors[ColNodeBg] = inactivebg;
    g.State.Colors[ColNodeBorder] = border;

    ImRect node_rect{
        ImGui::GetItemRectMin(),
        ImGui::GetItemRectMax()
    };

    ImVec2 titlebar_end = ImVec2{node_rect.Max.x, g.BodyPosY};
    ImVec2 body_pos = ImVec2{node_rect.Min.x, g.BodyPosY};

    g.NodeSplitter.SetCurrentChannel(draw_list, 0);     // Background layer.

    // Render title bar background
    ImU32 node_color = GetStyleColorU32(*g.NodeSelected ? ImNodesStyleCol_NodeTitleBarBgActive : hovered ? ImNodesStyleCol_NodeTitleBarBgHovered : ImNodesStyleCol_NodeTitleBarBg);
    draw_list->AddRectFilled(node_rect.Min, titlebar_end, node_color, g.State.Style.NodeRounding, ImDrawFlags_RoundCornersTop);

    // Render body background
    node_color = GetStyleColorU32(*g.NodeSelected ? ImNodesStyleCol_NodeBodyBgActive : hovered ? ImNodesStyleCol_NodeBodyBgHovered : ImNodesStyleCol_NodeBodyBg);
    draw_list->AddRectFilled(body_pos, node_rect.Max, node_color, g.State.Style.NodeRounding, ImDrawFlags_RoundCornersBottom);

    // Render outlines
    draw_list->AddRect(node_rect.Min, node_rect.Max, GetStyleColorU32(ImNodesStyleCol_NodeBorder), g.State.Style.NodeRounding);
    draw_list->AddLine(body_pos, titlebar_end, GetStyleColorU32(ImNodesStyleCol_NodeBorder));

    g.NodeSplitter.Merge(draw_list);
}

bool Slot(const char* title, int kind, ImVec2 &pos)
{
    IM_ASSERT(GContext != nullptr);
    Context &g = *GContext;
    auto* storage = ImGui::GetStateStorage();
    const float CIRCLE_RADIUS = g.Style.SlotRadius * g.State.Zoom;
    ImVec2 title_size = ImGui::CalcTextSize(title);
    // Pull entire slot a little bit out of the edge so that curves connect into it without visible seams
    float item_offset_x = g.State.Style.NodeSpacing.x + CIRCLE_RADIUS;
    if (!ImNodes::IsOutputSlotKind(kind))
        item_offset_x = -item_offset_x;
    ImGui::SetCursorScreenPos(pos + ImVec2{item_offset_x, 0 });

    pos.y += ImMax(title_size.y, 2*CIRCLE_RADIUS) + g.Style.ItemSpacing.y;

    if (ImNodes::BeginSlot(title, kind))
    {
        auto* draw_lists = ImGui::GetWindowDrawList();

        // Slot appearance can be altered depending on curve hovering state.
        bool is_active = ImNodes::IsSlotCurveHovered() ||
                         (ImNodes::IsConnectingCompatibleSlot() /*&& !IsAlreadyConnectedWithPendingConnection(title, kind)*/);

        ImColor color = GetStyleColorRef(is_active ? ImNodesStyleCol_ConnectionActive : ImNodesStyleCol_Connection);

        ImGui::PushStyleColor(ImGuiCol_Text, color.Value);

        // Compensate for large slot circles.
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImMax(CIRCLE_RADIUS - title_size.y*0.5f, 0.0f));

        if (ImNodes::IsOutputSlotKind(kind))
        {
            float *max_width_next = storage->GetFloatRef(ImGui::GetID("output-max-title-width-next"));
            *max_width_next = ImMax(*max_width_next, title_size.x);

            float offset = storage->GetFloat(ImGui::GetID("output-max-title-width"), title_size.x) - title_size.x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);

            ImGui::TextUnformatted(""); //title); <-HACK
            ImGui::SameLine(0.0f, g.Style.ItemSpacing.x);
        }

        ImRect circle_rect{
            ImGui::GetCursorScreenPos(),
            ImGui::GetCursorScreenPos() + ImVec2{CIRCLE_RADIUS * 2, CIRCLE_RADIUS * 2}
        };
        // Vertical-align circle in the middle of the line.
        float circle_offset_y = title_size.y / 2.f - CIRCLE_RADIUS;
        circle_rect.Min.y += circle_offset_y;
        circle_rect.Max.y += circle_offset_y;
        if( kind != 0 ) // <-HACK
            draw_lists->AddCircleFilled(circle_rect.GetCenter(), CIRCLE_RADIUS, color);

        ImGui::ItemSize(circle_rect.GetSize());
        ImGui::ItemAdd(circle_rect, ImGui::GetID(title));

        if (ImNodes::IsInputSlotKind(kind))
        {
            ImGui::SameLine(0.0f, g.Style.ItemSpacing.x);
            ImGui::TextUnformatted("");
        }

        ImGui::PopStyleColor();
        ImNodes::EndSlot();

        // A dirty trick to place output slot circle on the border.
        ImGui::GetCurrentWindow()->DC.CursorMaxPos.x -= item_offset_x;
        return true;
    }
    return false;
}

void InputSlots(const SlotInfo* slots, int snum)
{
    IM_ASSERT(GContext != nullptr);
    Context &g = *GContext;
    ImGuiStorage *storage = ImGui::GetStateStorage();

    PushStyleVar(ImNodesStyleVar_ItemSpacing, g.Style.ItemSpacing * g.State.Zoom);
    PushStyleVar(ImNodesStyleVar_NodeSpacing, g.State.Style.NodeSpacing * g.State.Zoom);

    // Get cursor screen position to be updated by slots as they are rendered.
    ImVec2 pos = ImGui::GetCursorScreenPos();

    // Render input slots
    ImGui::BeginGroup();
    {
        for (int i = 0; i < snum; i++)
            ImNodes::Ez::Slot(slots[i].title.c_str(), ImNodes::InputSlotKind(slots[i].kind), pos);
    }
    ImGui::EndGroup();

    storage->SetFloat(ImGui::GetID("input-width"), ImGui::GetItemRectSize().x*0 );

    // Move cursor to the next column
    ImGui::SetCursorScreenPos(ImVec2{storage->GetFloat(ImGui::GetID("content-x")), storage->GetFloat(ImGui::GetID("body-y"))});

    PopStyleVar(2);

    // Begin region for node content
    ImGui::BeginGroup();
}

void OutputSlots(const SlotInfo* slots, int snum)
{
    IM_ASSERT(GContext != nullptr);
    Context &g = *GContext;
    ImGuiStorage *storage = ImGui::GetStateStorage();

    // End region of node content
    ImGui::EndGroup();

    PushStyleVar(ImNodesStyleVar_ItemSpacing, g.Style.ItemSpacing * g.State.Zoom);
    PushStyleVar(ImNodesStyleVar_NodeSpacing, g.State.Style.NodeSpacing * g.State.Zoom);

    storage->SetFloat(ImGui::GetID("content-width"), ImGui::GetItemRectSize().x);

    // Get cursor screen position to be updated by slots as they are rendered.
    ImVec2 pos = ImVec2{storage->GetFloat(ImGui::GetID("output-x")), storage->GetFloat(ImGui::GetID("body-y"))};

    // Set cursor screen position as it is recorded as the starting point in BeginGroup() for the item rect size.
    ImGui::SetCursorScreenPos(pos);

    // Render output slots in the next column
    ImGui::BeginGroup();
    {
        for (int i = 0; i < snum; i++)
            ImNodes::Ez::Slot(slots[i].title.c_str(), ImNodes::OutputSlotKind(slots[i].kind), pos);
    }
    ImGui::EndGroup();

    storage->SetFloat(ImGui::GetID("output-width"), ImGui::GetItemRectSize().x);

    PopStyleVar(2);
}

bool Connection(void* input_node, const char* input_slot, void* output_node, const char* output_slot)
{
    IM_ASSERT(GContext != nullptr);
    Context &g = *GContext;
    auto draw_list = ImGui::GetWindowDrawList();

    g.CanvasSplitter.SetCurrentChannel(draw_list, 0);   // Connection layer.

    return ImNodes::Connection(input_node, input_slot, output_node, output_slot);
}

void PushStyleVar(ImNodesStyleVar idx, float val)
{
    IM_ASSERT(GContext != nullptr);
    Context &g = *GContext;
    float *var;
    switch (idx)
    {
    case ImNodesStyleVar_GridSpacing: var = &g.State.Style.GridSpacing; break;
    case ImNodesStyleVar_CurveThickness: var = &g.State.Style.CurveThickness; break;
    case ImNodesStyleVar_CurveStrength: var = &g.State.Style.CurveStrength; break;
    case ImNodesStyleVar_SlotRadius: var = &g.Style.SlotRadius; break;
    case ImNodesStyleVar_NodeRounding: var = &g.State.Style.NodeRounding; break;
    default: IM_ASSERT(0 && "Called PushStyleVar() float variant but variable is not a float!");
    }
    g.StyleVarStack.push_back(StyleVarMod(idx, *var));
    *var = val;
}

void PushStyleVar(ImNodesStyleVar idx, const ImVec2& val)
{
    IM_ASSERT(GContext != nullptr);
    Context &g = *GContext;
    ImVec2 *var;
    switch (idx)
    {
    case ImNodesStyleVar_NodeSpacing: var = &g.State.Style.NodeSpacing; break;
    case ImNodesStyleVar_ItemSpacing: var = &g.Style.ItemSpacing; break;
    default: IM_ASSERT(0 && "Called PushStyleVar() ImVec2 variant but variable is not a ImVec2!");
    }
    g.StyleVarStack.push_back(StyleVarMod(idx, *var));
    *var = val;
}

void PopStyleVar(int count)
{
    IM_ASSERT(GContext != nullptr);
    Context &g = *GContext;
    IM_ASSERT(g.StyleVarStack.size() >= count);
    while (count > 0)
    {
        StyleVarMod& backup = g.StyleVarStack.back();
        switch (backup.Index)
        {
        case ImNodesStyleVar_GridSpacing: g.State.Style.GridSpacing = backup.Value[0]; break;
        case ImNodesStyleVar_CurveThickness: g.State.Style.CurveThickness = backup.Value[0]; break;
        case ImNodesStyleVar_CurveStrength: g.State.Style.CurveStrength = backup.Value[0]; break;
        case ImNodesStyleVar_SlotRadius: g.Style.SlotRadius = backup.Value[0]; break;
        case ImNodesStyleVar_NodeRounding: g.State.Style.NodeRounding = backup.Value[0]; break;
        case ImNodesStyleVar_NodeSpacing: g.State.Style.NodeSpacing = ImVec2{backup.Value[0], backup.Value[1]}; break;
        case ImNodesStyleVar_ItemSpacing: g.Style.ItemSpacing = ImVec2{backup.Value[0], backup.Value[1]}; break;
        default: IM_ASSERT(0);
        }
        g.StyleVarStack.pop_back();
        count--;
    }
}

static ImVec4& GetStyleColorRef(ImNodesStyleCol idx)
{
    IM_ASSERT(GContext != nullptr);
    Context &g = *GContext;
    IM_ASSERT(idx < ImNodesStyleCol_COUNT);
    switch (idx)
    {
    case ImNodesStyleCol_GridLines:             return g.State.Colors[ColCanvasLines].Value;
    case ImNodesStyleCol_NodeBodyBg:            return g.Style.Colors.NodeBodyBg;
    case ImNodesStyleCol_NodeBodyBgHovered:     return g.Style.Colors.NodeBodyBgHovered;
    case ImNodesStyleCol_NodeBodyBgActive:      return g.Style.Colors.NodeBodyBgActive;
    case ImNodesStyleCol_NodeBorder:            return g.Style.Colors.NodeBorder;
    case ImNodesStyleCol_Connection:            return g.State.Colors[ColConnection].Value;
    case ImNodesStyleCol_ConnectionActive:      return g.State.Colors[ColConnectionActive].Value;
    case ImNodesStyleCol_SelectBg:              return g.State.Colors[ColSelectBg].Value;
    case ImNodesStyleCol_SelectBorder:          return g.State.Colors[ColSelectBorder].Value;
    case ImNodesStyleCol_NodeTitleBarBg:        return g.Style.Colors.NodeTitleBarBg;
    case ImNodesStyleCol_NodeTitleBarBgHovered: return g.Style.Colors.NodeTitleBarBgHovered;
    case ImNodesStyleCol_NodeTitleBarBgActive:  return g.Style.Colors.NodeTitleBarBgActive;
    default: IM_ASSERT(0);
    }
    static ImVec4 nothing(0,0,0,0);
    return nothing;
}

static ImU32 GetStyleColorU32(ImNodesStyleCol idx)
{
    return ImGui::ColorConvertFloat4ToU32(GetStyleColorRef(idx));
}

void PushStyleColor(ImNodesStyleCol idx, ImU32 col)
{
    PushStyleColor(idx, ImGui::ColorConvertU32ToFloat4(col));
}

void PushStyleColor(ImNodesStyleCol idx, const ImVec4& col)
{
    IM_ASSERT(GContext != nullptr);
    Context &g = *GContext;
    ImVec4 &val = GetStyleColorRef(idx);
    g.StyleColStack.push_back(StyleColMod{idx, val});
    val = col;
}

void PopStyleColor(int count)
{
    IM_ASSERT(GContext != nullptr);
    Context &g = *GContext;
    IM_ASSERT(g.StyleColStack.size() >= count);
    while (count > 0)
    {
        StyleColMod &backup = g.StyleColStack.back();
        ImVec4 &val = GetStyleColorRef(backup.Index);
        val = backup.Value;
        g.StyleColStack.pop_back();
        count--;
    }
}

}

}
