/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef ENVIRONMENT_MAP_IMPORTANCE_SAMPLING_CB_H
#define ENVIRONMENT_MAP_IMPORTANCE_SAMPLING_CB_H

struct EnvironmentMapImportanceSamplingConstants
{
	uint2 outputDim;            // Resolution of the importance map in texels.
	uint2 outputDimInSamples;   // Resolution of the importance map in samples.
	uint2 numSamples;           // Per-texel subsamples s.xy at finest mip.
	float invSamples;           // 1 / (s.x*s.y).
	float _padding0;
};


#endif // ENVIRONMENT_MAP_IMPORTANCE_SAMPLING_CB_H