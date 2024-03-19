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

#if USE_PRECOMPUTED_SOBOL_BUFFER
// there should be a better place for this, but it has to be defined before "NoiseAndSequences.hlsli" is included
Buffer<uint> t_precomputedSobol : register(t42);
#define SOBOL_PRECOMPUTED_BUFFER t_precomputedSobol
#endif

#include "NoiseAndSequences.hlsli"

/** Inline uniform random sample generator.

    This generator has only 32 bit state and sub-optimal statistical properties, however it's 'mostly fine' for up to millions of samples.
    TODO: try using LCG
    TODO: read & try out: https://pharr.org/matt/blog/2022/03/05/sampling-fp-unit-interval

*/
struct StatelessUniformSampleGenerator
{
    uint m_baseHash;
    uint m_effectHash;

//public:
    static StatelessUniformSampleGenerator make(uint2 pixelCoord, uint vertexIndex, uint sampleIndex)
    {
        StatelessUniformSampleGenerator ret;
        ret.m_baseHash = Hash32Combine(Hash32(vertexIndex + 0x035F9F29), (pixelCoord.x << 16) | pixelCoord.y);
        ret.m_baseHash = Hash32Combine(ret.m_baseHash, sampleIndex);
        ret.startEffect(SampleGeneratorEffectSeed::Base);
        return ret;
    }

    // Sets an effect-specific sampler state that is decorrelated from other effects.
    // The state is also deterministic for the specific path vertex, regardless of shader code ordering / branching.
    void startEffect(SampleGeneratorEffectSeed effectSeed, bool lowDiscrepancy = false, int subSampleIndex = 0, int subSampleCount = 1)
    {
        //if (effectSeed == SampleGeneratorEffectSeed::Base) // this should be faster, but it's actually slower in practice - why? does it indicate some of this doesn't get compiled out?
        //    m_effectHash = m_baseHash;
        //else                          
        m_effectHash = Hash32Combine(m_baseHash, (uint)effectSeed);
        if (subSampleIndex > 0)
            m_effectHash = Hash32Combine(m_baseHash, subSampleIndex);
    }
    
    // Returns the next sample value. This function updates the state.
    uint next()
    {
        m_effectHash = Hash32(m_effectHash);
        return m_effectHash;
    }
};

/** Inline quasi-random sample generator.

    Current implementation is based on "Practical Hash-based Owen Scrambling", Brent Burley 2020, https://jcgt.org/published/0009/04/01/paper.pdf
    Shader implementation borrows from Andrew Helmer's Shadertoy implementation (https://www.reddit.com/r/GraphicsProgramming/comments/l1go2r/owenscrambled_sobol_02_sequences_shadertoy/)

    The interface will only provide quasi-random sequence via nextLowDiscrepancyXD interface, otherwise it's a standard pseudo-random approach.

    It supports up to >6< dimensions and reverts back to pseudo-random for subsequent samples.

*/
struct StatelessLowDiscrepancySampleGenerator
{
    uint m_baseHash;
    uint m_effectHash;

    uint m_sampleIndex;
    uint m_dimension;   // 0xFFFFFFFF means it's in non-LD mode
    uint m_activeIndex; // in case subIndexing is used

//public:
    // pixelCoord and vertexIndex get hashed; sampleIndex is the index in the low discrepancy sequence
    static StatelessLowDiscrepancySampleGenerator make(uint2 pixelCoord, uint vertexIndex, uint sampleIndex)
    {
        StatelessLowDiscrepancySampleGenerator ret;
        ret.m_sampleIndex = sampleIndex;
        ret.m_baseHash = Hash32Combine(Hash32(vertexIndex + 0x035F9F29), (pixelCoord.x << 16) | pixelCoord.y);
        ret.startEffect(SampleGeneratorEffectSeed::Base);

        return ret;
    }

    // Sets an effect-specific sampler state that is decorrelated from other effects.
    // The state is also deterministic for the specific path vertex, regardless of shader code ordering / branching.
    // This either activates the non-low discrepancy path (faster) or a low-discrepancy (slower) sampler where
    // each subsequent 'next()' call advances dimension and provide a low discrepancy sample for the index.
    // Once all available dimensions provided by the LD sampler are used up (4, 5, 6, etc.), it reverts to the faster non-low discrepancy path.
    //
    // Subindex-ing feature allows sampling multiple sequence samples in case of inner loops (i.e. sampling multiple lights in a loop per bounce).
    void startEffect(SampleGeneratorEffectSeed effectSeed, bool lowDiscrepancy = false, int subSampleIndex = 0, int subSampleCount = 1)
    {
        m_activeIndex = m_sampleIndex * subSampleCount + subSampleIndex;
        if (lowDiscrepancy)
        {
            m_effectHash = Hash32Combine(m_baseHash, (uint)effectSeed);
            m_dimension = 0;    // set LD mode
        }
        else
        {
            m_effectHash = Hash32Combine(m_baseHash, (uint) effectSeed);
            m_effectHash = Hash32Combine(m_effectHash, m_activeIndex); // unlike the StatelessUniformSampleGenerator, we combine the sampleIndex here in uniform mode; a bit slower but allows for LD mode that needs it not combined into m_baseHash
            m_dimension = 0xFFFFFFFF; // set non-LD mode
        }
    }

    // Returns the next sample value. This function updates the state. 
    // It is subtly different from StatelessUniformSampleGenerator implementation because it applies m_sampleIndex here,
    // instead of at the creation time, which is slower, but allows for non-LD approach to be used together with LD
    // using the same interface.
    uint next()
    {
        const uint supportedDimensions = 5;
        
        if (m_dimension == 0xFFFFFFFF)
        {
            m_effectHash = Hash32(m_effectHash);
            return m_effectHash;
        }
        else
        {
            uint shuffle_seed = Hash32Combine( m_effectHash, 0 );   // this might not need combine, just use raw and then remove '1+' from '1+m_dimension' below
            uint dim_seed = Hash32Combine( m_effectHash, 1+m_dimension );
            uint shuffled_index = bhos_owen_scramble(m_activeIndex, shuffle_seed);
            #if 1
            // Sobol' sequence is expensive but we can use Laine-Kerras permutation for the 1st dimension and Sobol' only for second and subsequent. See 'bhos_sobol' function comments for more detail.
            uint dim_sample;
            [branch] if (m_dimension==0) 
                dim_sample = bhos_reverse_bits(shuffled_index);
            else
                dim_sample = bhos_sobol(shuffled_index, m_dimension);
            #else
            uint dim_sample = bhos_sobol(shuffled_index, m_dimension);
            #endif
            dim_sample = bhos_owen_scramble(dim_sample, dim_seed);

            // step dimension for next sample!
            m_dimension++;
            
            // for all subsequent samples fall back to pseudo-random
            if(m_dimension >= supportedDimensions)
            {
                m_effectHash = Hash32Combine(m_effectHash, m_activeIndex);
                m_dimension = 0xFFFFFFFF;    // set non-LD mode
            }
            return dim_sample;
        }
    }
};

#endif // __INLINE_SAMPLE_GENERATORS_HLSLI__