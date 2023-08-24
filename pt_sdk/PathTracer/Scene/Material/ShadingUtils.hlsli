/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __SHADING_UTILS_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __SHADING_UTILS_HLSLI__

#include "../ShadingData.hlsli"
#include "../SceneTypes.hlsli"
#include "MaterialTypes.hlsli"
#include "../../Utils/Math/MathHelpers.hlsli"
#include "../../Utils/Color/ColorHelpers.hlsli"

/** Convert RGB to normal (unnormalized).
*/
float3 rgbToNormal(float3 rgb)
{
    return rgb * 2.f - 1.f;
}

/** Convert RG to normal (unnormalized).
*/
float3 rgToNormal(float2 rg)
{
    float3 n;
    n.xy = rg * 2 - 1;

    // Saturate because error from BC5 can break the sqrt
    n.z = saturate(dot(rg, rg)); // z = r*r + g*g
    n.z = sqrt(1 - n.z);
    return n;
}

// TODO: this function is broken an may return negative values.
// https://gitlab-master.nvidia.com/nvresearch-gfx/Tools/Falcor/issues/780
float getMetallic(float3 diffuse, float3 spec)
{
    // This is based on the way that UE4 and Substance Painter 2 converts base+metallic+specular level to diffuse/spec colors
    // We don't have the specular level information, so the assumption is that it is equal to 0.5 (based on the UE4 documentation)
    // Note that I'm using the luminance here instead of the actual colors. The reason is that there's no guaraentee that all RGB channels will end up with the same metallic value
    float d = luminance(diffuse);
    float s = luminance(spec);
    if (s == 0) return 0;
    float a = 0.04;
    float b = s + d - 0.08;
    float c = 0.04 - s;
    float root = sqrt(b*b - 0.16*c);
    float m = (root - b) * 12.5;
    return m;
}

/** Apply normal map.
    This function perturbs the shading normal using a local normal sampled from a normal map.
    \param[in,out] sd ShadingData struct that is updated.
    \param[in] type Normal map type.
    \param[in] encodedNormal Encoded normal loaded from normal map.
    \param[in] tangent Tangent in world space (xyz) and bitangent sign (w). The tangent is *only* valid when w != 0.
*/
void applyNormalMap(inout ShadingData shadingData, const NormalMapType type, const float3 encodedNormal, const float4 tangentW)
{
    float3 mapN = float3(0,0,0);
    switch (type)
    {
    case NormalMapType::RGB:
        mapN = rgbToNormal(encodedNormal);
        break;
    case NormalMapType::RG:
        mapN = rgToNormal(encodedNormal.rg);
        break;
    default:
        return;
    }

    // Note if the normal ends up being parallel to the tangent, the tangent frame cannot be orthonormalized.
    // That case is rare enough that it is probably not worth the runtime cost to check for it here.
    // If it occurs we should foremost fix the asset, or if problems persist add a check here.

    // Apply the transformation.
    shadingData.N = normalize(shadingData.T * mapN.x + shadingData.B * mapN.y + shadingData.N * mapN.z);
    shadingData.T = normalize(tangentW.xyz - shadingData.N * dot(tangentW.xyz, shadingData.N));
    shadingData.B = cross(shadingData.N, shadingData.T) * tangentW.w;
}

/** Computes an orthonormal tangent space based on the normal and given tangent.
    \param[in,out] sd ShadingData struct that is updated.
    \param[in] tangent Interpolated tangent in world space (xyz) and bitangent sign (w). The tangent is *only* valid when w is != 0.
    \return True if a valid tangent space was computed based on the supplied tangent.
*/
bool computeTangentSpace(inout ShadingData shadingData, const float4 tangentW)
{
    // Check that tangent space exists and can be safely orthonormalized.
    // Otherwise invent a tanget frame based on the normal.
    // We check that:
    //  - Tangent exists, this is indicated by a nonzero sign (w).
    //  - It has nonzero length. Zeros can occur due to interpolation or bad assets.
    //  - It is not parallel to the normal. This can occur due to normal mapping or bad assets.
    //  - It does not have NaNs. These will propagate and trigger the fallback.

    float NdotT = dot(tangentW.xyz, shadingData.N);
    bool nonParallel = abs(NdotT) < 0.9999f;
    bool nonZero = dot(tangentW.xyz, tangentW.xyz) > 0.f;

    bool valid = tangentW.w != 0.f && nonZero && nonParallel;
    if (valid)
    {
        shadingData.T = normalize(tangentW.xyz - shadingData.N * NdotT);
        shadingData.B = cross(shadingData.N, shadingData.T) * tangentW.w;
    }
    else
    {
        shadingData.T = perp_stark(shadingData.N);
        shadingData.B = cross(shadingData.N, shadingData.T);
    }

    return valid;
}

/** Helper function to adjust the shading normal to reduce black pixels due to back-facing view direction.
    Note: This breaks the reciprocity of the BSDF!
*/
void adjustShadingNormal(inout ShadingData shadingData, VertexData v)
{
    float3 Ng = shadingData.frontFacing ? v.faceNormalW : -v.faceNormalW;
    float3 Ns = shadingData.N;

#ifdef FALCOR_INTERNAL
    // Algorithm from Appendix A.3 of https://arxiv.org/abs/1705.01263
    // The specular reflection of the view direction is computed.
    // If the reflection vector lies under the horizon, the shading normal is adjusted.
    // This ensures that the majority of the reflection lobe lies on the same side,
    // but it does not guarantee that all directions are valid in the general case.

    // Specular reflection in shading normal
    float3 R = reflect(-shadingData.V, Ns);
    float a = dot(Ng, R);
    if (a < 0) // Perturb normal
    {
        float b = max(0.001, dot(Ns, Ng));
        shadingData.N = normalize(shadingData.V + normalize(R - Ns * a / b));
#else
    // Blend the shading normal towards the geometric normal at grazing angles.
    // This is to avoid the view vector from becoming back-facing.
    const float kCosThetaThreshold = 0.1f;
    float cosTheta = dot(shadingData.V, Ns);
    if (cosTheta <= kCosThetaThreshold)
    {
        float t = saturate(cosTheta * (1.f / kCosThetaThreshold));
        shadingData.N = normalize(lerp(Ng, Ns, t));
#endif
        computeTangentSpace(shadingData, v.tangentW);
    }
}

#endif // __SHADING_UTILS_HLSLI__