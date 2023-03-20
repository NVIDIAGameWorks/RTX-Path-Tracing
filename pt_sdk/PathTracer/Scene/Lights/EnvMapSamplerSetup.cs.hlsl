/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

/** Compute shader for building a hierarchical importance map from an
    environment map. The result is used by EnvMapSampler.hlsli for sampling.
*/

#include "EnvMapSamplerSetup.cs.hlsli"