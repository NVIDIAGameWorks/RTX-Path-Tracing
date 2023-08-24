/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __HIT_INFO_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __HIT_INFO_HLSLI__

#include "../Config.h"

#include "SceneDefines.hlsli"
#include "HitInfoType.hlsli"
#include "SceneTypes.hlsli"

// import Utils.Math.FormatConversion;
// __exported import Scene.HitInfoType;
// __exported import Scene.SceneTypes;
// __exported import Scene.SDFs.SDFGridHitData;

/** Ray hit information.

    'HitInfo' is a polymorphic container for storing ray hit information.
    'PackedHitInfo' holds the data in 'HitInfo'.
    The upper bits of the first uint are used to store the type of the hit.
    A zero initialized value represent hit type 'None'.

    HitInfo stores data for one of the following type specific structs:
    TriangleHit, DisplacedTriangleHit, CurveHit, SDFGridHit, VolumeHit

    TriangleHit, DisplacedTriangleHit and CurveHit all store the hit point in terms of
    an instance ID and a primitive index, together with barycentrics.

    DisplacedTriangleHit additionally stores the displacement offset.

    To use HitInfo, the host needs to set the following defines:

    - HIT_INFO_DEFINES              Marks that the rest of the defines are available.
    - HIT_INFO_USE_COMPRESSION      Use compressed format (64 bits instead of 128 bits).
    - HIT_INFO_TYPE_BITS            Bits needed to encode the hit type.
    - HIT_INFO_INSTANCE_ID_BITS     Bits needed to encode the instance ID of the hit.
    - HIT_INFO_PRIMITIVE_INDEX_BITS Bits needed to encode the primitive index of the hit.

    If a bit size define is zero, no bits are needed (the field has only one possible value = 0).
*/

#if HIT_INFO_USE_COMPRESSION
typedef uint2 PackedHitInfo;
#define PACKED_HIT_INFO_ZERO uint2(0, 0)
#else
typedef uint4 PackedHitInfo;
#define PACKED_HIT_INFO_ZERO uint4(0, 0, 0, 0)
#endif

bool IsValid(const PackedHitInfo packedHitInfo) { return packedHitInfo[0] != 0; }

struct GeometryHit;
struct TriangleHit;

// Since HitInfo is used by types below, has to be declared first in hlsl.
/** Polymorphic hit information type.
*/
struct HitInfo
{
#ifdef HIT_INFO_DEFINES
    static const uint kTypeBits = HIT_INFO_TYPE_BITS;
    static const uint kInstanceIDBits = HIT_INFO_INSTANCE_ID_BITS;
    static const uint kPrimitiveIndexBits = HIT_INFO_PRIMITIVE_INDEX_BITS;
#else
    static const uint kTypeBits = 1;
    static const uint kInstanceIDBits = 1;
    static const uint kPrimitiveIndexBits = 1;
#endif

    static const uint kTypeOffset = 32u - kTypeBits;
    static const uint kInstanceIDOffset = kPrimitiveIndexBits;

    static const uint kHeaderBits = kTypeBits + kInstanceIDBits + kPrimitiveIndexBits;

    PackedHitInfo data;

    /** Initialize empty (invalid) hit info.
    */
    void __init();
    static HitInfo make();

    /** Initialize hit info from a packed hit info.
        \param[in] packed Packed hit information.
    */
    void __init(const PackedHitInfo packed);
    static HitInfo make(const PackedHitInfo packed);

    /** Initialize hit info from a triangle hit.
        \param[in] triangleHit Triangle hit information.
    */
    void __init(const TriangleHit triangleHit);
    static HitInfo make(const TriangleHit triangleHit);

#if 0   // not ported yet

    /** Initialize hit info from a displaced triangle hit.
        \param[in] displacedTriangleHit Displaced triangle hit information.
    */
    void __init(const DisplacedTriangleHit displacedTriangleHit);
    static HitInfo make(const DisplacedTriangleHit displacedTriangleHit);

    /** Initialize hit info from a curve hit.
        \param[in] curveHit Curve hit information.
    */
    void __init(const CurveHit curveHit);
    static HitInfo make(const CurveHit curveHit);

    void __init(const SDFGridHit sdfGridHit);
    static HitInfo make(const SDFGridHit sdfGridHit);

    void __init(const VolumeHit volumeHit);
    static HitInfo make(const VolumeHit volumeHit);

#endif

    /** Return true if object represents a valid hit.
    */
    bool isValid();

    /** Return hit type.
    */
    HitType getType();

    /** Return the triangle hit.
        Only valid if type is HitType::Triangle.
    */
    TriangleHit getTriangleHit();

#if 0   // not ported yet

    /** Return the displaced triangle hit.
        Only valid if type is HitType::DisplacedTriangle.
    */
    DisplacedTriangleHit getDisplacedTriangleHit();

#endif

#if 0   // not ported yet

    /** Return the curve hit.
        Only valid if type is HitType::Curve.
    */
    CurveHit getCurveHit();

    /** Return the SDF grid hit.
        Only valid if type is HitType::SDFGrid.
    */
    SDFGridHit getSDFGridHit();

    /** Return the volume hit.
        Only valid if type is HitType::Volume.
    */
    VolumeHit getVolumeHit();

#endif

    /** Return the packed hit info.
        \return Packed hit info.
    */
    PackedHitInfo getData();

    static void packHeader(inout PackedHitInfo packed, const HitType type);

    static void packHeader(inout PackedHitInfo packed, const HitType type, const GeometryInstanceID instanceID, const uint primitiveIndex);
    static void unpackHeader(const PackedHitInfo packed, out GeometryInstanceID instanceID, out uint primitiveIndex);
};

/** Geometry hit information (base class).
*/
struct GeometryHit
{
    GeometryInstanceID instanceID;
    uint primitiveIndex;
    float2 barycentrics;

    /** Return the barycentric weights.
    */
    float3 getBarycentricWeights()
    {
        return float3(1.f - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    }
};

/** Triangle hit information.

    Encoding (without compression):
    | header  | barycentrics.x | barycentrics.y |
    | 64 bits | 32 bit float   | 32 bit float   |

    Encoding (with compression):
    | header     | barycentrics.x | barycentrics.y |
    | 32 bits    | 16 bit unorm   | 16 bit unorm   |
*/
struct TriangleHit : GeometryHit
{
    void __init(const PackedHitInfo packed)
    {
#if SCENE_HAS_GEOMETRY_TYPE(GEOMETRY_TYPE_TRIANGLE_MESH)
        HitInfo::unpackHeader(packed, instanceID, primitiveIndex);
#if HIT_INFO_USE_COMPRESSION
        barycentrics = unpackUnorm2x16(packed[1]);
#else
        barycentrics.x = asfloat(packed[2]);
        barycentrics.y = asfloat(packed[3]);
#endif
#else
        #error type not supported?
        //this = {};
#endif
    }
    static TriangleHit make(const PackedHitInfo packed)  { TriangleHit ret; ret.__init(packed); return ret; }
    static TriangleHit make(const uint instanceIndex, const uint geometryIndex, const uint primitiveIndex, float2 barycentrics) 
    { 
        TriangleHit ret; 
        ret.instanceID      = GeometryInstanceID::make( InstanceIndex(), GeometryIndex() );
        ret.primitiveIndex  = primitiveIndex;
        ret.barycentrics    = barycentrics;
        return ret;
    }

    PackedHitInfo pack()
    {
        PackedHitInfo packed = PACKED_HIT_INFO_ZERO;
#if SCENE_HAS_GEOMETRY_TYPE(GEOMETRY_TYPE_TRIANGLE_MESH)
        HitInfo::packHeader(packed, HitType::Triangle, instanceID, primitiveIndex);
#if HIT_INFO_USE_COMPRESSION
        packed[1] = packUnorm2x16_unsafe(barycentrics);
#else
        packed[2] = asuint(barycentrics.x);
        packed[3] = asuint(barycentrics.y);
#endif
#endif
        return packed;
    }
};

#if 0   // not ported yet

/** Displaced triangle hit information.

    Encoding:
    | header  | barycentrics.x | barycentrics.y | displacement |
    | 64 bits | 24 bit unorm   | 24 bit unorm   | 16 bit float |
*/
struct DisplacedTriangleHit : GeometryHit
{
    float displacement;

    void __init(const PackedHitInfo packed)
    {
#if SCENE_HAS_GEOMETRY_TYPE(GEOMETRY_TYPE_DISPLACED_TRIANGLE_MESH)
        HitInfo::unpackHeader(packed, instanceID, primitiveIndex);
        const uint ux = (packed[2] >> 8);
        const uint uy = ((packed[2] & 0xff) << 16) | (packed[3] >> 16);
        barycentrics = float2(ux, uy) * (1.f / 16777215);
        displacement = f16tof32(packed[3]);
#else
        this = {};
#endif
    }
    static DisplacedTriangleHit make(const PackedHitInfo packed)  { DisplacedTriangleHit ret; ret.__init(packed); return ret; }

    PackedHitInfo pack()
    {
        PackedHitInfo packed = {};
#if SCENE_HAS_GEOMETRY_TYPE(GEOMETRY_TYPE_DISPLACED_TRIANGLE_MESH)
        HitInfo::packHeader(packed, HitType::DisplacedTriangle, instanceID, primitiveIndex);
        const uint2 u = trunc(barycentrics * 16777215.f + 0.5f);
        packed[2] = (u.x << 8) | (u.y >> 16);
        packed[3] = (u.y << 16) | f32tof16(displacement);
#endif
        return packed;
    }
};

#endif


#if 0   // not ported yet

/** Curve hit information.

    Encoding:
    | header  | barycentrics.x | barycentrics.y |
    | 64 bits | 32 bit float   | 32 bit float   |
*/
struct CurveHit : GeometryHit
{
    void __init(const PackedHitInfo packed)
    {
#if SCENE_HAS_GEOMETRY_TYPE(GEOMETRY_TYPE_CURVE)
        HitInfo::unpackHeader(packed, instanceID, primitiveIndex);
        barycentrics.x = asfloat(packed[2]);
        barycentrics.y = asfloat(packed[3]);
#else
        // this = {};
#endif
    }
    static CurveHit make(const PackedHitInfo packed)  { CurveHit ret; ret.__init(packed); return ret; }

    PackedHitInfo pack()
    {
        PackedHitInfo packed = {};
#if SCENE_HAS_GEOMETRY_TYPE(GEOMETRY_TYPE_CURVE)
        HitInfo::packHeader(packed, HitType::Curve, instanceID, primitiveIndex);
        packed[2] = asuint(barycentrics.x);
        packed[3] = asuint(barycentrics.y);
#endif
        return packed;
    }
};

/** SDF grid hit information.

    Encoding:
    | header  | extra  | extra  |
    | 64 bits | 32 bit | 32 bit |
*/
struct SDFGridHit
{
    GeometryInstanceID instanceID;
    SDFGridHitData hitData;

    void __init(const PackedHitInfo packed)
    {
#if SCENE_HAS_GEOMETRY_TYPE(GEOMETRY_TYPE_SDF_GRID)
        uint primitiveData;
        HitInfo::unpackHeader(packed, instanceID, primitiveData);

#ifdef FALCOR_INTERNAL

#if SCENE_SDF_GRID_IMPLEMENTATION == SCENE_SDF_GRID_IMPLEMENTATION_NDSDF
        hitData.packedVoxelUnitCoords = primitiveData;
        hitData.locationCode.x = packed[2];
        hitData.locationCode.y = packed[3];
#elif SCENE_SDF_GRID_IMPLEMENTATION == SCENE_SDF_GRID_IMPLEMENTATION_SVS
        hitData.primitiveID = primitiveData;
        hitData.packedVoxelUnitCoords = packed[2];
#elif SCENE_SDF_GRID_IMPLEMENTATION == SCENE_SDF_GRID_IMPLEMENTATION_SBS
#if SCENE_SDF_SBS_VIRTUAL_BRICK_COORDS_BITS + SCENE_SDF_SBS_BRICK_LOCAL_VOXEL_COORDS_BITS <= 30
        hitData.packedVoxelCoords = primitiveData;
        hitData.packedVoxelUnitCoords = packed[2];
#else
        hitData.packedVirtualBrickCoords = primitiveData;
        hitData.packedBrickLocalVoxelCoords = packed[2];
        hitData.packedVoxelUnitCoords = packed[3];
#endif
#elif SCENE_SDF_GRID_IMPLEMENTATION == SCENE_SDF_GRID_IMPLEMENTATION_SVO
        hitData.svoIndex = primitiveData;
        hitData.packedVoxelUnitCoords = packed[2];
#endif

#else // FALCOR_INTERNAL

#if SCENE_SDF_GRID_IMPLEMENTATION == SCENE_SDF_GRID_IMPLEMENTATION_NDSDF
        hitData.lod = primitiveData;
        hitData.hitT = asfloat(packed[2]);
#elif SCENE_SDF_GRID_IMPLEMENTATION == SCENE_SDF_GRID_IMPLEMENTATION_SVS || SCENE_SDF_GRID_IMPLEMENTATION == SCENE_SDF_GRID_IMPLEMENTATION_SBS
        hitData.primitiveID = primitiveData;
        hitData.hitT = asfloat(packed[2]);
#elif SCENE_SDF_GRID_IMPLEMENTATION == SCENE_SDF_GRID_IMPLEMENTATION_SVO
        hitData.svoIndex = primitiveData;
        hitData.hitT = asfloat(packed[2]);
#endif

#endif // FALCOR_INTERNAL

#else
		#error not implemented
        // this = {};
#endif // SCENE_HAS_GEOMETRY_TYPE(GEOMETRY_TYPE_SDF_GRID)
    }
    static SDFGridHit make(const PackedHitInfo packed)  { SDFGridHit ret; ret.__init(packed); return ret; }

    PackedHitInfo pack()
    {
        PackedHitInfo packed = {};

#if SCENE_HAS_GEOMETRY_TYPE(GEOMETRY_TYPE_SDF_GRID)
        uint primitiveData = {};
#ifdef FALCOR_INTERNAL

#if SCENE_SDF_GRID_IMPLEMENTATION == SCENE_SDF_GRID_IMPLEMENTATION_NDSDF
        primitiveData = hitData.packedVoxelUnitCoords;
        packed[2] = hitData.locationCode.x;
        packed[3] = hitData.locationCode.y;
#elif SCENE_SDF_GRID_IMPLEMENTATION == SCENE_SDF_GRID_IMPLEMENTATION_SVS
        primitiveData = hitData.primitiveID;
        packed[2] = hitData.packedVoxelUnitCoords;
#elif SCENE_SDF_GRID_IMPLEMENTATION == SCENE_SDF_GRID_IMPLEMENTATION_SBS
#if SCENE_SDF_SBS_VIRTUAL_BRICK_COORDS_BITS + SCENE_SDF_SBS_BRICK_LOCAL_VOXEL_COORDS_BITS <= 30
        primitiveData = hitData.packedVoxelCoords;
        packed[2] = hitData.packedVoxelUnitCoords;
#else
        primitiveData = hitData.packedVirtualBrickCoords;
        packed[2] = hitData.packedBrickLocalVoxelCoords;
        packed[3] = hitData.packedVoxelUnitCoords;
#endif
#elif SCENE_SDF_GRID_IMPLEMENTATION == SCENE_SDF_GRID_IMPLEMENTATION_SVO
        primitiveData = hitData.svoIndex;
        packed[2] = hitData.packedVoxelUnitCoords;
#endif

#else // FALCOR_INTERNAL

#if SCENE_SDF_GRID_IMPLEMENTATION == SCENE_SDF_GRID_IMPLEMENTATION_NDSDF
        primitiveData = hitData.lod;
        packed[2] = asuint(hitData.hitT);
#elif SCENE_SDF_GRID_IMPLEMENTATION == SCENE_SDF_GRID_IMPLEMENTATION_SVS || SCENE_SDF_GRID_IMPLEMENTATION == SCENE_SDF_GRID_IMPLEMENTATION_SBS
        primitiveData = hitData.primitiveID;
        packed[2] = asuint(hitData.hitT);
#elif SCENE_SDF_GRID_IMPLEMENTATION == SCENE_SDF_GRID_IMPLEMENTATION_SVO
        primitiveData = hitData.svoIndex;
        packed[2] = asuint(hitData.hitT);
#endif

#endif // FALCOR_INTERNAL

        HitInfo::packHeader(packed, HitType::SDFGrid, instanceID, primitiveData);
#endif // SCENE_HAS_GEOMETRY_TYPE(GEOMETRY_TYPE_SDF_GRID)

        return packed;
    }
};

#endif

#if 0   // not ported yet

/** Volume hit information.

    Encoding (without compression):
    | header  | t            | g            |
    | 64 bits | 32 bit float | 32 bit float |

    Encoding (with compression):
    | header  | g            | t            |
    | 16 bits | 16 bit float | 32 bit float |
*/
struct VolumeHit
{
    float t;
    float g;

    void __init(const PackedHitInfo packed)
    {
#if HIT_INFO_USE_COMPRESSION
        t = asfloat(packed[1]);
        g = f16tof32(packed[0] & 0xffff);
#else
        t = asfloat(packed[1]);
        g = asfloat(packed[2]);
#endif
    }
    static VolumeHit make(const PackedHitInfo packed)  { VolumeHit ret; ret.__init(packed); return ret; }

    PackedHitInfo pack()
    {
        PackedHitInfo packed = {};
        HitInfo::packHeader(packed, HitType::Volume);
#if HIT_INFO_USE_COMPRESSION
        packed[1] = asuint(t);
        packed[0] |= f32tof16(g) & 0xffff;
#else
        packed[1] = asuint(t);
        packed[2] = asuint(g);
#endif
        return packed;
    }
};

#endif

// Definition for HitInfo functions
void HitInfo::__init()
{
    data = PACKED_HIT_INFO_ZERO;
}
static HitInfo HitInfo::make()                                  
{ 
    HitInfo ret; ret.__init(); return ret; 
}

/** Initialize hit info from a packed hit info.
    \param[in] packed Packed hit information.
*/
void HitInfo::__init(const PackedHitInfo packed)
{
    data = packed;
}
static HitInfo HitInfo::make(const PackedHitInfo packed)
{ 
    HitInfo ret; ret.__init(packed); return ret; 
}

/** Initialize hit info from a triangle hit.
    \param[in] triangleHit Triangle hit information.
*/
void HitInfo::__init(const TriangleHit triangleHit)
{
    data = triangleHit.pack();
}
static HitInfo HitInfo::make(const TriangleHit triangleHit)
{ 
    HitInfo ret; ret.__init(triangleHit); return ret; 
}

#if 0   // not ported yet

/** Initialize hit info from a displaced triangle hit.
    \param[in] displacedTriangleHit Displaced triangle hit information.
*/
void HitInfo::__init(const DisplacedTriangleHit displacedTriangleHit)
{
    data = displacedTriangleHit.pack();
}
static HitInfo HitInfo::make(const DisplacedTriangleHit displacedTriangleHit)  
{ 
    HitInfo ret; ret.__init(displacedTriangleHit); return ret; 
}

/** Initialize hit info from a curve hit.
    \param[in] curveHit Curve hit information.
*/
HitInfo::__init(const CurveHit curveHit)
{
    data = curveHit.pack();
}
static HitInfo HitInfo::make(const CurveHit curveHit)
{ 
    HitInfo ret; ret.__init(curveHit); return ret;
}

/** Initialize hit info from a SDF grid hit.
    \param[in] sdfGridHit Curve hit information.
*/
HitInfo::__init(const SDFGridHit sdfGridHit)
{
    data = sdfGridHit.pack();
}
static HitInfo HitInfo::make(const SDFGridHit sdfGridHit)
{ 
    HitInfo ret; ret.__init(sdfGridHit); return ret; 
}

/** Initialize hit info from a volume hit.
    \param[in] volumeHit Volume hit information.
*/
HitInfo::__init(const VolumeHit volumeHit)
{
    data = volumeHit.pack();
}
static HitInfo HitInfo::make(const VolumeHit volumeHit)  
{ 
    HitInfo ret; ret.__init(volumeHit); return ret; 
}

#endif

/** Return true if object represents a valid hit.
*/
bool HitInfo::isValid()
{
    return getType() != HitType::None;
}

/** Return hit type.
*/
HitType HitInfo::getType()
{
    return (HitType)(data[0] >> kTypeOffset);
}

/** Return the triangle hit.
    Only valid if type is HitType::Triangle.
*/
TriangleHit HitInfo::getTriangleHit()
{
    return TriangleHit::make(data);
}

#if 0   // not ported yet

/** Return the displaced triangle hit.
    Only valid if type is HitType::DisplacedTriangle.
*/
DisplacedTriangleHit HitInfo::getDisplacedTriangleHit()
{
    return DisplacedTriangleHit::make(data);
}

#endif

#if 0   // not ported yet

/** Return the curve hit.
    Only valid if type is HitType::Curve.
*/
CurveHit HitInfo::getCurveHit()
{
    return CurveHit::make(data);
}

/** Return the SDF grid hit.
    Only valid if type is HitType::SDFGrid.
*/
SDFGridHit HitInfo::getSDFGridHit()
{
    return SDFGridHit::make(data);
}

/** Return the volume hit.
    Only valid if type is HitType::Volume.
*/
VolumeHit HitInfo::getVolumeHit()
{
    return VolumeHit::make(data);
}

#endif

/** Return the packed hit info.
    \return Packed hit info.
*/
PackedHitInfo HitInfo::getData()
{
    return data;
}

static void HitInfo::packHeader(inout PackedHitInfo packed, const HitType type)
{
    packed[0] = ((uint)type) << kTypeOffset;
}

static void HitInfo::packHeader(inout PackedHitInfo packed, const HitType type, const GeometryInstanceID instanceID, const uint primitiveIndex)
{
    if (kHeaderBits <= 32)
    {
        packed[0] = (((uint)type) << kTypeOffset) | (instanceID.data << kInstanceIDOffset) | primitiveIndex;
    }
    else
    {
        packed[0] = (((uint)type) << kTypeOffset) | instanceID.data;
        packed[1] = primitiveIndex;
    }
}

static void HitInfo::unpackHeader(const PackedHitInfo packed, out GeometryInstanceID instanceID, out uint primitiveIndex)
{
    if (kHeaderBits <= 32)
    {
        instanceID.data = (packed[0] >> kInstanceIDOffset) & ((1u << kInstanceIDBits) - 1u);
        primitiveIndex = packed[0] & ((1u << kPrimitiveIndexBits) - 1u);
    }
    else
    {
        instanceID.data = packed[0] & ((1u << kInstanceIDBits) - 1u);
        primitiveIndex = packed[1];
    }
}


#endif // __HIT_INFO_HLSLI__