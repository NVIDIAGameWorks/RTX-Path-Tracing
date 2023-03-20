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

#pragma once

#include <donut/engine/SceneTypes.h>
#include <nvrhi/nvrhi.h>
#include <unordered_map>
#include <mutex>

namespace donut::engine
{
    enum class MaterialResource
    {
        ConstantBuffer,
        Sampler,
        DiffuseTexture,
        SpecularTexture,
        NormalTexture,
        EmissiveTexture,
        OcclusionTexture,
        TransmissionTexture
    };

    struct MaterialResourceBinding
    {
        MaterialResource resource;
        uint32_t slot; // type depends on resource
    };

    class MaterialBindingCache
    {
    private:
        nvrhi::DeviceHandle m_Device;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        std::unordered_map<const Material*, nvrhi::BindingSetHandle> m_BindingSets;
        nvrhi::ShaderType m_ShaderType;
        std::vector<MaterialResourceBinding> m_BindingDesc;
        nvrhi::TextureHandle m_FallbackTexture;
        nvrhi::SamplerHandle m_Sampler;
        std::mutex m_Mutex;
        bool m_TrackLiveness;

        nvrhi::BindingSetHandle CreateMaterialBindingSet(const Material* material);
        nvrhi::BindingSetItem GetTextureBindingSetItem(uint32_t slot, const std::shared_ptr<LoadedTexture>& texture) const;

    public:
        MaterialBindingCache(
            nvrhi::IDevice* device, 
            nvrhi::ShaderType shaderType, 
            uint32_t registerSpace,
            const std::vector<MaterialResourceBinding>& bindings,
            nvrhi::ISampler* sampler,
            nvrhi::ITexture* fallbackTexture,
            bool trackLiveness = true);

        nvrhi::IBindingLayout* GetLayout() const;
        nvrhi::IBindingSet* GetMaterialBindingSet(const Material* material);
        void Clear();
    };
}