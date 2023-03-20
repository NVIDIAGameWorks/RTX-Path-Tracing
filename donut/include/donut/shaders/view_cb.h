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

#ifndef VIEW_CB_H
#define VIEW_CB_H

struct PlanarViewConstants
{
    float4x4    matWorldToView;
    float4x4    matViewToClip;
    float4x4    matWorldToClip;
    float4x4    matClipToView;
    float4x4    matViewToWorld;
    float4x4    matClipToWorld;

    float4x4    matViewToClipNoOffset;
    float4x4    matWorldToClipNoOffset;
    float4x4    matClipToViewNoOffset;
    float4x4    matClipToWorldNoOffset;

    float2      viewportOrigin;
    float2      viewportSize;

    float2      viewportSizeInv;
    float2      pixelOffset;

    float2      clipToWindowScale;
    float2      clipToWindowBias;

    float2      windowToClipScale;
    float2      windowToClipBias;

    float4      cameraDirectionOrPosition;
};

#endif // VIEW_CB_H