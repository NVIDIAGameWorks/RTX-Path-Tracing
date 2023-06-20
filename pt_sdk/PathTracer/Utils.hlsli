/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __UTILS_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __UTILS_HLSLI__

#if !defined(__cplusplus)
#include "Utils/Math/MathConstants.hlsli"

uint4   PackTwoFp32ToFp16(float4 a, float4 b)                             { return (f32tof16(clamp(a, -HLF_MAX, HLF_MAX)) << 16) | f32tof16(clamp(b, -HLF_MAX, HLF_MAX)); }
void    UnpackTwoFp32ToFp16(uint4 packed, out float4 a, out float4 b)     { a = f16tof32(packed >> 16); b = f16tof32(packed & 0xFFFF); }
uint3   PackTwoFp32ToFp16(float3 a, float3 b)                             { return (f32tof16(clamp(a, -HLF_MAX, HLF_MAX)) << 16) | f32tof16(clamp(b, -HLF_MAX, HLF_MAX)); }
void    UnpackTwoFp32ToFp16(uint3 packed, out float3 a, out float3 b)     { a = f16tof32(packed >> 16); b = f16tof32(packed & 0xFFFF); }

// Clamp .rgb by luminance
float3 LuminanceClamp(float3 signalIn, const float luminanceThreshold)
{
    float lumSig = luminance( signalIn );
    if( lumSig > luminanceThreshold )
        signalIn = signalIn / lumSig * luminanceThreshold;
    return signalIn;
}

// used for debugging, from https://www.shadertoy.com/view/llKGWG - Heat map, Created by joshliebe in 2016-Oct-15
float3 GradientHeatMap( float greyValue )
{
    greyValue = saturate(greyValue);
    float3 heat; heat.r = smoothstep(0.5, 0.8, greyValue);
    if(greyValue >= 0.90)
    	heat.r *= (1.1 - greyValue) * 5.0;
	if(greyValue > 0.7)
		heat.g = smoothstep(1.0, 0.7, greyValue);
	else
		heat.g = smoothstep(0.0, 0.7, greyValue);
	heat.b = smoothstep(1.0, 0.0, greyValue);          
    if(greyValue <= 0.3)
    	heat.b *= greyValue / 0.3;     
    return heat;
}

// Octahedral normal encoding/decoding - see https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
float2 OctWrap(float2 v)
{
	return (1.0 - abs(v.yx)) * (v.xy >= 0.0 ? 1.0 : -1.0);
}
float2 Encode_Oct(float3 n)
{
	n /= (abs(n.x) + abs(n.y) + abs(n.z));
	n.xy = n.z >= 0.0 ? n.xy : OctWrap(n.xy);
	n.xy = n.xy * 0.5 + 0.5;
	return n.xy;
}
float3 Decode_Oct(float2 f)
{
	f = f * 2.0 - 1.0;

	// https://twitter.com/Stubbesaurus/status/937994790553227264
	float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
	float t = saturate(-n.z);
	n.xy += n.xy >= 0.0 ? -t : t;
	return normalize(n);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// (R11G11B10 conversion code below taken from Miniengine's PixelPacking_R11G11B10.hlsli,  
// Copyright (c) Microsoft, MIT license, Developed by Minigraph, Author:  James Stanard; original file link:
// https://github.com/Microsoft/DirectX-Graphics-Samples/blob/master/MiniEngine/Core/Shaders/PixelPacking_R11G11B10.hlsli )
//
// The standard 32-bit HDR color format.  Each float has a 5-bit exponent and no sign bit.
uint Pack_R11G11B10_FLOAT( float3 rgb )
{
    // Clamp upper bound so that it doesn't accidentally round up to INF 
    // Exponent=15, Mantissa=1.11111
    rgb = min(rgb, asfloat(0x477C0000));  
    uint r = ((f32tof16(rgb.x) + 8) >> 4) & 0x000007FF;
    uint g = ((f32tof16(rgb.y) + 8) << 7) & 0x003FF800;
    uint b = ((f32tof16(rgb.z) + 16) << 17) & 0xFFC00000;
    return r | g | b;
}

float3 Unpack_R11G11B10_FLOAT( uint rgb )
{
    float r = f16tof32((rgb << 4 ) & 0x7FF0);
    float g = f16tof32((rgb >> 7 ) & 0x7FF0);
    float b = f16tof32((rgb >> 17) & 0x7FE0);
    return float3(r, g, b);
}


// ---- 8< ---- GLSL Number Printing - @P_Malin ---- 8< ----
// Smaller Number Printing - @P_Malin
// Creative Commons CC0 1.0 Universal (CC-0)

// Feel free to modify, distribute or use in commercial code, just don't hold me liable for anything bad that happens!
// If you use this code and want to give credit, that would be nice but you don't have to.

// I first made this number printing code in https://www.shadertoy.com/view/4sf3RN
// It started as a silly way of representing digits with rectangles.
// As people started actually using this in a number of places I thought I would try to condense the 
// useful function a little so that it can be dropped into other shaders more easily,
// just snip between the perforations below.
// Also, the licence on the previous shader was a bit restrictive for utility code.
//
// Disclaimer: The values printed may not be accurate!
// Accuracy improvement for fractional values taken from TimoKinnunen https://www.shadertoy.com/view/lt3GRj

// ---- 8< ---- GLSL Number Printing - @P_Malin ---- 8< ----
// Creative Commons CC0 1.0 Universal (CC-0) 
// https://www.shadertoy.com/view/4sBSWW
float _DigitBin( const int x )
{
    return x==0?480599.0:x==1?139810.0:x==2?476951.0:x==3?476999.0:x==4?350020.0:x==5?464711.0:x==6?464727.0:x==7?476228.0:x==8?481111.0:x==9?481095.0:0.0;
}
float glsl_mod(float x, float y)
{
    return x - y * floor(x/y);
}
float ShaderDrawFloat( float2 vStringCoords, float fValue, float fMaxDigits, float fDecimalPlaces )
{       
    if ((vStringCoords.y < 0.0) || (vStringCoords.y >= 1.0)) return 0.0;
    
    bool bNeg = ( fValue < 0.0 );
	fValue = abs(fValue);
    
	float fLog10Value = log2(abs(fValue)) / log2(10.0);
	float fBiggestIndex = max(floor(fLog10Value), 0.0);
	float fDigitIndex = fMaxDigits - floor(vStringCoords.x);
	float fCharBin = 0.0;
	if(fDigitIndex > (-fDecimalPlaces - 1.01)) {
		if(fDigitIndex > fBiggestIndex) {
			if((bNeg) && (fDigitIndex < (fBiggestIndex+1.5))) fCharBin = 1792.0;
		} else {		
			if(fDigitIndex == -1.0) {
				if(fDecimalPlaces > 0.0) fCharBin = 2.0;
			} else {
                float fReducedRangeValue = fValue;
                if(fDigitIndex < 0.0) { fReducedRangeValue = frac( fValue ); fDigitIndex += 1.0; }
				float fDigitValue = (abs(fReducedRangeValue / (pow(10.0, fDigitIndex))));
                fCharBin = _DigitBin(int(floor(glsl_mod(fDigitValue, 10.0))));
			}
        }
	}
    return floor(glsl_mod((fCharBin / pow(2.0, floor(frac(vStringCoords.x) * 4.0) + (floor(vStringCoords.y * 5.0) * 4.0))), 2.0));
}
float ShaderDrawFloat(const in float2 pixelCoord, const in float2 textCoord, const in float2 vFontSize, const in float fValue, const in float fMaxDigits, const in float fDecimalPlaces)
{
    float2 vStringCharCoords = (pixelCoord.xy - textCoord) / vFontSize;
    vStringCharCoords.y = 1-vStringCharCoords.y;
    
    return ShaderDrawFloat( vStringCharCoords, fValue, fMaxDigits, fDecimalPlaces );
}
// ---- 8< -------- 8< -------- 8< -------- 8< ----

float ShaderDrawCrosshair(const in float2 pixelCoord, const in float2 crossCoord, const in float size, const in float thickness)
{
    float2 incoords = abs( pixelCoord.xy - crossCoord );
    return all(incoords<size) && any(incoords<thickness) && !all(incoords<thickness);
}

#endif // !defined(__cplusplus)

// Morton coding/decoding based on https://fgiesen.wordpress.com/2022/09/09/morton-codes-addendum/ 
inline uint Morton16BitEncode(uint x, uint y) // x and y are expected to be max 8 bit
{
  uint temp = (x & 0xff) | ((y & 0xff)<<16);
  temp = (temp ^ (temp <<  4)) & 0x0f0f0f0f;
  temp = (temp ^ (temp <<  2)) & 0x33333333;
  temp = (temp ^ (temp <<  1)) & 0x55555555;
  return ((temp >> 15) | temp) & 0xffff;
}
inline uint2 Morton16BitDecode(uint morton) // morton is expected to be max 16 bit
{
  uint temp=(morton&0x5555)|((morton&0xaaaa)<<15);
  temp=(temp ^ (temp>>1))&0x33333333;
  temp=(temp ^ (temp>>2))&0x0f0f0f0f;
  temp^=temp>>4;
  return uint2( 0xff & temp, 0xff & (temp >> 16) );
}

// Generic Tiled Swizzled Addressing
#if 0 // use linear storage (change requires cpp and shader recompile)
inline uint GenericTSComputeLineStride(const uint imageWidth, const uint imageHeight)
{
    return imageWidth;
}
inline uint GenericTSComputePlaneStride(const uint imageWidth, const uint imageHeight)
{
    return imageWidth * imageHeight;
}
inline uint GenericTSComputeStorageElementCount(const uint imageWidth, const uint imageHeight, const uint imageDepth)
{ 
    return imageDepth * GenericTSComputePlaneStride(imageWidth, imageHeight);
}
inline uint GenericTSPixelToAddress(const uint2 pixelPos, const uint planeIndex, const uint lineStride, const uint planeStride)
{
    uint yInPlane = pixelPos.y;
    return yInPlane * lineStride + pixelPos.x + planeIndex * planeStride;
}
inline uint3 GenericTSAddressToPixel(const uint address, const uint lineStride, const uint planeStride)
{
    uint planeIndex = address / planeStride;
    uint localAddress = address % planeStride;
    uint2 pixelPos;
    pixelPos.x = localAddress % lineStride;
    pixelPos.y = localAddress / lineStride;
    return uint3(pixelPos, planeIndex);
}
#else // use tiled swizzled storage - this is not yet completely optimized but noticeably faster than scanline addressing
#define TS_USE_MORTON 1
#define TS_TILE_SIZE 8 // seems to be the sweet spot
#define TS_TILE_MASK (TS_TILE_SIZE*TS_TILE_SIZE-1)
inline uint GenericTSComputeLineStride(const uint imageWidth, const uint imageHeight)
{
    uint tileCountX = (imageWidth + TS_TILE_SIZE - 1) / TS_TILE_SIZE;
    return tileCountX * TS_TILE_SIZE;
}
inline uint GenericTSComputePlaneStride(const uint imageWidth, const uint imageHeight)
{
    uint tileCountY = (imageHeight + TS_TILE_SIZE - 1) / TS_TILE_SIZE;
    return GenericTSComputeLineStride(imageWidth, imageHeight) * tileCountY * TS_TILE_SIZE;
}
inline uint GenericTSComputeStorageElementCount(const uint imageWidth, const uint imageHeight, const uint imageDepth)
{ 
    return imageDepth * GenericTSComputePlaneStride(imageWidth, imageHeight);
}
inline uint GenericTSPixelToAddress(const uint2 pixelPos, const uint planeIndex, const uint lineStride, const uint planeStride) // <- pass ptConstants or StablePlane constants in...
{
    // coords within tile
    uint xInTile = pixelPos.x % TS_TILE_SIZE;
    uint yInTile = pixelPos.y % TS_TILE_SIZE;

#if TS_USE_MORTON
    uint tilePixelIndex = Morton16BitEncode(xInTile, yInTile);
#else // else simple scanline
    uint tilePixelIndex = xInTile + TS_TILE_SIZE * yInTile;
#endif
    uint tileBaseX = pixelPos.x - xInTile;
    uint tileBaseY = pixelPos.y - yInTile;
    return tileBaseX * TS_TILE_SIZE + tileBaseY * lineStride + tilePixelIndex + planeIndex * planeStride;
}
inline uint3 GenericTSAddressToPixel(const uint address, const uint lineStride, const uint planeStride) // <- pass ptConstants or StablePlane constants in...
{
    const uint planeIndex = address / planeStride;
    const uint localAddress = address % planeStride;
#if TS_USE_MORTON
    uint2 pixelPos = Morton16BitDecode(localAddress & TS_TILE_MASK);
#else // else simple scanline
    uint tilePixelIndex = localAddress % (TS_TILE_SIZE*TS_TILE_SIZE);
    uint2 pixelPos = uint2( tilePixelIndex % TS_TILE_SIZE, tilePixelIndex / TS_TILE_SIZE ); // linear
#endif
    uint maskedLocalAddressBase = (localAddress & ~TS_TILE_MASK)/TS_TILE_SIZE;
    pixelPos += uint2( maskedLocalAddressBase % lineStride, (maskedLocalAddressBase / lineStride) * TS_TILE_SIZE );
    return uint3(pixelPos.x, pixelPos.y, planeIndex);
}
#undef TS_USE_MORTON
#undef TS_TILE_SIZE
#undef TS_TILE_MASK
#endif

#if defined(__cplusplus) && defined(_DEBUG)
// useful test for custom addressing with random texture sizes
inline bool RunTSAddressingTest()
{
    bool res = true;
    for (int i = 0; i < 1000; i++)
    {
        uint2 randsiz = { (uint)(std::rand() % 2000) + 1, (uint)(std::rand() % 2000) + 1 };
        uint3 randp = { (uint)std::rand() % randsiz.x, (uint)std::rand() % randsiz.y, (uint)std::rand() % 3 };
        uint lineStride = GenericTSComputeLineStride(randsiz.x, randsiz.y);
        uint planeStride = GenericTSComputePlaneStride(randsiz.x, randsiz.y);
        uint addr = GenericTSPixelToAddress(randp.xy(), randp.z, lineStride, planeStride);
        uint3 randpt = GenericTSAddressToPixel(addr, lineStride, planeStride);
        assert(randp.x == randpt.x && randp.y == randpt.y && randp.z == randpt.z);
        res &= randp.x == randpt.x && randp.y == randpt.y && randp.z == randpt.z;
    }
    return res;
}
inline static bool g_TSAddressingTest = RunTSAddressingTest();
#endif


#endif // __UTILS_HLSLI__
