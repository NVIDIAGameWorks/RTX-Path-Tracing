/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __STANDARD_BSDF_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __STANDARD_BSDF_HLSLI__

#include "../../Config.h"    

#include "../../Utils/Math/MathConstants.hlsli"
#include "../../Utils/Math/MathHelpers.hlsli"

#include "IBSDF.hlsli"
#include "BxDF.hlsli"

/** Implementation of Falcor's standard surface BSDF.

    The BSDF has the following lobes:
    - Delta reflection (ideal specular reflection).
    - Specular reflection using a GGX microfacet model.
    - Diffuse reflection using Disney's diffuse BRDF.
    - Delta transmission (ideal specular transmission).
    - Specular transmission using a GGX microfacet model.
    - Diffuse transmission.

    The BSDF is a linear combination of the above lobes.
*/
struct StandardBSDF // : IBSDF
{
    StandardBSDFData data;      ///< BSDF parameters.
    float3 emission;            ///< Radiance emitted in the incident direction (wi).

    static StandardBSDF make() 
    { 
        StandardBSDF d;
        d.data = StandardBSDFData::make();
        d.emission = 0.f;
        return d;
    }

#if PTSDK_DIFFUSE_SPECULAR_SPLIT
    void eval(const ShadingData shadingData, const float3 wo, out float3 diffuse, out float3 specular)
    {
        float3 wiLocal = shadingData.toLocal(shadingData.V);
        float3 woLocal = shadingData.toLocal(wo);

        FalcorBSDF bsdf = FalcorBSDF::make(shadingData, data);

        bsdf.eval(wiLocal, woLocal/*, sampleGenerator*/, diffuse, specular);
    }
#endif

    float3 eval(const ShadingData shadingData, const float3 wo)
    {
        float3 wiLocal = shadingData.toLocal(shadingData.V);
        float3 woLocal = shadingData.toLocal(wo);

        FalcorBSDF bsdf = FalcorBSDF::make(shadingData, data);

#if PTSDK_DIFFUSE_SPECULAR_SPLIT
        float3 diffuse, specular;
        bsdf.eval(wiLocal, woLocal/*, sampleGenerator*/, diffuse, specular);
        return diffuse+specular;
#else
        return bsdf.eval(wiLocal, woLocal, sampleGenerator);
#endif
    }

    bool sample(const ShadingData shadingData, inout SampleGenerator sampleGenerator, out BSDFSample result, bool useImportanceSampling)
    {
        if (!useImportanceSampling) return sampleReference(shadingData, sampleGenerator, result);

        float3 wiLocal = shadingData.toLocal(shadingData.V);
        float3 woLocal = float3(0,0,0);

        FalcorBSDF bsdf = FalcorBSDF::make(shadingData, data);
#if RecycleSelectSamples
        bool valid = bsdf.sample(wiLocal, woLocal, result.pdf, result.weight, result.lobe, result.lobeP, sampleNext3D(sampleGenerator));
#else
        bool valid = bsdf.sample(wiLocal, woLocal, result.pdf, result.weight, result.lobe, result.lobeP, sampleNext4D(sampleGenerator));
#endif
        result.wo = shadingData.fromLocal(woLocal);

        return valid;
    }

    float evalPdf(const ShadingData shadingData, const float3 wo, bool useImportanceSampling)
    {
        if (!useImportanceSampling) return evalPdfReference(shadingData, wo);

        float3 wiLocal = shadingData.toLocal(shadingData.V);
        float3 woLocal = shadingData.toLocal(wo);

        FalcorBSDF bsdf = FalcorBSDF::make(shadingData, data);

        return bsdf.evalPdf(wiLocal, woLocal);
    }

    BSDFProperties getProperties(const ShadingData shadingData)
    {
        BSDFProperties p; p.flags = 0; // = {};

        p.emission = emission;

        // Clamp roughness so it's representable of what is actually used in FalcorBSDF.
        // Roughness^2 below kMinGGXAlpha is used to indicate perfectly smooth surfaces.
        float alpha = data.roughness * data.roughness;
        p.roughness = alpha < kMinGGXAlpha ? 0.f : data.roughness;

        // Compute approximation of the albedos.
        // For now use the blend weights and colors, but this should be improved to better numerically approximate the integrals.
        p.diffuseReflectionAlbedo = (1.f - data.diffuseTransmission) * (1.f - data.specularTransmission) * data.diffuse;
        p.diffuseTransmissionAlbedo = data.diffuseTransmission * data.transmission* (1.f - data.specularTransmission); // used to have  "* (1.f - data.specularTransmission)" too
        p.specularReflectionAlbedo = (1.f - data.specularTransmission) * data.specular;
        p.specularTransmissionAlbedo = data.specularTransmission * data.transmission;

        // Pass on our specular reflectance field unmodified.
        p.specularReflectance = data.specular;

        if (data.diffuseTransmission > 0.f || data.specularTransmission > 0.f) p.flags |= (uint)BSDFProperties::Flags::IsTransmissive;

        return p;
    }

    uint getLobes(const ShadingData shadingData)
    {
        return FalcorBSDF::getLobes(data);
    }


    // Additional functions

    /** Reference implementation that uses cosine-weighted hemisphere sampling.
        This is for testing purposes only.
        \param[in] sd Shading data.
        \param[in] sampleGenerator Sample generator.
        \param[out] result Generated sample. Only valid if true is returned.
        \return True if a sample was generated, false otherwise.
    */
    bool sampleReference(const ShadingData shadingData, inout SampleGenerator sampleGenerator, out BSDFSample result)
    {
        const bool isTransmissive = (getLobes(shadingData) & (uint)LobeType::Transmission) != 0;

        float3 wiLocal = shadingData.toLocal(shadingData.V);
        float3 woLocal = sample_cosine_hemisphere_concentric(sampleNext2D(sampleGenerator), result.pdf); // pdf = cos(theta) / pi

        if (isTransmissive)
        {
            if (sampleNext1D(sampleGenerator) < 0.5f)
            {
                woLocal.z = -woLocal.z;
            }
            result.pdf *= 0.5f;
            if (min(abs(wiLocal.z), abs(woLocal.z)) < kMinCosTheta || result.pdf == 0.f) return false;
        }
        else
        {
            if (min(wiLocal.z, woLocal.z) < kMinCosTheta || result.pdf == 0.f) return false;
        }

        FalcorBSDF bsdf = FalcorBSDF::make(shadingData, data);

        result.wo = shadingData.fromLocal(woLocal);
#if PTSDK_DIFFUSE_SPECULAR_SPLIT
        float3 diffuse, specular;
        bsdf.eval(wiLocal, woLocal/*, sampleGenerator*/, diffuse, specular);
        result.weight = (diffuse+specular) / result.pdf;
#else
        result.weight = bsdf.eval(wiLocal, woLocal/*, sampleGenerator*/) / result.pdf;
#endif
        result.lobe = (uint)(woLocal.z > 0.f ? (uint)LobeType::DiffuseReflection : (uint)LobeType::DiffuseTransmission);

        return true;
    }

    /** Evaluates the directional pdf for sampling the given direction using the reference implementation.
        \param[in] sd Shading data.
        \param[in] wo Outgoing direction.
        \return PDF with respect to solid angle for sampling direction wo.
    */
    float evalPdfReference(const ShadingData shadingData, const float3 wo)
    {
        const bool isTransmissive = (getLobes(shadingData) & (uint)LobeType::Transmission) != 0;

        float3 wiLocal = shadingData.toLocal(shadingData.V);
        float3 woLocal = shadingData.toLocal(wo);

        if (isTransmissive)
        {
            if (min(abs(wiLocal.z), abs(woLocal.z)) < kMinCosTheta) return 0.f;
            return 0.5f * woLocal.z * M_1_PI; // pdf = 0.5 * cos(theta) / pi
        }
        else
        {
            if (min(wiLocal.z, woLocal.z) < kMinCosTheta) return 0.f;
            return woLocal.z * M_1_PI; // pdf = cos(theta) / pi
        }
    }

    void evalDeltaLobes(const ShadingData shadingData, inout DeltaLobe deltaLobes[cMaxDeltaLobes], inout int deltaLobeCount, inout float nonDeltaPart)
    {
        float3 wiLocal = shadingData.toLocal(shadingData.V);
        
        FalcorBSDF bsdf = FalcorBSDF::make(shadingData, data); 
        bsdf.evalDeltaLobes(wiLocal, deltaLobes, deltaLobeCount, nonDeltaPart);
        
        // local to world!
        for ( uint i = 0; i < deltaLobeCount; i++ )
            deltaLobes[i].dir = shadingData.fromLocal(deltaLobes[i].dir);
    }


};

#endif // __STANDARD_BSDF_HLSLI__