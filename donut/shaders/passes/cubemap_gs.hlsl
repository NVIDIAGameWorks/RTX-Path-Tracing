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

#include <donut/shaders/forward_vertex.hlsli>

struct VSOutput
{
    float4 posClip : SV_Position;
    SceneVertex vtx;
};

struct GSOutput
{
    VSOutput Passthrough;
    uint ViewportMask : SV_ViewportArrayIndex;
};

#define USE_CULLING 1

int GetVertexPlaneMask(float3 v)
{ 
    return int(v.x < v.y) | 
        (int(v.x < -v.y) << 1) | 
        (int(v.x <  v.z) << 2) | 
        (int(v.x < -v.z) << 3) | 
        (int(v.z <  v.y) << 4) | 
        (int(v.z < -v.y) << 5) |
        (int(v.x <    1) << 8) |
        (int(v.x >   -1) << 9) |
        (int(v.y <    1) << 10) |
        (int(v.y >   -1) << 11) |
        (int(v.z <    1) << 12) |
        (int(v.z >   -1) << 13);
}

[maxvertexcount(1)]
void main(
    triangle VSOutput Input[3],
    inout TriangleStream<GSOutput> Output)
{
    GSOutput OutputVertex;

    OutputVertex.Passthrough = Input[0];

#if USE_CULLING
    int pm0 = GetVertexPlaneMask(Input[0].posClip.xyz);
    int pm1 = GetVertexPlaneMask(Input[1].posClip.xyz);
    int pm2 = GetVertexPlaneMask(Input[2].posClip.xyz);
    int prim_plane_mask_0 = pm0 & pm1 & pm2;
    int prim_plane_mask_1 = ~pm0 & ~pm1 & ~pm2;
    int combined_mask = prim_plane_mask_0 | (prim_plane_mask_1 << 16);

    int face_mask = 0;
    if((combined_mask & 0x00010f) == 0) face_mask |= 0x01;
    if((combined_mask & 0x0f0200) == 0) face_mask |= 0x02;
    if((combined_mask & 0x110422) == 0) face_mask |= 0x04;
    if((combined_mask & 0x220811) == 0) face_mask |= 0x08;
    if((combined_mask & 0x041038) == 0) face_mask |= 0x10;
    if((combined_mask & 0x382004) == 0) face_mask |= 0x20;

    OutputVertex.ViewportMask = face_mask;
#else
    OutputVertex.ViewportMask = 0x3f;
#endif

    Output.Append(OutputVertex);
}
