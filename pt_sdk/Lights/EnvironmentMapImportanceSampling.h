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
#include <memory>
#include <filesystem>
#include <donut/core/math/math.h>
#include <donut/engine/SceneTypes.h>

using namespace donut::math;
#include "../PathTracer/PathTracerShared.h"

namespace donut::engine
{
	class TextureCache;
	class TextureHandle;
	class ShaderFactory;
	class CommonRenderPasses;
}

namespace donut::render
{
	class MipMapGenPass;
}

struct EnvironmentMapImportanceSamplingParameters
{
	float3 tintColor = { 1.f, 1.f, 1.f };
	float intensity = 1.f;
	float3 rotationXYZ = { 0.f, 0.f, 0.f };
    bool loaded = false;
    bool enabled = true;
};

//namespace donut::render // should we use donut::pt_sdk for all our path tracing stuff?
//{
	class EnvironmentMap
	{
	private:
		nvrhi::DeviceHandle m_Device;
		std::shared_ptr<donut::engine::TextureCache> m_TextureCache;
		std::shared_ptr<donut::engine::ShaderFactory> m_ShaderFactory;
		std::shared_ptr<donut::engine::LoadedTexture> m_EnvironmentMapTexture;
		nvrhi::TextureHandle m_ImportanceMapTexture;
		nvrhi::SamplerHandle m_EnvironmentMapSampler;
		nvrhi::SamplerHandle m_ImportanceMapSampler; 

		nvrhi::ShaderHandle m_ImportanceMapComputeShader;
		nvrhi::BufferHandle m_ImportanceMapCB;
		nvrhi::BindingSetHandle m_ImportanceMapBindingSet;
		nvrhi::BindingLayoutHandle m_ImportanceMapBindingLayout;
		nvrhi::ComputePipelineHandle m_ImportanceMapPipeline;		
		std::unique_ptr<donut::render::MipMapGenPass> m_MipMapPass;

		EnvMapData m_EnvMapData;
		EnvMapSamplerData m_EnvMapSamplerData;

		void CreateImportanceMap(const uint32_t dimensions, const uint32_t samples);
		void GenerateImportanceMap(nvrhi::CommandListHandle commandList, const uint32_t dimensions, const uint32_t samples);
	public:
		EnvironmentMap(nvrhi::IDevice* device, 
			std::shared_ptr<donut::engine::TextureCache> textureCache, 
			std::shared_ptr<donut::engine::ShaderFactory> shaderFactory);
		void LoadTexture(const std::filesystem::path &path,
			std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses,
			nvrhi::CommandListHandle commandList,
			bool enableImportanceMap = true);
		/*void LoadTexture(const std::filesystem::path& path,
			float3 tint, 
			float intensity, 
			float3 rotation, 
			std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses,
			nvrhi::CommandListHandle commandList,
			bool enableImportanceMap = true);*/

		nvrhi::TextureHandle GetEnvironmentMap();
		nvrhi::TextureHandle GetImportanceMap();
		nvrhi::SamplerHandle GetEnvironmentSampler();
		nvrhi::SamplerHandle GetImportanceSampler();
		void SetConstantData(float intensity, float3 tintColor, float3 rotation, 
			EnvMapData& envMapData, EnvMapSamplerData& envMapSamplerData);
		float2 GetImportanceMapInverseDimensions();
		uint32_t GetImportanceMapIsBaseMip();
		EnvMapData GetEnvMapData();
		void Reset();
	};

//}
