/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "ExtendedScene.h"
#include <donut/core/json.h>
#include <donut/core/vfs/VFS.h>
#include <donut/core/log.h>
#include <json/value.h>
#include <nvrhi/utils.h>
#include <nvrhi/common/misc.h>

using namespace donut::math;
#include <donut/shaders/light_cb.h>

#include "LocalConfig.h"

using namespace donut;
using namespace donut::math;
using namespace donut::engine;

std::shared_ptr<engine::SceneGraphLeaf> EnvironmentLight::Clone()
{
    auto copy = std::make_shared<EnvironmentLight>();
    copy->color = color;
    copy->radianceScale = radianceScale;
    copy->textureIndex = textureIndex;
    copy->rotation = rotation;
    copy->path = path;
    return std::static_pointer_cast<SceneGraphLeaf>(copy);
}

void EnvironmentLight::Load(const Json::Value& node)
{
    node["radianceScale"] >> radianceScale;
    node["textureIndex"] >> textureIndex;
    node["rotation"] >> rotation;
    node["path"] >> path;
}

// what are SetProperty and Clone needed for? hmm
// bool EnvironmentMap::SetProperty(const std::string& name, const dm::float4& value)
// {
//     return SceneGraphLeaf::SetProperty(name, value);
// }
// [[nodiscard]] std::shared_ptr<SceneGraphLeaf> EnvironmentMap::Clone()
// {
//     auto copy = std::make_shared<EnvironmentMap>();
//     copy->m_Path = m_Path;
//     return copy;
// }

std::shared_ptr<donut::engine::SceneGraphLeaf> ExtendedSceneTypeFactory::CreateLeaf(const std::string& type)
{
    if (type == "EnvironmentLight")
    {
        return std::make_shared<EnvironmentLight>();
    } else
    if (type == "PerspectiveCamera" || type == "PerspectiveCameraEx")
    {
        return std::make_shared<PerspectiveCameraEx>();
    } else
    if (type == "MaterialPatch")
    {
        return std::make_shared<MaterialPatch>();
    } else
    if (type == "SampleSettings")
    {
        return std::make_shared<SampleSettings>();
    }
    return SceneTypeFactory::CreateLeaf(type);
}

void ExtendedScene::ProcessNodesRecursive(donut::engine::SceneGraphNode* node)
{
    // std::find_if doesn't compile on linux.
    auto _find_if = [](
        ResourceTracker<Material>::ConstIterator begin,
        ResourceTracker<Material>::ConstIterator end,
        std::function<bool(const std::shared_ptr<Material>& mat)> fn)->ResourceTracker<Material>::ConstIterator
    {
        for (ResourceTracker<Material>::ConstIterator it = begin; it != end; it++)
        {
            if (fn(*it)) {
                return it;
            }
        }
        return end;
    };

    if (node->GetLeaf() != nullptr)
    {
        std::shared_ptr<MaterialPatch> materialPatch = std::dynamic_pointer_cast<MaterialPatch>(node->GetLeaf());
        if (materialPatch != nullptr)
        {
            const std::string name = node->GetName();

            auto & materials = m_SceneGraph->GetMaterials();
            auto it = _find_if( materials.begin(), materials.end(), [&name](const std::shared_ptr<Material> & mat) { return mat->name == name; });
            if (it == materials.end())
            {
                log::warning("Material patch '%s' can't find material to patch!", name.c_str() );
                assert( false );
            }
            else
            {
                materialPatch->Patch(**it);
            }
        }
        std::shared_ptr<SampleSettings> sampleSettings = std::dynamic_pointer_cast<SampleSettings>(node->GetLeaf());
        if (sampleSettings != nullptr)
        {
            assert(m_loadedSettings == nullptr);    // multiple settings nodes? only last one will be loaded
            m_loadedSettings = sampleSettings;
        }
    }

    if (node->GetNextSibling() != nullptr)
        ProcessNodesRecursive(node->GetNextSibling());
    if (node->GetFirstChild() != nullptr)
        ProcessNodesRecursive(node->GetFirstChild());
}

bool ExtendedScene::LoadWithExecutor(const std::filesystem::path& jsonFileName, tf::Executor* executor)
{
	if (!Scene::LoadWithExecutor(jsonFileName, executor))
		return false;

    ProcessNodesRecursive( GetSceneGraph()->GetRootNode().get() );

#if 1 // example of modifying all materials after scene loading; this is the ideal place to do material modification without worrying about resetting relevant caches/dependencies
    auto& materials = m_SceneGraph->GetMaterials();
    for( auto it : materials )
    {
        Material & mat = *it;
        LocalConfig::PostMaterialLoad(mat);
    }
#endif

    return true;
}

std::shared_ptr<EnvironmentLight> donut::engine::FindEnvironmentLight(std::vector <std::shared_ptr<Light>> lights)
{
    for (auto light : lights)
    {
        if (light->GetLightType() == LightType_Environment)
        {
            return std::dynamic_pointer_cast<EnvironmentLight>(light);
        }
    }
    return nullptr;
}

void EnvironmentLight::FillLightConstants(LightConstants& lightConstants) const
{
    Light::FillLightConstants(lightConstants);
    lightConstants.intensity = 0.0f;
    lightConstants.color = { 0,0,0 };
}

std::shared_ptr<SceneGraphLeaf> PerspectiveCameraEx::Clone()
{
    auto copy = std::make_shared<PerspectiveCameraEx>();
    copy->zNear = zNear;
    copy->zFar = zFar;
    copy->verticalFov = verticalFov;
    copy->aspectRatio = aspectRatio;
    copy->enableAutoExposure = enableAutoExposure;
    copy->exposureCompensation = exposureCompensation;
    copy->exposureValue = exposureValue;
    copy->exposureValueMin = exposureValueMin;
    copy->exposureValueMax = exposureValueMax;
    return copy;
}

void PerspectiveCameraEx::Load(const Json::Value& node)
{
    node["enableAutoExposure"] >> enableAutoExposure;
    node["exposureCompensation"] >> exposureCompensation;
    node["exposureValue"] >> exposureValue;
    node["exposureValueMin"] >> exposureValueMin;
    node["exposureValueMax"] >> exposureValueMax;
    
    PerspectiveCamera::Load(node);
}

bool PerspectiveCameraEx::SetProperty(const std::string& name, const dm::float4& value)
{
    assert(false); // not implemented
    return PerspectiveCamera::SetProperty(name, value);
}

std::shared_ptr<SceneGraphLeaf> MaterialPatch::Clone()
{
    auto copy = std::make_shared<MaterialPatch>();
    assert( false ); // not properly implemented
    return copy;
}

MaterialDomain MaterialPatch::GetDomainFromString(const std::string& domain)
{
    if (domain == "Opaque")
        return MaterialDomain::Opaque;
    if (domain == "AlphaTested")
        return MaterialDomain::AlphaTested;
    if (domain == "AlphaBlended")
        return MaterialDomain::AlphaBlended;
    if (domain == "Transmissive")
        return MaterialDomain::Transmissive;
    if (domain == "TransmissiveAlphaTested")
        return MaterialDomain::TransmissiveAlphaTested;
    if (domain == "TransmissiveAlphaBlended")
        return MaterialDomain::TransmissiveAlphaBlended;

    assert(false && "Unrecognized domain");
    return MaterialDomain::Count;
}

void MaterialPatch::Load(const Json::Value& node)
{
    node["domain"] >> domain;
    node["volumeThicknessFactor"] >> volumeThicknessFactor;
    node["volumeAttenuationDistance"] >> volumeAttenuationDistance;
    node["volumeAttenuationColor"] >> volumeAttenuationColor;
    node["IoR"] >> ior;
    node["specularTransmission"] >> transmissionFactor;
    node["diffuseTransmission"] >> diffuseTransmissionFactor;
    node["nestedPriority"] >> nestedPriority;
    node["doubleSided"] >> doubleSided;
    node["thinSurface"] >> thinSurface;
    node["excludeFromNEE"] >> excludeFromNEE;
    node["roughness"] >> roughness;
    node["metalness"] >> metalness;
    node["normalTextureScale"] >> normalTextureScale;
    node["psdExclude"] >> psdExclude;
    node["psdDominantDeltaLobe"] >> psdDominantDeltaLobe;
    node["emissiveIntensity"] >> emissiveIntensity;
    node["shadowNoLFadeout"] >> shadowNoLFadeout;
    
    SceneGraphLeaf::Load(node);
}

bool MaterialPatch::SetProperty(const std::string& name, const dm::float4& value)
{
    assert( false ); // not implemented
    return false; 
}

void MaterialPatch::Patch(Material& mat)
{
    mat.domain = domain.has_value() ? GetDomainFromString(domain.value()) : mat.domain;
    if( volumeThicknessFactor.has_value() )
    {
        mat.thinSurface = volumeThicknessFactor.value() == 0;
        mat.volumeThicknessFactor = volumeThicknessFactor.value();
    }
    // safer to do with a macro - had a copy paste typo bug, this ensures it doesn't happen
#define MAT_VALUE_OR(NAME) mat.##NAME = ##NAME.value_or(mat.##NAME)
    MAT_VALUE_OR(volumeAttenuationDistance);
    MAT_VALUE_OR(volumeAttenuationColor);
    MAT_VALUE_OR(ior);
    MAT_VALUE_OR(transmissionFactor);
    MAT_VALUE_OR(diffuseTransmissionFactor);
    MAT_VALUE_OR(nestedPriority);
    MAT_VALUE_OR(doubleSided);
    MAT_VALUE_OR(thinSurface);
    MAT_VALUE_OR(excludeFromNEE);
    MAT_VALUE_OR(roughness);
    MAT_VALUE_OR(metalness);
    MAT_VALUE_OR(normalTextureScale);
    MAT_VALUE_OR(psdExclude);
    MAT_VALUE_OR(psdDominantDeltaLobe);
    MAT_VALUE_OR(emissiveIntensity);
    MAT_VALUE_OR(shadowNoLFadeout);
}

std::shared_ptr<SceneGraphLeaf> SampleSettings::Clone()
{
    auto copy = std::make_shared<SampleSettings>();
    assert(false); // not properly implemented
    return copy;
}

void SampleSettings::Load(const Json::Value& node)
{
    node["realtimeMode"] >> realtimeMode;
    node["enableAnimations"] >> enableAnimations;
    node["enableRTXDI"] >> enableRTXDI;
    node["startingCamera"] >> startingCamera;
    node["realtimeFireflyFilter"] >> realtimeFireflyFilter;
    node["maxBounces"] >> maxBounces;
    node["realtimeMaxDiffuseBounces"] >> realtimeMaxDiffuseBounces;
    node["referenceMaxDiffuseBounces"] >> referenceMaxDiffuseBounces;
    node["textureMIPBias"] >> textureMIPBias;
}

