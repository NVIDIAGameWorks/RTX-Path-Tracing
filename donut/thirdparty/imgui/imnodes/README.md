ImNodes
=======

A standalone [Dear ImGui](https://github.com/ocornut/imgui) node graph implementation.

![image](https://user-images.githubusercontent.com/19151258/59259827-23a19400-8c43-11e9-9fdb-a3e4465a98e5.png)

Library provides core features needed to create a node graph, while leaving it to the user to define content of node.
Node layouting is left to the user, however comprehensible example is available which can be used as a base.

## A (very) trivial example

```cpp
static ImNodes::CanvasState canvas;

if (ImGui::Begin("ImNodes", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
{
    ImNodes::BeginCanvas(&canvas);

    struct Node
    {
        ImVec2 pos{};
        bool selected{};
        ImNodes::Ez::SlotInfo inputs[1];
        ImNodes::Ez::SlotInfo outputs[1];
    };

    static Node nodes[3] = {
        {{50, 100}, false, {{"In", 1}}, {{"Out", 1}}},
        {{250, 50}, false, {{"In", 1}}, {{"Out", 1}}},
        {{250, 100}, false, {{"In", 1}}, {{"Out", 1}}},
    };

    for (Node& node : nodes)
    {
        if (ImNodes::Ez::BeginNode(&node, "Node Title", &node.pos, &node.selected))
        {
            ImNodes::Ez::InputSlots(node.inputs, 1);
            ImNodes::Ez::OutputSlots(node.outputs, 1);
            ImNodes::Ez::EndNode();
        }
    }

    ImNodes::Connection(&nodes[1], "In", &nodes[0], "Out");
    ImNodes::Connection(&nodes[2], "In", &nodes[0], "Out");

    ImNodes::EndCanvas();
}
ImGui::End();
```

![image](https://user-images.githubusercontent.com/19151258/59609404-ff045b00-911f-11e9-8dc0-361a083b6c37.png)
