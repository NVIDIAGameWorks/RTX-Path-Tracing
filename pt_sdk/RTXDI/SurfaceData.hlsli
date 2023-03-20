/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef SURFACE_DATA_HLSLI
#define SURFACE_DATA_HLSLI

#include "../PathTracerBridgeDonut.hlsli"
#include "../PathTracer/PathTracer.hlsli"

#include "ShaderParameters.h"
#include "HelperFunctions.hlsli"

struct PathTracerSurfaceData
{
	ShadingData sd;
	StandardBSDF bsdf;
	float viewDepth;
	uint planeHash;

	static PathTracerSurfaceData create(const ShadingData sd, const StandardBSDF bsdf, float viewDepth, const uint planeHash)
	{
		PathTracerSurfaceData surface;
		surface.sd = sd;
		surface.bsdf = bsdf;
		surface.viewDepth = viewDepth;
		surface.planeHash = planeHash;

		return surface;
	}

	static PathTracerSurfaceData makeEmpty()
	{
		PathTracerSurfaceData surface = (PathTracerSurfaceData)0;
		surface.viewDepth = BACKGROUND_DEPTH;

		return surface;
	}

	bool isEmpty()
	{
	    return viewDepth == BACKGROUND_DEPTH;
	}
};


//To do, delete below here

static PackedSurfaceData makeEmptyPackedSurface()
{
	PackedSurfaceData packedSurface;
	packedSurface.position = (0, 0, 0);
	packedSurface.viewDepth = BACKGROUND_DEPTH;
	packedSurface.packedNormal = 0;
	packedSurface.packedWeights = 0;
	packedSurface._pad = (0, 0);

	return packedSurface;
}

struct RtxdiSurfaceData
{
	float3 position;
	float viewDepth;
	float3 normal;
	float diffuse;
	float specular;
	float roughness;
	float diffuseProbability;
	float3 viewDir; // Not stored in PackedSurfaceData

	// Pack RtxdiSurfaceData to PackedSurfaceData
	PackedSurfaceData pack()
	{
		PackedSurfaceData packedSurface;
		packedSurface.position = position;
		packedSurface.viewDepth = viewDepth;
		packedSurface.packedNormal = packNormal2x16(normal);
		packedSurface.packedWeights = (Pack_R8_UFLOAT(diffuseProbability) << 24)
			| (Pack_R8_UFLOAT(roughness) << 16)
			| (Pack_R8_UFLOAT(specular) << 8)
			| Pack_R8_UFLOAT(diffuse);
		packedSurface._pad = (0, 0);

		return packedSurface;
	}

	// Make RtxdiSurfaceData from PackedSurfaceData
	static RtxdiSurfaceData make(PackedSurfaceData packedSurface, float3 viewDir)
	{
		RtxdiSurfaceData surface; 
		surface.position = packedSurface.position;
		surface.viewDepth = packedSurface.viewDepth;
		surface.normal = unpackNormal2x16(packedSurface.packedNormal);
		surface.diffuse = Unpack_R8_UFLOAT(packedSurface.packedWeights);
		surface.specular = Unpack_R8_UFLOAT(packedSurface.packedWeights >> 8);
		surface.roughness = Unpack_R8_UFLOAT(packedSurface.packedWeights >> 16);
		surface.diffuseProbability = Unpack_R8_UFLOAT(packedSurface.packedWeights >> 24);
		surface.viewDir = viewDir;

		return surface;
	}

	// Create a surface from the input
	static RtxdiSurfaceData create(const float3 position, const float3 cameraPosition, 
		const float3 normal, const float3 diffuse, const float3 specular, 
		const float specularRoughness)
	{
		RtxdiSurfaceData sd;
		sd.position = position;
		sd.normal = normal;
		sd.viewDepth = distance(cameraPosition, position);

		float diffuseWeight = calcLuminance(diffuse);
		float specularWeight = calcLuminance(specular);
		float sumWeights = diffuseWeight + specularWeight;
		float diffuseProb = sumWeights < 1e-7f ? 1.f : diffuseWeight / sumWeights;

		sd.diffuse = diffuseWeight;
		sd.specular = specularWeight;
		sd.diffuseProbability = diffuseProb;
		sd.roughness = specularRoughness;

		return sd;
	}

};

bool isValidPixelPosition(int2 pixelPosition)
{
    return all(pixelPosition >= 0) && pixelPosition.x < g_Const.ptConsts.imageWidth && pixelPosition.y < g_Const.ptConsts.imageHeight;
}

// Load a surface from the current or previous vbuffer at the specified pixel postion 
// Pixel positions may be out of bounds or negative, in which case the function is supposed to 
// return an invalid surface
PathTracerSurfaceData getGBufferSurface(int2 pixelPosition, bool previousFrame, OptimizationHints optimizationHints)
{
	//Return invalid surface data if pixel is out of bounds
	if (!isValidPixelPosition(pixelPosition))
	    return PathTracerSurfaceData::makeEmpty();

    // Note: at the moment this goes through the whole process of reconstructing the surface from hitInfo; in the 
    // future this will be replaced by a GBuffer-like approach where all of the required data is stored in textures.

    // Init globals
	uint sampleIndex = Bridge::getSampleIndex();
	DebugContext debug;
	debug.Init(pixelPosition, sampleIndex, g_Const.debug, u_FeedbackBuffer, u_DebugLinesBuffer, u_DebugDeltaPathTree, u_DeltaPathSearchStack, u_DebugVizOutput);
    StablePlanesContext stablePlanes = StablePlanesContext::make(pixelPosition, u_StablePlanesHeader, u_StablePlanesBuffer, u_PrevStablePlanesHeader, u_PrevStablePlanesBuffer, u_StableRadiance, u_SecondarySurfaceRadiance, g_Const.ptConsts);

    // Figure out the shading plane
    uint dominantStablePlaneIndex = stablePlanes.LoadDominantIndex();
    PackedHitInfo packedHitInfo; float3 rayDir; uint vertexIndex; uint SERSortKey; uint stableBranchID; float sceneLength; float3 pathThp; float3 motionVectors;
    stablePlanes.LoadStablePlane(pixelPosition, dominantStablePlaneIndex, previousFrame, vertexIndex, packedHitInfo, SERSortKey, stableBranchID, rayDir, sceneLength, pathThp, motionVectors);
	
    uint planeHash = (dominantStablePlaneIndex << 16) | vertexIndex;

    const HitInfo hit = HitInfo(packedHitInfo);
    if ((hit.isValid() && hit.getType() == HitType::Triangle))
    {
        // Load shading surface
        const Ray cameraRay = Bridge::computeCameraRay( pixelPosition );
        RayCone rayCone = RayCone::make(0, g_Const.ptConsts.camera.pixelConeSpreadAngle).propagateDistance(sceneLength);
        const SurfaceData bridgedData = Bridge::loadSurface(OptimizationHints::NoHints(), TriangleHit::make(packedHitInfo), rayDir, rayCone, Bridge::getPathTracerParams(), vertexIndex, debug);
        const ShadingData sd    = bridgedData.sd;
        const ActiveBSDF bsdf   = bridgedData.bsdf;
        BSDFProperties bsdfProperties = bsdf.getProperties(sd);

#if 1
        float viewDepth = g_Const.ptConsts.camera.nearZ+dot( cameraRay.dir * sceneLength, normalize(g_Const.ptConsts.camera.directionW) );
#else // same as above - useful for testing - for viz use `debug.DrawDebugViz( pixelPosition, float4( frac(viewDepth).xxx, 1) );`
        float3 virtualWorldPos = cameraRay.origin + cameraRay.dir * sceneLength;
        float viewDepth = mul(float4(virtualWorldPos, 1), g_Const.view.matWorldToView).z;
#endif

		uint lobes = bsdf.getLobes(sd);
		if ((lobes & (uint)LobeType::NonDeltaReflection) != 0)
            return PathTracerSurfaceData::create(sd, bsdf, viewDepth, planeHash);
    }

	return PathTracerSurfaceData::makeEmpty();
}

#endif //SURFACE_DATA_HLSLI