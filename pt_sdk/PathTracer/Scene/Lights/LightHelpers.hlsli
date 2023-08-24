/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

/** This file contains helper functions for analytic light source sampling.
    The code supports Falcor's analytic point, directional, and area lights,
    which are all defined in the scene description.

    Mesh lights (emissive geometry) and light probes are handled separately.

    This is work in progress. The code is not very well-tested.
*/


#ifndef __LIGHT_HELPERS_PARAMS__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __LIGHT_HELPERS_PARAMS__

#include "../../Config.h"

#include "../../Utils/Math/MathConstants.hlsli"
#include "LightData.hlsli"
#include "../../Utils/Math/MathHelpers.hlsli"

static const float kMinLightDistSqr = 1e-9f;
static const float kMaxLightDistance = FLT_MAX;

/** Describes a light sample for Falcor's analytic light sources.

    The struct contains a superset of what is normally needed for evaluating
    lighting integrals. Be careful not to access fields that are not needed,
    to make sure dead code elimination removes the computations.
*/
struct AnalyticLightSample
{
    float3  posW;           ///< Sampled point on the light source in world space (for local lights only).
    float3  normalW;        ///< Normal of the sampled point on the light source in world space (normalized).
    float3  dir;            ///< Direction from the shading point to the sampled point on the light in world space (normalized).
    float   distance;       ///< Distance from the shading point to sampled point on the light.
    float3  Li;             ///< Incident radiance at the shading point (unshadowed). Note: Already divided by the pdf.
    float   pdf;            ///< Probability density function with respect to solid angle at the shading point.
};

/** Internal helper function to finalize the shared computations for area light samples.
    The input sample must already have posW and normalW computed.
    \return True if the sample is valid, false otherwise.
*/
bool finalizeAreaLightSample(const float3 shadingPosW, const AnalyticLightData light, inout AnalyticLightSample ls)
{
    // Compute direction and distance to light.
    // The distance is clamped to a small epsilon to avoid div-by-zero below.
    float3 toLight = ls.posW - shadingPosW;
    float distSqr = max(dot(toLight, toLight), kMinLightDistSqr);
    ls.distance = sqrt(distSqr);
    ls.dir = toLight / ls.distance;

    // Compute incident radiance at shading point.
    // The area lights are single-sided by default, so radiance is zero when seen from the back-facing side.
    float cosTheta = dot(ls.normalW, -ls.dir);
    if (cosTheta <= 0.f) return false;
    ls.Li = light.intensity * (light.surfaceArea * cosTheta / distSqr);

    // Compute the PDF with respect to solid angle. Note this may be +inf.
    ls.pdf = distSqr / (cosTheta * light.surfaceArea);
    return true;
}

/** Samples a rectangular area light source.
    \param[in] shadingPosW Shading point in world space.
    \param[in] light Light data.
    \param[in] u Uniform random numbers.
    \param[out] ls Light sample struct.
    \return True if a sample was generated, false otherwise.
*/
bool sampleRectAreaLight(const float3 shadingPosW, const AnalyticLightData light, const float2 u, out AnalyticLightSample ls)
{
    // Pick a random sample on the quad.
    // The quad is from (-1,-1,0) to (1,1,0) in object space, but may be scaled by its transform matrix.
    float3 pos = float3(u.x * 2.f - 1.f, u.y * 2.f - 1.f, 0.f);

    // Apply model to world transformation matrix.
    ls.posW = mul(float4(pos, 1.f), light.transMat).xyz;

    // Setup world space normal.
    // TODO: normalW is not correctly oriented for mesh instances that have flipped triangle winding.
    ls.normalW = normalize(mul(float4(0.f, 0.f, 1.f, 0.f), light.transMatIT).xyz);

    return finalizeAreaLightSample(shadingPosW, light, ls);
}

/** Samples a spherical area light source.
    \param[in] shadingPosW Shading point in world space.
    \param[in] light Light data.
    \param[in] u Uniform random numbers.
    \param[out] ls Light sample struct.
    \return True if a sample was generated, false otherwise.
*/
bool sampleSphereAreaLight(const float3 shadingPosW, const AnalyticLightData light, const float2 u, out AnalyticLightSample ls)
{
    // Sample a random point on the sphere.
    // TODO: We should pick a random point on the hemisphere facing the shading point.
    float3 pos = sample_sphere(u);

    // Apply model to world transformation matrix.
    ls.posW = mul(float4(pos, 1.f), light.transMat).xyz;

    // Setup world space normal.
    ls.normalW = normalize(mul(float4(pos, 0.f), light.transMatIT).xyz);

    return finalizeAreaLightSample(shadingPosW, light, ls);
}

/** Samples disc area light source.
    \param[in] shadingPosW Shading point in world space.
    \param[in] light Light data.
    \param[in] Uniform random numbers.
    \param[out] ls Light sample struct.
    \return True if a sample was generated, false otherwise.
*/
bool sampleDiscAreaLight(const float3 shadingPosW, const AnalyticLightData light, const float2 u, out AnalyticLightSample ls)
{
    // Sample a random point on the disk.
    // TODO: Fix spelling disagreement between disc vs disk.
    float3 pos = float3(sample_disk(u), 0.f);

    // Transform to world space.
    ls.posW = mul(float4(pos, 1.f), light.transMat).xyz;

    // Setup world space normal.
    ls.normalW = normalize(mul(float4(0.f, 0.f, 1.f, 0.f), light.transMatIT).xyz);

    return finalizeAreaLightSample(shadingPosW, light, ls);
}

/** Samples a distant light source.
    \param[in] shadingPosW Shading point in world space.
    \param[in] light Light data.
    \param[in] u Uniform random numbers.
    \param[out] ls Light sample struct.
    \return True if a sample was generated, false otherwise.
*/
bool sampleDistantLight(const float3 shadingPosW, const AnalyticLightData light, const float2 u, out AnalyticLightSample ls)
{
    // A distant light doesn't have a position. Just clear to zero.
    ls.posW = float3(0.f, 0.f, 0.f);

    // Sample direction.
    float3 dir = sample_cone(u, light.cosSubtendedAngle);

    // Transform sampled direction to world space
    ls.dir = normalize(mul(dir, (float3x3)light.transMat));
    ls.normalW = -ls.dir;
    ls.distance = kMaxLightDistance;

    // Compute incident radiance at shading point.
    // A DistantLight's angle defines the solid angle it subtends.
    // But because the angle is intended to affect penumbra size, but not
    // perceived brigthness, we treat intensity as radiance.
    float solidAngle = (float)M_2PI * (1.f - light.cosSubtendedAngle);
    ls.Li = light.intensity;
    ls.pdf = 1.f / solidAngle;
    return true;
}

/** Samples a directional light source.
    \param[in] shadingPosW Shading point in world space.
    \param[in] light Light data.
    \param[out] ls Light sample struct.
    \return True if a sample was generated, false otherwise.
*/
bool sampleDirectionalLight(const float3 shadingPosW, const AnalyticLightData light, out AnalyticLightSample ls)
{
    // A directional light doesn't have a position. Just clear to zero.
    ls.posW = float3(0, 0, 0);

    // For a directional light, the normal is always along its light direction.
    ls.normalW = light.dirW;

    // Setup direction and distance to light.
    ls.distance = kMaxLightDistance;
    ls.dir = -light.dirW;

    // Setup incident radiance. For directional lights there is no falloff or cosine term.
    ls.Li = light.intensity;

    // For a directional light, the PDF with respect to solid angle is a Dirac function. Set to zero.
    ls.pdf = 0.f;

    return true;
}

/** Samples a point (spot) light.
    \param[in] shadingPosW Shading point in world space.
    \param[in] light Light data.
    \param[out] ls Light sample struct.
    \return True if a sample was generated, false otherwise.
*/
bool samplePointLight(const float3 shadingPosW, const AnalyticLightData light, out AnalyticLightSample ls)
{
    // Get the position and normal.
    ls.posW = light.posW;
    ls.normalW = light.dirW;

    // Compute direction and distance to light.
    // The distance is clamped to a small epsilon to avoid div-by-zero below.
    float3 toLight = ls.posW - shadingPosW;
    float distSqr = max(dot(toLight, toLight), kMinLightDistSqr);
    ls.distance = sqrt(distSqr);
    ls.dir = toLight / ls.distance;

    // Calculate the falloff for spot-lights.
    float cosTheta = -dot(ls.dir, light.dirW);
    float falloff = 1.f;
    if (cosTheta < light.cosOpeningAngle)
    {
        falloff = 0.f;
    }
    else if (light.penumbraAngle > 0.f)
    {
        float deltaAngle = light.openingAngle - acos(cosTheta);
        falloff = smoothstep(0.f, light.penumbraAngle, deltaAngle);
    }

    // Compute incident radiance at shading point.
    ls.Li = light.intensity * falloff / distSqr;

    // For a point light, the PDF with respect to solid angle is a Dirac function. Set to zero.
    ls.pdf = 0.f;

    return true;
}

#if 0
/** Samples an analytic light source.
    This function calls the correct sampling function depending on the type of light.
    \param[in] shadingPosW Shading point in world space.
    \param[in] light Light data.
    \param[in,out] sg Sample generator.
    \param[out] ls Sampled point on the light and associated sample data, only valid if true is returned.
    \return True if a sample was generated, false otherwise.
*/
bool sampleLight<S : ISampleGenerator>(const float3 shadingPosW, const AnalyticLightData light, inout S sg, out AnalyticLightSample ls)
{
    // Sample the light based on its type: point, directional, or area.
    switch (light.type)
    {
    case LightType::Point:
        return samplePointLight(shadingPosW, light, ls);
    case LightType::Directional:
        return sampleDirectionalLight(shadingPosW, light, ls);
    case LightType::Rect:
        return sampleRectAreaLight(shadingPosW, light, sampleNext2D(sg), ls);
    case LightType::Sphere:
        return sampleSphereAreaLight(shadingPosW, light, sampleNext2D(sg), ls);
    case LightType::Disc:
        return sampleDiscAreaLight(shadingPosW, light, sampleNext2D(sg), ls);
    case LightType::Distant:
        return sampleDistantLight(shadingPosW, light, sampleNext2D(sg), ls);
    default:
        ls = {};
        return false; // Should not happen
    }
}
#endif

#if 0
/** Evaluates a light approximately. This is useful for raster passes that don't use stochastic integration.
    For now only point and directional light sources are supported.
    \param[in] shadingPosW Shading point in world space.
    \param[in] light Light data.
    \param[out] ls Sampled point on the light and associated sample data (zero initialized if no sample is generated).
    \return True if a sample was generated, false otherwise.
*/
bool evalLightApproximate(const float3 shadingPosW, const AnalyticLightData light, out AnalyticLightSample ls)
{
    ls = {};

    switch (light.type)
    {
    case LightType::Point:
        return samplePointLight(shadingPosW, light, ls);
    case LightType::Directional:
        return sampleDirectionalLight(shadingPosW, light, ls);
    }

    // All other light types are not supported.
    return false;
}
#endif

#endif // __LIGHT_HELPERS_PARAMS__