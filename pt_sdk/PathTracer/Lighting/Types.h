/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __TYPES_HLSLI__
#define __TYPES_HLSLI__

#if !defined(__cplusplus)
#pragma pack_matrix(row_major)
#else
using namespace donut::math;
#endif

// Environment map color/intensity and orientation modifiers ("in-scene" settings)
struct EnvMapSceneParams
{
	float3x4    Transform;              ///< Local to world transform.
	float3x4    InvTransform;           ///< World to local transform.

    float3      ColorMultiplier;        ///< Color & radiance scale (Tint * Intensity)
    float       padding0;
};

// Environment map importance sampling internals
struct EnvMapImportanceSamplingParams
{
    // MIP descent sampling
    float2      ImportanceInvDim;       ///< 1.0 / dimension.
    uint        ImportanceBaseMip;      ///< Mip level for 1x1 resolution.
    uint        padding0;
};

// Returned by importance sampling functions
struct DistantLightSample
{
    float3  Dir;        ///< Sampled direction towards the light in world space.
    float   Pdf;        ///< Probability density function for the sampled direction with respect to solid angle.
    float3  Le;         ///< Emitted radiance.
};


#endif // #define __TYPES_HLSLI__