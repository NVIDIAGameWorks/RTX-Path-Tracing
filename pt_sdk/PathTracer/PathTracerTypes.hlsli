/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __PATH_TRACER_TYPES_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __PATH_TRACER_TYPES_HLSLI__

#include "Config.hlsli"    

#include "Utils/Math/Ray.hlsli"
#include "Utils/Geometry/GeometryHelpers.hlsli"
#include "Rendering/Materials/TexLODHelpers.hlsli"
#include "PathState.hlsli"
#include "PathTracerParams.hlsli"
#include "Scene/Material/TextureSampler.hlsli"
#include "Scene/ShadingData.hlsli"
#include "Scene/Material/ShadingUtils.hlsli"
#include "Rendering/Materials/LobeType.hlsli"
#include "Rendering/Materials/IBSDF.hlsli"
#include "Rendering/Materials/StandardBSDF.hlsli"
#include "PathState.hlsli"
#include "PathTracerParams.hlsli"
#include "ShaderDebug.hlsli"
#include "PathTracerHelpers.hlsli"
#include "PathPayload.hlsli"

#include "StablePlanes.hlsli"

#if ACTIVE_LOD_TEXTURE_SAMPLER == LOD_TEXTURE_SAMPLER_EXPLICIT
    #define ActiveTextureSampler ExplicitLodTextureSampler
#elif ACTIVE_LOD_TEXTURE_SAMPLER == LOD_TEXTURE_SAMPLER_RAY_CONES
    #define ActiveTextureSampler ExplicitRayConesLodTextureSampler
#else
    #error please specify texture LOD sampler
#endif

/** Types of samplable lights.
*/
enum class PathLightType : uint32_t // was PathTracer::LightType
{
    EnvMap      = 0,
    Emissive    = 1,
    Analytic    = 2
};

/** Holds auxiliary data storage locations for debugging, denoising and etc.
*/
struct AUXContext
{
    PathTracerConstants     ptConsts;
    DebugContext            debug;
    StablePlanesContext     stablePlanes;
    uint2                   pixelPos;
    uint                    pixelStorageIndex;
};

/** All surface data returned by the Bridge::loadSurface
*/
struct SurfaceData
{
    ShadingData sd;
    ActiveBSDF bsdf;
    float3 prevPosW;
    float interiorIoR; // a.k.a. material IoR
    static SurfaceData make( /*VertexData vd, */ShadingData sd, ActiveBSDF bsdf, float3 prevPosW, float interiorIoR )    { SurfaceData ret; /*ret.vd = vd; */ret.sd = sd; ret.bsdf = bsdf; ret.prevPosW = prevPosW; ret.interiorIoR = interiorIoR; return ret; }

    static SurfaceData make()
    {
        SurfaceData d;
        d.sd = ShadingData::make();
        d.bsdf = ActiveBSDF::make();
        d.prevPosW = 0;
        d.interiorIoR = 0;
        return d;
    }
};

/** Describes a light sample.
*/
struct PathLightSample  // was PathTracer::LightSample
{
    float3  Li;         ///< Incident radiance at the shading point (unshadowed). This is already divided by the pdf.
    float   pdf;        ///< Pdf with respect to solid angle at the shading point.
    float3  origin;     ///< Ray origin for visibility evaluation (offseted to avoid self-intersection).
    float   distance;   ///< Ray distance for visibility evaluation (shortened to avoid self-intersection).
    float3  dir;        ///< Ray direction for visibility evaluation (normalized).
    uint    lightType;  ///< Light type this sample comes from (PathLightType casted to uint).

    Ray getVisibilityRay() { return Ray::make(origin, dir, 0.f, distance); }

    static PathLightSample make() { PathLightSample ret; ret.Li = float3(0,0,0); ret.pdf = 0; ret.origin = float3(0,0,0); ret.distance = 0; ret.dir = float3(0,0,0); ret.lightType = 0; return ret; }
};

/** Describes a path vertex.
*/
struct PathVertex
{
    uint index;         ///< Vertex index (0 = camera, 1 = primary hit, 2 = secondary hit, etc.).
    float3 pos;         ///< Vertex position.
    float3 normal;      ///< Shading normal at the vertex (zero if not on a surface).
    float3 faceNormal;  ///< Geometry normal at the vertex (zero if not on a surface).

    /** Initializes a path vertex.
        \param[in] index Vertex index.
        \param[in] pos Vertex position.
        \param[in] normal Shading normal.
        \param[in] faceNormal Geometry normal.
    */
    void __init(uint index, float3 pos, float3 normal = float3(0,0,0), float3 faceNormal = float3(0,0,0))
    {
        this.index = index;
        this.pos = pos;
        this.normal = normal;
        this.faceNormal = faceNormal;
    }

    static PathVertex make(uint index, float3 pos, float3 normal = float3(0,0,0), float3 faceNormal = float3(0,0,0))   { PathVertex ret; ret.__init(index, pos, normal, faceNormal); return ret; }

    /** Get position with offset applied in direction of the geometry normal to avoid self-intersection
        for visibility rays.
        \param[in] rayDir Direction of the visibility ray (does not need to be normalized).
        \return Returns the offseted position.
    */
    float3 getRayOrigin(float3 rayDir)
    {
        return computeRayOrigin(pos, dot(faceNormal, rayDir) >= 0 ? faceNormal : -faceNormal);
    }
};

enum class MISHeuristic : uint32_t
{
    Balance     = 0,    ///< Balance heuristic.
    PowerTwo    = 1,    ///< Power heuristic (exponent = 2.0).
    PowerExp    = 2,    ///< Power heuristic (variable exponent).
};

struct VisibilityPayload
{
    uint missed;
    static VisibilityPayload make( ) { VisibilityPayload ret; ret.missed = 0; return ret; }
};

struct OptimizationHints
{
    bool    NoTextures;
    bool    NoTransmission;
    bool    OnlyDeltaLobes;
    bool    NoDeltaOnlySurfaces;
    uint    SortKey;            // for SER

    static OptimizationHints NoHints(uint sortKey = 0)
    {
        OptimizationHints ret;
        ret.NoTextures = false;
        ret.NoTransmission = false;
        ret.OnlyDeltaLobes = false;
        ret.NoDeltaOnlySurfaces = false;
        ret.SortKey = sortKey;
        return ret;
    }
    
    static OptimizationHints make(bool noTextures, bool noTransmission, bool onlyDeltaLobes, bool noDeltaOnlySurfaces, uint sortKey = 0)
    {
        OptimizationHints ret;
        ret.NoTextures = noTextures;
        ret.NoTransmission = noTransmission;
        ret.OnlyDeltaLobes = onlyDeltaLobes;
        ret.NoDeltaOnlySurfaces = noDeltaOnlySurfaces;
        ret.SortKey = sortKey;
        return ret;
    }
};

#endif // __PATH_TRACER_TYPES_HLSLI__