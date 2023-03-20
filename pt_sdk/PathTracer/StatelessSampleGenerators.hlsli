/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __INLINE_SAMPLE_GENERATORS_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __INLINE_SAMPLE_GENERATORS_HLSLI__

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

/** Inline uniform random sample generator.

    This generator has only 32 bit state and sub-optimal statistical properties, however it's 'mostly fine' for up to millions of samples.
    TODO: try using LCG
    TODO: read & try out: https://pharr.org/matt/blog/2022/03/05/sampling-fp-unit-interval

*/
struct StatelessUniformSampleGenerator
{
    //uint sampleIndex;
    //uint vertexSeed;
    //uint dimension;
    uint effectHash;

    static StatelessUniformSampleGenerator make(uint2 pixelCoord, uint vertexIndex, uint sampleIndex)
    {
        StatelessUniformSampleGenerator ret;
        //ret.sampleIndex = sampleIndex;
        uint vertexSeed = Hash32Combine( Hash32(vertexIndex+0x035F9F29), (pixelCoord.x << 16) | pixelCoord.y );
        vertexSeed = Hash32Combine( vertexSeed, sampleIndex );
        ret.effectHash = vertexSeed;
        //ret.vertexSeed = vertexSeed;
        //ret.startEffect(0);

        return ret;
    }

    void startEffect(uint effectSeed)
    {
        //effectHash = Hash32Combine(vertexSeed, effectSeed);
        //dimension = 0;
    }

    /** Returns the next sample value. This function updates the state.
    */
    uint next()
    {
        uint ret = effectHash;
        effectHash = Hash32(ret);
        //dimension++;
        return ret;
    }
};

#endif // __INLINE_SAMPLE_GENERATORS_HLSLI__