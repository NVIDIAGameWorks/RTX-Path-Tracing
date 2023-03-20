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

#ifndef MIPMAP_GEN_CB_H
#define MIPMAP_GEN_CB_H

#define GROUP_SIZE 16
#define LOD0_TILE_SIZE 8
#define NUM_LODS 4

// Number of compute dispatches needed to reduce all the 
// mip-levels at a maximum resolution of 16k : 
//     (uint)(std::ceil(std::log2f(16384)/NUM_LODS)) = 4
#define MAX_PASSES 4

#define MODE_COLOR  0
#define MODE_MIN    1
#define MODE_MAX    2
#define MODE_MINMAX 3

struct MipmmapGenConstants
{
    uint dispatch;
    uint numLODs;
    uint padding[2];
};

#endif // MIPMAP_GEN_CB_H