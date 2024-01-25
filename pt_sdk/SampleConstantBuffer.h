/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __SAMPLE_CONSTANT_BUFFER_H__
#define __SAMPLE_CONSTANT_BUFFER_H__

#if !defined(__cplusplus) // not needed in the port so far
#pragma pack_matrix(row_major) // matrices below are expected in row_major
#endif

#include <donut/shaders/light_cb.h>
#include <donut/shaders/view_cb.h>

#include "PathTracer/PathTracerShared.h"

#include "PathTracer/ShaderDebug.hlsli"

#include "PathTracer/Lighting/Types.h"

#define PTDEMO_LIGHT_CONSTANTS_COUNT 64

struct SampleConstants
{
    float4 ambientColor;
    int lightConstantsCount;
    uint materialCount;
    uint _padding1;
    uint _padding2;
    
    LightConstants lights[PTDEMO_LIGHT_CONSTANTS_COUNT];
    PlanarViewConstants view;
    PlanarViewConstants previousView;
    EnvMapSceneParams envMapSceneParams;
    EnvMapImportanceSamplingParams envMapImportanceSamplingParams;
    PathTracerConstants ptConsts;
    DebugConstants debug;
    float4 denoisingHitParamConsts;
};

// Used in a couple of places like multipass postprocess where you want to keep SampleConstants the same for all passes, but send just a few additional per-pass parameters 
// In path tracing used to pass subSampleIndex (when enabled).
// Set as 'push constants' (root constants)
struct SampleMiniConstants
{
    uint4 params;
};

// per-instance-geometry data (avoids 1 layer of indirection that requires reading from instance and geometry buffers)
struct SubInstanceData  // could have been called GeometryInstanceData but that's already used in Falcor codebase
{
    static const int Flags_AlphaTested      	 = (1<<16);
    static const int Flags_ExcludeFromNEE    	 = (1<<17);

    uint FlagsAndSERSortKey;
    uint GlobalGeometryIndex;           // index into t_GeometryData and t_GeometryDebugData
    uint AlphaTextureIndex;             // index into t_BindlessTextures
    float AlphaCutoff;                  // could be packed into 8 bits and kept in FlagsAndSERSortKey
};

#endif // __SAMPLE_CONSTANT_BUFFER_H__