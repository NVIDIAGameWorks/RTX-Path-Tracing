/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __UNIFORM_SAMPLE_GENERATOR_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __UNIFORM_SAMPLE_GENERATOR_HLSLI__

#include "../Math/BitTricks.hlsli"
#include "Pseudorandom/Xoshiro.hlsli"
#include "Pseudorandom/SplitMix64.hlsli"


/** Default uniform pseudorandom number generator.

    This generator has 128 bit state and should have acceptable statistical
    properties for most rendering applications.

    This sample generator requires shader model 6.0 or above.
*/
struct UniformSampleGenerator // : ISampleGenerator
{
    // struct Padded
    // {
    //     UniformSampleGenerator internal;
    // }

    /** Initializes the sample generator for a given pixel and sample number.
        \param[in] pixel Pixel id.
        \param[in] sampleNumber Sample number.
    */
    void __init(uint2 pixel, uint sampleNumber)
    {
        UniformSampleGenerator sampleGenerator;

        // Use SplitMix64 generator to generate a good pseudorandom initial state.
        // The pixel coord is expected to be max 28 bits (16K^2 is the resource limit in D3D12).
        // The sample number is expected to be practically max ~28 bits, e.g. 16spp x 16M samples.
        // As long as both stay <= 32 bits, we will always have a unique initial seed.
        // This is however no guarantee that the generated sequences will never overlap,
        // but it is very unlikely. For example, with these most extreme parameters of
        // 2^56 sequences of length L, the probability of overlap is P(overlap) = L*2^-16.
        SplitMix64 rng = createSplitMix64(interleave_32bit(pixel), sampleNumber);
        uint64_t s0 = nextRandom64(rng);
        uint64_t s1 = nextRandom64(rng);
        uint seed[4] = { uint(s0), uint(s0 >> 32), uint(s1), uint(s1 >> 32) };

        // Create xoshiro128** pseudorandom generator.
        this.rng = createXoshiro128StarStar(seed);
    }

    static UniformSampleGenerator make(uint2 pixel, uint sampleNumber)    { UniformSampleGenerator ret; ret.__init(pixel, sampleNumber); return ret; }

    /** Returns the next sample value. This function updates the state.
    */
    SETTER_DECL uint next()
    {
        return nextRandom(rng);
    }

    Xoshiro128StarStar rng;
};

#endif // __UNIFORM_SAMPLE_GENERATOR_HLSLI__