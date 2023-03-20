/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#include <nvrhi/nvrhi.h>
#include <donut/core/math/math.h>
#include <memory>
#include <unordered_map>
#include <rtxdi/RTXDI.h>
#include "../ComputePass.h"
#include "../RayTracingPass.h"
#include "RtxdiResources.h"

namespace rtxdi
{
	class Context;
	struct FrameParameters;
}

class RenderTargets;
class PrepareLightsPass;
class GenerateMipsPass;
class EnvironmentMap;

namespace donut::engine
{
	class ExtendedScene;
	class ShaderFactory;
	class CommonRenderPasses;
	struct ShaderMacro;
	class PlanarView;
}

struct CameraVectors
{
	donut::math::float3 cameraU;
	donut::math::float3 cameraV;
	donut::math::float3 cameraW;
};

enum class RtxdiResamplingModeType : uint32_t
{
	SpatialResampling,
	TemporalResampling, 
	SpatioTemporalResampling,	//Runs TemporalResampling followed by SpatialResampling
	FusedResampling,			//Combines generate, re-sampling and final sampling into one pass
	MaxCount
};

static const std::unordered_map<RtxdiResamplingModeType, std::string> resamplingModeToString = {
	{RtxdiResamplingModeType::SpatialResampling, "Spatial Re-sampling"},
	{RtxdiResamplingModeType::TemporalResampling, "Temporal Re-sampling"},
	{RtxdiResamplingModeType::SpatioTemporalResampling, "Spatio Temporal Re-sampling"},
	{RtxdiResamplingModeType::FusedResampling, "Fused Re-sampling"}
};

struct RtxdiUserSettings
{
	RtxdiResamplingModeType resamplingMode = RtxdiResamplingModeType::SpatioTemporalResampling;

	uint32_t spatialBiasCorrection = RTXDI_BIAS_CORRECTION_RAY_TRACED;
	uint32_t temporalBiasCorrection = RTXDI_BIAS_CORRECTION_RAY_TRACED;

	bool enablePreviousTLAS = false;
	bool environmentMapImportanceSampling = true;

	float rayEpsilon = 1.0e-4f;
	bool discardInvisibleSamples = true;
	bool enableInitialVisibility = true;
	bool enableFinalVisibility = true;
	bool enablePermutationSampling = false;
	bool visualizeRegirCells = false;

	int numPrimaryRegirSamples = 8;
	int numPrimaryLocalLightSamples = 8;
	int numPrimaryBrdfSamples = 1;
	int numPrimaryInfiniteLightSamples = 1;
	int numPrimaryEnvironmentSamples = 1;
	int maxHistoryLength = 20;
	int numSpatialSamples = 1;
	int numDisocclusionBoostSamples = 8;
	float brdfCutoff = 0.f;
	float spatialSamplingRadius = 32.f;
	float spatialDepthThreshold = 0.1f;
	float spatialNormalThreshold = 0.5f;
	float temporalDepthThreshold = 0.1f;
	float temporalNormalThreshold = 0.5f;
	float boilingFilterStrength = 0.2f;
	uint32_t numRegirBuildSamples = 8;

	struct
	{
		bool enableTemporalResampling = true;
		int maxHistoryLength = 10;
		int maxReservoirAge = 30;
		bool enablePermutationSampling = false;
		bool enableFallbackSampling = true;
		int temporalBiasCorrectionMode = RTXDI_BIAS_CORRECTION_BASIC;
		float boilingFilterStrength = 0.2f;

		bool enableSpatialResampling = true;
		int numSpatialSamples = 2;
		int spatialBiasCorrectionMode = RTXDI_BIAS_CORRECTION_BASIC;
		float spatialSamplingRadius = 32.f;

		bool enableFinalVisibility = true;
		bool enableFinalMIS = true;
	} gi;
};

struct RtxdiBridgeParameters
{
	rtxdi::FrameParameters frameParams;
	donut::math::uint2 frameDims;
	donut::math::float3 cameraPosition;
	donut::math::float2 jitter;
	CameraVectors cameraVectors;

	rtxdi::ReGIRContextParameters ReGIRParams;

	float environmentScale = 1.f;
	float environmentRotation = 0.f;
	
	uint32_t sampleIndex; 
	bool enableAA = false;

	RtxdiUserSettings userSettings;
};

//const uint32_t kMaxReservoirsBuffer = 3;  ///< Number of reservoirs per pixel to allocate (and hence the max # used). Set in RtxdiResource.h

class RtxdiPass
{
public:
	RtxdiPass(
		nvrhi::IDevice* device,
		std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
		std::shared_ptr<donut::engine::CommonRenderPasses> commonRenderPasses,
		nvrhi::BindingLayoutHandle bindlessLayout);
	~RtxdiPass();

	void SetEnvMapDirty();
	void Reset();
	void BeginFrame(
		nvrhi::CommandListHandle commandList,
		const RenderTargets& renderTargets,
		std::shared_ptr<EnvironmentMap> envMap, 
		const std::shared_ptr<donut::engine::ExtendedScene> scene,
		const RtxdiBridgeParameters& bridgeParams,
		const nvrhi::BindingLayoutHandle extraBindingLayout,
		bool useRTXDI);
	void FillConstants(nvrhi::CommandListHandle commandList);
	void Execute(
		nvrhi::CommandListHandle commandList,
		nvrhi::BindingSetHandle extraBindingSet = nullptr);
	void FillGIConstants(nvrhi::CommandListHandle commandList);
	void ExecuteGI(nvrhi::CommandListHandle commandList,
		nvrhi::BindingSetHandle extraBindingSet = nullptr);
	void EndFrame();
	
	std::shared_ptr<RtxdiResources> GetRTXDIResources() { return m_RtxdiResources; }
	nvrhi::BufferHandle GetRTXDIConstants() { return m_RtxdiConstantBuffer; }

private:
	void CreatePipelines(nvrhi::BindingLayoutHandle extraBindingLayout = nullptr, bool useRayQuery = true);
	void CreateBindingSet(const RenderTargets& renderTargets);

	std::unique_ptr<rtxdi::Context> m_RtxdiContext;
	std::shared_ptr<RtxdiResources> m_RtxdiResources;
	std::unique_ptr<PrepareLightsPass> m_PrepareLightsPass;
	std::unique_ptr<GenerateMipsPass> m_EnvironmentMapPdfMipmapPass;
	std::unique_ptr<GenerateMipsPass> m_LocalLightPdfMipmapPass;

	nvrhi::DeviceHandle m_Device; 
	std::shared_ptr<donut::engine::ShaderFactory> m_ShaderFactory;
	std::shared_ptr<donut::engine::CommonRenderPasses> m_CommonRenderPasses;
	std::shared_ptr<donut::engine::ExtendedScene> m_Scene;
	nvrhi::BindingLayoutHandle m_BindingLayout;
	nvrhi::BindingLayoutHandle m_BindlessLayout;
	nvrhi::BindingSetHandle m_BindingSet;
	nvrhi::BindingSetHandle m_PrevBindingSet;
	nvrhi::BufferHandle m_RtxdiConstantBuffer;

	ComputePass m_PresampleLightsPass;
	ComputePass m_PresampleEnvMapPass;
	ComputePass m_PresampleReGIRPass;
	ComputePass m_FinalSamplingPass;
	RayTracingPass m_GenerateInitialSamplesPass;
	RayTracingPass m_SpatialResamplingPass;
	RayTracingPass m_TemporalResamplingPass;

	RayTracingPass m_GITemporalResamplingPass;
	RayTracingPass m_GISpatialResamplingPass;
	RayTracingPass m_GIFinalShadingPass;

	void ExecuteComputePass(
		nvrhi::CommandListHandle& commandList, 
		ComputePass& pass, 
		const char* passName, 
		dm::int3 dispatchSize, 
		nvrhi::BindingSetHandle extraBindingSet = nullptr);

	void ExecuteRayTracingPass(
		nvrhi::CommandListHandle& commandList,
		RayTracingPass& pass,
		const char* passName,
		dm::int2 dispatchSize, 
		nvrhi::IBindingSet* extraBindingSet = nullptr
	);

	uint32_t GetNextReservoirIndex(uint32_t currentIndex);

	donut::engine::ShaderMacro GetReGirMacro(const rtxdi::ContextParameters& contextParameters);

	void FillCommonBridgeConstants(struct RtxdiBridgeConstants& bridgeConstants) const;

	RtxdiBridgeParameters m_BridgeParameters;
	bool m_EnvMapDirty;
	donut::math::uint m_CurrentSurfaceBufferIdx;
	CameraVectors m_CurrentCameraVectors;
	CameraVectors m_PreviousCameraVectors;
	uint32_t m_CurrentSampleIndex; 
	uint32_t m_PreviousSampleIndex;
	uint32_t m_CurrentReservoirIndex;
	uint32_t m_PreviousReservoirIndex;
};

