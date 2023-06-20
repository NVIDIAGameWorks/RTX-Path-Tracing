/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "RtxdiPass.h"
#include <rtxdi/RTXDI.h>

#include "../../external/RTXDI/rtxdi-sdk/include/rtxdi/RtxdiParameters.h"
#include "RtxdiResources.h"
#include "PrepareLightsPass.h"
#include "donut/engine/ShaderFactory.h"
#include "donut/engine/CommonRenderPasses.h"
#include "donut/engine/View.h"
#include "GeneratePdfMipsPass.h"
#include "../Lights/EnvironmentMapImportanceSampling.h"
#include "../ExtendedScene.h"
#include "ShaderParameters.h"

#include "../RenderTargets.h"

using namespace donut::math;
using namespace donut::engine;
using namespace donut::render;

RtxdiPass::RtxdiPass(
	nvrhi::IDevice* device,
	std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
	std::shared_ptr<donut::engine::CommonRenderPasses> commonRenderPasses,
	nvrhi::BindingLayoutHandle bindlessLayout) :
		m_Device(device),
		m_ShaderFactory(shaderFactory),
		m_CommonRenderPasses(commonRenderPasses),
		m_BindlessLayout(bindlessLayout),
		m_EnvMapDirty(false),
		m_PreviousReservoirIndex(0)
{
	//Create binding layouts
	nvrhi::BindingLayoutDesc layoutDesc;
	layoutDesc.visibility = nvrhi::ShaderType::All;
	layoutDesc.bindings = {
		nvrhi::BindingLayoutItem::StructuredBuffer_SRV(21),		//t_LightDataBuffer
		nvrhi::BindingLayoutItem::TypedBuffer_SRV(22),			//t_NeighborOffsets
		nvrhi::BindingLayoutItem::TypedBuffer_SRV(23),			//t_LightIndexMappingBuffer
		nvrhi::BindingLayoutItem::Texture_SRV(24),				//t_EnvironmentPdfTexture
		nvrhi::BindingLayoutItem::Texture_SRV(25),				//t_LocalLightPdfTexture
		nvrhi::BindingLayoutItem::StructuredBuffer_SRV(26),		//t_GeometryInstanceToLight
		
		nvrhi::BindingLayoutItem::StructuredBuffer_UAV(13),		//u_LightReservoirs
		nvrhi::BindingLayoutItem::StructuredBuffer_UAV(14),		//u_GIReservoirs
		nvrhi::BindingLayoutItem::TypedBuffer_UAV(15),			//u_RisBuffer
		nvrhi::BindingLayoutItem::TypedBuffer_UAV(16),			//u_RisLightDataBuffer

		nvrhi::BindingLayoutItem::VolatileConstantBuffer(5),	//g_RtxdiBridgeConst

		nvrhi::BindingLayoutItem::Sampler(4)
	};
	m_BindingLayout = m_Device->createBindingLayout(layoutDesc);

	m_RtxdiConstantBuffer = m_Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(RtxdiBridgeConstants), "RtxdiBridgeConstants", 16));
}

RtxdiPass::~RtxdiPass(){}

void RtxdiPass::SetEnvMapDirty()
{
	m_EnvMapDirty = true;
}

void RtxdiPass::CreatePipelines(nvrhi::BindingLayoutHandle extraBindingLayout /*= nullptr*/, bool useRayQuery /*= true*/)
{
	rtxdi::ContextParameters contextParameters;
	contextParameters = m_RtxdiContext->GetParameters();

	std::vector<donut::engine::ShaderMacro> regirMacros = { GetReGirMacro(contextParameters) };

	m_PresampleLightsPass.Init(m_Device, *m_ShaderFactory, "app/RTXDI/PresampleLights.hlsl", {}, m_BindingLayout, extraBindingLayout, m_BindlessLayout);
	m_PresampleEnvMapPass.Init(m_Device, *m_ShaderFactory, "app/RTXDI/PresampleEnvironmentMap.hlsl", {}, m_BindingLayout, extraBindingLayout, m_BindlessLayout);
	if (contextParameters.ReGIR.Mode != rtxdi::ReGIRMode::Disabled)
	{
		m_PresampleReGIRPass.Init(m_Device, *m_ShaderFactory, "app/RTXDI/PresampleReGIR.hlsl", regirMacros, m_BindingLayout, extraBindingLayout, m_BindlessLayout);
	}
	
	m_GenerateInitialSamplesPass.Init(m_Device, *m_ShaderFactory, "app/RTXDI/GenerateInitialSamples.hlsl", 
		regirMacros, useRayQuery, RTXDI_SCREEN_SPACE_GROUP_SIZE, m_BindingLayout, extraBindingLayout, m_BindlessLayout);
	m_SpatialResamplingPass.Init(m_Device, *m_ShaderFactory, "app/RTXDI/SpatialResampling.hlsl",
		{}, useRayQuery, RTXDI_SCREEN_SPACE_GROUP_SIZE, m_BindingLayout, extraBindingLayout, m_BindlessLayout);
	m_TemporalResamplingPass.Init(m_Device, *m_ShaderFactory, "app/RTXDI/TemporalResampling.hlsl",
		{}, useRayQuery, RTXDI_SCREEN_SPACE_GROUP_SIZE, m_BindingLayout, extraBindingLayout, m_BindlessLayout);
	
	m_FinalSamplingPass.Init(m_Device, *m_ShaderFactory, "app/RTXDI/DIFinalShading.hlsl", { { "USE_RAY_QUERY", "1" } }, m_BindingLayout, extraBindingLayout, m_BindlessLayout);
	
	m_GISpatialResamplingPass.Init(m_Device, *m_ShaderFactory, "app/RTXDI/GISpatialResampling.hlsl",
		{}, useRayQuery, RTXDI_SCREEN_SPACE_GROUP_SIZE, m_BindingLayout, extraBindingLayout, m_BindlessLayout);
	m_GITemporalResamplingPass.Init(m_Device, *m_ShaderFactory, "app/RTXDI/GITemporalResampling.hlsl",
		{}, useRayQuery, RTXDI_SCREEN_SPACE_GROUP_SIZE, m_BindingLayout, extraBindingLayout, m_BindlessLayout);
	m_GIFinalShadingPass.Init(m_Device, *m_ShaderFactory, "app/RTXDI/GIFinalShading.hlsl",
		{}, useRayQuery, RTXDI_SCREEN_SPACE_GROUP_SIZE, m_BindingLayout, extraBindingLayout, m_BindlessLayout);
    m_FusedDIGIFinalShadingPass.Init(m_Device, *m_ShaderFactory, "app/RTXDI/FusedDIGIFinalShading.hlsl",
        {}, useRayQuery, RTXDI_SCREEN_SPACE_GROUP_SIZE, m_BindingLayout, extraBindingLayout, m_BindlessLayout);
}

void RtxdiPass::CreateBindingSet(const RenderTargets& renderTargets)
{
	for (int currentFrame = 0; currentFrame <= 1; currentFrame++)
	{
		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			// RTXDI resources
			nvrhi::BindingSetItem::StructuredBuffer_SRV(21, m_RtxdiResources->LightDataBuffer),
			nvrhi::BindingSetItem::TypedBuffer_SRV(22, m_RtxdiResources->NeighborOffsetsBuffer),
			nvrhi::BindingSetItem::TypedBuffer_SRV(23, m_RtxdiResources->LightIndexMappingBuffer),
			nvrhi::BindingSetItem::Texture_SRV(24, m_RtxdiResources->EnvironmentPdfTexture),
			nvrhi::BindingSetItem::Texture_SRV(25, m_RtxdiResources->LocalLightPdfTexture),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(26, m_RtxdiResources->GeometryInstanceToLightBuffer),
			
			// Render targets
			nvrhi::BindingSetItem::StructuredBuffer_UAV(13, m_RtxdiResources->LightReservoirBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(14, m_RtxdiResources->GIReservoirBuffer),
			nvrhi::BindingSetItem::TypedBuffer_UAV(15, m_RtxdiResources->RisBuffer),
			nvrhi::BindingSetItem::TypedBuffer_UAV(16, m_RtxdiResources->RisLightDataBuffer),
			
			nvrhi::BindingSetItem::ConstantBuffer(5, m_RtxdiConstantBuffer),

			nvrhi::BindingSetItem::Sampler(4, m_CommonRenderPasses->m_LinearWrapSampler)
		};

		const nvrhi::BindingSetHandle bindingSet = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);
		if (currentFrame)
			m_BindingSet = bindingSet;
		else
			m_PrevBindingSet = bindingSet;
	}
}

void RtxdiPass::Reset()
{
	// Clear objects that depend on resolution 
	m_RtxdiContext = nullptr;
	m_RtxdiResources = nullptr;
	m_EnvironmentMapPdfMipmapPass = nullptr;
	m_LocalLightPdfMipmapPass = nullptr;
	m_BindingSet = nullptr;
}

void RtxdiPass::BeginFrame(
	nvrhi::CommandListHandle commandList,
	const RenderTargets& renderTargets,
	std::shared_ptr<EnvironmentMap> envMap,
	const std::shared_ptr<donut::engine::ExtendedScene> scene,
	const RtxdiBridgeParameters& bridgeParams,
	const nvrhi::BindingLayoutHandle extraBindingLayout,
	bool UseReSTIRDI)
{
	m_Scene = scene;
	m_BridgeParameters = bridgeParams;

	//Some setting changes requires the context to be recreated
	if (m_RtxdiContext)
	{
		if (m_RtxdiContext->GetParameters().ReGIR.Mode != m_BridgeParameters.userSettings.reGirSettings.Mode)
			Reset();
	}

	if (!m_RtxdiContext)
	{
		rtxdi::ContextParameters contextParams;
		contextParams.RenderWidth = m_BridgeParameters.frameDims.x;
		contextParams.RenderHeight = m_BridgeParameters.frameDims.y;
		contextParams.ReGIR = m_BridgeParameters.userSettings.reGirSettings;
		m_RtxdiContext = std::make_unique<rtxdi::Context>(contextParams);

		// Some RTXDI context settings affect the shader permutations
		CreatePipelines(extraBindingLayout, true);
	}

	if (!m_PrepareLightsPass)
	{
		m_PrepareLightsPass = std::make_unique<PrepareLightsPass>(m_Device, m_ShaderFactory, m_CommonRenderPasses, nullptr, m_BindlessLayout);
		m_PrepareLightsPass->CreatePipeline();
	}

	m_PrepareLightsPass->SetScene(m_Scene, envMap);

	//Check if resources have changed
	bool envMapPresent = envMap != nullptr;
	uint32_t numEmissiveMeshes, numEmissiveTriangles = 0;
	m_PrepareLightsPass->CountLightsInScene(numEmissiveMeshes, numEmissiveTriangles);
	uint32_t numPrimitiveLights = uint32_t(m_Scene->GetSceneGraph()->GetLights().size());
	uint32_t numGeometryInstances = uint32_t(m_Scene->GetSceneGraph()->GetGeometryInstancesCount());

	uint2 envMapSize = envMapPresent ?
		uint2(envMap->GetEnvironmentMap()->getDesc().width, envMap->GetEnvironmentMap()->getDesc().height)
		: uint2(2,2);

	if (m_RtxdiResources && (
		envMapSize.x > m_RtxdiResources->EnvironmentPdfTexture->getDesc().width ||
		envMapSize.y > m_RtxdiResources->EnvironmentPdfTexture->getDesc().height ||
		numEmissiveMeshes > m_RtxdiResources->GetMaxEmissiveMeshes() ||
		numEmissiveTriangles > m_RtxdiResources->GetMaxEmissiveTriangles() ||
		numPrimitiveLights > m_RtxdiResources->GetMaxPrimitiveLights() ||
		numGeometryInstances > m_RtxdiResources->GetMaxGeometryInstances()))
	{
		m_RtxdiResources = nullptr;
	}
	
	bool rtxdiResourceCreated = false;
	
	if (!m_RtxdiResources)
	{
		uint32_t meshAllocationQuantum = 128;
		uint32_t triangleAllocationQuantum = 1024;
		uint32_t primitiveAllocationQuantum = 128;

		m_RtxdiResources = std::make_shared<RtxdiResources>(
			m_Device,
			*m_RtxdiContext,
			(numEmissiveMeshes + meshAllocationQuantum - 1) & ~(meshAllocationQuantum - 1),
			(numEmissiveTriangles + triangleAllocationQuantum - 1) & ~(triangleAllocationQuantum - 1),
			(numPrimitiveLights + primitiveAllocationQuantum - 1) & ~(primitiveAllocationQuantum - 1),
			numGeometryInstances,
			envMapSize.x,
			envMapSize.y);

		rtxdiResourceCreated = true;
	}

	if (rtxdiResourceCreated)
	{
		m_PrepareLightsPass->CreateBindingSet(*m_RtxdiResources, renderTargets);
		m_RtxdiResources->InitializeNeighborOffsets(commandList, *m_RtxdiContext);
	}

	if (rtxdiResourceCreated || m_BindingSet == nullptr)
	{
		CreateBindingSet(renderTargets);
	}

	// In case where the RTXDI context is only needed for ReSTIR GI, skip the light preparation and mip chain passes
	if (UseReSTIRDI)
	{
		//This pass needs to happen before we fill the constant buffers 
		commandList->beginMarker("Prepare Light");
		m_PrepareLightsPass->Process(commandList, *m_RtxdiContext, m_BridgeParameters.frameParams);
		commandList->endMarker();

		//Create PDF mip chain passes
		if (envMapPresent && (!m_EnvironmentMapPdfMipmapPass || rtxdiResourceCreated || m_EnvMapDirty))
		{
			m_EnvironmentMapPdfMipmapPass = std::make_unique<GenerateMipsPass>(
				m_Device,
				m_ShaderFactory,
				envMap->GetEnvironmentMap(),
				m_RtxdiResources->EnvironmentPdfTexture);

			SetEnvMapDirty();
		}

		if (!m_LocalLightPdfMipmapPass || rtxdiResourceCreated)
		{
			m_LocalLightPdfMipmapPass = std::make_unique<GenerateMipsPass>(
				m_Device,
				m_ShaderFactory,
				nullptr,
				m_RtxdiResources->LocalLightPdfTexture);
		}
	}

	FillConstants(commandList);
}

void RtxdiPass::Execute(
	nvrhi::CommandListHandle commandList,
	nvrhi::BindingSetHandle extraBindingSet,
    bool skipFinal
)
{
	commandList->beginMarker("ReSTIR DI");

	commandList->beginMarker("GeneratePDFTextures");
	
	m_LocalLightPdfMipmapPass->Process(commandList);

	//if environment map has updated, generate pdf texture. 
	if (m_EnvMapDirty)
	{
       if (m_EnvironmentMapPdfMipmapPass!=nullptr)
		    m_EnvironmentMapPdfMipmapPass->Process(commandList);
		m_EnvMapDirty = false;
	}
	commandList->endMarker();
	
	// Pre-sample lights
	if (m_BridgeParameters.frameParams.enableLocalLightImportanceSampling && m_BridgeParameters.frameParams.numLocalLights > 0)
	{
		dm::int3 presampleDispatchSize = {
			dm::div_ceil(m_RtxdiContext->GetParameters().TileSize, RTXDI_PRESAMPLING_GROUP_SIZE),
			int(m_RtxdiContext->GetParameters().TileCount),
			1
		};
		ExecuteComputePass(commandList, m_PresampleLightsPass, "Pre-sample Lights", presampleDispatchSize, extraBindingSet);
	}

	if (m_BridgeParameters.frameParams.environmentLightPresent)
	{
		dm::int3 presampleDispatchSize = {
			dm::div_ceil(m_RtxdiContext->GetParameters().EnvironmentTileSize, RTXDI_PRESAMPLING_GROUP_SIZE),
			int(m_RtxdiContext->GetParameters().EnvironmentTileCount),
			1
		};
		ExecuteComputePass(commandList, m_PresampleEnvMapPass, "Pre-sample Envmap", presampleDispatchSize, extraBindingSet);
	}

	//Build ReGIR structure 
	if (m_RtxdiContext->GetParameters().ReGIR.Mode != rtxdi::ReGIRMode::Disabled &&
		m_BridgeParameters.frameParams.numLocalLights > 0)
	{
		dm::int3 worldGridDispatchSize = {
			dm::div_ceil(m_RtxdiContext->GetReGIRLightSlotCount(), RTXDI_GRID_BUILD_GROUP_SIZE), 1, 1 };
		ExecuteComputePass(commandList, m_PresampleReGIRPass, "Pre-sample ReGir", worldGridDispatchSize, extraBindingSet);
	}

	dm::int2 dispatchSize = { (int) m_RtxdiContext->GetParameters().RenderWidth, (int) m_RtxdiContext->GetParameters().RenderHeight };
	
	if (m_BridgeParameters.userSettings.resamplingMode == RtxdiResamplingModeType::FusedResampling)
	{
		// TODO: combine initial, temporal, spatial and final sampling in one pass
        // In case this is implemented, probably no point in doing fused ReSTIR-DI and ReSTIR-GI sampling, so 
        // then remove skipFinal and related logic.
	}
	else
	{
		//Generate sample, pick re-sampling method, final sampling
		ExecuteRayTracingPass(commandList, m_GenerateInitialSamplesPass, "Generate Initial Samples", dispatchSize, extraBindingSet);

		if (m_BridgeParameters.userSettings.resamplingMode == RtxdiResamplingModeType::TemporalResampling ||
			m_BridgeParameters.userSettings.resamplingMode == RtxdiResamplingModeType::SpatioTemporalResampling)
		{
			nvrhi::utils::BufferUavBarrier(commandList, m_RtxdiResources->LightReservoirBuffer);
			ExecuteRayTracingPass(commandList, m_TemporalResamplingPass, "Temporal Re-sampling", dispatchSize, extraBindingSet);
		}
		
		if (m_BridgeParameters.userSettings.resamplingMode == RtxdiResamplingModeType::SpatialResampling ||
			m_BridgeParameters.userSettings.resamplingMode == RtxdiResamplingModeType::SpatioTemporalResampling)
		{
			nvrhi::utils::BufferUavBarrier(commandList, m_RtxdiResources->LightReservoirBuffer);
			ExecuteRayTracingPass(commandList, m_SpatialResamplingPass, "Spatial Re-sampling", dispatchSize, extraBindingSet);

		}

        //Full screen light sampling pass
        nvrhi::utils::BufferUavBarrier(commandList, m_RtxdiResources->LightReservoirBuffer);

        if (!skipFinal)
        {
            // TODO: fix this
            dm::int3 screenSpaceDispatchSize = {
                ((int)m_RtxdiContext->GetParameters().RenderWidth + RTXDI_SCREEN_SPACE_GROUP_SIZE - 1) / RTXDI_SCREEN_SPACE_GROUP_SIZE,
                ((int)m_RtxdiContext->GetParameters().RenderHeight + RTXDI_SCREEN_SPACE_GROUP_SIZE - 1) / RTXDI_SCREEN_SPACE_GROUP_SIZE,
                1 };

            ExecuteComputePass(commandList, m_FinalSamplingPass, "Final Sampling", screenSpaceDispatchSize, extraBindingSet);
        }
    }
	
	commandList->endMarker();
}

void RtxdiPass::FillConstants(nvrhi::CommandListHandle commandList)
{
	// Set the ReGir center and the camera position 
	m_BridgeParameters.frameParams.regirCenter = { m_BridgeParameters.cameraPosition.x, m_BridgeParameters.cameraPosition.y,m_BridgeParameters.cameraPosition.z };

	RtxdiBridgeConstants bridgeConstants{};
	m_RtxdiContext->FillRuntimeParameters(bridgeConstants.runtimeParams, m_BridgeParameters.frameParams);
	FillSharedConstants(bridgeConstants);
	FillDIConstants(bridgeConstants.reStirDI);
	FillGIConstants(bridgeConstants.reStirGI);

	commandList->writeBuffer(m_RtxdiConstantBuffer, &bridgeConstants, sizeof(RtxdiBridgeConstants));
}

void RtxdiPass::FillSharedConstants(struct RtxdiBridgeConstants& bridgeConstants) const
{
	bridgeConstants.frameIndex = m_BridgeParameters.frameParams.frameIndex;
	bridgeConstants.frameDim = m_BridgeParameters.frameDims;
	bridgeConstants.rayEpsilon = m_BridgeParameters.userSettings.rayEpsilon;
	bridgeConstants.environmentMapImportanceSampling = m_BridgeParameters.userSettings.environmentMapImportanceSampling;
	bridgeConstants.localLightPdfTextureSize = uint2(m_RtxdiResources->LocalLightPdfTexture->getDesc().width, m_RtxdiResources->LocalLightPdfTexture->getDesc().height);
	bridgeConstants.localLightPdfLastMipLevel = m_RtxdiResources->LocalLightPdfTexture->getDesc().mipLevels - 1 ;
	bridgeConstants.environmentPdfTextureSize = uint2(m_RtxdiResources->EnvironmentPdfTexture->getDesc().width, m_RtxdiResources->EnvironmentPdfTexture->getDesc().height);
	bridgeConstants.environmentPdfLastMipLevel = m_RtxdiResources->EnvironmentPdfTexture->getDesc().mipLevels - 1;
	bridgeConstants.maxLights = uint32_t(m_RtxdiResources->LightDataBuffer->getDesc().byteSize / (sizeof(PolymorphicLightInfo) * 2));;
}

void RtxdiPass::FillDIConstants(struct ReStirDIConstants& diConstants)
{
	//Reservoir variables
	if (m_BridgeParameters.userSettings.resamplingMode == RtxdiResamplingModeType::Disabled)
	{
		diConstants.initialOutputBufferIndex = 0;
		diConstants.finalShadingReservoir = diConstants.initialOutputBufferIndex;
	}
	else if (m_BridgeParameters.userSettings.resamplingMode == RtxdiResamplingModeType::FusedResampling)
	{
		diConstants.temporalInputBufferIndex = m_PreviousReservoirIndex;
		diConstants.finalShadingReservoir = 1 - m_PreviousReservoirIndex;
	}
	else
	{	//Spatio Temporal Re-sampling assumes we have at least 3 reservoirs. The number of reservoirs are set here RtxdiResources::c_NumReservoirBuffers;
		//For the initial output we want an index which is not the temporal input or output index
		diConstants.initialOutputBufferIndex = GetNextReservoirIndex(m_PreviousReservoirIndex);
		diConstants.temporalInputBufferIndex = m_PreviousReservoirIndex;
		diConstants.temporalOutputBufferIndex = GetNextReservoirIndex(m_PreviousReservoirIndex);

		if (m_BridgeParameters.userSettings.resamplingMode == RtxdiResamplingModeType::TemporalResampling || m_BridgeParameters.userSettings.resamplingMode == RtxdiResamplingModeType::SpatioTemporalResampling)
			diConstants.spatialInputBufferIndex = diConstants.temporalOutputBufferIndex;
		else
			diConstants.spatialInputBufferIndex = diConstants.initialOutputBufferIndex;

		diConstants.spatialOutputBufferIndex = GetNextReservoirIndex(diConstants.spatialInputBufferIndex);

		if (m_BridgeParameters.userSettings.resamplingMode == RtxdiResamplingModeType::SpatialResampling || m_BridgeParameters.userSettings.resamplingMode == RtxdiResamplingModeType::SpatioTemporalResampling)
			diConstants.finalShadingReservoir = diConstants.spatialOutputBufferIndex;
		else
			diConstants.finalShadingReservoir = diConstants.temporalOutputBufferIndex;
	}

	m_CurrentReservoirIndex = diConstants.finalShadingReservoir;

	//Generate initial samples variables
	diConstants.numPrimaryRegirSamples = m_BridgeParameters.userSettings.numPrimaryRegirSamples;
	diConstants.numPrimaryLocalLightSamples = m_BridgeParameters.userSettings.numPrimaryLocalLightSamples;
	diConstants.numPrimaryInfiniteLightSamples = m_BridgeParameters.userSettings.numPrimaryInfiniteLightSamples;
	diConstants.numPrimaryEnvironmentSamples = m_BridgeParameters.userSettings.numPrimaryEnvironmentSamples;
	diConstants.numPrimaryBrdfSamples = m_BridgeParameters.userSettings.numPrimaryBrdfSamples;
	diConstants.brdfCutoff = m_BridgeParameters.userSettings.brdfCutoff;
	diConstants.enableInitialVisibility = m_BridgeParameters.userSettings.enableInitialVisibility;

	//Spatial re-sampling variables
	diConstants.numSpatialSamples = m_BridgeParameters.userSettings.numSpatialSamples;
	diConstants.numDisocclusionBoostSamples = m_BridgeParameters.userSettings.numDisocclusionBoostSamples;
	diConstants.spatialBiasCorrection = m_BridgeParameters.userSettings.spatialBiasCorrection;
	diConstants.spatialSamplingRadius = m_BridgeParameters.userSettings.spatialSamplingRadius;
	diConstants.spatialDepthThreshold = m_BridgeParameters.userSettings.spatialDepthThreshold;
	diConstants.spatialNormalThreshold = m_BridgeParameters.userSettings.spatialNormalThreshold;
	diConstants.discountNaiveSamples = m_BridgeParameters.userSettings.discountNaiveSamples;

	//Temporal re-sampling variables
	diConstants.maxHistoryLength = m_BridgeParameters.userSettings.maxHistoryLength;
	diConstants.temporalBiasCorrection = m_BridgeParameters.userSettings.temporalBiasCorrection;
	diConstants.temporalDepthThreshold = m_BridgeParameters.userSettings.temporalDepthThreshold;
	diConstants.temporalNormalThreshold = m_BridgeParameters.userSettings.temporalNormalThreshold;
	diConstants.discardInvisibleSamples = m_BridgeParameters.userSettings.discardInvisibleSamples;
	diConstants.enablePermutationSampling = m_BridgeParameters.userSettings.enablePermutationSampling;
	diConstants.boilingFilterStrength = m_BridgeParameters.userSettings.boilingFilterStrength;

	//Final sampling pass 
	diConstants.enableFinalVisibility = m_BridgeParameters.userSettings.enableFinalVisibility;
	//diConstants.visualizeRegirCells = m_BridgeParameters.userSettings.visualizeRegirCells;

	//ReGir 
	diConstants.numRegirBuildSamples = m_BridgeParameters.userSettings.numRegirBuildSamples;
}

void RtxdiPass::FillGIConstants(struct ReStirGIConstants& giConstants)
{
	const auto& settings = m_BridgeParameters.userSettings;

	if (settings.gi.enableSpatialResampling)
	{
		giConstants.temporalInputBufferIndex = 1;
		giConstants.temporalOutputBufferIndex = 0;
		giConstants.spatialInputBufferIndex = 0;
		giConstants.spatialOutputBufferIndex = 1;
		giConstants.finalShadingReservoir = 1;
	}
	else
	{
		giConstants.temporalInputBufferIndex = m_BridgeParameters.frameParams.frameIndex & 1;
		giConstants.temporalOutputBufferIndex = !giConstants.temporalInputBufferIndex;
		giConstants.finalShadingReservoir = giConstants.temporalOutputBufferIndex;
	}

    // Temporal resampling variables
	// Temporal and initial passes are combined, so need to pass the toggle into the shader.
	giConstants.enableTemporalResampling = settings.gi.enableTemporalResampling;
	giConstants.maxHistoryLength = settings.gi.maxHistoryLength;
	giConstants.maxReservoirAge = settings.gi.maxReservoirAge;
	giConstants.enablePermutationSampling = settings.gi.enablePermutationSampling;
	giConstants.enableFallbackSampling = settings.gi.enableFallbackSampling;
	giConstants.boilingFilterStrength = settings.gi.boilingFilterStrength;
	giConstants.temporalDepthThreshold = settings.temporalDepthThreshold;
	giConstants.temporalNormalThreshold = settings.temporalNormalThreshold;
	giConstants.temporalBiasCorrection = settings.gi.temporalBiasCorrectionMode;
	if (giConstants.temporalBiasCorrection == RTXDI_BIAS_CORRECTION_PAIRWISE)
		giConstants.temporalBiasCorrection = RTXDI_BIAS_CORRECTION_RAY_TRACED;

	// Spatial resampling variables
	giConstants.numSpatialSamples = settings.gi.numSpatialSamples;
	giConstants.spatialSamplingRadius = settings.gi.spatialSamplingRadius;
	giConstants.spatialDepthThreshold = settings.spatialDepthThreshold;
	giConstants.spatialNormalThreshold = settings.spatialNormalThreshold;
	giConstants.spatialBiasCorrection = settings.gi.spatialBiasCorrectionMode;
	if (giConstants.spatialBiasCorrection == RTXDI_BIAS_CORRECTION_PAIRWISE)
		giConstants.spatialBiasCorrection = RTXDI_BIAS_CORRECTION_RAY_TRACED;
		
	// Final shading pass 
	giConstants.enableFinalVisibility = settings.gi.enableFinalVisibility;
	giConstants.enableFinalMIS = settings.gi.enableFinalMIS;
}

void RtxdiPass::ExecuteGI(nvrhi::CommandListHandle commandList, nvrhi::BindingSetHandle extraBindingSet, bool skipFinal)
{
	commandList->beginMarker("ReSTIR GI");

	dm::int2 dispatchSize = { (int)m_RtxdiContext->GetParameters().RenderWidth, (int)m_RtxdiContext->GetParameters().RenderHeight };

	ExecuteRayTracingPass(commandList, m_GITemporalResamplingPass, "Temporal Resampling", dispatchSize, extraBindingSet);

	if (m_BridgeParameters.userSettings.gi.enableSpatialResampling)
	{
		nvrhi::utils::BufferUavBarrier(commandList, m_RtxdiResources->GIReservoirBuffer);

		ExecuteRayTracingPass(commandList, m_GISpatialResamplingPass, "Spatial Resampling", dispatchSize, extraBindingSet);
	}

	nvrhi::utils::BufferUavBarrier(commandList, m_RtxdiResources->GIReservoirBuffer);

    if (!skipFinal)
	    ExecuteRayTracingPass(commandList, m_GIFinalShadingPass, "Final Shading", dispatchSize, extraBindingSet);

	commandList->endMarker(); // ReSTIR GI
}

void RtxdiPass::ExecuteFusedDIGIFinal(nvrhi::CommandListHandle commandList, nvrhi::BindingSetHandle extraBindingSet)
{
    dm::int2 dispatchSize = { (int)m_RtxdiContext->GetParameters().RenderWidth, (int)m_RtxdiContext->GetParameters().RenderHeight };
    ExecuteRayTracingPass(commandList, m_FusedDIGIFinalShadingPass, "Fused DI GI Final Shading", dispatchSize, extraBindingSet);
}

void RtxdiPass::EndFrame()
{
	//Store the current data for the next frame
	m_PreviousReservoirIndex = m_CurrentReservoirIndex;
}

void RtxdiPass::ExecuteComputePass(
	nvrhi::CommandListHandle& commandList,
	ComputePass& pass,
	const char* passName,
	dm::int3 dispatchSize,
	nvrhi::BindingSetHandle extraBindingSet /*= nullptr*/)
{
	commandList->beginMarker(passName);

	pass.Execute(commandList, dispatchSize.x, dispatchSize.y, dispatchSize.z, m_BindingSet,
		extraBindingSet, m_Scene->GetDescriptorTable());

	commandList->endMarker();
}

void RtxdiPass::ExecuteRayTracingPass(
	nvrhi::CommandListHandle& commandList, 
	RayTracingPass& pass, 
	const char* passName, 
	dm::int2 dispatchSize, 
	nvrhi::IBindingSet* extraBindingSet /* = nullptr */
)
{
	commandList->beginMarker(passName);
	
	pass.Execute(commandList, dispatchSize.x, dispatchSize.y, m_BindingSet, 
		extraBindingSet, m_Scene->GetDescriptorTable());

	commandList->endMarker();
}

uint32_t RtxdiPass::GetNextReservoirIndex(uint32_t currentIndex)
{
	return (currentIndex + 1) % RtxdiResources::c_NumReservoirBuffers;
}

ShaderMacro RtxdiPass::GetReGirMacro(const rtxdi::ContextParameters& contextParameters)
{
	std::string regirMode;

	switch (contextParameters.ReGIR.Mode)
	{
	case rtxdi::ReGIRMode::Disabled :
		regirMode = "RTXDI_REGIR_DISABLED";
		break;
	case rtxdi::ReGIRMode::Grid :
		regirMode = "RTXDI_REGIR_GRID";
		break;
	case rtxdi::ReGIRMode::Onion :
		regirMode = "RTXDI_REGIR_ONION";
		break;
	}

	return { "RTXDI_REGIR_MODE", regirMode };
}
