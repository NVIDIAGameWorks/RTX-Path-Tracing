/*
* Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
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

#include <nvrhi/nvrhi.h>
#include <memory>
#include <filesystem>

namespace donut::vfs
{
    class IFileSystem;
}

namespace donut::engine
{
    class ShaderFactory;
    class DescriptorTableManager;

    struct IesProfile
    {
        std::string name;
        std::vector<float> rawData;
        nvrhi::TextureHandle texture;
        int textureIndex;
    };

    class IesProfileLoader
    {
        nvrhi::DeviceHandle m_Device;
        nvrhi::ShaderHandle m_ComputeShader;
        nvrhi::ComputePipelineHandle m_ComputePipeline;
        nvrhi::BindingLayoutHandle m_BindingLayout;

        std::shared_ptr<donut::engine::ShaderFactory> m_ShaderFactory;
        std::shared_ptr<donut::engine::DescriptorTableManager> m_DescriptorTableManager;

    public:
        IesProfileLoader(
            nvrhi::IDevice* device,
            std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
            std::shared_ptr<donut::engine::DescriptorTableManager> descriptorTableManager);

        std::shared_ptr<IesProfile> LoadIesProfile(donut::vfs::IFileSystem& fs, const std::filesystem::path& path);

        void BakeIesProfile(IesProfile& profile, nvrhi::ICommandList* commandList);
    };

}