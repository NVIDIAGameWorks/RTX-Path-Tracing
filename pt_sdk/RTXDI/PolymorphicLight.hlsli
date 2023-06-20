/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef POLYMORPHIC_LIGHT_HLSLI
#define POLYMORPHIC_LIGHT_HLSLI

#include <donut/shaders/packing.hlsli>
#include "HelperFunctions.hlsli"
#include "LightShaping.hlsli"
#include "../PathTracer/Utils/Color/ColorHelpers.hlsli"
#include "../PathTracer/Utils/Geometry/GeometryHelpers.hlsli"
#include "../PathTracer/Scene/Lights/LightHelpers.hlsli"
#include "../PathTracer/Scene/Lights/EnvMapSampler.hlsli"
#include "../../external/RTXDI/rtxdi-sdk/include/rtxdi/RtxdiHelpers.hlsli"

#define LIGHT_SAMPING_EPSILON 1e-10
#define DISTANT_LIGHT_DISTANCE 10000.0

#ifndef ENVIRONMENT_SAMPLER
#define ENVIRONMENT_SAMPLER s_EnvironmentSampler
#endif

struct PolymorphicLightSample
{
    float3 position;
    float3 normal;
    float3 radiance;
    float solidAnglePdf;
#if ENABLE_DEBUG_RTXDI_VIZUALISATION
    uint index;
#endif
};

PolymorphicLightType getLightType(PolymorphicLightInfo lightInfo)
{
    uint typeCode = (lightInfo.colorTypeAndFlags >> kPolymorphicLightTypeShift) 
        & kPolymorphicLightTypeMask;

    return (PolymorphicLightType)typeCode;
}

float unpackLightRadiance(uint logRadiance)
{
    return (logRadiance == 0) ? 0 : exp2((float(logRadiance - 1) / 65534.0) * (kPolymorphicLightMaxLog2Radiance - kPolymorphicLightMinLog2Radiance) + kPolymorphicLightMinLog2Radiance);
}

float3 unpackLightColor(PolymorphicLightInfo lightInfo)
{
    float3 color = Unpack_R8G8B8_UFLOAT(lightInfo.colorTypeAndFlags);
    float radiance = unpackLightRadiance(lightInfo.logRadiance & 0xffff);
    return color * radiance.xxx;
}

void packLightColor(float3 radiance, inout PolymorphicLightInfo lightInfo)
{   
    float intensity = max(radiance.r, max(radiance.g, radiance.b));

    if (intensity > 0.0)
    {
        float logRadiance = saturate((log2(intensity) - kPolymorphicLightMinLog2Radiance) 
            / (kPolymorphicLightMaxLog2Radiance - kPolymorphicLightMinLog2Radiance));
        uint packedRadiance = min(uint32_t(ceil(logRadiance * 65534.0)) + 1, 0xffffu);
        float unpackedRadiance = unpackLightRadiance(packedRadiance);

        float3 normalizedRadiance = saturate(radiance.rgb / unpackedRadiance.xxx);

        lightInfo.logRadiance |= packedRadiance;
        lightInfo.colorTypeAndFlags |= Pack_R8G8B8_UFLOAT(normalizedRadiance);
    }
}

bool packCompactLightInfo(PolymorphicLightInfo lightInfo, out uint4 res1, out uint4 res2)
{
    if (unpackLightShaping(lightInfo).isSpot)
    {
        res1 = 0;
        res2 = 0;
        return false;
    }

    res1.xyz = asuint(lightInfo.center.xyz);
    res1.w = lightInfo.colorTypeAndFlags;

    res2.x = lightInfo.direction1;
    res2.y = lightInfo.direction2;
    res2.z = lightInfo.scalars;
    res2.w = lightInfo.logRadiance;
    return true;
}

PolymorphicLightInfo unpackCompactLightInfo(const uint4 data1, const uint4 data2)
{
    PolymorphicLightInfo lightInfo = (PolymorphicLightInfo)0;
    lightInfo.center.xyz = asfloat(data1.xyz);
    lightInfo.colorTypeAndFlags = data1.w;
    lightInfo.direction1 = data2.x;
    lightInfo.direction2 = data2.y;
    lightInfo.scalars = data2.z;
    lightInfo.logRadiance = data2.w;
    return lightInfo;
}

// Computes estimated distance between a given point in space and a random point inside
// a spherical volume. Since the geometry of this solution is spherically symmetric,
// only the distance from the volume center to the point and the volume radius matter here.
float getAverageDistanceToVolume(float distanceToCenter, float volumeRadius)
{
    // The expression and factor are fitted to a Monte Carlo estimated curve.
    // At distanceToCenter == 0, this function returns (0.75 * volumeRadius) which is analytically accurate.
    // At infinity, the result asymptotically approaches distanceToCenter.

    const float nonlinearFactor = 1.1547;

    return distanceToCenter + volumeRadius * square(volumeRadius) 
        / square(distanceToCenter + volumeRadius * nonlinearFactor);
}


// Note: Sphere lights always assume an interaction point is not going to be inside of the sphere, so special logic handling this case
// can be avoided in sampling logic (for PDF/radiance calculation), as well as individual PDF calculation and radiance evaluation.
struct SphereLight
{
    float3 position;
    float radius; // Note: Assumed to always be >0 to avoid point light special cases
    float3 radiance;
    LightShaping shaping;

    // Interface methods

    PolymorphicLightSample calcSample(in const float2 random, in const float3 viewerPosition)
    {
        const float3 lightVector = position - viewerPosition;
        const float lightDistance2 = dot(lightVector, lightVector);
        const float lightDistance = sqrt(lightDistance2);
        const float radius2 = square(radius);

        // Note: Sampling based on PBRT's solid angle sphere sampling, resulting in fewer rays occluded by the light itself,
        // ignoring special case for when viewing inside the light (which should just use normal spherical area sampling)
        // for performance. Similarly condition ignored in PDF calculation as well.

        // Compute theta and phi for cone sampling

        const float2 u = random;
        const float sinThetaMax2 = radius2 / lightDistance2;
        const float cosThetaMax = sqrt(max(0.0f, 1.0f - sinThetaMax2));
        const float phi = 2.0f * c_pi * u.x;
        const float cosTheta = lerp(cosThetaMax, 1.0f, u.y);
        const float sinTheta = sqrt(max(0.0f, 1.0f - square(cosTheta)));
        const float sinTheta2 = sinTheta * sinTheta;

        // Calculate the alpha value representing the spherical coordinates of the sample point

        const float dc = lightDistance;
        const float dc2 = lightDistance2;
        const float ds = dc * cosTheta - sqrt(max(LIGHT_SAMPING_EPSILON, radius2 - dc2 * sinTheta2));
        const float cosAlpha = (dc2 + radius2 - square(ds)) / (2.0f * dc * radius);
        const float sinAlpha = sqrt(max(0.0f, 1.0f - square(cosAlpha)));

        // Construct a coordinate frame to sample in around the direction of the light vector

        const float3 sampleSpaceNormal = normalize(lightVector);
        float3 sampleSpaceTangent;
        float3 sampleSpaceBitangent;
        branchlessONB(sampleSpaceNormal, sampleSpaceTangent, sampleSpaceBitangent);

        // Calculate sample position and normal on the sphere

        float sinPhi;
        float cosPhi;
        sincos(phi, sinPhi, cosPhi);

        const float3 radiusVector = sphericalDirection(
            sinAlpha, cosAlpha, sinPhi, cosPhi, -sampleSpaceTangent, -sampleSpaceBitangent, -sampleSpaceNormal);
        const float3 spherePositionSample = position + radius * radiusVector;
        const float3 sphereNormalSample = normalize(radiusVector);
        // Note: Reprojection for position to minimize error here skipped for performance

        // Calculate the pdf

        // Note: The cone already represents a solid angle effectively so its pdf is already a solid angle pdf
        const float solidAnglePdf = 1.0f / (2.0f * c_pi * (1.0f - cosThetaMax));

        // Create the light sample

        PolymorphicLightSample lightSample;

        lightSample.position = spherePositionSample;
        lightSample.normal = sphereNormalSample;
        lightSample.radiance = radiance;
        lightSample.solidAnglePdf = solidAnglePdf;

        return lightSample;
    }

    float getSurfaceArea()
    {
        return 4 * c_pi * square(radius);
    }

    float getPower()
    {
        return getSurfaceArea() * c_pi * luminance(radiance) * getShapingFluxFactor(shaping);
    }

    float getWeightForVolume(in const float3 volumeCenter, in const float volumeRadius)
    {
        if (!testSphereIntersectionForShapedLight(position, radius, shaping, volumeCenter, volumeRadius))
            return 0.0;

        float distance = length(volumeCenter - position);
        distance = getAverageDistanceToVolume(distance, volumeRadius);

        float sinHalfAngle = radius / distance;
        float solidAngle = 2 * c_pi * (1.0 - sqrt(1.0 - square(sinHalfAngle)));

        return solidAngle * luminance(radiance);
    }

    static SphereLight Create(in const PolymorphicLightInfo lightInfo)
    {
        SphereLight sphereLight;

        sphereLight.position = lightInfo.center;
        sphereLight.radius = f16tof32(lightInfo.scalars);
        sphereLight.radiance = unpackLightColor(lightInfo);
        sphereLight.shaping = unpackLightShaping(lightInfo);

        return sphereLight;
    }
};

// Point light is a sphere light with zero radius.
// On the host side, they are both created from LightType_Point, depending on the radius.
// The values returned from all interface methods of PointLight are the same as SphereLight
// would produce in the limit when radius approaches zero, with some exceptions in calcSample.
struct PointLight
{
    float3 position;
    float3 flux;
    float outerAngle;
    float innerAngle;
    LightShaping shaping;

    // Interface methods

    PolymorphicLightSample calcSample(in const float3 viewerPosition)
    {
        AnalyticLightData ald = AnalyticLightData::make();
        ald.posW = position; 
        ald.intensity = flux;
        ald.openingAngle = outerAngle;
        ald.cosOpeningAngle = cos(outerAngle);
        ald.penumbraAngle = innerAngle;
        ald.type = (uint)AnalyticLightType::Point;

        AnalyticLightSample als;
        samplePointLight(viewerPosition, ald, als);

        PolymorphicLightSample pls;
        pls.position = als.posW;
        pls.normal = als.normalW;
        pls.radiance = als.Li;
        pls.solidAnglePdf = 1.0f;

        return pls;
    }

    float getPower()
    {
        return 4.0 * c_pi * luminance(flux) * getShapingFluxFactor(shaping);
    }

    float getWeightForVolume(in const float3 volumeCenter, in const float volumeRadius)
    {
        if (!testSphereIntersectionForShapedLight(position, 0, shaping, volumeCenter, volumeRadius))
            return 0.0;

        float distance = length(volumeCenter - position);
        distance = getAverageDistanceToVolume(distance, volumeRadius);

        return luminance(flux) / square(distance);
    }

    static PointLight Create(in const PolymorphicLightInfo lightInfo)
    {
        PointLight pointLight;

        pointLight.position = lightInfo.center;
        pointLight.flux = unpackLightColor(lightInfo);
        pointLight.outerAngle = f16tof32(lightInfo.direction1);
        pointLight.innerAngle = f16tof32(lightInfo.direction1 >> 16);
        pointLight.shaping = unpackLightShaping(lightInfo);

        return pointLight;
    }
};

struct CylinderLight
{
    float3 position;
    float radius; // Note: Assumed to always be >0 to avoid line light special cases
    float3 radiance;
    float axisLength; // Note: Assumed to always be >0 to avoid ring light special cases
    float3 tangent;

    // Interface methods

    PolymorphicLightSample calcSample(in const float2 random, in const float3 viewerPosition)
    {
        // Construct a coordinate frame around the tangent vector

        float3 normal;
        float3 bitangent;
        branchlessONB(tangent, normal, bitangent);

        // Compute phi and z

        const float2 u = random;
        const float phi = 2.0f * c_pi * u.x;

        float sinPhi;
        float cosPhi;
        sincos(phi, sinPhi, cosPhi);

        const float z = (u.y - 0.5f) * axisLength;

        // Calculate sample position and normal on the cylinder

        const float3 radiusVector = sinPhi * bitangent + cosPhi * normal;
        const float3 cylinderPositionSample = position + z * tangent + radius * radiusVector;
        const float3 cylinderNormalSample = normalize(radiusVector);
        // Note: Reprojection for position to minimize error here skipped for performance

        // Calculate pdf

        const float areaPdf = 1.0f / getSurfaceArea();
        const float3 sampleVector = cylinderPositionSample - viewerPosition;
        const float sampleDistance = length(sampleVector);
        const float sampleCosTheta = dot(normalize(sampleVector), -cylinderNormalSample);
        const float solidAnglePdf = pdfAtoW(areaPdf, sampleDistance, abs(sampleCosTheta));

        // Create the light sample

        PolymorphicLightSample lightSample;

        lightSample.position = cylinderPositionSample;
        lightSample.normal = cylinderNormalSample;

        if (sampleCosTheta <= 0.0f)
        {
            lightSample.radiance = float3(0.0f, 0.0f, 0.0f);
            lightSample.solidAnglePdf = 0.0f;
        }
        else
        {
            lightSample.radiance = radiance;
            lightSample.solidAnglePdf = solidAnglePdf;
        }

        return lightSample;
    }

    float getSurfaceArea()
    {
        return 2.0f * c_pi * radius * axisLength;
    }

    float getPower()
    {
        return getSurfaceArea() * c_pi * luminance(radiance);
    }

    float getWeightForVolume(in const float3 volumeCenter, in const float volumeRadius)
    {
        float distance = length(volumeCenter - position);
        distance = getAverageDistanceToVolume(distance, volumeRadius);

        // Assume illumination by a quad that represents the cylinder when viewed from afar.
        float quadArea = 2.0 * radius * axisLength;
        float approximateSolidAngle = quadArea / square(distance);
        approximateSolidAngle = min(approximateSolidAngle, 2 * c_pi);

        return approximateSolidAngle * luminance(radiance);
    }

    static CylinderLight Create(in const PolymorphicLightInfo lightInfo)
    {
        CylinderLight cylinderLight;

        cylinderLight.position = lightInfo.center;
        cylinderLight.radius = f16tof32(lightInfo.scalars);
        cylinderLight.radiance = unpackLightColor(lightInfo);
        cylinderLight.axisLength = f16tof32(lightInfo.scalars >> 16);
        cylinderLight.tangent = octToNdirUnorm32(lightInfo.direction1);

        return cylinderLight;
    }
};

struct DiskLight
{
    float3 position;
    float radius; // Note: Assumed to always be >0 to avoid point light special cases
    float3 radiance;
    float3 normal;

    // Interface methods

    PolymorphicLightSample calcSample(in const float2 random, in const float3 viewerPosition)
    {
        float3 tangent;
        float3 bitangent;
        branchlessONB(normal, tangent, bitangent);

        // Compute a raw disk sample

        const float2 rawDiskSample = sampleDisk(random) * radius;

        // Calculate sample position and normal on the disk

        const float3 diskPositionSample = position + tangent * rawDiskSample.x + bitangent * rawDiskSample.y;
        const float3 diskNormalSample = normal;

        // Calculate pdf

        const float areaPdf = 1.0f / getSurfaceArea();
        const float3 sampleVector = diskPositionSample - viewerPosition;
        const float sampleDistance = length(sampleVector);
        const float sampleCosTheta = dot(normalize(sampleVector), -diskNormalSample);
        const float solidAnglePdf = pdfAtoW(areaPdf, sampleDistance, abs(sampleCosTheta));

        // Create the light sample

        PolymorphicLightSample lightSample;

        lightSample.position = diskPositionSample;
        lightSample.normal = diskNormalSample;

        if (sampleCosTheta <= 0.0f)
        {
            lightSample.radiance = float3(0.0f, 0.0f, 0.0f);
            lightSample.solidAnglePdf = 0.0f;
        }
        else
        {
            lightSample.radiance = radiance;
            lightSample.solidAnglePdf = solidAnglePdf;
        }

        return lightSample;
    }

    float getSurfaceArea()
    {
        return c_pi * square(radius);
    }

    float getPower()
    {
        return getSurfaceArea() * c_pi * luminance(radiance);// * getShapingFluxFactor(shaping);
    }

    float getWeightForVolume(in const float3 volumeCenter, in const float volumeRadius)
    {
        float distanceToPlane = dot(volumeCenter - position, normal);
        if (distanceToPlane < -volumeRadius)
            return 0; // Cull - the entire volume is below the light's horizon

        float distance = length(volumeCenter - position);
        distance = getAverageDistanceToVolume(distance, volumeRadius);

        float approximateSolidAngle = getSurfaceArea() / square(distance);
        approximateSolidAngle = min(approximateSolidAngle, 2 * c_pi);

        return approximateSolidAngle * luminance(radiance);
    }

    static DiskLight Create(in const PolymorphicLightInfo lightInfo)
    {
        DiskLight diskLight;

        diskLight.position = lightInfo.center;
        diskLight.radius = f16tof32(lightInfo.scalars);
        diskLight.normal = octToNdirUnorm32(lightInfo.direction1);
        diskLight.radiance = unpackLightColor(lightInfo);

        return diskLight;
    }
};

struct RectLight
{
    float3 position;
    float2 dimensions; // Note: Assumed to always be >0 to avoid point light special cases
    float3 dirx;
    float3 diry;
    float3 radiance;

    float3 normal;

    // Interface methods

    PolymorphicLightSample calcSample(in const float2 random, in const float3 viewerPosition)
    {
        // Compute x and y

        const float2 u = random;
        const float2 rawRectangleSample = float2((u.x - 0.5f) * dimensions.x, (u.y - 0.5f) * dimensions.y);

        // Calculate sample position on the rectangle

        const float3 rectanglePositionSample = position + dirx * rawRectangleSample.x + diry * rawRectangleSample.y;
        const float3 rectangleNormalSample = normal;

        // Calculate pdf

        const float areaPdf = 1.0f / getSurfaceArea();
        const float3 sampleVector = rectanglePositionSample - viewerPosition;
        const float sampleDistance = length(sampleVector);
        const float sampleCosTheta = dot(normalize(sampleVector), -rectangleNormalSample);
        const float solidAnglePdf = pdfAtoW(areaPdf, sampleDistance, abs(sampleCosTheta));

        // Create the light sample

        PolymorphicLightSample lightSample;

        lightSample.position = rectanglePositionSample;
        lightSample.normal = rectangleNormalSample;

        if (sampleCosTheta <= 0.0f)
        {
            lightSample.radiance = float3(0.0f, 0.0f, 0.0f);
            lightSample.solidAnglePdf = 0.0f;
        }
        else
        {
            lightSample.radiance = radiance;
            lightSample.solidAnglePdf = solidAnglePdf;
        }

        return lightSample;
    }

    float getSurfaceArea()
    {
        return dimensions.x * dimensions.y;
    }

    float getPower()
    {
        return getSurfaceArea() * c_pi * luminance(radiance);
    }

    float getWeightForVolume(in const float3 volumeCenter, in const float volumeRadius)
    {
        float distanceToPlane = dot(volumeCenter - position, normal);
        if (distanceToPlane < -volumeRadius)
            return 0; // Cull - the entire volume is below the light's horizon

        float distance = length(volumeCenter - position);
        distance = getAverageDistanceToVolume(distance, volumeRadius);

        float approximateSolidAngle = getSurfaceArea() / square(distance);
        approximateSolidAngle = min(approximateSolidAngle, 2 * c_pi);

        return approximateSolidAngle * luminance(radiance);
    }

    static RectLight Create(in const PolymorphicLightInfo lightInfo)
    {
        RectLight rectLight;

        rectLight.position = lightInfo.center;
        rectLight.dimensions.x = f16tof32(lightInfo.scalars);
        rectLight.dimensions.y = f16tof32(lightInfo.scalars >> 16);
        rectLight.dirx = octToNdirUnorm32(lightInfo.direction1);
        rectLight.diry = octToNdirUnorm32(lightInfo.direction2);
        rectLight.radiance = unpackLightColor(lightInfo);

        // Note: Precomputed to avoid recomputation when evaluating multiple quantities on the same light
        rectLight.normal = cross(rectLight.dirx, rectLight.diry);


        return rectLight;
    }
};

struct DirectionalLight
{
    float3 direction;
    float cosHalfAngle; // Note: Assumed to be != 1 to avoid delta light special case
    float sinHalfAngle;
    float angularSize;
    float solidAngle;
    float3 radiance;

    // Interface methods

    PolymorphicLightSample calcSample(in const float2 random, in const float3 viewerPosition)
    {
       //Convert to the Falcor format 
        AnalyticLightData ald = AnalyticLightData::make();
        ald.dirW = direction;
        ald.intensity = radiance;
        ald.type = (uint)AnalyticLightType::Distant;
       
        // AnalyticLightType::Distant requires transform in transMat, so make one up
        // - doesn't make any difference that it's made up unless it's moving, 
        float3 T, B;
        branchlessONB(direction, T, B);
        ald.transMat[0].xyz = T;
        ald.transMat[1].xyz = B;
        ald.transMat[2].xyz = direction;
        ald.cosSubtendedAngle = cosHalfAngle;
        AnalyticLightSample als; 
        sampleDistantLight(viewerPosition, ald, random, als);

        PolymorphicLightSample pls;
        pls.position = viewerPosition - als.dir * DISTANT_LIGHT_DISTANCE;
        pls.normal = als.normalW;
        pls.radiance = als.Li;
        pls.solidAnglePdf = 1 / (M_2PI * (1.f - cosHalfAngle)); 
        
        return pls;
    }

    // Helper methods

    static DirectionalLight Create(in const PolymorphicLightInfo lightInfo)
    {
        DirectionalLight directionalLight;

        directionalLight.direction = octToNdirUnorm32(lightInfo.direction1);

        float halfAngle = f16tof32(lightInfo.scalars);
        directionalLight.angularSize = 2 * halfAngle;
        sincos(halfAngle, directionalLight.sinHalfAngle, directionalLight.cosHalfAngle);
        directionalLight.solidAngle = f16tof32(lightInfo.scalars >> 16);
        directionalLight.radiance = unpackLightColor(lightInfo);

        return directionalLight;
    }
};
#define FLT_MIN             1.175494351e-38F        // min normalized positive value

struct TriangleLight
{
    float3 base;
    float3 edge1;
    float3 edge2;
    float3 radiance;
    float3 normal;
    float surfaceArea;
#if ENABLE_DEBUG_RTXDI_VIZUALISATION
    uint index;
#endif
    //bool isFlipped; 
    
    // Interface methods
    PolymorphicLightSample calcSample(in const float2 random, in const float3 viewerPosition)
    {
        PolymorphicLightSample result = (PolymorphicLightSample)0;

        float3 bary = sampleTriangle(random);
        result.position = base + edge1 * bary.y + edge2 * bary.z;
        result.position = computeRayOrigin(result.position, normal);
        result.normal = normal;
#if ENABLE_DEBUG_RTXDI_VIZUALISATION
        result.index = index;
#endif

        /*const float3 toLight = result.position - viewerPosition;
        if (dot(normal, -toLight) <=0)
            return result;*/

        /*result.solidAnglePdf = calcSolidAnglePdf(viewerPosition, result.position, result.normal);
        if (!result.solidAnglePdf)
            return result;

        result.radiance = radiance; 
        return result;  
       */
        //Falcor
        const float3 toLight = result.position - viewerPosition;
        const float distSqr = max(FLT_MIN, dot(toLight, toLight));
        const float distance = sqrt(distSqr);
        const float3 dir = toLight / distance;
        float cosTheta = dot(normal, -dir);
        
        result.solidAnglePdf = 0.f;
        result.radiance = 0.f;
        if (cosTheta <= 0.f) return result;

        float denom = max(FLT_MIN, cosTheta * surfaceArea);
        result.solidAnglePdf = distSqr / denom;
        result.radiance = radiance; 

        return result;
    }

    float calcSolidAnglePdf(in const float3 viewerPosition,
                            in const float3 lightSamplePosition,
                            in const float3 lightSampleNormal)
    {
        float3 L = lightSamplePosition - viewerPosition;
        float Ldist = length(L);
        L /= Ldist;

        const float areaPdf = max(FLT_MIN, 1.0 / surfaceArea);
        const float sampleCosTheta = saturate(dot(L, -lightSampleNormal));
        if (sampleCosTheta <= 0.f)
            return 0;

        return pdfAtoW(areaPdf, Ldist, sampleCosTheta);
    }

    float getPower()
    {
        return surfaceArea * c_pi * luminance(radiance);
    }

    float getWeightForVolume(in const float3 volumeCenter, in const float volumeRadius)
    {
        float distanceToPlane = dot(volumeCenter - base, normal);
        if (distanceToPlane < -volumeRadius)
            return 0; // Cull - the entire volume is below the light's horizon

        float3 barycenter = base + ((edge1 + edge2) / 3.0);
        float distance = length(barycenter - volumeCenter);
        distance = getAverageDistanceToVolume(distance, volumeRadius);

        float approximateSolidAngle = surfaceArea / square(distance);
        approximateSolidAngle = min(approximateSolidAngle, 2 * c_pi);

        return approximateSolidAngle * luminance(radiance);
    }

    // Helper methods

    static TriangleLight Create(in const PolymorphicLightInfo lightInfo)
    {
        TriangleLight triLight;

        triLight.edge1 = f16tof32(uint3(lightInfo.direction1, lightInfo.direction2, lightInfo.scalars) & 0xffff);
        triLight.edge2 = f16tof32(uint3(lightInfo.direction1, lightInfo.direction2, lightInfo.scalars) >> 16);
        triLight.base = lightInfo.center - ((triLight.edge1 + triLight.edge2) / 3.0);
        triLight.radiance = unpackLightColor(lightInfo);
#if ENABLE_DEBUG_RTXDI_VIZUALISATION
        triLight.index = (f16tof32(lightInfo.logRadiance >> 16));
#endif

        float3 lightNormal = cross(triLight.edge1, triLight.edge2);
        float lightNormalLength = length(lightNormal);
        
        //Check for tiny triangles? 
        if(lightNormalLength > 0.0)
        {
            triLight.surfaceArea = 0.5 * lightNormalLength;
            triLight.normal = lightNormal / lightNormalLength;
        }
        else
        {
           triLight.surfaceArea = 0.0;
           triLight.normal = 0.0; 
        }

        return triLight;
    }

    PolymorphicLightInfo Store()
    {
        PolymorphicLightInfo lightInfo = (PolymorphicLightInfo)0;

        packLightColor(radiance, lightInfo);
        lightInfo.center = base + ((edge1 + edge2) / 3.0); 
        float3 edges = (f32tof16(edge1) & 0xffff) | (f32tof16(edge2) << 16);
        lightInfo.direction1 = edges.x;
        lightInfo.direction2 = edges.y;
        lightInfo.scalars = edges.z;
        lightInfo.colorTypeAndFlags |= uint(PolymorphicLightType::kTriangle) << kPolymorphicLightTypeShift;
        //lightInfo.logRadiance |= f32tof16((uint) isFlipped) << 16; //Store in the empty slot
        return lightInfo;
    }
};

struct EnvironmentLight
{
    uint2 textureSize;
   
    // Interface methods

    PolymorphicLightSample calcSample(in const float2 random, in const float3 viewerPosition)
    {
        PolymorphicLightSample pls;
       
        EnvMap envMap = EnvMap::make(
            t_EnvironmentMap,
            s_EnvironmentMapSampler,
            g_Const.envMapData
        );

        float3 direction = envMap.uvToWorld(random);
        pls.position = viewerPosition + direction * DISTANT_LIGHT_DISTANCE;
        pls.normal = -direction;
        pls.radiance = envMap.eval(random);

        float elevation = (random.y - 0.5f) * M_PI;
        float cosElevation = cos(elevation);
        // Inverse of the solid angle of one texel of the environment map
        pls.solidAnglePdf = (textureSize.x * textureSize.y) / max(FLT_MIN, 2.f * M_PI * M_PI * cosElevation);
        return pls;
    }

    // Helper methods

    static EnvironmentLight Create(in const PolymorphicLightInfo lightInfo)
    {
        EnvironmentLight envLight;

        envLight.textureSize.x = lightInfo.direction2 & 0xffff;
        envLight.textureSize.y = lightInfo.direction2 >> 16;

        return envLight;
    }
};

struct PolymorphicLight
{
    static PolymorphicLightSample calcSample(
        in const PolymorphicLightInfo lightInfo, 
        in const float2 random, 
        in const float3 viewerPosition)
    {
        PolymorphicLightSample lightSample = (PolymorphicLightSample)0;

        switch (getLightType(lightInfo))
        {
        case PolymorphicLightType::kSphere:      lightSample = SphereLight::Create(lightInfo).calcSample(random, viewerPosition); break;
        case PolymorphicLightType::kPoint:       lightSample = PointLight::Create(lightInfo).calcSample(viewerPosition); break;
        case PolymorphicLightType::kCylinder:    lightSample = CylinderLight::Create(lightInfo).calcSample(random, viewerPosition); break;
        case PolymorphicLightType::kDisk:        lightSample = DiskLight::Create(lightInfo).calcSample(random, viewerPosition); break;
        case PolymorphicLightType::kRect:        lightSample = RectLight::Create(lightInfo).calcSample(random, viewerPosition); break;
        case PolymorphicLightType::kTriangle:    lightSample = TriangleLight::Create(lightInfo).calcSample(random, viewerPosition); break;
        case PolymorphicLightType::kDirectional: lightSample = DirectionalLight::Create(lightInfo).calcSample(random, viewerPosition); break;
        case PolymorphicLightType::kEnvironment: lightSample = EnvironmentLight::Create(lightInfo).calcSample(random, viewerPosition); break;
        }

        if (lightSample.solidAnglePdf > 0)
        {
            lightSample.radiance *= evaluateLightShaping(unpackLightShaping(lightInfo),
                viewerPosition, lightSample.position);
        }

        return lightSample;
    }

    static float getPower(
        in const PolymorphicLightInfo lightInfo)
    {
        switch (getLightType(lightInfo))
        {
        case PolymorphicLightType::kSphere:      return SphereLight::Create(lightInfo).getPower();
        case PolymorphicLightType::kPoint:       return PointLight::Create(lightInfo).getPower();
        case PolymorphicLightType::kCylinder:    return CylinderLight::Create(lightInfo).getPower();
        case PolymorphicLightType::kDisk:        return DiskLight::Create(lightInfo).getPower();
        case PolymorphicLightType::kRect:        return RectLight::Create(lightInfo).getPower();
        case PolymorphicLightType::kTriangle:    return TriangleLight::Create(lightInfo).getPower();
        case PolymorphicLightType::kDirectional: return 0; // infinite lights don't go into the local light PDF map
        case PolymorphicLightType::kEnvironment: return 0;
        default: return 0;
        }
    }

    static float getWeightForVolume(
        in const PolymorphicLightInfo lightInfo, 
        in const float3 volumeCenter,
        in const float volumeRadius)
    {
        switch (getLightType(lightInfo))
        {
        case PolymorphicLightType::kSphere:      return SphereLight::Create(lightInfo).getWeightForVolume(volumeCenter, volumeRadius);
        case PolymorphicLightType::kPoint:       return PointLight::Create(lightInfo).getWeightForVolume(volumeCenter, volumeRadius);
        case PolymorphicLightType::kCylinder:    return CylinderLight::Create(lightInfo).getWeightForVolume(volumeCenter, volumeRadius);
        case PolymorphicLightType::kDisk:        return DiskLight::Create(lightInfo).getWeightForVolume(volumeCenter, volumeRadius);
        case PolymorphicLightType::kRect:        return RectLight::Create(lightInfo).getWeightForVolume(volumeCenter, volumeRadius);
        case PolymorphicLightType::kTriangle:    return TriangleLight::Create(lightInfo).getWeightForVolume(volumeCenter, volumeRadius);
        case PolymorphicLightType::kDirectional: return 0; // infinite lights do not affect volume sampling
        case PolymorphicLightType::kEnvironment: return 0;
        default: return 0;
        }
    }
};

#endif // POLYMORPHIC_LIGHT_HLSLI