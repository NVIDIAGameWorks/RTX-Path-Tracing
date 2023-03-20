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

#include <donut/core/math/math.h>

namespace donut::math
{
	// Projection matrix implementations

	float4x4 orthoProjD3DStyle(float left, float right, float bottom, float top, float zNear, float zFar)
	{
		float xScale = 1.0f / (right - left);
		float yScale = 1.0f / (top - bottom);
		float zScale = 1.0f / (zFar - zNear);
		return float4x4(
					2.0f * xScale, 0, 0, 0,
					0, 2.0f * yScale, 0, 0,
					0, 0, zScale, 0,
					-(left + right) * xScale, -(bottom + top) * yScale, -zNear * zScale, 1);
	}

	float4x4 orthoProjOGLStyle(float left, float right, float bottom, float top, float zNear, float zFar)
	{
		float xScale = 1.0f / (right - left);
		float yScale = 1.0f / (top - bottom);
		float zScale = 1.0f / (zFar - zNear);
		return float4x4(
					2.0f * xScale, 0, 0, 0,
					0, 2.0f * yScale, 0, 0,
					0, 0, -2.0f * zScale, 0,
					-(left + right) * xScale, -(bottom + top) * yScale, -(zNear + zFar) * zScale, 1);
	}

	float4x4 perspProjD3DStyle(float left, float right, float bottom, float top, float zNear, float zFar)
	{
		float xScale = 1.0f / (right - left);
		float yScale = 1.0f / (top - bottom);
		float zScale = 1.0f / (zFar - zNear);
		return float4x4(
					2.0f * xScale, 0, 0, 0,
					0, 2.0f * yScale, 0, 0,
					-(left + right) * xScale, -(bottom + top) * yScale, zFar * zScale, 1,
					0, 0, -zNear * zFar * zScale, 0);
	}

	float4x4 perspProjOGLStyle(float left, float right, float bottom, float top, float zNear, float zFar)
	{
		float xScale = 1.0f / (right - left);
		float yScale = 1.0f / (top - bottom);
		float zScale = 1.0f / (zFar - zNear);
		return float4x4(
					2.0f * zNear * xScale, 0, 0, 0,
					0, 2.0f * zNear * yScale, 0, 0,
					(left + right) * xScale, (bottom + top) * yScale, -(zNear + zFar) * zScale, -1,
					0, 0, -2.0f * zNear * zFar * zScale, 0);
	}

	float4x4 perspProjD3DStyleReverse(float left, float right, float bottom, float top, float zNear)
	{
		float xScale = 1.0f / (right - left);
		float yScale = 1.0f / (top - bottom);

		return float4x4(
					2.0f * xScale, 0, 0, 0,
					0, 2.0f * yScale, 0, 0,
					-(left + right) * xScale, -(bottom + top) * yScale, 0, 1,
					0, 0, zNear, 0);
	}

	float4x4 perspProjD3DStyle(float verticalFOV, float aspect, float zNear, float zFar)
	{
		float yScale = 1.0f / tanf(0.5f * verticalFOV);
		float xScale = yScale / aspect;
		float zScale = 1.0f / (zFar - zNear);
		return float4x4(
					xScale, 0, 0, 0,
					0, yScale, 0, 0,
					0, 0, zFar * zScale, 1,
					0, 0, -zNear * zFar * zScale, 0);
	}

	float4x4 perspProjOGLStyle(float verticalFOV, float aspect, float zNear, float zFar)
	{
		float yScale = 1.0f / tanf(0.5f * verticalFOV);
		float xScale = yScale / aspect;
		float zScale = 1.0f / (zFar - zNear);
		return float4x4(
					xScale, 0, 0, 0,
					0, yScale, 0, 0,
					0, 0, -(zNear + zFar) * zScale, -1,
					0, 0, -2.0f * zNear * zFar * zScale, 0);
	}

	float4x4 perspProjD3DStyleReverse(float verticalFOV, float aspect, float zNear)
	{
		float yScale = 1.0f / tanf(0.5f * verticalFOV);
		float xScale = yScale / aspect;

		return float4x4(
					xScale, 0, 0, 0,
					0, yScale, 0, 0,
					0, 0, 0, 1,
					0, 0, zNear, 0);
	}

}
