/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __STATELESS_HQ_UNIFORM_SAMPLE_GENERATOR_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __STATELESS_HQ_UNIFORM_SAMPLE_GENERATOR_HLSLI__

#include "Utils/Math/BitTricks.hlsli"
#include "Utils/Sampling/Pseudorandom/Xoshiro.hlsli"
#include "Utils/Sampling/Pseudorandom/SplitMix64.hlsli"

/** Inline uniform random sample generator using high quality xoshiro128 RTG (http://xoshiro.di.unimi.it/xoshiro128starstar.c)

    This implementation is intended for collecting high quality reference. It is slower and ignores startEffect interface.
*/
struct StatelessHQUniformSampleGenerator
{
    Xoshiro128StarStar rng;

//public:
    static StatelessHQUniformSampleGenerator make(uint2 pixelCoord, uint vertexIndex, uint sampleIndex)
    {
        StatelessHQUniformSampleGenerator ret;
        
        // This is an inline variant of Falcor's UniformSampleGenerator; due to it being inline and not
        // keeping state along whole path, it means we have to re-seed per vertex (bounce) and not per pixel, 
        // increasing chances of overlapping sequences.
        // See original implementation for more info on seed generation:
        // (https://github.com/NVIDIAGameWorks/Falcor/blob/9fdfdbb37516f4273e952a5e30b85af8ccfe171d/Source/Falcor/Utils/Sampling/UniformSampleGenerator.slang#L57).

        SplitMix64 rng = createSplitMix64(interleave_32bit(pixelCoord), sampleIndex + (vertexIndex<<24)); // 24 bits for sample index guarantees first 16 mil samples without overlapping vertexIndex, and 8 bits for vertex index is plenty
        uint64_t s0 = nextRandom64(rng);
        uint64_t s1 = nextRandom64(rng);
        uint seed[4] = { uint(s0), uint(s0 >> 32), uint(s1), uint(s1 >> 32) };

        // Create xoshiro128** pseudorandom generator.
        ret.rng = createXoshiro128StarStar(seed);        
        
        return ret;
    }

    // Sets an effect-specific sampler state that is decorrelated from other effects.
    // The state is also deterministic for the specific path vertex, regardless of shader code ordering / branching.
    void startEffect(SampleGeneratorEffectSeed effectSeed, bool lowDiscrepancy = false, int subSampleIndex = 0, int subSampleCount = 1)
    {
    }
    
    // Returns the next sample value. This function updates the state.
    uint next()
    {
        return nextRandom(rng);
    }
};

#endif // __STATELESS_HQ_UNIFORM_SAMPLE_GENERATOR_HLSLI__