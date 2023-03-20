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

#include <string>
#include <vector>
#include <unordered_map>
#include <nvrhi/nvrhi.h>
#include <memory>
#include <filesystem>


namespace donut::vfs
{
    class IBlob;
    class IFileSystem;
}

namespace donut::engine
{
    struct ShaderMacro
    {
        std::string name;
        std::string definition;

        ShaderMacro(const std::string& _name, const std::string& _definition)
            : name(_name)
            , definition(_definition)
        { }
    };

    class ShaderFactory
    {
    private:
        nvrhi::DeviceHandle m_Device;
        std::unordered_map<std::string, std::shared_ptr<vfs::IBlob>> m_BytecodeCache;
		std::shared_ptr<vfs::IFileSystem> m_fs;
		std::filesystem::path m_basePath;

    public:
        ShaderFactory(
            nvrhi::DeviceHandle rendererInterface,
            std::shared_ptr<vfs::IFileSystem> fs,
			const std::filesystem::path& basePath);

        void ClearCache();

        nvrhi::ShaderHandle CreateShader(const char* fileName, const char* entryName, const std::vector<ShaderMacro>* pDefines, nvrhi::ShaderType shaderType);
        nvrhi::ShaderHandle CreateShader(const char* fileName, const char* entryName, const std::vector<ShaderMacro>* pDefines, const nvrhi::ShaderDesc& desc);
        nvrhi::ShaderLibraryHandle CreateShaderLibrary(const char* fileName, const std::vector<ShaderMacro>* pDefines);

        std::shared_ptr<vfs::IBlob> GetBytecode(const char* fileName, const char* entryName);
    };
}