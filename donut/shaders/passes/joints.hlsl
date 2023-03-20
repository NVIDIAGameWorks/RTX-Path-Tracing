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

#include <donut/shaders/view_cb.h>
#include <donut/shaders/packing.hlsli>
#include <donut/shaders/vulkan.hlsli>

// simple line drawing shader for the joints render pass

cbuffer c_Constants : register(b0 VK_DESCRIPTOR_SET(0))
{
    PlanarViewConstants g_Constants;
};

struct VertexAttributes
{
    float3 position : POSITION;
    uint color : COLOR;
};

void main_vs(
    in VertexAttributes i_vtx
    , in uint i_instanceID : SV_InstanceID
    , out float4 o_position : SV_Position
    , out float3 o_color : COLOR
)
{
    o_position = mul(float4(i_vtx.position, 1), g_Constants.matWorldToClip);

    o_color = Unpack_RGB8_SNORM(i_vtx.color);
}

void main_ps(
    in float4 i_position : SV_Position
    , in float3 i_color : COLOR
    , out float4 o_color : SV_Target0
)
{
    o_color = float4(i_color, 1);
}