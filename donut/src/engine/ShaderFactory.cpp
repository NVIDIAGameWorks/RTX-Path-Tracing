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

#include <donut/engine/ShaderFactory.h>
#include <donut/core/vfs/VFS.h>
#include <donut/core/log.h>
#include <nvrhi/common/shader-blob.h>

using namespace std;
using namespace donut::vfs;
using namespace donut::engine;

ShaderFactory::ShaderFactory(nvrhi::DeviceHandle rendererInterface,
	std::shared_ptr<IFileSystem> fs,
	const std::filesystem::path& basePath)
	: m_Device(rendererInterface)
	, m_fs(fs)
	, m_basePath(basePath)
{
}

void ShaderFactory::ClearCache()
{
	m_BytecodeCache.clear();
}

std::shared_ptr<IBlob> ShaderFactory::GetBytecode(const char* fileName, const char* entryName)
{
    if (!entryName)
        entryName = "main";

    string adjustedName = fileName;
    {
        size_t pos = adjustedName.find(".hlsl");
        if (pos != string::npos)
            adjustedName.erase(pos, 5);

        if (entryName && strcmp(entryName, "main"))
            adjustedName += "_" + string(entryName);
    }

    std::filesystem::path shaderFilePath = m_basePath / (adjustedName + ".bin");

    std::shared_ptr<IBlob>& data = m_BytecodeCache[shaderFilePath.generic_string()];

    if (data)
        return data;

    data = m_fs->readFile(shaderFilePath);

    if (!data)
    {
        log::error("Couldn't read the binary file for shader %s from %s", fileName, shaderFilePath.generic_string().c_str());
        return nullptr;
    }

    return data;
}


nvrhi::ShaderHandle ShaderFactory::CreateShader(const char* fileName, const char* entryName, const vector<ShaderMacro>* pDefines, nvrhi::ShaderType shaderType)
{
    nvrhi::ShaderDesc desc = nvrhi::ShaderDesc(shaderType);
    desc.debugName = fileName;
    return CreateShader(fileName, entryName, pDefines, desc);
}

nvrhi::ShaderHandle ShaderFactory::CreateShader(const char* fileName, const char* entryName, const vector<ShaderMacro>* pDefines, const nvrhi::ShaderDesc& desc)
{
    std::shared_ptr<IBlob> byteCode = GetBytecode(fileName, entryName);

    if(!byteCode)
        return nullptr;

    vector<nvrhi::ShaderConstant> constants;
    if (pDefines)
    {
        for (const ShaderMacro& define : *pDefines)
            constants.push_back(nvrhi::ShaderConstant{ define.name.c_str(), define.definition.c_str() });
    }

    nvrhi::ShaderDesc descCopy = desc;
    descCopy.entryName = entryName;

    return nvrhi::createShaderPermutation(m_Device, descCopy, byteCode->data(), byteCode->size(),
        constants.data(), uint32_t(constants.size()));
}

nvrhi::ShaderLibraryHandle ShaderFactory::CreateShaderLibrary(const char* fileName, const std::vector<ShaderMacro>* pDefines)
{
    std::shared_ptr<IBlob> byteCode = GetBytecode(fileName, nullptr);

    if (!byteCode)
        return nullptr;

    vector<nvrhi::ShaderConstant> constants;
    if (pDefines)
    {
        for (const ShaderMacro& define : *pDefines)
            constants.push_back(nvrhi::ShaderConstant{ define.name.c_str(), define.definition.c_str() });
    }

    return nvrhi::createShaderLibraryPermutation(m_Device, byteCode->data(), byteCode->size(),
        constants.data(), uint32_t(constants.size()));
}
