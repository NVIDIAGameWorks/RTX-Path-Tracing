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

#include "view_cb.h"

float3 GetMotionVector(float3 svPosition, float3 prevWorldPos, PlanarViewConstants view, PlanarViewConstants viewPrev)
{
    float4 clipPos = mul(float4(prevWorldPos, 1), viewPrev.matWorldToClip);
    if (clipPos.w <= 0)
        return 0;

    clipPos.xyz /= clipPos.w;
    float2 prevWindowPos = clipPos.xy * view.clipToWindowScale + view.clipToWindowBias;

    float3 motion;
    motion.xy = prevWindowPos - svPosition.xy + (view.pixelOffset - viewPrev.pixelOffset);
    motion.z = clipPos.z - svPosition.z;
    return motion;
}
