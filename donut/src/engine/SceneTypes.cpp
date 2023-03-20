/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include <donut/engine/SceneGraph.h>
#include <donut/engine/ShadowMap.h>
#include <donut/core/json.h>
#include <json/value.h>

using namespace donut::math;
#include <donut/shaders/light_cb.h>
#include <donut/shaders/bindless.h>

using namespace donut::engine;

void Light::FillLightConstants(LightConstants& lightConstants) const
{
    lightConstants.color = color;
    lightConstants.shadowCascades = int4(-1);
    lightConstants.perObjectShadows = int4(-1);
    lightConstants.shadowChannel = int4(shadowChannel, -1, -1, -1);
    if (shadowMap)
        lightConstants.outOfBoundsShadow = shadowMap->IsLitOutOfBounds() ? 1.f : 0.f;
    else
        lightConstants.outOfBoundsShadow = 1.f;
}

bool Light::SetProperty(const std::string& name, const dm::float4& value)
{
    if (name == "color")
    {
        color = value.xyz();
        return true;
    }

    return SceneGraphLeaf::SetProperty(name, value);
}

dm::double3 Light::GetPosition() const
{
    auto node = GetNode();
    if (!node)
        return dm::double3::zero();

    daffine3 localToWorld = node->GetLocalToWorldTransform();
    return localToWorld.m_translation;
}

dm::double3 Light::GetDirection() const
{
    auto node = GetNode();
    if (!node)
        return dm::double3::zero();

    daffine3 localToWorld = node->GetLocalToWorldTransform();
    return -normalize(double3(localToWorld.m_linear.row2));
}

void Light::SetPosition(const dm::double3& position) const
{
    auto node = GetNode();
    if (!node)
    {
        assert(!"Lights must be attached in order to set their position");
        return;
    }

    SceneGraphNode* parent = node->GetParent();
    dm::daffine3 parentToWorld = dm::daffine3::identity();
    if (parent)
        parentToWorld = parent->GetLocalToWorldTransform();

    dm::double3 translation = inverse(parentToWorld).transformPoint(position);
    node->SetTranslation(translation);
}

void Light::SetDirection(const dm::double3& direction) const
{
    auto node = GetNode();
    if (!node)
    {
        assert(!"Lights must be attached in order to set their direction");
        return;
    }

    SceneGraphNode* parent = node->GetParent();
    dm::daffine3 parentToWorld = dm::daffine3::identity();
    if (parent)
        parentToWorld = daffine3(parent->GetLocalToWorldTransform());
    
    daffine3 worldToLocal = lookatZ(direction);
    daffine3 localToParent = inverse(worldToLocal * parentToWorld);
    
    dquat rotation;
    double3 scaling;
    decomposeAffine<double>(localToParent, nullptr, &rotation, &scaling);
    
    node->SetTransform(nullptr, &rotation, &scaling);
}

std::shared_ptr<SceneGraphLeaf> DirectionalLight::Clone()
{
    auto copy = std::make_shared<DirectionalLight>();
    copy->color = color;
    copy->irradiance = irradiance;
    copy->angularSize = angularSize;
    return std::static_pointer_cast<SceneGraphLeaf>(copy);
}

void DirectionalLight::FillLightConstants(LightConstants& lightConstants) const
{
    Light::FillLightConstants(lightConstants);

    lightConstants.lightType = LightType_Directional;
    lightConstants.direction = float3(normalize(GetDirection()));
    float clampedAngularSize = clamp(angularSize, 0.f, 90.f);
    lightConstants.angularSizeOrInvRange = dm::radians(clampedAngularSize);
    lightConstants.intensity = irradiance;
}

void DirectionalLight::Load(const Json::Value& node)
{
    node["color"] >> color;
    node["irradiance"] >> irradiance;
    node["angularSize"] >> angularSize;
}

void DirectionalLight::Store(Json::Value& node) const
{
    node["type"] << "DirectionalLight";
    node["color"] << color;
    node["irradiance"] << irradiance;
    node["angularSize"] << angularSize;
}

bool DirectionalLight::SetProperty(const std::string& name, const dm::float4& value)
{
    if (name == "irradiance")
    {
        irradiance = value.x;
        return true;
    }

    if (name == "angularSize")
    {
        angularSize = value.x;
        return true;
    }

    return Light::SetProperty(name, value);
}

inline float square(const float x) { return x * x; }

std::shared_ptr<SceneGraphLeaf> SpotLight::Clone()
{
    auto copy = std::make_shared<SpotLight>();
    copy->color = color;
    copy->intensity = intensity;
    copy->radius = radius;
    copy->range = range;
    copy->innerAngle = innerAngle;
    copy->outerAngle = outerAngle;
    return std::static_pointer_cast<SceneGraphLeaf>(copy);
}

void SpotLight::FillLightConstants(LightConstants& lightConstants) const
{
    Light::FillLightConstants(lightConstants);

    lightConstants.lightType = LightType_Spot;
    lightConstants.direction = float3(GetDirection());
    lightConstants.position = float3(GetPosition());
    lightConstants.radius = radius;
    lightConstants.angularSizeOrInvRange = (range <= 0.f) ? 0.f : 1.f / range;
    lightConstants.intensity = intensity;
    lightConstants.color = color;
    lightConstants.innerAngle = dm::radians(innerAngle);
    lightConstants.outerAngle = dm::radians(outerAngle);
}

void SpotLight::Load(const Json::Value& node)
{
    node["color"] >> color;
    node["intensity"] >> intensity;
    node["innerAngle"] >> innerAngle;
    node["outerAngle"] >> outerAngle;
    node["radius"] >> radius;
    node["range"] >> range;
}

void SpotLight::Store(Json::Value& node) const
{
    node["type"] << "SpotLight";
    node["color"] << color;
    node["intensity"] << intensity;
    node["innerAngle"] << innerAngle;
    node["outerAngle"] << outerAngle;
    node["radius"] << radius;
    node["range"] << range;
}

bool SpotLight::SetProperty(const std::string& name, const dm::float4& value)
{
    if (name == "intensity")
    {
        intensity = value.x;
        return true;
    }

    if (name == "radius")
    {
        radius = value.x;
        return true;
    }

    if (name == "range")
    {
        range = value.x;
        return true;
    }

    if (name == "innerAngle")
    {
        innerAngle = value.x;
        return true;
    }

    if (name == "outerAngle")
    {
        outerAngle = value.x;
        return true;
    }

    return Light::SetProperty(name, value);
}

std::shared_ptr<SceneGraphLeaf> PointLight::Clone()
{
    auto copy = std::make_shared<PointLight>();
    copy->color = color;
    copy->intensity = intensity;
    copy->radius = radius;
    copy->range = range;
    return std::static_pointer_cast<SceneGraphLeaf>(copy);
}

void PointLight::FillLightConstants(LightConstants& lightConstants) const
{
    Light::FillLightConstants(lightConstants);

    lightConstants.lightType = LightType_Point;
    lightConstants.position = float3(GetPosition());
    lightConstants.radius = radius;
    lightConstants.angularSizeOrInvRange = (range <= 0.f) ? 0.f : 1.f / range;
    lightConstants.intensity = intensity;
    lightConstants.color = color;
}

void PointLight::Load(const Json::Value& node)
{
    node["color"] >> color;
    node["intensity"] >> intensity;
    node["radius"] >> radius;
    node["range"] >> range;
}

void PointLight::Store(Json::Value& node) const
{
    node["type"] << "PointLight";
    node["color"] << color;
    node["intensity"] << intensity;
    node["radius"] << radius;
    node["range"] << range;
}

bool PointLight::SetProperty(const std::string& name, const dm::float4& value)
{
    if (name == "intensity")
    {
        intensity = value.x;
        return true;
    }

    if (name == "radius")
    {
        radius = value.x;
        return true;
    }

    if (name == "range")
    {
        range = value.x;
        return true;
    }
    
    return Light::SetProperty(name, value);
}

nvrhi::VertexAttributeDesc donut::engine::GetVertexAttributeDesc(VertexAttribute attribute, const char* name, uint32_t bufferIndex)
{
    nvrhi::VertexAttributeDesc result = {};
    result.name = name;
    result.bufferIndex = bufferIndex;
    result.arraySize = 1;

    switch (attribute)
    {
    case VertexAttribute::Position:
    case VertexAttribute::PrevPosition:
        result.format = nvrhi::Format::RGB32_FLOAT;
        result.elementStride = sizeof(float3);
        break;
    case VertexAttribute::TexCoord1:
    case VertexAttribute::TexCoord2:
        result.format = nvrhi::Format::RG32_FLOAT;
        result.elementStride = sizeof(float2);
        break;
    case VertexAttribute::Normal:
    case VertexAttribute::Tangent:
        result.format = nvrhi::Format::RGBA8_SNORM;
        result.elementStride = sizeof(uint32_t);
        break;
    case VertexAttribute::Transform:
        result.format = nvrhi::Format::RGBA32_FLOAT;
        result.arraySize = 3;
        result.offset = offsetof(InstanceData, transform);
        result.elementStride = sizeof(InstanceData);
        result.isInstanced = true;
        break;
    case VertexAttribute::PrevTransform:
        result.format = nvrhi::Format::RGBA32_FLOAT;
        result.arraySize = 3;
        result.offset = offsetof(InstanceData, prevTransform);
        result.elementStride = sizeof(InstanceData);
        result.isInstanced = true;
        break;

    default:
        assert(!"unknown attribute");
    }

    return result;
}

const char* donut::engine::MaterialDomainToString(MaterialDomain domain)
{
    switch (domain)
    {
    case MaterialDomain::Opaque: return "Opaque";
    case MaterialDomain::AlphaTested: return "AlphaTested";
    case MaterialDomain::AlphaBlended: return "AlphaBlended";
    case MaterialDomain::Transmissive: return "Transmissive";
    case MaterialDomain::TransmissiveAlphaTested: return "TransmissiveAlphaTested";
    case MaterialDomain::TransmissiveAlphaBlended: return "TransmissiveAlphaBlended";
    case MaterialDomain::Count: return "Count";
    default: return "<Invalid>";
    }
}

bool LightProbe::IsActive() const
{
    if (!enabled)
        return false;
    if (bounds.isempty())
        return false;
    if ((diffuseScale == 0.f || !diffuseMap) && (specularScale == 0.f || !specularMap))
        return false;

    return true;
}

void LightProbe::FillLightProbeConstants(LightProbeConstants& lightProbeConstants) const
{
    lightProbeConstants.diffuseArrayIndex = diffuseArrayIndex;
    lightProbeConstants.specularArrayIndex = specularArrayIndex;
    lightProbeConstants.diffuseScale = diffuseScale;
    lightProbeConstants.specularScale = specularScale;
    lightProbeConstants.mipLevels = specularMap ? static_cast<float>(specularMap->getDesc().mipLevels) : 0.f;

    for (uint32_t nPlane = 0; nPlane < frustum::PLANES_COUNT; nPlane++)
    {
        lightProbeConstants.frustumPlanes[nPlane] = float4(bounds.planes[nPlane].normal, bounds.planes[nPlane].distance);
    }
}
