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

#include <donut/shaders/scene_material.hlsli>
#include <donut/shaders/material_bindings.hlsli>
#include <donut/shaders/motion_vectors.hlsli>
#include <donut/shaders/forward_vertex.hlsli>
#include <donut/shaders/gbuffer_cb.h>
#include <donut/shaders/vulkan.hlsli>

cbuffer c_GBuffer : register(b1 VK_DESCRIPTOR_SET(1))
{
    GBufferFillConstants c_GBuffer;
};

void main(
    in float4 i_position : SV_Position,
	in SceneVertex i_vtx,
#if MOTION_VECTORS
    in float3 i_prevWorldPos : PREV_WORLD_POS,
#endif
    in bool i_isFrontFace : SV_IsFrontFace,
    out float4 o_channel0 : SV_Target0,
    out float4 o_channel1 : SV_Target1,
    out float4 o_channel2 : SV_Target2,
    out float4 o_channel3 : SV_Target3
#if MOTION_VECTORS
    , out float3 o_motion : SV_Target4
#endif
)
{
    MaterialTextureSample textures = SampleMaterialTexturesAuto(i_vtx.texCoord);

    MaterialSample surface = EvaluateSceneMaterial(i_vtx.normal, i_vtx.tangent, g_Material, textures);

#if ALPHA_TESTED
    if (g_Material.domain != MaterialDomain_Opaque)
        clip(surface.opacity - g_Material.alphaCutoff);
#endif

    if (!i_isFrontFace)
        surface.shadingNormal = -surface.shadingNormal;

    o_channel0.xyz = surface.diffuseAlbedo;
    o_channel0.w = surface.opacity;
    o_channel1.xyz = surface.specularF0;
    o_channel1.w = surface.occlusion;
    o_channel2.xyz = surface.shadingNormal;
    o_channel2.w = surface.roughness;
    o_channel3.xyz = surface.emissiveColor;
    o_channel3.w = 0;

#if MOTION_VECTORS
    o_motion = GetMotionVector(i_position.xyz, i_prevWorldPos, c_GBuffer.view, c_GBuffer.viewPrev);
#endif
}
