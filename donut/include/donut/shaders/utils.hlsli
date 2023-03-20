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

#ifndef UTILS_HLSLI
#define UTILS_HLSLI

static const float K_PI = 3.14159265;

float square(float x) { return x * x; }
float2 square(float2 x) { return x * x; }
float3 square(float3 x) { return x * x; }
float4 square(float4 x) { return x * x; }

float3 slerp(float3 a, float3 b, float angle, float t)
{
    t = saturate(t);
    float sin1 = sin(angle * t);
    float sin2 = sin(angle * (1 - t));
    float ta = sin1 / (sin1 + sin2);
    float3 result = lerp(a, b, ta);
    return normalize(result);
}

float copysign(float x, float y)
{
    uint xi = asint(x), yi = asint(y);
    xi &= 0x7fffffff;
    xi |= yi & 0x80000000;
    return asfloat(xi);
}

// "sign - but doesn't return 0"
float snz(float v) { return (v >= 0.f) ? 1.f : -1.f; }
float2 snz(float2 v) { return float2(snz(v.x), snz(v.y)); }
float3 snz(float3 v) { return float3(snz(v.x), snz(v.y), snz(v.z)); }
float4 snz(float4 v) { return float4(snz(v.x), snz(v.y), snz(v.z), snz(v.w)); }

// Helper function to reflect the folds of the lower hemisphere
// over the diagonals in the octahedral map
float2 octWrap(float2 v)
{
    return (1.f - abs(v.yx)) * (v.xy >= 0.f ? 1.f : -1.f);
}

/**********************/
// Signed encodings
// Converts a normalized direction to the octahedral map (non-equal area, signed)
// n - normalized direction
// Returns a signed position in octahedral map [-1, 1] for each component
float2 ndirToOctSigned(float3 n)
{
    // Project the sphere onto the octahedron (|x|+|y|+|z| = 1) and then onto the xy-plane
    float2 p = n.xy * (1.f / (abs(n.x) + abs(n.y) + abs(n.z)));
    return (n.z < 0.f) ? octWrap(p) : p;
}

// Converts a point in the octahedral map to a normalized direction (non-equal area, signed)
// p - signed position in octahedral map [-1, 1] for each component 
// Returns normalized direction
float3 octToNdirSigned(float2 p)
{
    // https://twitter.com/Stubbesaurus/status/937994790553227264
    float3 n = float3(p.x, p.y, 1.0 - abs(p.x) - abs(p.y));
    float t = max(0, -n.z);
    n.xy += n.xy >= 0.0 ? -t : t;
    return normalize(n);
}

/**********************/
// Unorm 32 bit encodings
// Converts a normalized direction to the octahedral map (non-equal area, unsigned normalized)
// n - normalized direction
// Returns a packed 32 bit unsigned normalized position in octahedral map
// The two components of the result are stored in UNORM16 format, [0..1]
uint ndirToOctUnorm32(float3 n)
{
    float2 p = ndirToOctSigned(n);
    p = saturate(p.xy * 0.5 + 0.5);
    return uint(p.x * 0xfffe) | (uint(p.y * 0xfffe) << 16);
}

// Converts a point in the octahedral map (non-equal area, unsigned normalized) to normalized direction
// pNorm - a packed 32 bit unsigned normalized position in octahedral map
// Returns normalized direction
float3 octToNdirUnorm32(uint pUnorm)
{
    float2 p;
    p.x = saturate(float(pUnorm & 0xffff) / 0xfffe);
    p.y = saturate(float(pUnorm >> 16) / 0xfffe);
    p = p * 2.0 - 1.0;
    return octToNdirSigned(p);
}

// Project the direction onto the first two spherical harmonics.
// The coefficients like sqrt(1/4pi) have been dropped because they
// cancel out at interpolation stage.
// Multiply the output of this function by the signal magnitude.
float4 directionToSphericalHarmonics(float3 normalizedDirection)
{
    return float4(normalizedDirection, 1.0);
}

// Interpolate the first two spherical harmonics for a given direction.
// Works with numbers returned by `directionToSphericalHarmonics`.
float interpolateSphericalHarmonics(float4 sh, float3 normalizedDirection)
{
    return 0.5 * (dot(sh.xyz, normalizedDirection) + sh.w);
}

// Smart bent normal for ray tracing
// See appendix A.3 in https://arxiv.org/pdf/1705.01263.pdf
float3 getBentNormal(float3 geometryNormal, float3 shadingNormal, float3 viewDirection)
{
    // Flip the normal in case we're looking at the geometry from its back side
    if (dot(geometryNormal, viewDirection) > 0)
    {
        geometryNormal = -geometryNormal;
        shadingNormal = -shadingNormal;
    }

    // Specular reflection in shading normal
    float3 R = reflect(viewDirection, shadingNormal);
    float a = dot(geometryNormal, R);
    if (a < 0) // Perturb normal
    {
        float b = max(0.001, dot(shadingNormal, geometryNormal));
        return normalize(-viewDirection + normalize(R - shadingNormal * a / b));
    }

    return shadingNormal;
}

float3 computeRayIntersectionBarycentrics(float3 vertices[3], float3 rayOrigin, float3 rayDirection)
{
    float3 edge1 = vertices[1] - vertices[0];
    float3 edge2 = vertices[2] - vertices[0];

    float3 pvec = cross(rayDirection, edge2);

    float det = dot(edge1, pvec);
    float inv_det = 1.0f / det;

    float3 tvec = rayOrigin - vertices[0];

    float alpha = dot(tvec, pvec) * inv_det;

    float3 qvec = cross(tvec, edge1);

    float beta = dot(rayDirection, qvec) * inv_det;

    return float3(1.f - alpha - beta, alpha, beta);
}

float2 interpolate(float2 vertices[3], float3 bary)
{
    return vertices[0] * bary[0] + vertices[1] * bary[1] + vertices[2] * bary[2];
}

float3 interpolate(float3 vertices[3], float3 bary)
{
    return vertices[0] * bary[0] + vertices[1] * bary[1] + vertices[2] * bary[2];
}

float4 interpolate(float4 vertices[3], float3 bary)
{
    return vertices[0] * bary[0] + vertices[1] * bary[1] + vertices[2] * bary[2];
}

#endif // UTILS_HLSLI