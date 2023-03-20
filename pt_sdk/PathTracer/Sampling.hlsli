/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __SAMPLING_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __SAMPLING_HLSLI__

#include "Config.hlsli"

#if USING_STATELESS_SAMPLE_GENERATOR

#include "StatelessSampleGenerators.hlsli"
#define SampleGenerator StatelessUniformSampleGenerator

#elif 1

#include "Utils\Sampling\TinyUniformSampleGenerator.hlsli"
#define SampleGenerator TinyUniformSampleGenerator

#elif 0

#include "Utils\Sampling\UniformSampleGenerator.hlsli"
#define SampleGenerator UniformSampleGenerator

#else
#error One sampler approach has to be selected!
#endif

/** Convenience functions for generating 1D/2D/3D values in the range [0,1).
*/
float sampleNext1D( inout SampleGenerator sg )
{
    // Use upper 24 bits and divide by 2^24 to get a number u in [0,1).
    // In floating-point precision this also ensures that 1.0-u != 0.0.
    uint bits = sg.next();
    return (bits >> 8) * 0x1p-24;
}
float2 sampleNext2D( inout SampleGenerator sg )
{
#ifndef SAMPLER_HAS_SPECIALIZED_2D3D
    float2 sample;
    // Don't use the float2 initializer to ensure consistent order of evaluation.
    sample.x = sampleNext1D(sg);
    sample.y = sampleNext1D(sg);
    return sample;
#else
    uint2 bits = sg.next2D();
    return (bits >> 8) * 0x1p-24.xx;
#endif
}
float3 sampleNext3D( inout SampleGenerator sg )
{
#ifndef SAMPLER_HAS_SPECIALIZED_2D3D
    float3 sample;
    // Don't use the float3 initializer to ensure consistent order of evaluation.
    sample.x = sampleNext1D(sg);
    sample.y = sampleNext1D(sg);
    sample.z = sampleNext1D(sg);
    return sample;
#else
    uint3 bits = sg.next3D();
    return (bits >> 8) * 0x1p-24.xxx;
#endif
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif // __SAMPLING_HLSLI__
