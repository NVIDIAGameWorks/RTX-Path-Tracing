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

#include <donut/engine/MaterialBindingCache.h>
#include <donut/core/log.h>

using namespace donut::engine;

MaterialBindingCache::MaterialBindingCache(
    nvrhi::IDevice* device, 
    nvrhi::ShaderType shaderType, 
    uint32_t registerSpace,
    const std::vector<MaterialResourceBinding>& bindings,
    nvrhi::ISampler* sampler,
    nvrhi::ITexture* fallbackTexture,
    bool trackLiveness)
    : m_Device(device)
    , m_ShaderType(shaderType)
    , m_BindingDesc(bindings)
    , m_FallbackTexture(fallbackTexture)
    , m_Sampler(sampler)
    , m_TrackLiveness(trackLiveness)
{
    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = shaderType;
    layoutDesc.registerSpace = registerSpace;

    for (const auto& item : bindings)
    {
        nvrhi::BindingLayoutItem layoutItem{};
        layoutItem.slot = item.slot;
        
        switch (item.resource)
        {
        case MaterialResource::ConstantBuffer:
            layoutItem.type = nvrhi::ResourceType::ConstantBuffer;
            break;
        case MaterialResource::DiffuseTexture:
        case MaterialResource::SpecularTexture:
        case MaterialResource::NormalTexture:
        case MaterialResource::EmissiveTexture:
        case MaterialResource::OcclusionTexture:
        case MaterialResource::TransmissionTexture:
            layoutItem.type = nvrhi::ResourceType::Texture_SRV;
            break;
        case MaterialResource::Sampler:
            layoutItem.type = nvrhi::ResourceType::Sampler;
            break;
        default:
            log::error("MaterialBindingCache: unknown MaterialResource value (%d)", item.resource);
            return;
        }

        layoutDesc.bindings.push_back(layoutItem);
    }

    m_BindingLayout = m_Device->createBindingLayout(layoutDesc);
}

nvrhi::IBindingLayout* donut::engine::MaterialBindingCache::GetLayout() const
{
    return m_BindingLayout;
}

nvrhi::IBindingSet* donut::engine::MaterialBindingCache::GetMaterialBindingSet(const Material* material)
{
    std::lock_guard<std::mutex> lockGuard(m_Mutex);

    nvrhi::BindingSetHandle& bindingSet = m_BindingSets[material];

    if (bindingSet)
        return bindingSet;

    bindingSet = CreateMaterialBindingSet(material);

    return bindingSet;
}

void donut::engine::MaterialBindingCache::Clear()
{
    std::lock_guard<std::mutex> lockGuard(m_Mutex);

    m_BindingSets.clear();
}

nvrhi::BindingSetItem MaterialBindingCache::GetTextureBindingSetItem(uint32_t slot, const std::shared_ptr<LoadedTexture>& texture) const
{
    return nvrhi::BindingSetItem::Texture_SRV(slot, texture && texture->texture ? texture->texture.Get() : m_FallbackTexture.Get());
}

nvrhi::BindingSetHandle donut::engine::MaterialBindingCache::CreateMaterialBindingSet(const Material* material)
{
    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.trackLiveness = m_TrackLiveness;

    for (const auto& item : m_BindingDesc)
    {
        nvrhi::BindingSetItem setItem;

        switch (item.resource)
        {
        case MaterialResource::ConstantBuffer:
            setItem = nvrhi::BindingSetItem::ConstantBuffer(
                item.slot, 
                material->materialConstants);
            break;

        case MaterialResource::Sampler:
            setItem = nvrhi::BindingSetItem::Sampler(
                item.slot,
                m_Sampler);
            break;

        case MaterialResource::DiffuseTexture:
            setItem = GetTextureBindingSetItem(item.slot, material->baseOrDiffuseTexture);
            break;

        case MaterialResource::SpecularTexture:
            setItem = GetTextureBindingSetItem(item.slot, material->metalRoughOrSpecularTexture);
            break;

        case MaterialResource::NormalTexture:
            setItem = GetTextureBindingSetItem(item.slot, material->normalTexture);
            break;

        case MaterialResource::EmissiveTexture:
            setItem = GetTextureBindingSetItem(item.slot, material->emissiveTexture);
            break;

        case MaterialResource::OcclusionTexture:
            setItem = GetTextureBindingSetItem(item.slot, material->occlusionTexture);
            break;

        case MaterialResource::TransmissionTexture:
            setItem = GetTextureBindingSetItem(item.slot, material->transmissionTexture);
            break;

        default:
            log::error("MaterialBindingCache: unknown MaterialResource value (%d)", item.resource);
            return nullptr;
        }

        bindingSetDesc.bindings.push_back(setItem);
    }

    return m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);
}
