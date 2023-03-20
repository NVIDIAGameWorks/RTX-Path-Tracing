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

#include <donut/shaders/material_cb.h>
#include <donut/shaders/scene_material.hlsli>
#include <donut/shaders/vulkan.hlsli>

Texture2D t_BaseOrDiffuse : register(t0 VK_DESCRIPTOR_SET(1));
SamplerState s_MaterialSampler : register(s0 VK_DESCRIPTOR_SET(1));

cbuffer c_Material : register(b1 VK_DESCRIPTOR_SET(1))
{
    MaterialConstants g_Material;
};

void main(
    in float4 i_position : SV_Position,
	in float2 i_texCoord : TEXCOORD
)
{
    MaterialTextureSample textures = DefaultMaterialTextures();
    textures.baseOrDiffuse = t_BaseOrDiffuse.Sample(s_MaterialSampler, i_texCoord);

    MaterialSample materialSample = EvaluateSceneMaterial(/* normal = */ float3(1, 0, 0),
        /* tangent = */ float4(0, 1, 0, 0), g_Material, textures);

    clip(materialSample.opacity - g_Material.alphaCutoff);
}
