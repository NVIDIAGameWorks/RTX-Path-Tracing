/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __NOISE_AND_SEQUENCES_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __NOISE_AND_SEQUENCES_HLSLI__

// must be power of 2! when using precomputed sobol, integration quality slowly deteriorates when sampling more than precomputed number, such that at 64 times the precomputed size it can drop below using pure pseudorandom
#define SOBOL_MAX_DIMENSIONS                                        5
#define SOBOL_PRECOMPUTED_INDEX_COUNT                               65536
// don't forget to initialize storage, i.e.
// StructuredBuffer<uint> g_precomputedSobol : register( t42 );
// #define SOBOL_PRECOMPUTED_BUFFER g_precomputedSobol

#if !defined(__cplusplus)

// *************************************************************************************************************************************
// Some of the noise and LD sampling functions in this file originate from:
// https://github.com/GameTechDev/XeGTAO/blob/master/Source/Rendering/Shaders/vaNoise.hlsl, 
// Original license provided below:
// *************************************************************************************************************************************
// MIT License
// 
// Copyright (C) 2016-2021, Intel Corporation 
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// *************************************************************************************************************************************
// Quasi-random sequence based on "Practical Hash-based Owen Scrambling", Brent Burley 2020, https://jcgt.org/published/0009/04/01/paper.pdf
// with shader implementation borrowing from Andrew Helmer's Shadertoy implementation (https://www.reddit.com/r/GraphicsProgramming/comments/l1go2r/owenscrambled_sobol_02_sequences_shadertoy/)
// *************************************************************************************************************************************

// Note: this will turn 0 into 0! if that's a problem do Hash32( x+constant ) - HashCombine does something similar already
uint Hash32( uint x )
{
// This little gem is from https://nullprogram.com/blog/2018/07/31/, "Prospecting for Hash Functions" by Chris Wellons
// There's also the inverse for the lowbias32, and a 64bit version.
#if 1   // faster, higher bias
    // exact bias: 0.17353355999581582
    // uint32_t lowbias32(uint32_t x)
    // {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    //}
#else // slower, lower bias
// exact bias: 0.020888578919738908
// uint32_t triple32(uint32_t x)
// {
    x ^= x >> 17;
    x *= uint(0xed5ad4bb);
    x ^= x >> 11;
    x *= uint(0xac4c1b51);
    x ^= x >> 15;
    x *= uint(0x31848bab);
    x ^= x >> 14;
    return x;
//}
#endif
}

// popular hash_combine (boost, etc.)
uint Hash32Combine( const uint seed, const uint value )
{
    return seed ^ (Hash32(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2));       
}

// same as Hash32Combine, just without re-hashing of input value
uint Hash32CombineSimple( const uint seed, const uint value )
{
    return seed ^ (value + (seed << 6) + (seed >> 2));
}

// converting the whole 32bit uint to [0, 1)
float Hash32ToFloat(uint hash)
{ 
    // there are good reasons to do it this way:
    // a.) converting the upper 24bits to [0, 1) because the higher bits have better distribution in some hash algorithms (like sobol)
    // b.) this is the only way to guarantee [0, 1) since float32 mantissa is only 23 bits (and I might be off by a bit here, maybe we need "(hash>>9) / 8388608.0" but I brute-force-tested it and below seems to work
    return (hash>>8) / 16777216.0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Quasi-random sequence - R*
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
// https://www.shadertoy.com/view/4lVcRm, https://www.shadertoy.com/view/MtVBRw, 
// https://www.shadertoy.com/view/3ltSzS
float R1seq(int index, float offset)
{
    return frac(offset + float(index) * 0.618033988749894848204586834365641218413556121186522017520);
}
float2 R2seq(int index, float offset)
{
    return frac(offset.xx + float2(index.xx) * float2(0.754877666246692760049508896358532874940835564978799543103, 0.569840290998053265911399958119574964216147658520394151385));
}
float3 R3seq(int index, float offset)
{
    const float g = 1.22074408460575947536;
    float a1 = 1.0f / g, a2 = 1.0f / (g * g), a3 = 1.0f / (g * g * g);
    return float3(float(fmod((offset + a1 * float(index)), 1.0f)), float(fmod((offset + a2 * float(index)), 1.0)), float(fmod((offset + a3 * float(index)), 1.0)));
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Quasi-random sequence - Hash-based Owen Scrambled Sobol'
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Based on "Practical Hash-based Owen Scrambling", Brent Burley, Walt Disney Animation Studios
// With simplifications/optimizations taken out from https://www.shadertoy.com/view/wlyyDm# (relevant reddit thread:
// https://www.reddit.com/r/GraphicsProgramming/comments/l1go2r/owenscrambled_sobol_02_sequences_shadertoy/)
// This simplification uses Laine-Kerras permutation for the 1st dimension and Sobol' only for second and subsequent
// dimensions (good performance + multi-dimensional stratification).
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
uint bhos_sobol(uint index, uniform uint dimension)
{
#if 1 && defined(SOBOL_PRECOMPUTED_BUFFER)
    return SOBOL_PRECOMPUTED_BUFFER[ (index % SOBOL_PRECOMPUTED_INDEX_COUNT) + SOBOL_PRECOMPUTED_INDEX_COUNT * dimension ];
#else
    const uint directions[SOBOL_MAX_DIMENSIONS][32] = {
        0x80000000, 0x40000000, 0x20000000, 0x10000000,
        0x08000000, 0x04000000, 0x02000000, 0x01000000,
        0x00800000, 0x00400000, 0x00200000, 0x00100000,
        0x00080000, 0x00040000, 0x00020000, 0x00010000,
        0x00008000, 0x00004000, 0x00002000, 0x00001000,
        0x00000800, 0x00000400, 0x00000200, 0x00000100,
        0x00000080, 0x00000040, 0x00000020, 0x00000010,
        0x00000008, 0x00000004, 0x00000002, 0x00000001,

        0x80000000, 0xc0000000, 0xa0000000, 0xf0000000,
        0x88000000, 0xcc000000, 0xaa000000, 0xff000000,
        0x80800000, 0xc0c00000, 0xa0a00000, 0xf0f00000,
        0x88880000, 0xcccc0000, 0xaaaa0000, 0xffff0000,
        0x80008000, 0xc000c000, 0xa000a000, 0xf000f000,
        0x88008800, 0xcc00cc00, 0xaa00aa00, 0xff00ff00,
        0x80808080, 0xc0c0c0c0, 0xa0a0a0a0, 0xf0f0f0f0,
        0x88888888, 0xcccccccc, 0xaaaaaaaa, 0xffffffff,

        0x80000000, 0xc0000000, 0x60000000, 0x90000000,
        0xe8000000, 0x5c000000, 0x8e000000, 0xc5000000,
        0x68800000, 0x9cc00000, 0xee600000, 0x55900000,
        0x80680000, 0xc09c0000, 0x60ee0000, 0x90550000,
        0xe8808000, 0x5cc0c000, 0x8e606000, 0xc5909000,
        0x6868e800, 0x9c9c5c00, 0xeeee8e00, 0x5555c500,
        0x8000e880, 0xc0005cc0, 0x60008e60, 0x9000c590,
        0xe8006868, 0x5c009c9c, 0x8e00eeee, 0xc5005555,

        0x80000000, 0xc0000000, 0x20000000, 0x50000000,
        0xf8000000, 0x74000000, 0xa2000000, 0x93000000,
        0xd8800000, 0x25400000, 0x59e00000, 0xe6d00000,
        0x78080000, 0xb40c0000, 0x82020000, 0xc3050000,
        0x208f8000, 0x51474000, 0xfbea2000, 0x75d93000,
        0xa0858800, 0x914e5400, 0xdbe79e00, 0x25db6d00,
        0x58800080, 0xe54000c0, 0x79e00020, 0xb6d00050,
        0x800800f8, 0xc00c0074, 0x200200a2, 0x50050093,

        0x80000000, 0x40000000, 0x20000000, 0xb0000000,
        0xf8000000, 0xdc000000, 0x7a000000, 0x9d000000,
        0x5a800000, 0x2fc00000, 0xa1600000, 0xf0b00000,
        0xda880000, 0x6fc40000, 0x81620000, 0x40bb0000,
        0x22878000, 0xb3c9c000, 0xfb65a000, 0xddb2d000,
        0x78022800, 0x9c0b3c00, 0x5a0fb600, 0x2d0ddb00,
        0xa2878080, 0xf3c9c040, 0xdb65a020, 0x6db2d0b0,
        0x800228f8, 0x400b3cdc, 0x200fb67a, 0xb00ddb9d,
    };

    uint X = 0u;
    [unroll]
    for (uint bit = 0; bit < 32; bit++) {
        uint mask = (index >> bit) & 1u;
        X ^= mask * directions[dimension][bit];
    }
    return X;
#endif
}
//
uint bhos_reverse_bits(uint x) 
{
#if 1
    return reversebits(x);  // hey we've got this in HLSL! awesome.
#else
    x = (((x & 0xaaaaaaaau) >> 1) | ((x & 0x55555555u) << 1));
    x = (((x & 0xccccccccu) >> 2) | ((x & 0x33333333u) << 2));
    x = (((x & 0xf0f0f0f0u) >> 4) | ((x & 0x0f0f0f0fu) << 4));
    x = (((x & 0xff00ff00u) >> 8) | ((x & 0x00ff00ffu) << 8));
    return ((x >> 16) | (x << 16));
#endif
}
//
uint bhos_owen_hash(uint x, uint seed) 
{
#if 0 // this is the original laine_karras_permutation from Burley2020
    x += seed;
    x ^= x * 0x6c50b47cu;
    x ^= x * 0xb82f1e52u;
    x ^= x * 0xc7afe638u;
    x ^= x * 0x8d22f6e6u;
#else // this is from https://psychopath.io/post/2021_01_30_building_a_better_lk_hash (updated)
    x ^= x * 0x3d20adea;
    x += seed;
    x *= (seed >> 16) | 1;
    x ^= x * 0x05526c56;
    x ^= x * 0x53a22864;
#endif
    return x;
}
//
uint bhos_owen_scramble( uint x, uint seed ) // nested_uniform_scramble_base2
{
    x = bhos_reverse_bits(x);
    x = bhos_owen_hash(x, seed);
    x = bhos_reverse_bits(x);
    return x;
}
//
uint burley_shuffled_scrambled_sobol_1D_uint( uint index, uint seed ) 
{
    uint shuffle_seed = Hash32Combine( seed, 0 );
    uint x_seed = Hash32Combine( seed, 1 );
    uint shuffled_index = bhos_owen_scramble(index, shuffle_seed);
    uint x = bhos_reverse_bits(shuffled_index);
    return bhos_owen_scramble(x, x_seed);
}
///
float burley_shuffled_scrambled_sobol_1D( uint index, uint seed ) 
{
    return Hash32ToFloat( burley_shuffled_scrambled_sobol_1D_uint(index, seed) );
}
//
float2 burley_shuffled_scrambled_sobol_2D( uint index, uint seed ) 
{
    uint shuffle_seed = Hash32Combine( seed, 0 );
    uint x_seed = Hash32Combine( seed, 1 );
    uint y_seed = Hash32Combine( seed, 2 );

    uint shuffled_index = bhos_owen_scramble(index, shuffle_seed);

    uint x = bhos_reverse_bits(shuffled_index);
    uint y = bhos_sobol(shuffled_index, 1);
    x = bhos_owen_scramble(x, x_seed);
    y = bhos_owen_scramble(y, y_seed);

    return float2( Hash32ToFloat(x), Hash32ToFloat(y) );
}
//
float3 burley_shuffled_scrambled_sobol_3D( uint index, uint seed ) 
{
    uint shuffle_seed = Hash32Combine( seed, 0 );
    uint x_seed = Hash32Combine( seed, 1 );
    uint y_seed = Hash32Combine( seed, 2 );
    uint z_seed = Hash32Combine( seed, 3 );

    uint shuffled_index = bhos_owen_scramble(index, shuffle_seed);

    uint x = bhos_reverse_bits(shuffled_index);
    uint y = bhos_sobol(shuffled_index, 1);
    uint z = bhos_sobol(shuffled_index, 2);
    x = bhos_owen_scramble(x, x_seed);
    y = bhos_owen_scramble(y, y_seed);
    z = bhos_owen_scramble(z, z_seed);

    return float3( Hash32ToFloat(x), Hash32ToFloat(y), Hash32ToFloat(z) );
}
//
float4 burley_shuffled_scrambled_sobol_4D( uint index, uint seed ) 
{
    uint shuffle_seed = Hash32Combine( seed, 0 );
    uint x_seed = Hash32Combine( seed, 1 );
    uint y_seed = Hash32Combine( seed, 2 );
    uint z_seed = Hash32Combine( seed, 3 );
    uint w_seed = Hash32Combine( seed, 4 );

    uint shuffled_index = bhos_owen_scramble(index, shuffle_seed);

    uint x = bhos_reverse_bits(shuffled_index);
    uint y = bhos_sobol(shuffled_index, 1);
    uint z = bhos_sobol(shuffled_index, 2);
    uint w = bhos_sobol(shuffled_index, 3);
    x = bhos_owen_scramble(x, x_seed);
    y = bhos_owen_scramble(y, y_seed);
    z = bhos_owen_scramble(z, z_seed);
    w = bhos_owen_scramble(w, w_seed);

    return float4( Hash32ToFloat(x), Hash32ToFloat(y), Hash32ToFloat(z), Hash32ToFloat(w) );
}
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Z-curve / Morton code
// refs : https://www.shadertoy.com/view/4sscDn
//        https://en.wikipedia.org/wiki/Z-order_curve
//        https://fgiesen.wordpress.com/2009/12/13/decoding-morton-codes/
//
static const int c_MORTON_MASKS[] = {0x55555555u, 0x33333333u, 0x0F0F0F0Fu, 0x00FF00FFu, 0x0000FFFFu};
//
int Morton2DPack( uint2 I ) 
{       // --- grid location to curve index
    uint n=8u;
    [unroll] for( int i=3; i>=0; i-- )
    {
        I =  (I | (I << n)) & c_MORTON_MASKS[i];
        n /= 2u;
    }
    return I.x | (I.y << 1);
}
//
uint2 Morton2DUnpack( uint z )
{      // --- curve index to grid location 
    uint n=1;
    uint2 I = uint2(z,z>>1) & c_MORTON_MASKS[0];
    [unroll] for( int i=1; i<=4; i++ )
    {
        I = (I | (I >>  n)) & c_MORTON_MASKS[i];
        n *= 2u;
    }
    return I;
}
//
float2 LD1DTo2D_Morton( float ldSample1D )
{
    // use Morton code to get nicer spatial distribution; expanding to 24bits because more makes no difference and is slower
    // since we're using 24 bits, that's 12 per direction so divide by (1<<12==4096) to get [0, 1)
    uint2 mort = Morton2DUnpack((uint) (ldSample1D * 16777216.0));
    //return (mort+0.5.xx) / 4096.0.xx;     // not sure if this one is more correct
    return (mort) / 4095.0.xx;
}
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#else // #if !defined(__cplusplus)

uint32_t SobolC(uint32_t index, uint32_t dim)
{
    uint32_t directions_g[5][32] = {
        0x80000000, 0x40000000, 0x20000000, 0x10000000,
        0x08000000, 0x04000000, 0x02000000, 0x01000000,
        0x00800000, 0x00400000, 0x00200000, 0x00100000,
        0x00080000, 0x00040000, 0x00020000, 0x00010000,
        0x00008000, 0x00004000, 0x00002000, 0x00001000,
        0x00000800, 0x00000400, 0x00000200, 0x00000100,
        0x00000080, 0x00000040, 0x00000020, 0x00000010,
        0x00000008, 0x00000004, 0x00000002, 0x00000001,

        0x80000000, 0xc0000000, 0xa0000000, 0xf0000000,
        0x88000000, 0xcc000000, 0xaa000000, 0xff000000,
        0x80800000, 0xc0c00000, 0xa0a00000, 0xf0f00000,
        0x88880000, 0xcccc0000, 0xaaaa0000, 0xffff0000,
        0x80008000, 0xc000c000, 0xa000a000, 0xf000f000,
        0x88008800, 0xcc00cc00, 0xaa00aa00, 0xff00ff00,
        0x80808080, 0xc0c0c0c0, 0xa0a0a0a0, 0xf0f0f0f0,
        0x88888888, 0xcccccccc, 0xaaaaaaaa, 0xffffffff,

        0x80000000, 0xc0000000, 0x60000000, 0x90000000,
        0xe8000000, 0x5c000000, 0x8e000000, 0xc5000000,
        0x68800000, 0x9cc00000, 0xee600000, 0x55900000,
        0x80680000, 0xc09c0000, 0x60ee0000, 0x90550000,
        0xe8808000, 0x5cc0c000, 0x8e606000, 0xc5909000,
        0x6868e800, 0x9c9c5c00, 0xeeee8e00, 0x5555c500,
        0x8000e880, 0xc0005cc0, 0x60008e60, 0x9000c590,
        0xe8006868, 0x5c009c9c, 0x8e00eeee, 0xc5005555,

        0x80000000, 0xc0000000, 0x20000000, 0x50000000,
        0xf8000000, 0x74000000, 0xa2000000, 0x93000000,
        0xd8800000, 0x25400000, 0x59e00000, 0xe6d00000,
        0x78080000, 0xb40c0000, 0x82020000, 0xc3050000,
        0x208f8000, 0x51474000, 0xfbea2000, 0x75d93000,
        0xa0858800, 0x914e5400, 0xdbe79e00, 0x25db6d00,
        0x58800080, 0xe54000c0, 0x79e00020, 0xb6d00050,
        0x800800f8, 0xc00c0074, 0x200200a2, 0x50050093,

        0x80000000, 0x40000000, 0x20000000, 0xb0000000,
        0xf8000000, 0xdc000000, 0x7a000000, 0x9d000000,
        0x5a800000, 0x2fc00000, 0xa1600000, 0xf0b00000,
        0xda880000, 0x6fc40000, 0x81620000, 0x40bb0000,
        0x22878000, 0xb3c9c000, 0xfb65a000, 0xddb2d000,
        0x78022800, 0x9c0b3c00, 0x5a0fb600, 0x2d0ddb00,
        0xa2878080, 0xf3c9c040, 0xdb65a020, 0x6db2d0b0,
        0x800228f8, 0x400b3cdc, 0x200fb67a, 0xb00ddb9d,
    };

    if (dim > 4) return 0;
    uint32_t X = 0;
    for (int bit = 0; bit < 32; bit++) {
        int mask = (index >> bit) & 1;
        X ^= mask * directions_g[dim][bit];
    }
    return X;
}

void PrecomputeSobol( uint32_t * outBuffer )
{
    for( uint dim = 0; dim < SOBOL_MAX_DIMENSIONS; dim++ )
        for( uint index = 0; index < SOBOL_PRECOMPUTED_INDEX_COUNT; index++ )
            outBuffer[ dim * SOBOL_PRECOMPUTED_INDEX_COUNT + index ] = SobolC( index, dim );
}

#endif // #if !defined(__cplusplus)

#endif // __NOISE_AND_SEQUENCES_HLSLI__
