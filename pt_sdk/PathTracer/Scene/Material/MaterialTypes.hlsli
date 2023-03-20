/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __MATERIAL_TYPES_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __MATERIAL_TYPES_HLSLI__

#include "../../Config.hlsli"    

BEGIN_NAMESPACE_FALCOR

/** Material type.
*/
enum class MaterialType // : uint32_t
{
    Standard,
    Cloth,
    Hair,
    MERL,

    Count // Must be last
};

/** Alpha mode. This specifies how alpha testing should be done.
*/
enum class AlphaMode
{
    Opaque,     ///< No alpha test.
    Mask,       ///< Alpha mask.

    Count // Must be last
};

/** Shading models supported by the standard material.
*/
enum class ShadingModel
{
    MetalRough,
    SpecGloss,

    Count // Must be last
};

/** Normal map type. This specifies the encoding of a normal map.
*/
enum class NormalMapType
{
    None,       ///< Normal map is not used.
    RGB,        ///< Normal encoded in RGB channels in [0,1].
    RG,         ///< Tangent space encoding in RG channels in [0,1].

    Count // Must be last
};

END_NAMESPACE_FALCOR

#endif // __MATERIAL_TYPES_HLSLI__