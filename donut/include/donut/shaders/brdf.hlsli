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

#ifndef BRDF_HLSLI
#define BRDF_HLSLI

#include <donut/shaders/utils.hlsli>

// Converts a Beckmann roughness parameter to a Phong specular power
float RoughnessToSpecPower(in float m) {
    return 2.0f / (m * m + 1e-4) - 2.0f;
}

// Converts a Blinn-Phong specular power to a Beckmann roughness parameter
float SpecPowerToRoughness(in float s) {
    return clamp(sqrt(max(0, 2.0f / (s + 2.0f))), 0, 1);
}

float Lambert(float3 normal, float3 lightIncident)
{
    return max(0, -dot(normal, lightIncident)) / K_PI;
}

// Constructs an orthonormal basis based on the provided normal.
// https://graphics.pixar.com/library/OrthonormalB/paper.pdf
void ConstructONB(float3 normal, out float3 tangent, out float3 bitangent)
{
    float sign = (normal.z >= 0) ? 1 : -1;
    float a = -1.0 / (sign + normal.z);
    float b = normal.x * normal.y * a;
    tangent = float3(1.0f + sign * normal.x * normal.x * a, sign * b, -sign * normal.x);
    bitangent = float3(b, sign + normal.y * normal.y * a, -normal.y);
}

float Schlick_Fresnel(float F0, float VdotH)
{
    return F0 + (1 - F0) * pow(max(1 - VdotH, 0), 5);
}

float3 Schlick_Fresnel(float3 F0, float VdotH)
{
    return F0 + (1 - F0) * pow(max(1 - VdotH, 0), 5);
}

float G1_Smith(float roughness, float NdotL)
{
    float alpha = square(roughness);
    return 2.0 * NdotL / (NdotL + sqrt(square(alpha) + (1.0 - square(alpha)) * square(NdotL)));
}

float G_Smith_over_NdotV(float roughness, float NdotV, float NdotL)
{
    float alpha = square(roughness);
    float g1 = NdotV * sqrt(square(alpha) + (1.0 - square(alpha)) * square(NdotL));
    float g2 = NdotL * sqrt(square(alpha) + (1.0 - square(alpha)) * square(NdotV));
    return 2.0 * NdotL / (g1 + g2);
}

float3 GGX_times_NdotL(float3 V, float3 L, float3 N, float roughness, float3 F0)
{
    float3 H = normalize(L + V);

    float NoL = saturate(dot(N, L));
    float VoH = saturate(dot(V, H));
    float NoV = saturate(dot(N, V));
    float NoH = saturate(dot(N, H));

    if (NoL > 0)
    {
        float G = G_Smith_over_NdotV(roughness, NoV, NoL);
        float alpha = square(roughness);
        float D = square(alpha) / (K_PI * square(square(NoH) * square(alpha) + (1 - square(NoH))));

        float3 F = Schlick_Fresnel(F0, VoH);

        return F * (D * G / 4);
    }
    return 0;
}

float3 GGX_AnalyticalLights_times_NdotL(float3 lightIncident, float3 viewIncident, float3 normal, float roughness, float3 specularF0, float halfAngularSize)
{
    float3 N = normal;
    float3 V = -viewIncident;
    float3 L = -lightIncident;
    float3 R = reflect(viewIncident, N);

    // Correction of light vector L for spherical / directional area lights.
    // Inspired by "Real Shading in Unreal Engine 4" by B. Karis, 
    // re-formulated to work in spherical coordinates instead of world space.
    float AngleLR = acos(clamp(dot(R, L), -1, 1));

    float3 CorrectedL = (AngleLR > 0) ? slerp(L, R, AngleLR, saturate(halfAngularSize / AngleLR)) : L;
    float3 H = normalize(CorrectedL + V);

    float NdotH = saturate(dot(N, H));
    float NdotL = saturate(dot(N, CorrectedL));
    float NdotV = saturate(dot(N, V));
    float VdotH = saturate(dot(V, H));

    float Alpha = max(0.01, square(roughness));

    // Normalization for the widening of D, see the paper referenced above.
    float CorrectedAlpha = saturate(Alpha + 0.5 * tan(halfAngularSize));
    float SphereNormalization = square(Alpha / CorrectedAlpha);

    // GGX / Trowbridge-Reitz NDF with normalization for sphere lights
    float D = square(Alpha) / (K_PI * square(square(NdotH) * (square(Alpha) - 1) + 1)) * SphereNormalization;

    // Schlick model for geometric attenuation
    // The (NdotL * NdotV) term in the numerator is cancelled out by the same term in the denominator of the final result.
    float k = square(roughness + 1) / 8.0;
    float G = 1 / ((NdotL * (1 - k) + k) * (NdotV * (1 - k) + k));

    float3 F = Schlick_Fresnel(specularF0, VdotH);

    return F * (D * G * NdotL/ 4);
}

// Converts area measure PDF to solid angle measure PDF
float PdfAtoW(float pdfA, float distance_, float cosTheta)
{
    return pdfA * square(distance_) / cosTheta;
}

// Returns uniformly sampled barycentric coordinates inside a triangle
float3 SampleTriangle(float2 random)
{
    float sqrtx = sqrt(random.x);

    return float3(
        1 - sqrtx,
        sqrtx * (1 - random.y),
        sqrtx * random.y);
}

float2 SampleDisk(float2 random)
{
    float angle = 2 * K_PI * random.x;
    return float2(cos(angle), sin(angle)) * sqrt(random.y);
}

float3 SampleCosHemisphere(float2 random, out float solidAnglePdf)
{
    float2 tangential = SampleDisk(random);
    float elevation = sqrt(saturate(1.0 - random.y));

    solidAnglePdf = elevation / K_PI;

    return float3(tangential.xy, elevation);
}

float3 SampleSphere(float2 random, out float solidAnglePdf)
{
    // See (6-8) in https://mathworld.wolfram.com/SpherePointPicking.html

    random.y = random.y * 2.0 - 1.0;

    float2 tangential = SampleDisk(float2(random.x, 1.0 - square(random.y)));
    float elevation = random.y;

    solidAnglePdf = 0.25f / K_PI;

    return float3(tangential.xy, elevation);
}

// Returns the sampled H vector in tangent space, assuming N = (0, 0, 1).
float3 ImportanceSampleGGX(float2 random, float roughness)
{
    float alpha = square(roughness);

    float phi = 2 * K_PI * random.x;
    float cosTheta = sqrt((1 - random.y) / (1 + (square(alpha) - 1) * random.y));
    float sinTheta = sqrt(1 - cosTheta * cosTheta);

    float3 H;
    H.x = sinTheta * cos(phi);
    H.y = sinTheta * sin(phi);
    H.z = cosTheta;

    return H;
}

// Returns the sampled H vector in tangent space, assuming N = (0, 0, 1).
// Ve is in the same tangent space.
float3 ImportanceSampleGGX_VNDF(float2 random, float roughness, float3 Ve, float ndf_trim)
{
    float alpha = square(roughness);

    float3 Vh = normalize(float3(alpha * Ve.x, alpha * Ve.y, Ve.z));

    float lensq = square(Vh.x) + square(Vh.y);
    float3 T1 = lensq > 0.0 ? float3(-Vh.y, Vh.x, 0.0) * (1 / sqrt(lensq)) : float3(1.0, 0.0, 0.0);
    float3 T2 = cross(Vh, T1);

    float r = sqrt(random.x * ndf_trim);
    float phi = 2.0 * K_PI * random.y;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(1.0 - square(t1)) + s * t2;

    float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - square(t1) - square(t2))) * Vh;

    float3 H;
    H.x = alpha * Nh.x;
    H.y = alpha * Nh.y;
    H.z = max(0.0, Nh.z);

    return H;
}

float ImportanceSampleGGX_VNDF_PDF(float roughness, float3 N, float3 V, float3 L)
{
    float3 H = normalize(L + V);
    float NoH = saturate(dot(N, H));
    float VoH = saturate(dot(V, H));

    float alpha = square(roughness);
    float D = square(alpha) / (K_PI * square(square(NoH) * square(alpha) + (1 - square(NoH))));
    return (VoH > 0.0) ? D / (4.0 * VoH) : 0.0;
}

#endif // BRDF_HLSLI
