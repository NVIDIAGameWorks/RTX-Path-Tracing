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

#pragma pack_matrix(row_major)

#include <donut/shaders/gbuffer_cb.h>
#include <donut/shaders/forward_vertex.hlsli>
#include <donut/shaders/vulkan.hlsli>

cbuffer c_GBuffer : register(b1 VK_DESCRIPTOR_SET(1))
{
    GBufferFillConstants c_GBuffer;
};

void main(
    in SceneVertex i_vtx,
    in float4 i_instanceMatrix0 : TRANSFORM0,
    in float4 i_instanceMatrix1 : TRANSFORM1,
    in float4 i_instanceMatrix2 : TRANSFORM2,
#if MOTION_VECTORS
    in float4 i_prevInstanceMatrix0 : PREV_TRANSFORM0,
    in float4 i_prevInstanceMatrix1 : PREV_TRANSFORM1,
    in float4 i_prevInstanceMatrix2 : PREV_TRANSFORM2,
#endif
    in uint i_instance : SV_InstanceID,
    out float4 o_position : SV_Position,
    out SceneVertex o_vtx,
#if MOTION_VECTORS
    out float3 o_prevWorldPos : PREV_WORLD_POS,
#endif
    out uint o_instance : INSTANCE
)
{
    float3x4 instanceMatrix = float3x4(i_instanceMatrix0, i_instanceMatrix1, i_instanceMatrix2);

    o_vtx = i_vtx;
    o_vtx.pos = mul(instanceMatrix, float4(i_vtx.pos, 1.0)).xyz;
    o_vtx.normal = mul(instanceMatrix, float4(i_vtx.normal, 0)).xyz;
    o_vtx.tangent.xyz = mul(instanceMatrix, float4(i_vtx.tangent.xyz, 0)).xyz;
    o_vtx.tangent.w = i_vtx.tangent.w;
#if MOTION_VECTORS
    float3x4 prevInstanceMatrix = float3x4(i_prevInstanceMatrix0, i_prevInstanceMatrix1, i_prevInstanceMatrix2);
    o_prevWorldPos = mul(prevInstanceMatrix, float4(i_vtx.prevPos, 1.0)).xyz;
#endif

    float4 worldPos = float4(o_vtx.pos, 1.0);
    float4 viewPos = mul(worldPos, c_GBuffer.view.matWorldToView);
    o_position = mul(viewPos, c_GBuffer.view.matViewToClip);
    o_instance = i_instance;
}
