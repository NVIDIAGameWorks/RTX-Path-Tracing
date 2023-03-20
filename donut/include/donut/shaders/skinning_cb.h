/*
* Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
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

#ifndef SKINNING_CB_H
#define SKINNING_CB_H

#define SkinningFlag_FirstFrame     0x01
#define SkinningFlag_Normals        0x02
#define SkinningFlag_Tangents       0x04
#define SkinningFlag_TexCoord1      0x08
#define SkinningFlag_TexCoord2      0x10

struct SkinningConstants
{
    uint numVertices;
    uint flags;
    uint inputPositionOffset;
    uint inputNormalOffset;

    uint inputTangentOffset;
    uint inputTexCoord1Offset;
    uint inputTexCoord2Offset;
    uint inputJointIndexOffset;

    uint inputJointWeightOffset;
    uint outputPositionOffset;
    uint outputPrevPositionOffset;
    uint outputNormalOffset;

    uint outputTangentOffset;
    uint outputTexCoord1Offset;
    uint outputTexCoord2Offset;
};

#endif // SKINNING_CB_H