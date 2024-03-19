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

#include "Config.h"    

#include "Utils/Math/Ray.hlsli"
#include "Rendering/Materials/TexLODHelpers.hlsli"
#include "PathState.hlsli"
#include "Scene/Material/TextureSampler.hlsli"
#include "Scene/ShadingData.hlsli"
#include "Scene/Material/ShadingUtils.hlsli"
#include "Rendering/Materials/LobeType.hlsli"
#include "Rendering/Materials/IBSDF.hlsli"
#include "Rendering/Materials/StandardBSDF.hlsli"
#include "PathState.hlsli"
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

namespace PathTracer
{
    /** Holds path tracer shader working data for state, settings, debugging, denoising and etc. Everything that is shared across (DispatchRays/Compute) draw call, between all pixels, 
        but also some pixel-specific stuff (like pixelPos heh). It's what a PathTracer instance would store if it could be an OOP object.
    */
    struct WorkingContext
    {
        PathTracerConstants     ptConsts;
        DebugContext            debug;
        StablePlanesContext     stablePlanes;
        uint2                   pixelPos;
        uint                    padding0;
        uint                    padding1;
    };

    /** All surface data returned by the Bridge::loadSurface
    */
    struct SurfaceData
    {
        ShadingData     shadingData;
        ActiveBSDF      bsdf;
        float3          prevPosW;
        float           interiorIoR; // a.k.a. material IoR
        static SurfaceData make( /*VertexData vd, */ShadingData shadingData, ActiveBSDF bsdf, float3 prevPosW, float interiorIoR )    
        { 
            SurfaceData ret; 
            ret.shadingData     = shadingData; 
            ret.bsdf            = bsdf; 
            ret.prevPosW        = prevPosW; 
            ret.interiorIoR     = interiorIoR; 
            return ret; 
        }
    
        static SurfaceData make()
        {
            SurfaceData d;
            d.shadingData   = ShadingData::make();
            d.bsdf          = ActiveBSDF::make();
            d.prevPosW      = 0;
            d.interiorIoR   = 0;
            return d;
        }
    };

    /** Describes a light sample.
        It is considered in the context of a shaded surface point, from which Distance and Direction to light sample are computed.
        In case of emissive triangle light source, it is adviseable to compute anti-self-intersection offset before computing
        distance and direction, even though distance shortening is needed anyways for shadow rays due to precision issues.
        Use ComputeVisibilityRay to correctly compute surface offset.
    */
    struct PathLightSample  // was PathTracer::LightSample in Falcor
    {
        float3  Li;         ///< Incident radiance at the shading point (unshadowed). This is already divided by the pdf.
        float   Pdf;        ///< Pdf with respect to solid angle at the shading point.
        float   Distance;   ///< Ray distance for visibility evaluation (NOT shortened yet to avoid self-intersection). Ray starts at shading surface.
        float3  Direction;  ///< Ray direction (normalized). Ray starts at shading surface.
        
        // Computes shading surface visibility ray starting position with an offset to avoid self intersection at source, and a
        // shortening offset to avoid self-intersection at the light source end. 
        // Optimal selfIntersectionShorteningK default found empirically.
        Ray ComputeVisibilityRay(float3 surfaceWorldPos, float3 surfaceFaceNormal, const float selfIntersectionShorteningK = 0.9985)
        { 
            surfaceWorldPos = ComputeRayOrigin(surfaceWorldPos, surfaceFaceNormal, Direction);
            return Ray::make(surfaceWorldPos, Direction, 0.0, Distance*selfIntersectionShorteningK); 
        }

        static PathLightSample make() 
        { 
            PathLightSample ret; 
            ret.Li = float3(0,0,0); 
            ret.Pdf = 0; 
            ret.Distance = 0; 
            ret.Direction = float3(0,0,0); 
            return ret; 
        }
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
            return ComputeRayOrigin(pos, dot(faceNormal, rayDir) >= 0 ? faceNormal : -faceNormal);
        }
    };

    enum class MISHeuristic : uint32_t
    {
        Balance     = 0,    ///< Balance heuristic.
        PowerTwo    = 1,    ///< Power heuristic (exponent = 2.0).
        PowerExp    = 2,    ///< Power heuristic (variable exponent).
    };

    // Output part of the interface to the path tracer - this will likely change over time.
    struct NEEResult
    {
        bool    Valid;
        
#if RTXPT_DIFFUSE_SPECULAR_SPLIT
        float3  DiffuseRadiance;
        float3  SpecularRadiance;
#else
    #error current denoiser requires RTXPT_DIFFUSE_SPECULAR_SPLIT
#endif
        float   RadianceSourceDistance;         // consider splitting into specular and diffuse
        float   ScatterEmissiveMISWeight;       // MIS weight computed for scatter counterpart (packed to fp16 in path payload) - this is a placeholder for now, it might actually be converted to hold pdf of emissive sample for later computation on surface
        float   ScatterEnvironmentMISWeight;    // MIS weight computed for scatter counterpart (packed to fp16 in path payload)
        
        // initialize to empty
        static NEEResult empty(bool useEmissiveLights, bool useEnvLights) 
        { 
            NEEResult ret; 
            ret.ScatterEmissiveMISWeight    = useEmissiveLights ? 1.0 : 0.0;
            ret.ScatterEnvironmentMISWeight = useEnvLights ? 1.0 : 0.0;
            ret.Valid = false; 
            return ret; 
        }    
    };
    
    struct VisibilityPayload
    {
        uint missed;
        static VisibilityPayload make( ) 
        { 
            VisibilityPayload ret; 
            ret.missed = 0; 
            return ret; 
        }
    };

    struct OptimizationHints
    {
        bool    NoTextures;
        bool    NoTransmission;
        bool    OnlyDeltaLobes;
        bool    NoDeltaOnlySurfaces;
        uint    SERSortKey;

        static OptimizationHints NoHints(uint sortKey = 0)
        {
            OptimizationHints ret;
            ret.NoTextures = false;
            ret.NoTransmission = false;
            ret.OnlyDeltaLobes = false;
            ret.NoDeltaOnlySurfaces = false;
            ret.SERSortKey = sortKey;
            return ret;
        }
    
        static OptimizationHints make(bool noTextures, bool noTransmission, bool onlyDeltaLobes, bool noDeltaOnlySurfaces, uint sortKey = 0)
        {
            OptimizationHints ret;
            ret.NoTextures = noTextures;
            ret.NoTransmission = noTransmission;
            ret.OnlyDeltaLobes = onlyDeltaLobes;
            ret.NoDeltaOnlySurfaces = noDeltaOnlySurfaces;
            ret.SERSortKey = sortKey;
            return ret;
        }
    };

    struct ScatterResult
    {
        bool    Valid;
        bool    IsDelta;
        bool    IsTransmission;
        float   Pdf;
        float3  Dir;
        
        static ScatterResult empty() 
        { 
            ScatterResult ret; 
            ret.Valid = false; 
            ret.IsDelta = false; 
            ret.IsTransmission = false; 
            ret.Pdf = 0.0; 
            ret.Dir = float3(0,0,0); 
            return ret; 
        }
    };
    
}


#endif // __PATH_TRACER_TYPES_HLSLI__