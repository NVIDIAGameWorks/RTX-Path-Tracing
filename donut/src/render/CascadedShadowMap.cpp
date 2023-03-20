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

#include <donut/render/CascadedShadowMap.h>
#include <donut/render/DepthPass.h>
#include <donut/render/DrawStrategy.h>
#include <donut/render/PlanarShadowMap.h>

using namespace donut::math;
using namespace donut::engine;
using namespace donut::render;

CascadedShadowMap::CascadedShadowMap(
    nvrhi::IDevice* device, 
    int resolution, 
    int numCascades,
    int numPerObjectShadows,
    nvrhi::Format format,
    bool isUAV)
{
    assert(numCascades > 0);
    assert(numCascades <= 4);

    nvrhi::TextureDesc desc;
    desc.width = resolution;
    desc.height = resolution;
    desc.sampleCount = 1;
    desc.isRenderTarget = true;
    desc.isTypeless = true;
    desc.format = format;
    desc.debugName = "ShadowMap";
    desc.useClearValue = true;
    desc.clearValue = nvrhi::Color(1.f);
    desc.initialState = nvrhi::ResourceStates::ShaderResource;
    desc.keepInitialState = true;
    desc.dimension = nvrhi::TextureDimension::Texture2DArray;
    desc.arraySize = numCascades + numPerObjectShadows;
	desc.isUAV = isUAV;
    m_ShadowMapTexture = device->createTexture(desc);

    nvrhi::Viewport cascadeViewport = nvrhi::Viewport(float(resolution), float(resolution));

    for (int cascade = 0; cascade < numCascades; cascade++)
    {
        std::shared_ptr<PlanarShadowMap> planarShadowMap = std::make_shared<PlanarShadowMap>(device, m_ShadowMapTexture, cascade, cascadeViewport);
        m_Cascades.push_back(planarShadowMap);

        m_CompositeView.AddView(planarShadowMap->GetPlanarView());
    }

    m_NumberOfCascades = 0;

    for (int object = 0; object < numPerObjectShadows; object++)
    {
        std::shared_ptr<PlanarShadowMap> planarShadowMap = std::make_shared<PlanarShadowMap>(device, m_ShadowMapTexture, numCascades + object, cascadeViewport);
        planarShadowMap->SetFalloffDistance(0.f); // disable falloff on per-object shadows: their bboxes are short by design

        m_PerObjectShadows.push_back(planarShadowMap);
    }
}

bool CascadedShadowMap::SetupForPlanarView(const DirectionalLight& light, frustum viewFrustum, float maxShadowDistance, float lightSpaceZUp, float lightSpaceZDown, float exponent, float3 preViewTranslation, int numberOfCascades)
{
    assert(exponent > 1);

    assert(length(viewFrustum.planes[frustum::NEAR_PLANE].normal) > 0);

    if (numberOfCascades < 0)
        m_NumberOfCascades = static_cast<int>(m_Cascades.size());
    else
        m_NumberOfCascades = std::min(numberOfCascades, static_cast<int>(m_Cascades.size()));

    if (maxShadowDistance > 0.f)
    {
        plane& nearPlane = viewFrustum.planes[frustum::NEAR_PLANE];
        plane& farPlane = viewFrustum.planes[frustum::FAR_PLANE];
        farPlane.normal = -nearPlane.normal;
        farPlane.distance = -nearPlane.distance + maxShadowDistance;
    }
    else
    {
        assert(length(viewFrustum.planes[frustum::FAR_PLANE].normal) > 0);
    }

    std::array<float3, frustum::numCorners> corners;
    for (uint32_t i = 0; i < frustum::numCorners; i++)
    {
        corners[i] = viewFrustum.getCorner(i);
    }

    float far = 1.f;
    float near = far / exponent;
    
    daffine3 viewToWorld = light.GetNode()->GetLocalToWorldTransform();
    viewToWorld = dm::scaling(dm::double3(1.0, 1.0, -1.0)) * viewToWorld;
    affine3 worldToView = affine3(inverse(viewToWorld));
    bool viewModified = false;

    for (int cascade = m_NumberOfCascades - 1; cascade >= 0; cascade--)
    {
        if (cascade == 0)
            near = 0.f;

        std::array<float3, frustum::numCorners> cascadeViewCorners;
        for (uint32_t i = 0; i < frustum::numCorners; i++)
        {
            float3 corner = lerp(corners[i & 3], corners[(i & 3) + 4], (i & 4) ? far : near);
            cascadeViewCorners[i] = worldToView.transformPoint(corner);
        }

        box3 cascadeViewBounds = box3(frustum::numCorners, cascadeViewCorners.data());

        float3 cascadeCenter = dm::float3(viewToWorld.transformPoint(dm::double3(cascadeViewBounds.center())));
        float3 halfShadowBoxSize = cascadeViewBounds.diagonal() * 0.5f;
        halfShadowBoxSize.xy() = std::max(halfShadowBoxSize.x, halfShadowBoxSize.y);
        float fadeRange = halfShadowBoxSize.x * 0.1f;

        float zDown = std::max(halfShadowBoxSize.z, lightSpaceZDown);
        float zUp = std::max(halfShadowBoxSize.z, lightSpaceZUp);
        cascadeCenter += dm::float3(light.GetDirection()) * (zDown - zUp) * 0.5f;
        halfShadowBoxSize.z = (zDown + zUp) * 0.5f;

        if (m_Cascades[cascade]->SetupDynamicDirectionalLightView(light, cascadeCenter, halfShadowBoxSize, preViewTranslation, fadeRange))
            viewModified = true;

        far = near;
        near = far / exponent;
    }

    return viewModified;
}

bool CascadedShadowMap::SetupForPlanarViewStable(const DirectionalLight& light, frustum projectionFrustum, affine3 inverseViewMatrix, float maxShadowDistance, float lightSpaceZUp, float lightSpaceZDown, float exponent, float3 preViewTranslation, int numberOfCascades)
{
    assert(exponent > 1);
    
    assert(length(projectionFrustum.planes[frustum::NEAR_PLANE].normal) > 0);

    if (numberOfCascades < 0)
        m_NumberOfCascades = static_cast<int>(m_Cascades.size());
    else
        m_NumberOfCascades = std::min(numberOfCascades, static_cast<int>(m_Cascades.size()));

    if (maxShadowDistance > 0.f)
    {
        plane& nearPlane = projectionFrustum.nearPlane();
        plane& farPlane = projectionFrustum.farPlane();
        farPlane.normal = -nearPlane.normal;
        farPlane.distance = -nearPlane.distance + maxShadowDistance;
    }
    else
    {
        assert(length(projectionFrustum.planes[frustum::FAR_PLANE].normal) > 0);
    }

    std::array<float3, frustum::numCorners> corners;
    for (uint32_t i = 0; i < frustum::numCorners; i++)
    {
        corners[i] = projectionFrustum.getCorner(i);
    }

    float far = 1.f;
    float near = far / exponent;
    
    bool viewModified = false;

    for (int cascade = m_NumberOfCascades - 1; cascade >= 0; cascade--)
    {
        if (cascade == 0)
            near = 0.f;

        std::array<float3, frustum::numCorners> cascadeCorners;
        for (uint32_t i = 0; i < frustum::numCorners; i++)
        {
            cascadeCorners[i] = lerp(corners[i & 3], corners[(i & 3) + 4], (i & 4) ? far : near);
        }

        float3 nearDiagonalCenter = (cascadeCorners[frustum::C_BOTTOM | frustum::C_LEFT | frustum::C_NEAR] + cascadeCorners[frustum::C_TOP | frustum::C_RIGHT | frustum::C_NEAR]) * 0.5f;
        float3 farDiagonalCenter = (cascadeCorners[frustum::C_BOTTOM | frustum::C_LEFT | frustum::C_FAR] + cascadeCorners[frustum::C_TOP | frustum::C_RIGHT | frustum::C_FAR]) * 0.5f;
        float nearCenterToFarCenter = length(farDiagonalCenter - nearDiagonalCenter);
        float nearDiagonalHalfLength = length(cascadeCorners[frustum::C_BOTTOM | frustum::C_LEFT | frustum::C_NEAR] - cascadeCorners[frustum::C_TOP | frustum::C_RIGHT | frustum::C_NEAR]) * 0.5f;
        float farDiagonalHalfLength = length(cascadeCorners[frustum::C_BOTTOM | frustum::C_LEFT | frustum::C_FAR] - cascadeCorners[frustum::C_TOP | frustum::C_RIGHT | frustum::C_FAR]) * 0.5f;

        float nearCenterToSphereCenter = (square(nearCenterToFarCenter) + square(farDiagonalHalfLength) - square(nearDiagonalHalfLength)) / (2.f * nearCenterToFarCenter);
        nearCenterToSphereCenter = clamp(nearCenterToSphereCenter, 0.f, nearCenterToFarCenter);
        float3 sphereCenter = nearDiagonalCenter + normalize(farDiagonalCenter - nearDiagonalCenter) * nearCenterToSphereCenter;
        float sphereRadius = sqrtf(square(farDiagonalHalfLength) + square(nearCenterToFarCenter - nearCenterToSphereCenter));

        float3 cascadeCenter = inverseViewMatrix.transformPoint(sphereCenter);

        float3 halfShadowBoxSize = sphereRadius;
        float fadeRange = sphereRadius * 0.1f;

        float zDown = std::max(sphereRadius, lightSpaceZDown);
        float zUp = std::max(sphereRadius, lightSpaceZUp);
        cascadeCenter += dm::float3(light.GetDirection()) * (zDown - zUp) * 0.5f;
        halfShadowBoxSize.z = (zDown + zUp) * 0.5f;

        if (m_Cascades[cascade]->SetupDynamicDirectionalLightView(light, cascadeCenter, halfShadowBoxSize, preViewTranslation, fadeRange))
            viewModified = true;

        far = near;
        near = far / exponent;
    }

    return viewModified;
}

bool CascadedShadowMap::SetupForCubemapView(const DirectionalLight& light, float3 center, float maxShadowDistance, float lightSpaceZUp, float lightSpaceZDown, float exponent, int numberOfCascades)
{
    assert(maxShadowDistance > 0);
    assert(exponent > 1);

    if (numberOfCascades < 0)
        m_NumberOfCascades = static_cast<int>(m_Cascades.size());
    else
        m_NumberOfCascades = std::min(numberOfCascades, static_cast<int>(m_Cascades.size()));

    box3 unitBox = box3(-1.f, 1.f);
    std::array<float3, box3::numCorners> corners;
    for (uint32_t i = 0; i < box3::numCorners; i++)
    {
        corners[i] = unitBox.getCorner(i);
    }

    float far = maxShadowDistance;

    daffine3 viewToWorld = light.GetNode()->GetLocalToWorldTransform();
    viewToWorld = dm::scaling(dm::double3(1.0, 1.0, -1.0)) * viewToWorld;
    affine3 worldToView = affine3(inverse(viewToWorld));
    bool viewModified = false;

    for (int cascade = m_NumberOfCascades - 1; cascade >= 0; cascade--)
    {
        std::array<float3, box3::numCorners> cascadeViewCorners;
        for (uint32_t i = 0; i < box3::numCorners; i++)
        {
            float3 corner = center + corners[i] * far;
            cascadeViewCorners[i] = worldToView.transformPoint(corner);
        }

        box3 cascadeViewBounds = box3(box3::numCorners, cascadeViewCorners.data());

        float3 cascadeCenter = float3(viewToWorld.transformPoint(double3(cascadeViewBounds.center())));
        float3 halfShadowBoxSize = cascadeViewBounds.diagonal() * 0.5f;
        halfShadowBoxSize.xy() = std::max(halfShadowBoxSize.x, halfShadowBoxSize.y);
        float fadeRange = halfShadowBoxSize.x * 0.1f;

        float zDown = std::max(halfShadowBoxSize.z, lightSpaceZDown);
        float zUp = std::max(halfShadowBoxSize.z, lightSpaceZUp);
        cascadeCenter += dm::float3(light.GetDirection()) * (zDown - zUp) * 0.5f;
        halfShadowBoxSize.z = (zDown + zUp) * 0.5f;

        if (m_Cascades[cascade]->SetupDynamicDirectionalLightView(light, cascadeCenter, halfShadowBoxSize, fadeRange))
            viewModified = true;
        
        far = far / exponent;
    }

    return viewModified;
}

bool CascadedShadowMap::SetupPerObjectShadow(const DirectionalLight& light, uint32_t object, const box3& objectBounds)
{
    return m_PerObjectShadows[object]->SetupWholeSceneDirectionalLightView(light, objectBounds);
}

void CascadedShadowMap::SetupProxyViews()
{
    for (auto cascade : m_Cascades)
    {
        cascade->SetupProxyView();
    }

    for (auto object : m_PerObjectShadows)
    {
        object->SetupProxyView();
    }
}

void CascadedShadowMap::SetLitOutOfBounds(bool litOutOfBounds)
{
    for (auto cascade : m_Cascades)
    {
        cascade->SetLitOutOfBounds(litOutOfBounds);
    }
}

void CascadedShadowMap::SetFalloffDistance(float distance)
{
    for (auto cascade : m_Cascades)
    {
        cascade->SetFalloffDistance(distance);
    }
}

dm::float4x4 CascadedShadowMap::GetWorldToUvzwMatrix() const
{
    assert(false);
    return float4x4::identity();
}

const ICompositeView& CascadedShadowMap::GetView() const
{
    return m_CompositeView;
}

nvrhi::ITexture* CascadedShadowMap::GetTexture() const
{
    return m_ShadowMapTexture;
}

uint32_t CascadedShadowMap::GetNumberOfCascades() const
{
    return static_cast<uint32_t>(m_NumberOfCascades);
}

const IShadowMap* CascadedShadowMap::GetCascade(uint32_t index) const
{
    if (static_cast<int>(index) < m_NumberOfCascades)
        return m_Cascades[index].get();

    return nullptr;
}

uint32_t CascadedShadowMap::GetNumberOfPerObjectShadows() const
{
    return static_cast<uint32_t>(m_PerObjectShadows.size());
}

const IShadowMap* CascadedShadowMap::GetPerObjectShadow(uint32_t index) const
{
    if (static_cast<size_t>(index) < m_PerObjectShadows.size())
        return m_PerObjectShadows[index].get();

    return nullptr;
}

dm::int2 CascadedShadowMap::GetTextureSize() const
{
    const nvrhi::TextureDesc& textureDesc = m_ShadowMapTexture->getDesc();
    return int2(textureDesc.width, textureDesc.height);
}

dm::box2 CascadedShadowMap::GetUVRange() const
{
    assert(false);
    return m_Cascades[0]->GetUVRange();
}

dm::float2 CascadedShadowMap::GetFadeRangeInTexels() const
{
    return m_Cascades[0]->GetFadeRangeInTexels();
}

bool CascadedShadowMap::IsLitOutOfBounds() const
{
    return m_Cascades[0]->IsLitOutOfBounds();
}

void CascadedShadowMap::FillShadowConstants(struct ShadowConstants& constants) const
{
    assert(false);
}

std::shared_ptr<donut::engine::PlanarView> donut::render::CascadedShadowMap::GetCascadeView(uint32_t cascade)
{
    if (static_cast<int>(cascade) < m_NumberOfCascades)
        return m_Cascades[cascade]->GetPlanarView();

    return nullptr;
}

std::shared_ptr<donut::engine::PlanarView> donut::render::CascadedShadowMap::GetPerObjectView(uint32_t object)
{
    if (object < m_PerObjectShadows.size())
        return m_PerObjectShadows[object]->GetPlanarView();

    return nullptr;
}

void donut::render::CascadedShadowMap::SetNumberOfCascadesUnsafe(int cascades)
{
	m_NumberOfCascades = (cascades < 0 || cascades >= (int)m_Cascades.size()) ? (int)m_Cascades.size() : cascades;
}

void CascadedShadowMap::Clear(nvrhi::ICommandList* commandList)
{
    const nvrhi::FormatInfo& depthFormatInfo = nvrhi::getFormatInfo(m_ShadowMapTexture->getDesc().format);

    commandList->clearDepthStencilTexture(m_ShadowMapTexture, nvrhi::AllSubresources, true, 1.f, depthFormatInfo.hasStencil, 0);
}
