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
#include "../PathTracer/Scene/Lights/EnvMapSampler.hlsli"
#include <rtxdi/RtxdiHelpers.hlsli>

#define LIGHT_SAMPING_EPSILON 1e-10
#define DISTANT_LIGHT_DISTANCE 10000.0

struct PolymorphicLightSample
{
    float3 position;
    float3 normal;
    float3 radiance;
    float solidAnglePdf;
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

#define VOLUME_SAMPLE_MODE_AVERAGE 0
#define VOLUME_SAMPLE_MODE_CLOSEST 1

#define VOLUME_SAMPLE_MODE VOLUME_SAMPLE_MODE_AVERAGE

// Computes estimated distance between a given point in space and a random point inside
// a spherical volume. Since the geometry of this solution is spherically symmetric,
// only the distance from the volume center to the point and the volume radius matter here.
float getAverageDistanceToVolume(float distanceToCenter, float volumeRadius)
{
    // The expression and factor are fitted to a Monte Carlo estimated curve.
    // At distanceToCenter == 0, this function returns (0.75 * volumeRadius) which is analytically accurate.
    // At infinity, the result asymptotically approaches distanceToCenter.
    const float nonlinearFactor = 1.1547;

    float distance = distanceToCenter + volumeRadius * square(volumeRadius)
        / square(distanceToCenter + volumeRadius * nonlinearFactor);

#if VOLUME_SAMPLE_MODE == VOLUME_SAMPLE_MODE_CLOSEST
    // if we're outside the volume, find the closest point
    if (distanceToCenter > volumeRadius)
    {
        distance = distanceToCenter - volumeRadius;
    }
#endif
    return distance;
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
    float3 direction;
    float outerAngle;
    float innerAngle;
    LightShaping shaping;

    // Interface methods

    PolymorphicLightSample calcSample(in const float3 viewerPosition)
    {
        const float3 lightVector = position - viewerPosition;

        // We cannot compute finite values for radiance and solidAnglePdf for a point light,
        // so return the limit of (radiance / solidAnglePdf) with radius --> 0 as radiance.
        PolymorphicLightSample lightSample;
        lightSample.position = position;
        lightSample.normal = normalize(-lightVector);
        lightSample.radiance = flux / dot(lightVector, lightVector);
        lightSample.solidAnglePdf = 1.0;

        return lightSample;
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
        pointLight.direction = octToNdirUnorm32(lightInfo.direction1);
        pointLight.outerAngle = f16tof32(lightInfo.direction2);
        pointLight.innerAngle = f16tof32(lightInfo.direction2 >> 16);
        pointLight.shaping = unpackLightShaping(lightInfo);

        return pointLight;
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
        const float2 diskSample = sampleDisk(random);

        float3 tangent, bitangent;
        branchlessONB(direction, tangent, bitangent);

        const float3 distantDirectionSample = direction
            + tangent * diskSample.x * sinHalfAngle
            + bitangent * diskSample.y * sinHalfAngle;

        // Calculate sample position on the distant light
        // Since there is no physical distant light to hit (as it is at infinity), this simply uses a large
        // number far enough away from anything in the world.

        const float3 distantPositionSample = viewerPosition - distantDirectionSample * DISTANT_LIGHT_DISTANCE;
        const float3 distantNormalSample = direction;

        // Create the light sample

        PolymorphicLightSample lightSample;

        lightSample.position = distantPositionSample;
        lightSample.normal = distantNormalSample;
        lightSample.radiance = radiance;
        lightSample.solidAnglePdf = 1.0 / solidAngle;

        return lightSample;
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

struct TriangleLight
{
    float3 base;
    float3 edge1;
    float3 edge2;
    float3 radiance;
    float3 normal;
    float surfaceArea;
    
    // Interface methods
    PolymorphicLightSample calcSample(in const float2 random, in const float3 viewerPosition)
    {
        PolymorphicLightSample result = (PolymorphicLightSample)0;

        float3 bary = sampleTriangle(random);
        result.position = base + edge1 * bary.y + edge2 * bary.z;
        result.position = computeRayOrigin(result.position, normal);
        result.normal = normal;

        const float3 toLight = result.position - viewerPosition;
        const float distSqr = max(FLT_MIN, dot(toLight, toLight));
        const float distance = sqrt(distSqr);
        const float3 dir = toLight / distance;
        float cosTheta = dot(normal, -dir);
        
        result.solidAnglePdf = 0.f;
        result.radiance = 0.f;
        if (cosTheta <= 0.f) return result;

        const float areaPdf = max(FLT_MIN, 1.0 / surfaceArea);
        result.solidAnglePdf = pdfAtoW(areaPdf, distance, cosTheta);
        result.radiance = radiance; 

        return result;
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

        float3 lightNormal = cross(triLight.edge1, triLight.edge2);
        float lightNormalLength = length(lightNormal);
        
        //Check for tiny triangles
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
        //lightInfo.logRadiance |= f32tof16((uint) empty slot) << 16; //unused
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
        case PolymorphicLightType::kTriangle:    return TriangleLight::Create(lightInfo).getWeightForVolume(volumeCenter, volumeRadius);
        case PolymorphicLightType::kDirectional: return 0; // infinite lights do not affect volume sampling
        case PolymorphicLightType::kEnvironment: return 0;
        default: return 0;
        }
    }
};

#endif // POLYMORPHIC_LIGHT_HLSLI