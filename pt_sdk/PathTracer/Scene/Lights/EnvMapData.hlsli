/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once
#include "Utils/HostDeviceShared.hlsli"

BEGIN_NAMESPACE_FALCOR

/** This is a host/device structure that describes the environment map proeprties.
*/
struct EnvMapData
{
    float3x4    transform;              ///< Local to world transform.
    float3x4    invTransform;           ///< World to local transform.

    float3      tint = {1.f, 1.f, 1.f}; ///< Color tint
    float       intensity = 1.f;        ///< Radiance scale
};

END_NAMESPACE_FALCOR
