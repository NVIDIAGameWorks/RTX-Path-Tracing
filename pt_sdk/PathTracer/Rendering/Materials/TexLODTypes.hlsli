/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

/** Enums for texture level-of-detail -- see TexLODHelpers.hlsli
*/

#ifndef __TEX_LOD_TYPES_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __TEX_LOD_TYPES_HLSLI__

#include "../../Utils/HostDeviceShared.hlsli"

BEGIN_NAMESPACE_FALCOR

/** This enum is shared between CPU/GPU.
    It enumerates the different the different texture LOD modes.
*/
enum class TexLODMode : uint32_t
{
    Mip0 = 0,
    RayCones = 1,
    RayDiffs = 2,
};

/** This enum is shared between CPU/GPU.
    Both Combo and Unified avoid computing surface spread from the G-buffer.
    Combo uses ray differentials to compute the spread at the first hit and then average edge curvature for all hits after that.
    Unified uses interpolated edge curvatures for the first hit and then average edge curvatures for all hits after that.
*/
enum class RayConeMode : uint32_t
{
    Combo = 0,
    Unified = 1,
};

END_NAMESPACE_FALCOR

#endif // __TEX_LOD_TYPES_HLSLI__