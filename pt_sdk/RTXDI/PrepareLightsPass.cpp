/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "PrepareLightsPass.h"
#include "RtxdiResources.h"
#include "../ExtendedScene.h"
#include <donut/engine/Scene.h>

#include <donut/engine/ShaderFactory.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/core/log.h>
#include <nvrhi/utils.h>

#include <algorithm>
#include <utility>

using namespace donut::math;
#include "ShaderParameters.h"
#include "../Lighting/Distant/EnvMapBaker.h"
#include "../Lighting/Distant/EnvMapImportanceSamplingBaker.h"
#include "../RenderTargets.h"

using namespace donut::engine;


PrepareLightsPass::PrepareLightsPass(
    nvrhi::IDevice* device, 
    std::shared_ptr<donut::engine::ShaderFactory> shaderFactory, 
    std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses,
    std::shared_ptr<donut::engine::ExtendedScene> scene,
    nvrhi::IBindingLayout* bindlessLayout) 
    : m_Device(device)
    , m_BindlessLayout(bindlessLayout)
    , m_ShaderFactory(std::move(shaderFactory))
    , m_CommonPasses(std::move(commonPasses))
    , m_Scene(std::move(scene))
{
    nvrhi::BindingLayoutDesc bindingLayoutDesc;
    bindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
    bindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), //PushConstants(0, sizeof(PrepareLightsConstants)),
        nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0),
        nvrhi::BindingLayoutItem::TypedBuffer_UAV(1),
        nvrhi::BindingLayoutItem::Texture_UAV(2),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(4),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(5),
        nvrhi::BindingLayoutItem::Texture_SRV(6),
        nvrhi::BindingLayoutItem::Texture_SRV(7),
        nvrhi::BindingLayoutItem::Texture_UAV(50),

        nvrhi::BindingLayoutItem::Sampler(0),
        nvrhi::BindingLayoutItem::Sampler(1),
        nvrhi::BindingLayoutItem::Sampler(2)
    };

    m_BindingLayout = m_Device->createBindingLayout(bindingLayoutDesc);

    nvrhi::BufferDesc constBufferDesc;
    constBufferDesc.byteSize = sizeof(PrepareLightsConstants);
    constBufferDesc.debugName = "PrepareLightsConstants";
    constBufferDesc.isConstantBuffer = true;
    constBufferDesc.isVolatile = true;
    constBufferDesc.maxVersions = 16;
    m_ConstantBuffer = device->createBuffer(constBufferDesc);
}


void PrepareLightsPass::SetScene(std::shared_ptr<donut::engine::ExtendedScene> scene,
    std::shared_ptr<EnvMapBaker> environmentMap, EnvMapSceneParams envMapSceneParams)
{
    m_Scene = scene;
    m_EnvironmentMap = environmentMap;
    m_EnvironmentMapSceneParams = envMapSceneParams;
}

void PrepareLightsPass::CreatePipeline()
{
    donut::log::debug("Initializing PrepareLightsPass...");

    m_ComputeShader = m_ShaderFactory->CreateShader("app/RTXDI/PrepareLights.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);

    nvrhi::ComputePipelineDesc pipelineDesc;
    pipelineDesc.bindingLayouts = { m_BindingLayout, m_BindlessLayout };
    pipelineDesc.CS = m_ComputeShader;
    m_ComputePipeline = m_Device->createComputePipeline(pipelineDesc);
}

void PrepareLightsPass::CreateBindingSet(RtxdiResources& resources, const RenderTargets& renderTargets)
{
    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),// PushConstants(0, sizeof(PrepareLightsConstants)),
        nvrhi::BindingSetItem::StructuredBuffer_UAV(0, resources.LightDataBuffer),
        nvrhi::BindingSetItem::TypedBuffer_UAV(1, resources.LightIndexMappingBuffer),
        nvrhi::BindingSetItem::Texture_UAV(2, resources.LocalLightPdfTexture),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(0, resources.TaskBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(1, resources.PrimitiveLightBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(2, m_Scene->GetInstanceBuffer()),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(3, m_Scene->GetGeometryBuffer()),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(4, m_Scene->GetGeometryDebugBuffer()),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(5, m_Scene->GetMaterialBuffer()),
        nvrhi::BindingSetItem::Texture_SRV(6, m_EnvironmentMap ? m_EnvironmentMap->GetEnvMapCube() : m_CommonPasses->m_BlackCubeMapArray),
        nvrhi::BindingSetItem::Texture_SRV(7, m_EnvironmentMap ? m_EnvironmentMap->GetImportanceSampling()->GetImportanceMap() : m_CommonPasses->m_BlackTexture),
        nvrhi::BindingSetItem::Texture_UAV(50, renderTargets.DebugVizOutput),
        nvrhi::BindingSetItem::Sampler(0, m_CommonPasses->m_AnisotropicWrapSampler),
        nvrhi::BindingSetItem::Sampler(1, m_EnvironmentMap ? m_EnvironmentMap->GetEnvMapCubeSampler() : m_CommonPasses->m_AnisotropicWrapSampler),
        nvrhi::BindingSetItem::Sampler(2, m_EnvironmentMap ? m_EnvironmentMap->GetImportanceSampling()->GetImportanceMapSampler() : m_CommonPasses->m_AnisotropicWrapSampler)
    };

    m_BindingSet = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);
    m_TaskBuffer = resources.TaskBuffer;
    m_PrimitiveLightBuffer = resources.PrimitiveLightBuffer;
    m_LightIndexMappingBuffer = resources.LightIndexMappingBuffer;
    m_GeometryInstanceToLightBuffer = resources.GeometryInstanceToLightBuffer;
    m_LocalLightPdfTexture = resources.LocalLightPdfTexture;
    m_MaxLightsInBuffer = uint32_t(resources.LightDataBuffer->getDesc().byteSize / (sizeof(PolymorphicLightInfo) * 2));
}

void PrepareLightsPass::CountLightsInScene(uint32_t& numEmissiveMeshes, uint32_t& numEmissiveTriangles)
{
    numEmissiveMeshes = 0;
    numEmissiveTriangles = 0;

    const auto& instances = m_Scene->GetSceneGraph()->GetMeshInstances();
    for (const auto& instance : instances)
    {
        for (const auto& geometry : instance->GetMesh()->geometries)
        {
            if (any(geometry->material->emissiveColor != 0.f))
            {
                numEmissiveMeshes += 1;
                numEmissiveTriangles += geometry->numIndices / 3;
            }
        }
    }
}

static inline uint floatToUInt(float _V, float _Scale)
{
    return (uint)floor(_V * _Scale + 0.5f);
}

static inline uint FLOAT3_to_R8G8B8_UNORM(float unpackedInputX, float unpackedInputY, float unpackedInputZ)
{
    return (floatToUInt(saturate(unpackedInputX), 0xFF) & 0xFF) |
        ((floatToUInt(saturate(unpackedInputY), 0xFF) & 0xFF) << 8) |
        ((floatToUInt(saturate(unpackedInputZ), 0xFF) & 0xFF) << 16);
}

static void packLightColor(const float3& color, PolymorphicLightInfo& lightInfo)
{
    float maxRadiance = std::max(color.x, std::max(color.y, color.z));

    if (maxRadiance <= 0.f)
        return;

    float logRadiance = (::log2f(maxRadiance) - kPolymorphicLightMinLog2Radiance) / (kPolymorphicLightMaxLog2Radiance - kPolymorphicLightMinLog2Radiance);
    logRadiance = saturate(logRadiance);
    uint32_t packedRadiance = std::min(uint32_t(ceilf(logRadiance * 65534.f)) + 1, 0xffffu);
    float unpackedRadiance = ::exp2f((float(packedRadiance - 1) / 65534.f) * (kPolymorphicLightMaxLog2Radiance - kPolymorphicLightMinLog2Radiance) + kPolymorphicLightMinLog2Radiance);

    lightInfo.colorTypeAndFlags |= FLOAT3_to_R8G8B8_UNORM(color.x / unpackedRadiance, color.y / unpackedRadiance, color.z / unpackedRadiance);
    lightInfo.logRadiance |= packedRadiance;
}

static float2 unitVectorToOctahedron(const float3 N)
{
    float m = abs(N.x) + abs(N.y) + abs(N.z);
    float2 XY = { N.x, N.y };
    XY.x /= m;
    XY.y /= m;
    if (N.z <= 0.0f)
    {
        float2 signs;
        signs.x = XY.x >= 0.0f ? 1.0f : -1.0f;
        signs.y = XY.y >= 0.0f ? 1.0f : -1.0f;
        float x = (1.0f - abs(XY.y)) * signs.x;
        float y = (1.0f - abs(XY.x)) * signs.y;
        XY.x = x;
        XY.y = y;
    }
    return { XY.x, XY.y };
}

static uint32_t packNormalizedVector(const float3 x)
{
    float2 XY = unitVectorToOctahedron(x);
    XY.x = XY.x * .5f + .5f;
    XY.y = XY.y * .5f + .5f;
    uint X = floatToUInt(saturate(XY.x), (1 << 16) - 1);
    uint Y = floatToUInt(saturate(XY.y), (1 << 16) - 1);
    uint packedOutput = X;
    packedOutput |= Y << 16;
    return packedOutput;
}

// Modified from original, based on the method from the DX fallback layer sample
static uint16_t fp32ToFp16(float v)
{
    // Multiplying by 2^-112 causes exponents below -14 to denormalize
    static const union FU {
        uint ui;
        float f;
    } multiple = { 0x07800000 }; // 2**-112

    FU BiasedFloat;
    BiasedFloat.f = v * multiple.f;
    const uint u = BiasedFloat.ui;

    const uint sign = u & 0x80000000;
    uint body = u & 0x0fffffff;

    return (uint16_t)(sign >> 16 | body >> 13) & 0xFFFF;
}

static bool ConvertLight(const donut::engine::Light& light, PolymorphicLightInfo& polymorphic, bool enableImportanceSampledEnvironmentLight, EnvMapBaker* environmentMap)
{
    switch (light.GetLightType())
    {
#if 0 // we're now baking these into the environment map!
    case LightType_Directional: {
        auto& directional = static_cast<const donut::engine::DirectionalLight&>(light);
        float clampedAngularSize = clamp(directional.angularSize, 0.f, 90.f);
        float halfAngularSizeRad = 0.5f * dm::radians(clampedAngularSize);
        float solidAngle = float(2 * dm::PI_d * (1.0 - cos(halfAngularSizeRad)));
        float3 radiance = directional.color * directional.irradiance / solidAngle;

        polymorphic.colorTypeAndFlags = (uint32_t)PolymorphicLightType::kDirectional << kPolymorphicLightTypeShift;
        packLightColor(radiance, polymorphic);
        polymorphic.direction1 = packNormalizedVector(float3(normalize(directional.GetDirection())));
        // Can't pass cosines of small angles reliably with fp16
        polymorphic.scalars = fp32ToFp16(halfAngularSizeRad) | (fp32ToFp16(solidAngle) << 16);
        return true;
    }
#endif
 
	case LightType_Spot: {
		auto& spot = static_cast<const SpotLight&>(light);

        // Sphere lights not supported in the PTSDK currently
        if (spot.radius == 0.f)
        {
			float3 flux = spot.color * spot.intensity;

			polymorphic.colorTypeAndFlags = (uint32_t)PolymorphicLightType::kPoint << kPolymorphicLightTypeShift;
			packLightColor(flux, polymorphic);
			polymorphic.center = float3(spot.GetPosition());
            polymorphic.direction1 = packNormalizedVector(float3(normalize(spot.GetDirection())));
            polymorphic.direction2 = fp32ToFp16(dm::radians(spot.outerAngle));
			polymorphic.direction2 |= fp32ToFp16(dm::radians(spot.innerAngle)) << 16;
        }
        else
        {

            float projectedArea = dm::PI_f * square(spot.radius);
            float3 radiance = spot.color * spot.intensity / projectedArea;
            float softness = saturate(1.f - spot.innerAngle / spot.outerAngle);

            polymorphic.colorTypeAndFlags = (uint32_t)PolymorphicLightType::kSphere << kPolymorphicLightTypeShift;
            polymorphic.colorTypeAndFlags |= kPolymorphicLightShapingEnableBit;
            packLightColor(radiance, polymorphic);
            polymorphic.center = float3(spot.GetPosition());
            polymorphic.scalars = fp32ToFp16(spot.radius);
            polymorphic.primaryAxis = packNormalizedVector(float3(normalize(spot.GetDirection())));
            polymorphic.cosConeAngleAndSoftness = fp32ToFp16(cosf(dm::radians(spot.outerAngle)));
            polymorphic.cosConeAngleAndSoftness |= fp32ToFp16(softness) << 16;
        }

      
		return true;
	}
   /* case LightType_Spot: {
   *    // Spot Light with ies profile
        auto& spot = static_cast<const SpotLightWithProfile&>(light);
        float projectedArea = dm::PI_f * square(spot.radius);
        float3 radiance = spot.color * spot.intensity / projectedArea;
        float softness = saturate(1.f - spot.innerAngle / spot.outerAngle);

        polymorphic.colorTypeAndFlags = (uint32_t)PolymorphicLightType::kSphere << kPolymorphicLightTypeShift;
        polymorphic.colorTypeAndFlags |= kPolymorphicLightShapingEnableBit;
        packLightColor(radiance, polymorphic);
        polymorphic.center = float3(spot.GetPosition());
        polymorphic.scalars = fp32ToFp16(spot.radius);
        polymorphic.primaryAxis = packNormalizedVector(float3(normalize(spot.GetDirection())));
        polymorphic.cosConeAngleAndSoftness = fp32ToFp16(cosf(dm::radians(spot.outerAngle)));
        polymorphic.cosConeAngleAndSoftness |= fp32ToFp16(softness) << 16;

        if (spot.profileTextureIndex >= 0)
        {
            polymorphic.iesProfileIndex = spot.profileTextureIndex;
            polymorphic.colorTypeAndFlags |= kPolymorphicLightIesProfileEnableBit;
        }

        return true;
    }*/
    case LightType_Point: {
        auto& point = static_cast<const donut::engine::PointLight&>(light);
     
        if (point.radius == 0.f)
        {
            float3 flux = point.color * point.intensity;

            polymorphic.colorTypeAndFlags = (uint32_t)PolymorphicLightType::kPoint << kPolymorphicLightTypeShift;
            packLightColor(flux, polymorphic);
            polymorphic.center = float3(point.GetPosition());
            // Set the default values so we can use the same path for spot lights 
            polymorphic.direction2 = fp32ToFp16(dm::PI_f) | fp32ToFp16(0.0f) << 16;
        }
        else
        {
            float projectedArea = dm::PI_f * square(point.radius);
            float3 radiance = point.color * point.intensity / projectedArea;

            polymorphic.colorTypeAndFlags = (uint32_t)PolymorphicLightType::kSphere << kPolymorphicLightTypeShift;
            packLightColor(radiance, polymorphic);
            polymorphic.center = float3(point.GetPosition());
            polymorphic.scalars = fp32ToFp16(point.radius);
        }

        return true;
    }
    case LightType_Environment: {
        // do not add an environment light to the light list if the option is disabled 
        if (enableImportanceSampledEnvironmentLight)
        {
            auto& env = static_cast<const EnvironmentLight&>(light);

            polymorphic.colorTypeAndFlags = (uint32_t)PolymorphicLightType::kEnvironment << kPolymorphicLightTypeShift;
        
            polymorphic.direction2 = 0;

            return true;
        }
        return false;
    }
    default:
        return false;
    }
}

static int isInfiniteLight(const donut::engine::Light& light)
{
    switch (light.GetLightType())
    {
    case LightType_Directional:
        return 1;

    case LightType_Environment:
        return 2;

    default:
        return 0;
    }
}

RTXDI_LightBufferParameters PrepareLightsPass::Process(nvrhi::ICommandList* commandList)
{
    RTXDI_LightBufferParameters lightBufferParams = {};
    
    commandList->beginMarker("PrepareLights");

    std::vector<PrepareLightsTask> tasks;
    std::vector<PolymorphicLightInfo> primitiveLightInfos;
    uint32_t lightBufferOffset = 0;
    std::vector<uint32_t> geometryInstanceToLight(m_Scene->GetSceneGraph()->GetGeometryInstancesCount(), RTXDI_INVALID_LIGHT_INDEX);

    const auto& instances = m_Scene->GetSceneGraph()->GetMeshInstances();
    for (const auto& instance : instances)
    {
        const auto& mesh = instance->GetMesh();

        assert(instance->GetGeometryInstanceIndex() < geometryInstanceToLight.size());
        uint32_t firstGeometryInstanceIndex = instance->GetGeometryInstanceIndex();

        for (size_t geometryIndex = 0; geometryIndex < mesh->geometries.size(); ++geometryIndex)
        {
            const auto& geometry = mesh->geometries[geometryIndex];

            size_t instanceHash = 0;
            nvrhi::hash_combine(instanceHash, instance.get());
            nvrhi::hash_combine(instanceHash, geometryIndex);

            if (!any(geometry->material->emissiveColor != 0.f) || geometry->material->emissiveIntensity <= 0.f)
            {
                // remove the info about this instance, just in case it was emissive and now it's not
                m_InstanceLightBufferOffsets.erase(instanceHash);
                continue;
            }

            geometryInstanceToLight[firstGeometryInstanceIndex + geometryIndex] = lightBufferOffset;

            // find the previous offset of this instance in the light buffer
            auto pOffset = m_InstanceLightBufferOffsets.find(instanceHash);

            assert(geometryIndex < 0xfff);

            PrepareLightsTask task;
            task.instanceAndGeometryIndex = (instance->GetInstanceIndex() << 12) | uint32_t(geometryIndex & 0xfff);
            task.lightBufferOffset = lightBufferOffset;
            task.triangleCount = geometry->numIndices / 3;
            task.previousLightBufferOffset = (pOffset != m_InstanceLightBufferOffsets.end()) ? int(pOffset->second) : -1;

            // record the current offset of this instance for use on the next frame
            m_InstanceLightBufferOffsets[instanceHash] = lightBufferOffset;

            lightBufferOffset += task.triangleCount;

            tasks.push_back(task);
        }
    }

    commandList->writeBuffer(m_GeometryInstanceToLightBuffer, geometryInstanceToLight.data(), geometryInstanceToLight.size() * sizeof(uint32_t));

	lightBufferParams.localLightBufferRegion.firstLightIndex = 0;
	lightBufferParams.localLightBufferRegion.numLights = lightBufferOffset;

    auto sortedLights = m_Scene->GetSceneGraph()->GetLights();
    std::sort(sortedLights.begin(), sortedLights.end(), [](const auto& a, const auto& b) 
        { return isInfiniteLight(*a) < isInfiniteLight(*b); });

    uint32_t numFinitePrimLights = 0;
    uint32_t numInfinitePrimLights = 0;

    bool enableImportanceSampledEnvironmentLight = m_EnvironmentMap ? true : false;

    for (const std::shared_ptr<Light>& pLight : sortedLights)
    {
        PolymorphicLightInfo polymorphicLight = {};
       
        if (!ConvertLight(*pLight, polymorphicLight, enableImportanceSampledEnvironmentLight, m_EnvironmentMap.get()))
            continue;

        // find the previous offset of this instance in the light buffer
        auto pOffset = m_PrimitiveLightBufferOffsets.find(pLight.get());

        PrepareLightsTask task;
        task.instanceAndGeometryIndex = TASK_PRIMITIVE_LIGHT_BIT | uint32_t(primitiveLightInfos.size());
        task.lightBufferOffset = lightBufferOffset;
        task.triangleCount = 1; // technically zero, but we need to allocate 1 thread in the grid to process this light
        task.previousLightBufferOffset = (pOffset != m_PrimitiveLightBufferOffsets.end()) ? pOffset->second : -1;

        // record the current offset of this instance for use on the next frame
        m_PrimitiveLightBufferOffsets[pLight.get()] = lightBufferOffset;

        lightBufferOffset += task.triangleCount;

        tasks.push_back(task);
        primitiveLightInfos.push_back(polymorphicLight);

        if (isInfiniteLight(*pLight))
            numInfinitePrimLights++;
        else
            numFinitePrimLights++;
    }

	lightBufferParams.localLightBufferRegion.numLights += numFinitePrimLights;
	lightBufferParams.infiniteLightBufferRegion.firstLightIndex = lightBufferParams.localLightBufferRegion.numLights;
    // Note we do not include the environment map in numInfiniteLights
	lightBufferParams.infiniteLightBufferRegion.numLights = numInfinitePrimLights - enableImportanceSampledEnvironmentLight;;
	lightBufferParams.environmentLightParams.lightIndex = lightBufferParams.infiniteLightBufferRegion.firstLightIndex + lightBufferParams.infiniteLightBufferRegion.numLights;
	lightBufferParams.environmentLightParams.lightPresent = enableImportanceSampledEnvironmentLight ? 1u : 0u;
    
    commandList->writeBuffer(m_TaskBuffer, tasks.data(), tasks.size() * sizeof(PrepareLightsTask));

    if (!primitiveLightInfos.empty())
    {
        commandList->writeBuffer(m_PrimitiveLightBuffer, primitiveLightInfos.data(), primitiveLightInfos.size() * sizeof(PolymorphicLightInfo));
    }

    // clear the mapping buffer - value of 0 means all mappings are invalid
    commandList->clearBufferUInt(m_LightIndexMappingBuffer, 0);

    // Clear the PDF texture mip 0 - not all of it might be written by this shader
    commandList->clearTextureFloat(m_LocalLightPdfTexture, 
        nvrhi::TextureSubresourceSet(0, 1, 0, 1), 
        nvrhi::Color(0.f));

    nvrhi::ComputeState state;
    state.pipeline = m_ComputePipeline;
    state.bindings = { m_BindingSet, m_Scene->GetDescriptorTable() };

    PrepareLightsConstants constants;
    constants.numTasks = uint32_t(tasks.size());
    constants.currentFrameLightOffset = m_MaxLightsInBuffer * m_OddFrame;
    constants.previousFrameLightOffset = m_MaxLightsInBuffer * !m_OddFrame;
    constants._padding = 0;
    constants.envMapSceneParams = {};
    if (enableImportanceSampledEnvironmentLight)
    {
        constants.envMapSceneParams = m_EnvironmentMapSceneParams;
        constants.envMapImportanceSamplingParams = m_EnvironmentMap->GetImportanceSampling()->GetShaderParams( );
    }

    commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(PrepareLightsConstants));
    //commandList->setPushConstants(&constants, sizeof(constants));

    commandList->setComputeState(state);
    
    //Skip the prepare lights dispatch if there are no lights. Note the Environment map is handled in another pass
    if (lightBufferOffset > 0)
        commandList->dispatch(dm::div_ceil(lightBufferOffset, 256));

    commandList->endMarker();

	lightBufferParams.localLightBufferRegion.firstLightIndex += constants.currentFrameLightOffset;
	lightBufferParams.infiniteLightBufferRegion.firstLightIndex += constants.currentFrameLightOffset;
	lightBufferParams.environmentLightParams.lightIndex += constants.currentFrameLightOffset;
    
    m_OddFrame = !m_OddFrame;

    return lightBufferParams;
}

nvrhi::TextureHandle PrepareLightsPass::GetEnvironmentMapTexture()
{
    return m_EnvironmentMap ? m_EnvironmentMap->GetEnvMapCube() : nullptr;
}
