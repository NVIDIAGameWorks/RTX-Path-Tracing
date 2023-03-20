/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NrdConfig.h"

namespace NrdConfig {

    nrd::RelaxDiffuseSpecularSettings getDefaultRELAXSettings() {

        nrd::RelaxDiffuseSpecularSettings settings;
        settings.enableAntiFirefly = true;
        settings.hitDistanceReconstructionMode = nrd::HitDistanceReconstructionMode::AREA_3X3;

        settings.historyFixFrameNum = 4;
        settings.spatialVarianceEstimationHistoryThreshold = 4;

        settings.enableReprojectionTestSkippingWithoutMotion = false;

        // (pixels) - pre-accumulation spatial reuse pass blur radius (0 = disabled, must be used in case of probabilistic sampling)
        settings.diffusePrepassBlurRadius = 0.0f;   // value >0 helps with boiling but will mess up shadows
        settings.specularPrepassBlurRadius = 0.0f;  // value >0 helps with boiling but will mess up shadows

        // diffuse
        settings.diffuseMaxFastAccumulatedFrameNum = 4;
        settings.diffusePhiLuminance = 0.5f;

        // specular
        settings.specularMaxFastAccumulatedFrameNum = 6;
        settings.specularPhiLuminance = 0.35f;
        settings.specularLobeAngleSlack = 0.15f;

        settings.confidenceDrivenLuminanceEdgeStoppingRelaxation = 0.5f;
        settings.roughnessEdgeStoppingRelaxation = 0.3f;
        settings.specularLobeAngleFraction = 0.93f;

        settings.atrousIterationNum = 6;

        settings.diffuseMaxAccumulatedFrameNum = 60;
        settings.specularMaxAccumulatedFrameNum = 60;

        settings.depthThreshold = 0.004f;

        return settings;
    }

    nrd::ReblurSettings getDefaultREBLURSettings() {

        nrd::ReblurSettings settings;
        settings.enableAntiFirefly = true;
        settings.hitDistanceReconstructionMode = nrd::HitDistanceReconstructionMode::AREA_3X3;
        settings.maxAccumulatedFrameNum = 60;

        return settings;
    }
}

