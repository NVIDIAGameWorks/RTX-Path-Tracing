/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __MATERIAL_DATA_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __MATERIAL_DATA_HLSLI__

#include "../../Config.h"    

#include "MaterialTypes.hlsli"

/** This is a host/device structure for material header data for all material types (8B).
*/
struct MaterialHeader
{
    uint2 packedData; // = {}; <- not supported in .hlsl ! use MaterialHeader::make()

    static const uint kMaterialTypeBits = 16;
    static const uint kNestedPriorityBits = 4;
    static const uint kLobeTypeBits = 8; // Only 6 bits needed if packing LobeType
    //static const uint kSamplerIDBits = 8;
    static const uint kAlphaModeBits = 1;
    static const uint kAlphaThresholdBits = 16; // Using float16_t format
    static const uint kPSDDominantDeltaLobeP1Bits = 4;

    // packedData.x bit layout
    static const uint kMaterialTypeOffset = 0;
    static const uint kNestedPriorityOffset = kMaterialTypeOffset + kMaterialTypeBits;
    static const uint kLobeTypeOffset = kNestedPriorityOffset + kNestedPriorityBits;
    static const uint kDoubleSidedFlagOffset = kLobeTypeOffset + kLobeTypeBits;
    static const uint kThinSurfaceFlagOffset = kDoubleSidedFlagOffset + 1;
    static const uint kEmissiveFlagOffset = kThinSurfaceFlagOffset + 1;
    static const uint kIsBasicMaterialFlagOffset = kEmissiveFlagOffset + 1;

    static const uint kTotalHeaderBitsX = kIsBasicMaterialFlagOffset + 1;

    // packedData.y bit layout
    static const uint kAlphaThresholdOffset = 0;
    static const uint kAlphaModeOffset = kAlphaThresholdOffset + kAlphaThresholdBits;
    //static const uint kSamplerIDOffset = kAlphaModeOffset + kAlphaModeBits;
    static const uint kPSDExcludeFlagOffset = kAlphaModeOffset + kAlphaModeBits;
    static const uint kPSDDominantDeltaLobeP1Offset = kPSDExcludeFlagOffset + 1;

    static const uint kTotalHeaderBitsY = kPSDDominantDeltaLobeP1Offset + kPSDDominantDeltaLobeP1Bits;


    /** Set material type.
    */
    SETTER_DECL void setMaterialType(MaterialType type) { packedData.x = PACK_BITS(kMaterialTypeBits, kMaterialTypeOffset, packedData.x, (uint)type); }

    /** Get material type.
    */
    MaterialType getMaterialType() CONST_FUNCTION { return (MaterialType)(EXTRACT_BITS(kMaterialTypeBits, kMaterialTypeOffset, packedData.x)); }

    /** Set alpha testing mode.
    */
    SETTER_DECL void setAlphaMode(AlphaMode mode) { packedData.y = PACK_BITS(kAlphaModeBits, kAlphaModeOffset, packedData.y, (uint)mode); }

    /** Get material type.
    */
    AlphaMode getAlphaMode() CONST_FUNCTION { return (AlphaMode)EXTRACT_BITS(kAlphaModeBits, kAlphaModeOffset, packedData.y); }

    /** Set alpha threshold.
    */
    // native types require "[-HV 2018] -enable-16bit-types -T *s_6_2"
    // SETTER_DECL void setAlphaThreshold(float16_t alphaThreshold) { packedData.y = PACK_BITS_UNSAFE(kAlphaThresholdBits, kAlphaThresholdOffset, packedData.y, (uint)asuint16(alphaThreshold)); }
    SETTER_DECL void setAlphaThreshold(float alphaThreshold) { packedData.y = PACK_BITS_UNSAFE(kAlphaThresholdBits, kAlphaThresholdOffset, packedData.y, f32tof16(alphaThreshold)); }

    /** Get alpha threshold.
    */
    //float16_t getAlphaThreshold() CONST_FUNCTION { return asfloat16((uint16_t)EXTRACT_BITS(kAlphaThresholdBits, kAlphaThresholdOffset, packedData.y)); }
    float getAlphaThreshold() CONST_FUNCTION { return f16tof32(EXTRACT_BITS(kAlphaThresholdBits, kAlphaThresholdOffset, packedData.y)); }

    /** Set the nested priority used for nested dielectrics.
    */
    SETTER_DECL void setNestedPriority(uint32_t priority) { packedData.x = PACK_BITS(kNestedPriorityBits, kNestedPriorityOffset, packedData.x, priority); }

    /** Get the nested priority used for nested dielectrics.
        \return Nested priority, with 0 reserved for the highest possible priority.
    */
    uint getNestedPriority() CONST_FUNCTION { return EXTRACT_BITS(kNestedPriorityBits, kNestedPriorityOffset, packedData.x); }

    /** Set active BxDF lobes.
        \param[in] activeLobes Bit mask of active lobes. See LobeType.
    */
    SETTER_DECL void setActiveLobes(uint activeLobes) { packedData.x = PACK_BITS(kLobeTypeBits, kLobeTypeOffset, packedData.x, activeLobes); }

    /** Get active BxDF lobes.
        \return Bit mask of active lobes. See LobeType.
    */
    uint getActiveLobes() CONST_FUNCTION { return EXTRACT_BITS(kLobeTypeBits, kLobeTypeOffset, packedData.x); }

    // /** Set default texture sampler ID.
    // */
    // SETTER_DECL void setDefaultTextureSamplerID(uint samplerID) { packedData.y = PACK_BITS(kSamplerIDBits, kSamplerIDOffset, packedData.y, samplerID); }
    // 
    // /** Get default texture sampler ID.
    // */
    // uint getDefaultTextureSamplerID() CONST_FUNCTION { return EXTRACT_BITS(kSamplerIDBits, kSamplerIDOffset, packedData.y); }

    /** Set double-sided flag.
    */
    SETTER_DECL void setDoubleSided(bool doubleSided) { packedData.x = PACK_BITS(1, kDoubleSidedFlagOffset, packedData.x, doubleSided ? 1 : 0); }

    /** Get double-sided flag.
    */
    bool isDoubleSided() CONST_FUNCTION { return packedData.x & (1u << kDoubleSidedFlagOffset); }

    /** Set thin surface flag.
    */
    SETTER_DECL void setThinSurface(bool thinSurface) { packedData.x = PACK_BITS(1, kThinSurfaceFlagOffset, packedData.x, thinSurface ? 1 : 0); }

    /** Get thin surface flag.
    */
    bool isThinSurface() CONST_FUNCTION { return packedData.x & (1u << kThinSurfaceFlagOffset); }

    /** Set emissive flag.
    */
    SETTER_DECL void setEmissive(bool isEmissive) { packedData.x = PACK_BITS(1, kEmissiveFlagOffset, packedData.x, isEmissive ? 1 : 0); }

    /** Get emissive flag.
    */
    bool isEmissive() CONST_FUNCTION { return packedData.x & (1u << kEmissiveFlagOffset); }

    /** Set basic material flag. This flag is an optimization to allow quick type checking.
    */
    SETTER_DECL void setIsBasicMaterial(bool isBasicMaterial) { packedData.x = PACK_BITS(1, kIsBasicMaterialFlagOffset, packedData.x, isBasicMaterial ? 1 : 0); }

    /** Get basic material flag. This flag is an optimization to allow quick type checking.
    */
    bool isBasicMaterial() CONST_FUNCTION { return packedData.x & (1u << kIsBasicMaterialFlagOffset); }

    void setPSDExclude(bool psdExclude) { packedData.y = PACK_BITS(1, kPSDExcludeFlagOffset, packedData.y, psdExclude ? 1 : 0); }
    bool isPSDExclude() CONST_FUNCTION  { return packedData.y & (1u << kPSDExcludeFlagOffset); }

    void setPSDDominantDeltaLobeP1(uint psdDominantDeltaLobeP1) { packedData.y = PACK_BITS(kPSDDominantDeltaLobeP1Bits, kPSDDominantDeltaLobeP1Offset, packedData.y, (uint)psdDominantDeltaLobeP1); }
    uint getPSDDominantDeltaLobeP1() CONST_FUNCTION             { return EXTRACT_BITS(kPSDDominantDeltaLobeP1Bits, kPSDDominantDeltaLobeP1Offset, packedData.y); }

#ifdef HOST_CODE
    friend bool operator==(const MaterialHeader& lhs, const MaterialHeader& rhs);
    friend bool operator!=(const MaterialHeader& lhs, const MaterialHeader& rhs) { return !(lhs == rhs); }
#endif

    static MaterialHeader make( ) { MaterialHeader header; header.packedData = uint2(0,0); return header; }
};

/** This is a host/device structure for material payload data (120B).
    The format of the data depends on the material type. 
*/
struct MaterialPayload
{
    uint data[30];
};

/** This is a host/device structure representing a blob of material data (128B).
    The material data blob consists of a header and a payload. The header is always valid.
    The blob is sized to fit into one cacheline for efficient access. Do not change the size.
    All material types should keep their main material data within this footprint.
    Material that need more can reference additional sideband data stored elsewhere.
*/
struct MaterialDataBlob
{
    MaterialHeader header; // 8B
    MaterialPayload payload; // 120B
};

#endif // __MATERIAL_DATA_HLSLI__