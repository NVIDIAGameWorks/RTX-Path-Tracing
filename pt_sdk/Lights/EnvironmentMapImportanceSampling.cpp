/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "EnvironmentMapImportanceSampling.h"
#include "EnvironmentMapImportanceSampling_cb.h"
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/TextureCache.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/render/MipMapGenPass.h>

using namespace donut::engine;
using namespace donut::render;

const uint32_t kDefaultDimension = 512;
const uint32_t kDefaultSpp = 64;
const uint32_t kDefaultThreadCount = 16;

EnvironmentMap::EnvironmentMap(nvrhi::IDevice* device, 
	std::shared_ptr<donut::engine::TextureCache> textureCache, 
	std::shared_ptr<donut::engine::ShaderFactory> shaderFactory) 
	: m_Device(device), m_TextureCache(textureCache), 
	m_ShaderFactory(shaderFactory),
	m_EnvMapDimensions(0,0)
{
	//Create sampler 
	nvrhi::SamplerDesc samplerDesc;
	samplerDesc.setAddressU(nvrhi::SamplerAddressMode::Wrap);
	samplerDesc.setAllFilters(true);
	m_EnvironmentMapSampler = m_Device->createSampler(samplerDesc);

	//Create importance map shader and resources
	m_ImportanceMapComputeShader = m_ShaderFactory->CreateShader("app/PathTracer/Scene/Lights/EnvMapSamplerSetup.cs.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
	assert(m_ImportanceMapComputeShader);

	nvrhi::BufferDesc constBufferDesc;
	constBufferDesc.byteSize = sizeof(EnvironmentMapImportanceSamplingConstants);
	constBufferDesc.debugName = "EnvironmentMapImportanceSamplingConstants";
	constBufferDesc.isConstantBuffer = true;
	constBufferDesc.isVolatile = true;
	constBufferDesc.maxVersions = 16;
	m_ImportanceMapCB = m_Device->createBuffer(constBufferDesc);

	samplerDesc.setAllFilters(false);
	samplerDesc.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp);
	m_ImportanceMapSampler = m_Device->createSampler(samplerDesc);

	nvrhi::BindingLayoutDesc layoutDesc;
	layoutDesc.visibility = nvrhi::ShaderType::Compute;
	layoutDesc.bindings = {
		nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
		nvrhi::BindingLayoutItem::Texture_SRV(0),
		nvrhi::BindingLayoutItem::Texture_UAV(0),
		nvrhi::BindingLayoutItem::Sampler(0)
	};
	m_ImportanceMapBindingLayout = m_Device->createBindingLayout(layoutDesc);

	nvrhi::ComputePipelineDesc pipelineDesc;
	pipelineDesc.setComputeShader(m_ImportanceMapComputeShader);
	pipelineDesc.addBindingLayout(m_ImportanceMapBindingLayout);
	m_ImportanceMapPipeline = m_Device->createComputePipeline(pipelineDesc);
}

void EnvironmentMap::LoadTexture(const std::filesystem::path& path,
	std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses,
	nvrhi::CommandListHandle commandList,
	bool enableImportanceMap)
{
	//Check if we have already have a texture
	if (m_EnvironmentMapTexture) {
		Reset();
	}

	commandList->open();
	m_TextureCache->LoadTextureFromFile(path, false, commonPasses.get(), commandList);
	commandList->close();
	m_Device->executeCommandList(commandList);
	m_Device->waitForIdle();

	std::shared_ptr<TextureData> loadedTexture = m_TextureCache->GetLoadedTexture(path);
	if (loadedTexture != nullptr && loadedTexture->format != nvrhi::Format::UNKNOWN )
	{
		m_EnvironmentMapTexture = loadedTexture;
		m_EnvMapDimensions.x = m_EnvironmentMapTexture->texture->getDesc().width;
		m_EnvMapDimensions.y = m_EnvironmentMapTexture->texture->getDesc().height;
	}
    else
    {
        Reset();
        return;
    }

	if(!m_ImportanceMapTexture)
		CreateImportanceMap(kDefaultDimension, kDefaultSpp);
	GenerateImportanceMap(commandList, kDefaultDimension, kDefaultDimension);
}

nvrhi::TextureHandle EnvironmentMap::GetImportanceMap()
{
	return m_ImportanceMapTexture;
}

nvrhi::TextureHandle EnvironmentMap::GetEnvironmentMap()
{
	if (m_EnvironmentMapTexture)
	{
		return m_EnvironmentMapTexture->texture;
	}
	return nullptr;
}

nvrhi::SamplerHandle EnvironmentMap::GetEnvironmentSampler()
{
	return m_EnvironmentMapSampler;
}

nvrhi::SamplerHandle EnvironmentMap::GetImportanceSampler()
{
	return m_ImportanceMapSampler;
}

void EnvironmentMap::Reset()
{
	if (m_EnvironmentMapTexture)
	{
		m_TextureCache->UnloadTexture(m_EnvironmentMapTexture);
		m_EnvironmentMapTexture = nullptr;
	}
	m_ImportanceMapBindingSet = nullptr;
}

void EnvironmentMap::SetConstantData(float intensity, float3 tintColor, float3 rotation, EnvMapData &envMapData, EnvMapSamplerData &envMapSamplerData)
{
	envMapData.intensity = intensity;
	envMapData.tint = tintColor;
	float3 rotationInRadians = radians(rotation);
	affine3 rotationTransform = donut::math::rotation(rotationInRadians);
	affine3 inverseTransform = inverse(rotationTransform);
	affineToColumnMajor(rotationTransform, envMapData.transform);
	affineToColumnMajor(inverseTransform, envMapData.invTransform);

	envMapSamplerData.importanceInvDim = GetImportanceMapInverseDimensions();
	envMapSamplerData.importanceBaseMip = GetImportanceMapIsBaseMip();

	m_EnvMapData = envMapData;
	m_EnvMapSamplerData = envMapSamplerData;
}

void EnvironmentMap::CreateImportanceMap(const uint32_t dimensions, const uint32_t samples)
{
	assert(ispow2(dimensions) && ispow2(samples));
		
	uint32_t mips = (uint32_t)log2(dimensions) + 1;
	assert((1u << (mips - 1)) == dimensions);
	assert(mips > 1 && mips <= 12);
	
	nvrhi::TextureDesc texDesc;
	texDesc.format = nvrhi::Format::R32_FLOAT;
	texDesc.width = dimensions;
	texDesc.height = dimensions;
	texDesc.mipLevels = mips;
	texDesc.isRenderTarget = true;
	texDesc.isUAV = true;
	texDesc.debugName = "ImportanceMap";
	texDesc.setInitialState(nvrhi::ResourceStates::UnorderedAccess);
	texDesc.keepInitialState = true;
	m_ImportanceMapTexture = m_Device->createTexture(texDesc);
	 
	assert(m_ImportanceMapTexture);

	m_MipMapPass = std::make_unique<MipMapGenPass>(m_Device, m_ShaderFactory, m_ImportanceMapTexture, MipMapGenPass::MODE_COLOR);
}

void EnvironmentMap::GenerateImportanceMap(nvrhi::CommandListHandle commandList, const uint32_t dimensions, const uint32_t samples)
{
	assert(m_EnvironmentMapTexture);

	if (!m_ImportanceMapBindingSet)
	{
		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, m_ImportanceMapCB),
			nvrhi::BindingSetItem::Texture_SRV(0, m_EnvironmentMapTexture->texture),
			nvrhi::BindingSetItem::Texture_UAV(0, m_ImportanceMapTexture),
			nvrhi::BindingSetItem::Sampler(0, m_EnvironmentMapSampler),
		};
		m_ImportanceMapBindingSet = m_Device->createBindingSet(bindingSetDesc, m_ImportanceMapBindingLayout);
	}

	nvrhi::ComputeState state;
	state.pipeline = m_ImportanceMapPipeline;
	state.bindings = { m_ImportanceMapBindingSet };

	uint32_t samplesX = std::max(1u, (uint32_t)std::sqrt(samples));
	uint32_t samplesY = samples / samplesX;
	uint32_t groupCount = dimensions / kDefaultThreadCount;

	EnvironmentMapImportanceSamplingConstants constants = {};
	constants.outputDim = dimensions;
	constants.outputDimInSamples = uint2(dimensions * samplesX, dimensions * samplesY);
	constants.numSamples = uint2(samplesX, samplesY);
	constants.invSamples = 1.f / (samplesX * samplesY);
	
	commandList->open();
	commandList->writeBuffer(m_ImportanceMapCB, &constants, sizeof(EnvironmentMapImportanceSamplingConstants));
	commandList->setComputeState(state);
	commandList->dispatch(groupCount, groupCount);
	m_MipMapPass->Dispatch(commandList);
	commandList->close();

	m_Device->executeCommandList(commandList);
	m_Device->waitForIdle();
}
float2 EnvironmentMap::GetImportanceMapInverseDimensions()
{
	if (!m_ImportanceMapTexture)
		return float2(0, 0);
	return 1.f / float2((float)m_ImportanceMapTexture->getDesc().width, (float)m_ImportanceMapTexture->getDesc().height);
}

uint32_t EnvironmentMap::GetImportanceMapIsBaseMip()
{
	if (!m_ImportanceMapTexture)
		return 0;
	return m_ImportanceMapTexture->getDesc().mipLevels - 1;
}

EnvMapData EnvironmentMap::GetEnvMapData()
{
	return m_EnvMapData;
}

