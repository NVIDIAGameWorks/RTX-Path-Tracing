/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __TINY_UNIFORM_SAMPLE_GENERATOR_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __TINY_UNIFORM_SAMPLE_GENERATOR_HLSLI__

#include "Pseudorandom/LCG.hlsli"

#include "../Math/HashUtils.hlsli"
#include "../Math/BitTricks.hlsli"

/** Tiny uniform random sample generator.

    This generator has only 32 bit state and sub-optimal statistical properties.
    Do not use for anything critical; correlation artifacts may be prevalent.
*/
struct TinyUniformSampleGenerator // : ISampleGenerator
{
    // struct Padded
    // {
    //     TinyUniformSampleGenerator internal;
    //     uint3 _pad;
    // };

    /** Initializes the sample generator.
        \param[in] seed Seed value.
    */
    void __init(uint seed)
    {
        rng = createLCG(seed);
    }

    static TinyUniformSampleGenerator make(uint seed) { TinyUniformSampleGenerator ret; ret.__init(seed); return ret; }

    /** Initializes the sample generator for a given pixel and sample number.
        \param[in] pixel Pixel id.
        \param[in] sampleNumber Sample number.
    */
    void __init(uint2 pixel, uint sampleNumber)
    {
        // Use block cipher to generate a pseudorandom initial seed.
        uint seed = blockCipherTEA(interleave_32bit(pixel), sampleNumber).x;
        rng = createLCG(seed);
    }

    static TinyUniformSampleGenerator make(uint2 pixel, uint sampleNumber)    { TinyUniformSampleGenerator ret; ret.__init(pixel, sampleNumber); return ret; }


    /** Returns the next sample value. This function updates the state.
    */
    SETTER_DECL uint next()
    {
        return nextRandom(rng);
    }

    LCG rng;    ///< Simple LCG 32-bit pseudorandom number generator.
};

#endif // __TINY_UNIFORM_SAMPLE_GENERATOR_HLSLI__