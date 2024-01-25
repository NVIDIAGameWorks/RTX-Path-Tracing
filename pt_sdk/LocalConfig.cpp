/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "LocalConfig.h"

#include "SampleUI.h"
#include "Sample.h"


void LocalConfig::PreferredSceneOverride(std::string& preferredScene)
{
    if (PTSDK_LOCAL_CONFIG_ID_STRING == "REF_VS_REALTIME" || PTSDK_LOCAL_CONFIG_ID_STRING == "REGIR")
    {
#if 0 // // test for making reference pixel-identical to realtime; test for playing with NEE use ReGIR only
        preferredScene = "kitchen-with-test-stuff.scene.json";
#endif
    }
    if (PTSDK_LOCAL_CONFIG_ID_STRING == "DENOISER_TUNING")
    {
        //preferredScene = "transparent-machines.scene.json";
    }
    if (PTSDK_LOCAL_CONFIG_ID_STRING == "GENERIC_STABLE_LIGHTS")
    {
        preferredScene = "convergence-test.scene.json";
    }
    if (PTSDK_LOCAL_CONFIG_ID_STRING == "PROC_SKY_TESTING")
    {
        preferredScene = "programmer-art-proc-sky.scene.json";
    }
}

void LocalConfig::PostAppInit(Sample& sample, SampleUIData& sampleUI)
{
    if (PTSDK_LOCAL_CONFIG_ID_STRING == "REF_VS_REALTIME")
    {
#if 0 // test for making reference pixel-identical to realtime
        sampleUI.AccumulationAA = false;
        sampleUI.AccumulationTarget = 4;
        sampleUI.RealtimeSamplesPerPixel = sampleUI.AccumulationTarget;
        sampleUI.RealtimeAA = 0;
        sampleUI.RealtimeDenoiser = false;
        sampleUI.RealtimeNoise = false;
        sampleUI.StablePlanesActiveCount = 1;
        sampleUI.AllowPrimarySurfaceReplacement = false;
        sampleUI.RealtimeDiffuseBounceCount = sampleUI.ReferenceDiffuseBounceCount;
        sampleUI.RealtimeFireflyFilterEnabled = false;
        sampleUI.ReferenceFireflyFilterEnabled = false;
        //sampleUI.EnableRussianRoulette = false;
        //sampleUI.BounceCount = 1;
#endif
    }
    if (PTSDK_LOCAL_CONFIG_ID_STRING == "REGIR")
    {
#if 1 // test for playing with NEE use ReGIR only
        sampleUI.AccumulationTarget = 256;
        sampleUI.RealtimeMode = false;
        sampleUI.AllowRTXDIInReferenceMode = false;
        sampleUI.UseReSTIRDI = false;
        sampleUI.UseReSTIRGI = false;
        sampleUI.ToneMappingParams.autoExposure = false;
        //sampleUI.RTXDI.reGirSettings.Mode = rtxdi::ReGIRMode::Grid;
        sampleUI.ReferenceFireflyFilterEnabled = false;
        sampleUI.EnableRussianRoulette = false;
        sampleUI.BounceCount = 1;
#endif
    }

    // This disables...
    //  * ReSTIR DI & ReSTIR GI 
    //  * AutoExposure 
    //  * Stable Planes (set to 1) 
    // ...and increases brute force sampling - useful for denoiser tuning as it removes temporal issues and prevents stable planes from hiding issues.
    // Once denoiser works well, try enabling things one by one (and reducing NEE & global samples back to 1)
    if (PTSDK_LOCAL_CONFIG_ID_STRING == "DENOISER_TUNING")
    {
        sampleUI.RealtimeMode = true;
        sampleUI.UseReSTIRDI = false;           // avoid any temporal issues from DI
        sampleUI.UseReSTIRGI = false;           // avoid any temporal issues from GI
        sampleUI.ToneMappingParams.autoExposure = false;    // for stable before/after image comparisons
        sampleUI.StablePlanesActiveCount = 1;   // disable SPs - we want as good raw denoising as possible without SPs hiding any issues
        sampleUI.RealtimeSamplesPerPixel = 2;   // boost global samples
        sampleUI.NEELocalFullSamples = 2;       // boost full samples
        sampleUI.NEEDistantFullSamples = 2;     // boost full samples
        sampleUI.NEELocalType = 1;              // avoid any temporal issues from ReGIR (due to presampling + multiple full local samples)
        sampleUI.RealtimeAA = 1;
    }

    if (PTSDK_LOCAL_CONFIG_ID_STRING == "ENVMAP_TUNING")
    {
        sampleUI.AccumulationTarget = 256;
        sampleUI.RealtimeMode = false;
        sampleUI.UseReSTIRDI = false;
        sampleUI.UseReSTIRGI = false;
        sampleUI.ToneMappingParams.autoExposure = false;
        sampleUI.StablePlanesActiveCount = 1;
        sampleUI.ReferenceFireflyFilterEnabled = false;
        sampleUI.EnableRussianRoulette = false;
        sampleUI.BounceCount = 2;

        for (int i = 0; sampleUI.TogglableNodes != nullptr && i < sampleUI.TogglableNodes->size(); i++)
        {
            TogglableNode & node = (*sampleUI.TogglableNodes)[i];
            if (node.UIName == "Ceiling")
                node.SetSelected(false);
        }
    }

    if (PTSDK_LOCAL_CONFIG_ID_STRING == "GENERIC_STABLE_LIGHTS")
    {
        sampleUI.AccumulationTarget = 4096;
        sampleUI.RealtimeMode = false;
        sampleUI.UseReSTIRDI = false;
        sampleUI.UseReSTIRGI = false;
        sampleUI.ToneMappingParams.autoExposure = false;
        sampleUI.StablePlanesActiveCount = 1;
        //sampleUI.ReferenceFireflyFilterEnabled = false;
        //sampleUI.EnableRussianRoulette = false;
        //sampleUI.BounceCount = 2;
    }

}

void LocalConfig::PostSceneLoad(Sample& sample, SampleUIData& sampleUI)
{
}

void LocalConfig::PostMaterialLoad(donut::engine::Material& mat)
{
#if 0 // convert transmissive to white opaque
    if (mat.domain == MaterialDomain::Transmissive || mat.domain == MaterialDomain::TransmissiveAlphaBlended || mat.domain == MaterialDomain::TransmissiveAlphaTested)
    {
        mat.baseOrDiffuseColor = float3(1, 1, 1);
        mat.enableBaseOrDiffuseTexture = false;
    }
    if (mat.domain == MaterialDomain::Transmissive) mat.domain = MaterialDomain::Opaque;
    if (mat.domain == MaterialDomain::TransmissiveAlphaBlended) mat.domain = MaterialDomain::AlphaBlended;
    if (mat.domain == MaterialDomain::TransmissiveAlphaTested)  mat.domain = MaterialDomain::AlphaTested;
#endif
#if 1 // disable emissive lights
    if (PTSDK_LOCAL_CONFIG_ID_STRING == "ENVMAP_TUNING")
        mat.emissiveIntensity = 0.0f;
#endif
}

