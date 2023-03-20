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

#include <donut/shaders/scene_material.hlsli>
#include <donut/shaders/material_bindings.hlsli>
#include <donut/shaders/forward_vertex.hlsli>
#include <donut/shaders/vulkan.hlsli>

struct Constants {
    uint instanceOffset;
};

#ifdef SPIRV

VK_PUSH_CONSTANT ConstantBuffer<Constants> g_Const : register(b2);

#else

cbuffer c_Instance : register(b2)
{
    Constants g_Const;
};

#endif

void main(
    in float4 i_position : SV_Position,
    in SceneVertex i_vtx,
    in uint i_instance : INSTANCE,
    out uint4 o_output : SV_Target0
)
{
#if ALPHA_TESTED
    MaterialTextureSample textures = SampleMaterialTexturesAuto(i_vtx.texCoord);

    MaterialSample surface = EvaluateSceneMaterial(i_vtx.normal, i_vtx.tangent, g_Material, textures);
    
    clip(surface.opacity - g_Material.alphaCutoff);
#endif

    o_output.x = uint(g_Material.materialID);
    o_output.y = g_Const.instanceOffset + i_instance;
    o_output.zw = 0;
}
