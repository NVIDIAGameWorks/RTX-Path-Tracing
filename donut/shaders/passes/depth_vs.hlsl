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
#include <donut/shaders/depth_cb.h>

cbuffer c_Depth : register(b0)
{
    DepthPassConstants g_Depth;
};

void main(
	in float3 i_pos : POSITION,
    in float2 i_texCoord : TEXCOORD,
    in float3x4 i_instanceMatrix : TRANSFORM,
	in uint i_instance : SV_InstanceID,
    out float4 o_position : SV_Position,
    out float2 o_texCoord : TEXCOORD)
{
    float3x4 instanceMatrix = i_instanceMatrix;

	float4 worldPos = float4(mul(instanceMatrix, float4(i_pos, 1.0)), 1.0);
	o_position = mul(worldPos, g_Depth.matWorldToClip);

    o_texCoord = i_texCoord;
}
