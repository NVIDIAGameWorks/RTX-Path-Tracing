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

#include <donut/shaders/blit_cb.h>

#if TEXTURE_ARRAY
Texture2DArray tex : register(t0);
#else
Texture2D tex : register(t0);
#endif
SamplerState samp : register(s0);

cbuffer c_Blit : register(b0)
{
    BlitConstants g_Blit;
};

void main(
	in float4 i_pos : SV_Position,
	in float2 i_uv : UV,
	out float4 o_rgba : SV_Target)
{
#if TEXTURE_ARRAY
	float4 x = tex.SampleLevel(samp, float3(i_uv, 0), 0);
	
	float4 a = tex.SampleLevel(samp, float3(i_uv, 0), 0, int2(-1,  0));
	float4 b = tex.SampleLevel(samp, float3(i_uv, 0), 0, int2( 1,  0));
	float4 c = tex.SampleLevel(samp, float3(i_uv, 0), 0, int2( 0,  1));
	float4 d = tex.SampleLevel(samp, float3(i_uv, 0), 0, int2( 0, -1));

	float4 e = tex.SampleLevel(samp, float3(i_uv, 0), 0, int2(-1, -1));
	float4 f = tex.SampleLevel(samp, float3(i_uv, 0), 0, int2( 1,  1));
	float4 g = tex.SampleLevel(samp, float3(i_uv, 0), 0, int2(-1,  1));
	float4 h = tex.SampleLevel(samp, float3(i_uv, 0), 0, int2( 1, -1));
#else
	float4 x = tex.SampleLevel(samp, i_uv, 0);
	
	float4 a = tex.SampleLevel(samp, i_uv, 0, int2(-1,  0));
	float4 b = tex.SampleLevel(samp, i_uv, 0, int2( 1,  0));
	float4 c = tex.SampleLevel(samp, i_uv, 0, int2( 0,  1));
	float4 d = tex.SampleLevel(samp, i_uv, 0, int2( 0, -1));

	float4 e = tex.SampleLevel(samp, i_uv, 0, int2(-1, -1));
	float4 f = tex.SampleLevel(samp, i_uv, 0, int2( 1,  1));
	float4 g = tex.SampleLevel(samp, i_uv, 0, int2(-1,  1));
	float4 h = tex.SampleLevel(samp, i_uv, 0, int2( 1, -1));
#endif
	
	o_rgba = x * (6.828427 * g_Blit.sharpenFactor + 1) 
		- (a + b + c + d) * g_Blit.sharpenFactor 
		- (e + g + f + h) * g_Blit.sharpenFactor * 0.7071;
}
