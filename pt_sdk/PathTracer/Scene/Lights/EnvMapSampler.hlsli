/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

/** Utility functions for environment map sampling.

    Use the class EnvMapSampler on the host to load and prepare the env map.
    The class builds an hierarchical importance map, which is used here
    for importance sampling.
*/

//import Scene.Scene;
//import Utils.Math.MathHelpers;

#ifndef __ENV_MAP_SAMPLER__
#define __ENV_MAP_SAMPLER__

#include "../../Utils/Math/MathHelpers.hlsli"
#include "EnvMap.hlsli"

/** Struct returned from the sampling functions.
*/

struct EnvMapSample
{
    float3 dir;         ///< Sampled direction towards the light in world space.
    float pdf;          ///< Probability density function for the sampled direction with respect to solid angle.
    float3 Le;          ///< Emitted radiance.
};

/** Struct for sampling and evaluating an environment map.
*/
struct EnvMapSampler
{
    SamplerState        importanceSampler;      ///< Point sampling with clamp to edge.
    Texture2D<float>    importanceMap;          ///< Hierarchical importance map (entire mip chain).
    float2              importanceInvDim;       ///< 1.0 / dimension.
    uint                importanceBaseMip;      ///< Mip level for 1x1 resolution.
    // TODO: Add scalar value for total integrated intensity, i.e., same as 1x1 mip

//pt_sdk specific - START
     struct Scene
     {
        EnvMap envMap; 
     };

     Scene gScene;

    static EnvMapSampler make(
        SamplerState        s_ImportanceSampler,        ///< Point sampling with clamp to edge.
        Texture2D<float>    t_ImportanceMap,            ///< Hierarchical importance map (entire mip chain).
        float2              importanceInvDim,           ///< 1.0 / dimension.
        uint                importanceBaseMip,          ///< Mip level for 1x1 resolution.
        Texture2D<float4>   t_EnvironmentMap,           ///< Environment map texture.
        SamplerState        s_EnvironmentMapSampler,    ///< Environment map sampler.
        EnvMapData          envMapData                  ///< Environment map data.        
        ) 
    {
        EnvMapSampler envMapSampler;
        envMapSampler.importanceSampler = s_ImportanceSampler;
        envMapSampler.importanceMap = t_ImportanceMap;    
        envMapSampler.importanceInvDim = importanceInvDim; 
        envMapSampler.importanceBaseMip = importanceBaseMip;
        
        envMapSampler.gScene.envMap = EnvMap::make(t_EnvironmentMap, s_EnvironmentMapSampler, envMapData);
        
        return envMapSampler;
    }
//pt_sdk specific - END

    /** Evaluates the radiance coming from world space direction 'dir'.
    */
    float3 eval(float3 dir, float lod = 0.f)
    {
        return gScene.envMap.eval(dir, lod);
    }

    /** Importance sampling of the environment map.
    */
    EnvMapSample sample(const float2 rnd)
    {
        EnvMapSample result;
        
        float2 p = rnd;     // Random sample in [0,1)^2.
        uint2 pos = 0;      // Top-left texel pos of current 2x2 region.

        // Iterate over mips of 2x2...NxN resolution.
        for (int mip = importanceBaseMip - 1; mip >= 0; mip--)
        {
            // Scale position to current mip.
            pos *= 2;

            // Load the four texels at the current position.
            float w[4];
            w[0] = importanceMap.Load(int3(pos, mip));
            w[1] = importanceMap.Load(int3(pos + uint2(1, 0), mip));
            w[2] = importanceMap.Load(int3(pos + uint2(0, 1), mip));
            w[3] = importanceMap.Load(int3(pos + uint2(1, 1), mip));

            float q[2];
            q[0] = w[0] + w[2];
            q[1] = w[1] + w[3];

            uint2 off;

            // Horizontal warp.
            float d = q[0] / (q[0] + q[1]);   // TODO: Do we need to guard against div-by-zero. We should ensure we never go down a path that has p=0.

            if (p.x < d) // left
            {
                off.x = 0;
                p.x = p.x / d;
            }
            else // right
            {
                off.x = 1;
                p.x = (p.x - d) / (1.f - d);
            }

            // Vertical warp.
            // Avoid stack allocation by not using dynamic indexing.
            // float e = w[off.x] / q[off.x];
            float e = off.x == 0 ? (w[0] / q[0]) : (w[1] / q[1]);

            if (p.y < e) // bottom
            {
                off.y = 0;
                p.y = p.y / e;
            }
            else // top
            {
                off.y = 1;
                p.y = (p.y - e) / (1.f - e);
            }

            pos += off;
        }

        // At this point, we have chosen a texel 'pos' in the range [0,dimension) for each component.
        // The 2D sample point 'p' has been warped along the way, and is in the range [0,1) representing sub-texel location.

#if 0 // if correctness testing vs envMapEvalPdf - due to differences in UV math samples on the border can be different; this is rare and is fine/acceptable but use this to temporarily ensure it's only that
        float eps = 2e-4f; // // eps found empirically
        p = p * (1.0-2.0*eps).xx+eps.xx;
#endif

        // Compute final sample position and map to direction.
        float2 uv = ((float2)pos + p) * importanceInvDim;     // Final sample in [0,1)^2.
        float3 dir = oct_to_ndir_equal_area_unorm(uv);

        // Compute final pdf.
        // We sample exactly according to the intensity of where the final samples lies in the octahedral map, normalized to its average intensity.
        float avg_w = importanceMap.Load(int3(0, 0, importanceBaseMip)); // 1x1 mip holds integral over importance map. TODO: Replace by constant or rescale in setup so that the integral is 1.0
        float pdf = importanceMap[pos] / avg_w;

        result.dir = gScene.envMap.toWorld(dir);
        result.pdf = pdf * M_1_4PI;
        result.Le = gScene.envMap.eval(result.dir);

        return result;
    }

    /** Evaluates the probability density function for a specific direction.
        Note that the sample() function already returns the pdf for the sampled location.
        But, in some cases we need to evaluate the pdf for other directions (e.g. for MIS).

        \param[in] dir World space direction (normalized).
        \return Probability density function evaluated for direction 'dir'.
    */
    float evalPdf(float3 dir)
    {
        float2 uv = ndir_to_oct_equal_area_unorm(gScene.envMap.toLocal(dir));
        float avg_w = importanceMap.Load(int3(0, 0, importanceBaseMip)); // 1x1 mip holds integral over importance map. TODO: Replace by constant or rescale in setup so that the integral is 1.0
        float pdf = importanceMap.SampleLevel(importanceSampler, uv, 0) / avg_w;
        return pdf * (1.f / M_4PI);
    }
};

#endif //__ENV_MAP_SAMPLER__