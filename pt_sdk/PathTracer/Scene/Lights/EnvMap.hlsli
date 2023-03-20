/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

//import Scene.Lights./*EnvMapData;
//import Utils.Math.MathHelpers;

#include "../../Utils/Math/MathHelpers.hlsli"
#include "../../PathTracerShared.h"

/** Struct for accessing an environment map.
*/
struct EnvMap
{
    Texture2D<float4>   envMap;         ///< Environment map texture.
    SamplerState        envSampler;     ///< Environment map sampler.

    EnvMapData          data;           ///< Environment map data.

//pt_sdk specific - START
    static EnvMap make(
        Texture2D<float4>   t_EnvironmentMap,           ///< Environment map texture.
        SamplerState        s_EnvironmentMapSampler,    ///< Environment map sampler.
        EnvMapData          envMapData                  ///< Environment map data.     
    )
    {
        EnvMap envMap;
        envMap.envMap = t_EnvironmentMap;
        envMap.envSampler = s_EnvironmentMapSampler;
        envMap.data = envMapData;

        return envMap;
    }
//pt_sdk specific - END

    /** Returns the dimensions of the env map texture.
    */
    uint2 getDimensions()
    {
        uint2 dim;
        envMap.GetDimensions(dim.x, dim.y);
        return dim;
    }

    /** Evaluates the radiance at a given texel.
    */
    float3 evalTexel(uint2 coord)
    {
        return envMap[coord].rgb * getIntensity();
    }

    /** Evaluates the radiance at a given uv coordinate.
    */
    float3 eval(float2 uv, float lod = 0.f)
    {
        return envMap.SampleLevel(envSampler, uv, lod).rgb * getIntensity();
    }

    /** Evaluates the radiance coming from world space direction 'dir'.
    */
    float3 eval(float3 dir, float lod = 0.f)
    {
        return eval(worldToUv(dir), lod);
    }

    /** Transform direction in local space to uv coordinates.
    */
    float2 localToUv(float3 dir)
    {
        return world_to_latlong_map(dir);
    }

    /** Transform uv coordinates to direction in local space.
    */
    float3 uvToLocal(float2 uv)
    {
        return latlong_map_to_world(uv);
    }

    /** Transform direction in world space to uv coordinates.
    */
    float2 worldToUv(float3 dir)
    {
        return localToUv(toLocal(dir));
    }

    /** Transform uv coordinates to direction in world space.
    */
    float3 uvToWorld(float2 uv)
    {
        return toWorld(uvToLocal(uv));
    }

    /** Transform direction from local to world space.
    */
    float3 toWorld(float3 dir)
    {
        // TODO: For identity transform we might want to skip this statically.
        return mul(dir, (float3x3)data.transform);
    }

    /** Transform direction from world to local space.
    */
    float3 toLocal(float3 dir)
    {
        // TODO: For identity transform we might want to skip this statically.
        return mul(dir, (float3x3)data.invTransform);
    }

    /** Get the intensity scaling factor (including tint).
    */
    float3 getIntensity()
    {
        return data.intensity * data.tint;
    }
};
