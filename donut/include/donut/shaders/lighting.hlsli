/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include "light_cb.h"

#include <donut/shaders/brdf.hlsli>

/*
float _GGX(SurfaceParams surface, float3 lightIncident, float3 viewIncident, float halfAngularSize)
{
    float3 N = surface.normal;
    float3 V = -viewIncident;
    float3 L = -lightIncident;
    float3 R = reflect(viewIncident, N);

    // Correction of light vector L for spherical / directional area lights.
    // Inspired by "Real Shading in Unreal Engine 4" by B. Karis, 
    // re-formulated to work in spherical coordinates instead of world space.
    float AngleLR = acos(clamp(dot(R, L), -1, 1));

    float3 CorrectedL = (AngleLR > 0) ? slerp(L, R, AngleLR, saturate(halfAngularSize / AngleLR)) : L;
    float3 H = normalize(CorrectedL + V);

    float NdotH = max(0, dot(N, H));
    float NdotL = max(0, dot(N, CorrectedL));
    float NdotV = max(0, dot(N, V));

    float Alpha = max(0.01, square(surface.roughness));

    // Normalization for the widening of D, see the paper referenced above.
    float CorrectedAlpha = saturate(Alpha + 0.5 * tan(halfAngularSize));
    float SphereNormalization = square(Alpha / CorrectedAlpha);

    // GGX / Trowbridge-Reitz NDF with normalization for sphere lights
    float D = square(Alpha) / (M_PI * square(square(NdotH) * (square(Alpha) - 1) + 1)) * SphereNormalization;
    
    // Schlick model for geometric attenuation
    // The (NdotL * NdotV) term in the numerator is cancelled out by the same term in the denominator of the final result.
    float k = square(surface.roughness + 1) / 8.0;
    float G = 1 / ((NdotL * (1 - k) + k) * (NdotV * (1 - k) + k));

    return D * G / 4;
}
*/

float GetClippedDiskArea(float3 incidentVector, float3 geometryNormal, float angularSize)
{
    float NdotL = -dot(incidentVector, geometryNormal);
    float angleAboveHorizon = K_PI * 0.5 - acos(NdotL);
    float fractionAboveHorizon = angleAboveHorizon / angularSize;

    // an approximation
    return smoothstep(-0.5, 0.5, fractionAboveHorizon);
}

void ShadeSurface(LightConstants light, MaterialSample materialSample, float3 surfacePos, float3 viewIncident, out float3 o_diffuseRadiance, out float3 o_specularRadiance)
{
    o_diffuseRadiance = 0;
    o_specularRadiance = 0;

    float3 incidentVector = 0;
    float halfAngularSize = 0;
    float irradiance = 0;

    if (light.lightType == LightType_Directional)
    {
        incidentVector = light.direction;

        halfAngularSize = light.angularSizeOrInvRange * 0.5;

        irradiance = light.intensity;
    }
    else if (light.lightType == LightType_Spot || light.lightType == LightType_Point)
    {
        float3 lightToSurface = surfacePos - light.position;
        float distance = sqrt(dot(lightToSurface, lightToSurface));
        float rDistance = 1.0 / distance;
        incidentVector = lightToSurface * rDistance;

        float attenuation = 1;
        if (light.angularSizeOrInvRange > 0)
        {
            attenuation = square(saturate(1.0 - square(square(distance * light.angularSizeOrInvRange))));

            if (attenuation == 0)
                return;
        }

        float spotlight = 1;
        if (light.lightType == LightType_Spot)
        {
            float LdotD = dot(incidentVector, light.direction);
            float directionAngle = acos(LdotD);
            spotlight = 1 - smoothstep(light.innerAngle, light.outerAngle, directionAngle);

            if (spotlight == 0)
                return;
        }

        if (light.radius > 0)
        {
            halfAngularSize = atan(min(light.radius * rDistance, 1));

            // A good enough approximation for 2 * (1 - cos(halfAngularSize)), numerically more accurate for small angular sizes
            float solidAngleOverPi = square(halfAngularSize);

            float radianceTimesPi = light.intensity / square(light.radius);

            irradiance = radianceTimesPi * solidAngleOverPi;
        }
        else
        {
            irradiance = light.intensity * square(rDistance);
        }

        irradiance *= spotlight * attenuation;
    }
    else
    {
        return;
    }
    
    o_diffuseRadiance = Lambert(materialSample.shadingNormal, incidentVector)
        * materialSample.diffuseAlbedo
        * irradiance;

    o_specularRadiance = GGX_AnalyticalLights_times_NdotL(incidentVector, viewIncident, materialSample.shadingNormal,
        materialSample.roughness, materialSample.specularF0, halfAngularSize) * irradiance;
}

float GetLightProbeWeight(LightProbeConstants lightProbe, float3 position)
{
    float weight = 1;

    // XXXX manuelk : work-around for a dxc/dxil compiler bug that crashes 
    // the passes using this ; uncomment the unroll to test if the compiler
    // has been fixed.
    //[unroll]
    for (uint nPlane = 0; nPlane < 6; nPlane++)
    {
        float4 plane = lightProbe.frustumPlanes[nPlane];

        float planeResult = plane.w - dot(plane.xyz, position.xyz);

        weight *= saturate(planeResult);
    }

    return weight;
}
