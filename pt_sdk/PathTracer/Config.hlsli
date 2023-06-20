/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __CONFIG_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __CONFIG_HLSLI__

#if !defined(__cplusplus) // not needed in the port so far
#include "Utils/HostDeviceShared.hlsli"
#endif

#define MAX_BOUNCE_COUNT                        96      // max value that SampleUIData::BounceCount can be set to - technically max value should be 255 with existing bounce counters

// In Falcor these macros get programmatically defined in Scene.cpp, getSceneDefines

#define SCENE_GEOMETRY_TYPES ( (1u << GEOMETRY_TYPE_TRIANGLE_MESH) )

// default defines from Falcor/PT_SDK test scene with some mods
#define HIT_INFO_DEFINES                        1
#define HIT_INFO_INSTANCE_ID_BITS               29      // was 5, could be less but let's max it out for now
#define HIT_INFO_PRIMITIVE_INDEX_BITS           32      // was 11, could be less but let's max it out for now
#define HIT_INFO_TYPE_BITS                      3
#define HIT_INFO_USE_COMPRESSION                0

// we use static specialization for BSDFs at the moment (even though parts can be compile-time removed with )
#define ActiveBSDF                              StandardBSDF

#define LOD_TEXTURE_SAMPLER_EXPLICIT            1
#define LOD_TEXTURE_SAMPLER_RAY_CONES           2
#define ACTIVE_LOD_TEXTURE_SAMPLER              LOD_TEXTURE_SAMPLER_RAY_CONES  // LOD_TEXTURE_SAMPLER_EXPLICIT

// PATH_TRACER_MODE options
#define PATH_TRACER_MODE_REFERENCE              0       // stable planes ignored
#define PATH_TRACER_MODE_BUILD_STABLE_PLANES    1       // stable planes being built: only non-noisy rays (roughness close to 0) traced akin to Whitted-style ray-tracing, stopping at diffuse vertices and setting up denoising planes; all emissive collected and stable
#define PATH_TRACER_MODE_FILL_STABLE_PLANES     2       // standard noisy ray tracing, except it tracks the stable path that matches planes built in _BUILD_ pass, and deposits radiance accordingly (and ignores previously captured stable emissive)

#define DEBUG_VIZ_MIP_COLORS                    false   // use to display mip-based gradient instead of base color !!! DISABLED IN THE LAST REFACTORING !!!

// there's a significant cost to enabling these; #ifdef _DEBUG unfortunately doesn't work because it's never defined for shaders :(
#define ENABLE_DEBUG_VIZUALISATION              1       // added cost is up to 5%
#define ENABLE_DEBUG_DELTA_TREE_VIZUALISATION   0       // added cost can be over 10%; requires ENABLE_DEBUG_VIZUALISATION to be enabled < !!!! currently disabled because it's buggy - needs a refactor
#define ENABLE_DEBUG_RTXDI_VIZUALISATION        0       // added cost is ~5%; requires ENABLE_DEBUG_VIZUALISATION to be enabled

#if ENABLE_DEBUG_VIZUALISATION == 0
#undef ENABLE_DEBUG_RTXDI_VIZUALISATION
#undef ENABLE_DEBUG_DELTA_TREE_VIZUALISATION
#define ENABLE_DEBUG_DELTA_TREE_VIZUALISATION   0
#define ENABLE_DEBUG_RTXDI_VIZUALISATION        0
#endif

#ifndef NON_PATH_TRACING_PASS
#define NON_PATH_TRACING_PASS 0
#endif

// for NVAPI integration
#define NV_SHADER_EXTN_SLOT                 u127    // pick an arbitrary unused slot
#define NV_SHADER_EXTN_SLOT_NUM             127     // must match NV_SHADER_EXTN_SLOT_NUM
#define NV_SHADER_EXTN_REGISTER_SPACE       space0  // pick an arbitrary unused space
#define NV_SHADER_EXTN_REGISTER_SPACE_NUM   0       // must match NV_SHADER_EXTN_REGISTER_SPACE

#if !defined(__cplusplus) // not needed in the port so far
// user-specific conversion between 32bit uint path ID and pixel location (other things can get packed in if needed)
uint2   PathIDToPixel( uint id )        { return uint2( id >> 16, id & 0xFFFF ); }
uint    PathIDFromPixel( uint2 pixel )  { return pixel.x << 16 | pixel.y; }
#endif

#define  kMaxSceneDistance                  50000.0         // used as a general max distance between any two surface points in the scene, excluding environment map - should be less than kMaxRayTravel; 50k is within fp16 floats; note: actual sceneLength can be longer due to bounces.
#define  kMaxRayTravel                      (1e15f)         // one AU is ~1.5e11; 1e15 is high enough to use as environment map distance to avoid parallax but low enough to avoid precision issues with various packing and etc.

#define  cStablePlaneCount                  (3u)            // more than 3 is not supported although 4 could be supported if needed (with some reshuffling)

#define  NUM_COMPUTE_THREADS_PER_DIM        8

#endif // __CONFIG_HLSLI__
