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

#include <donut/app/ApplicationBase.h>
#include <donut/engine/Scene.h>
#include <donut/engine/TextureCache.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/core/vfs/VFS.h>

#include <cstdlib>
#include <sstream>

#ifndef _WIN32
#include <unistd.h>
#include <cstdio>
#include <climits>
#else
#define PATH_MAX MAX_PATH
#endif // _WIN32

using namespace donut::vfs;
using namespace donut::engine;
using namespace donut::app;

ApplicationBase::ApplicationBase(DeviceManager* deviceManager)
    : Super(deviceManager)
    , m_SceneLoaded(false)
    , m_AllTexturesFinalized(false)
    , m_IsAsyncLoad(true)
{
}

void ApplicationBase::Render(nvrhi::IFramebuffer* framebuffer)
{
    if (m_TextureCache)
    {
        bool anyTexturesProcessed = m_TextureCache->ProcessRenderingThreadCommands(*m_CommonPasses, 20.f);

        if (m_SceneLoaded && !anyTexturesProcessed)
            m_AllTexturesFinalized = true;
    }
    else
        m_AllTexturesFinalized = true;

    if (!m_SceneLoaded || !m_AllTexturesFinalized)
    {
        RenderSplashScreen(framebuffer);
        return;
    }

    if (m_SceneLoaded && m_SceneLoadingThread)
    {
        m_SceneLoadingThread->join();
        m_SceneLoadingThread = nullptr;

        // SceneLoaded() would already get called from 
        // BeginLoadingScene() in case of synchronous loads
        SceneLoaded();
    }

    RenderScene(framebuffer);
}

void ApplicationBase::RenderScene(nvrhi::IFramebuffer* framebuffer)
{

}

void ApplicationBase::RenderSplashScreen(nvrhi::IFramebuffer* framebuffer)
{

}

void ApplicationBase::SceneUnloading()
{

}

void ApplicationBase::SceneLoaded()
{
    if (m_TextureCache)
    {
        m_TextureCache->ProcessRenderingThreadCommands(*m_CommonPasses, 0.f);
        m_TextureCache->LoadingFinished();
    }
    m_SceneLoaded = true;
}

void ApplicationBase::SetAsynchronousLoadingEnabled(bool enabled)
{
    m_IsAsyncLoad = enabled;
}

bool ApplicationBase::IsSceneLoading() const
{
    return m_SceneLoadingThread != nullptr;
}

bool ApplicationBase::IsSceneLoaded() const
{
    return m_SceneLoaded;
}

void ApplicationBase::BeginLoadingScene(std::shared_ptr<IFileSystem> fs, const std::filesystem::path& sceneFileName)
{
    if (m_SceneLoaded)
        SceneUnloading();

    m_SceneLoaded = false;
    m_AllTexturesFinalized = false;

    if (m_TextureCache)
    {
        m_TextureCache->Reset();
    }
    GetDevice()->waitForIdle();
    GetDevice()->runGarbageCollection();


    if (m_IsAsyncLoad)
    {
        m_SceneLoadingThread = std::make_unique<std::thread>([this, fs, sceneFileName]() {
			m_SceneLoaded = LoadScene(fs, sceneFileName); 
			});
    }
    else
    {
        m_SceneLoaded = LoadScene(fs, sceneFileName);
        SceneLoaded();
    }
}

std::shared_ptr<CommonRenderPasses> ApplicationBase::GetCommonPasses() const
{
    return m_CommonPasses;
}

const char* donut::app::GetShaderTypeName(nvrhi::GraphicsAPI api)
{
    switch (api)
    {
#if DONUT_USE_DXIL_ON_DX12
    case nvrhi::GraphicsAPI::D3D11:
        return "dxbc";
    case nvrhi::GraphicsAPI::D3D12:
        return "dxil";
#else
    case nvrhi::GraphicsAPI::D3D11:
    case nvrhi::GraphicsAPI::D3D12:
        return "dxbc";
#endif
    case nvrhi::GraphicsAPI::VULKAN:
        return "spirv";
    default:
        assert(!"Unknown graphics API");
        return "";
    }
}

std::filesystem::path donut::app::FindDirectoryWithShaderBin(nvrhi::GraphicsAPI api, IFileSystem& fs, const std::filesystem::path& startPath, const std::filesystem::path& relativeFilePath, const std::string& baseFileName, int maxDepth)
{
	std::string shaderFileSuffix = ".bin";
    std::filesystem::path shaderFileBasePath = GetShaderTypeName(api);
    std::filesystem::path findBytecodeFileName = relativeFilePath / shaderFileBasePath / (baseFileName + shaderFileSuffix);
	return FindDirectoryWithFile(fs, startPath, findBytecodeFileName, maxDepth);
}

std::filesystem::path donut::app::FindDirectory(IFileSystem& fs, const std::filesystem::path& startPath, const std::filesystem::path& dirname, int maxDepth)
{
	std::filesystem::path searchPath = "";

	for (int depth = 0; depth < maxDepth; depth++)
	{
		std::filesystem::path currentPath = startPath / searchPath / dirname;

		if (fs.folderExists(currentPath))
		{
			return currentPath.lexically_normal();
		}

		searchPath = ".." / searchPath;
	}
	return {};
}

std::filesystem::path donut::app::FindDirectoryWithFile(IFileSystem& fs, const std::filesystem::path& startPath, const std::filesystem::path& relativeFilePath, int maxDepth)
{
    std::filesystem::path searchPath = "";

    for (int depth = 0; depth < maxDepth; depth++)
    {
        std::filesystem::path currentPath = startPath / searchPath / relativeFilePath;

        if (fs.fileExists(currentPath))
        {
            return currentPath.parent_path().lexically_normal();
        }

        searchPath = ".." / searchPath;
    }
	return {};
}

std::filesystem::path donut::app::FindMediaFolder(const std::filesystem::path& name)
    {
		donut::vfs::NativeFileSystem fs;

	// first check if the environment variable is set
	const char* value = getenv(env_donut_media_path);
	if (value && fs.folderExists(value))
		return value;

	return FindDirectory(fs, GetDirectoryWithExecutable(), name);
}

// XXXX mk: as of C++20, there is no portable solution (yet ?)
std::filesystem::path donut::app::GetDirectoryWithExecutable()
{
	
    char path[PATH_MAX] = {0};
#ifdef _WIN32
    if (GetModuleFileNameA(nullptr, path, dim(path)) == 0)
        return "";
#else // _WIN32
	// /proc/self/exe is mostly linux-only, but can't hurt to try it elsewhere
	if (readlink("/proc/self/exe", path, std::size(path)) <= 0)
	{
		// portable but assumes executable dir == cwd
		if (!getcwd(path, std::size(path)))
			return ""; // failure
	}
#endif // _WIN32

    std::filesystem::path result = path;
    result = result.parent_path();

    return result;
}

nvrhi::GraphicsAPI donut::app::GetGraphicsAPIFromCommandLine(int argc, const char* const* argv)
{
    for (int n = 1; n < argc; n++)
    {
        const char* arg = argv[n];

        if (!strcmp(arg, "-d3d11") || !strcmp(arg, "-dx11"))
            return nvrhi::GraphicsAPI::D3D11;
        else if (!strcmp(arg, "-d3d12") || !strcmp(arg, "-dx12"))
            return nvrhi::GraphicsAPI::D3D12;
        else if(!strcmp(arg, "-vk") || !strcmp(arg, "-vulkan"))
            return nvrhi::GraphicsAPI::VULKAN;
    }

#if USE_DX12
    return nvrhi::GraphicsAPI::D3D12;
#elif USE_VK
    return nvrhi::GraphicsAPI::VULKAN;
#elif USE_DX11
    return nvrhi::GraphicsAPI::D3D11;
#else
    #error "No Graphics API defined"
#endif
}

std::vector<std::string> donut::app::FindScenes(vfs::IFileSystem& fs, std::filesystem::path const& path)
{
    std::vector<std::string> scenes;
    std::vector<std::string> sceneExtensions = { ".scene.json", ".gltf", ".glb" };

    std::deque<std::filesystem::path> searchList;
    searchList.push_back(path);

    while(!searchList.empty())
    {
        std::filesystem::path currentPath = searchList.front();
        searchList.pop_front();

        // search current directory
        fs.enumerateFiles(currentPath, sceneExtensions, [&scenes, &currentPath](std::string_view name)
        {
            scenes.push_back((currentPath / name).generic_string());
        });

        // search subdirectories
        fs.enumerateDirectories(currentPath, [&searchList, &currentPath](std::string_view name)
        {
            if (name != "glTF-Draco")
                searchList.push_back(currentPath / name);
        });
    }

    return scenes;
}

std::string donut::app::FindPreferredScene(const std::vector<std::string>& available, const std::string& preferred)
{
    if (available.empty())
        return "";

    for (auto s : available)
        if (s.find(preferred) != std::string::npos)
            return s;

    return available.front();
}
