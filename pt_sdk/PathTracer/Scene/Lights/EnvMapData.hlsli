/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __ENV_MAP_DATA_H__
#define __ENV_MAP_DATA_H__

#ifndef __cplusplus
#pragma pack_matrix(row_major)
#endif

/** This is a host/device structure that describes the environment map proeprties.
*/
struct EnvMapData
{
	float3x4    transform;              ///< Local to world transform.
	float3x4    invTransform;           ///< World to local transform.

    float3      tint;                   ///< Color tint
    float       intensity;              ///< Radiance scale
};

// From ..\Falcor\Source\Falcor\Rendering\Lights\EnvMapSampler.hlsli
struct EnvMapSamplerData
{
    float2      importanceInvDim;       ///< 1.0 / dimension.
    uint        importanceBaseMip;      ///< Mip level for 1x1 resolution.
    float       _padding0;
};

#endif // __ENV_MAP_DATA_H__
