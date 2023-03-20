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

#include <donut/app/DeviceManager.h>
#include <donut/core/vfs/VFS.h>
#include <nvrhi/nvrhi.h>
#include <filesystem>
#include <thread>
#include <vector>

namespace donut::engine
{
    class TextureCache;
    class CommonRenderPasses;
}

namespace donut::app
{
    class ApplicationBase : public IRenderPass
    {
    private:
        bool m_SceneLoaded;
        bool m_AllTexturesFinalized;

    protected:
        typedef IRenderPass Super;

        std::shared_ptr<engine::TextureCache> m_TextureCache;
        std::unique_ptr<std::thread> m_SceneLoadingThread;
        std::shared_ptr<engine::CommonRenderPasses> m_CommonPasses;

        bool m_IsAsyncLoad;

    public:
        ApplicationBase(DeviceManager* deviceManager);

        virtual void Render(nvrhi::IFramebuffer* framebuffer) override;

        virtual void RenderScene(nvrhi::IFramebuffer* framebuffer);
        virtual void RenderSplashScreen(nvrhi::IFramebuffer* framebuffer);
        virtual void BeginLoadingScene(std::shared_ptr<vfs::IFileSystem> fs, const std::filesystem::path& sceneFileName);
        virtual bool LoadScene(std::shared_ptr<vfs::IFileSystem> fs, const std::filesystem::path& sceneFileName) = 0;
        virtual void SceneUnloading();
        virtual void SceneLoaded();

        void SetAsynchronousLoadingEnabled(bool enabled);
        bool IsSceneLoading() const;
        bool IsSceneLoaded() const;

        std::shared_ptr<engine::CommonRenderPasses> GetCommonPasses() const;
    };

	// returns the path to the currently running application's binary
	std::filesystem::path GetDirectoryWithExecutable();

	// searches paths upward from 'startPath' for a directory 'dirname'
	std::filesystem::path FindDirectory(vfs::IFileSystem& fs, const std::filesystem::path& startPath, const std::filesystem::path& dirname, int maxDepth = 5);
    
	// searches paths upward from 'startPath' for a file with 'relativePath'
    std::filesystem::path FindDirectoryWithFile(vfs::IFileSystem& fs, const std::filesystem::path& startPath, const std::filesystem::path& relativeFilePath, int maxDepth = 5);
	
	// searches path for scene files (traverses direct subdirectories too if 'subdirs' is true)
	std::vector<std::string> FindScenes(vfs::IFileSystem& fs, std::filesystem::path const& path);

    // returns the name of the subdirectory with shaders, i.e. "dxil", "dxbc" or "spirv" - depending on the API and build settings.
    const char* GetShaderTypeName(nvrhi::GraphicsAPI api);

	// searches upward from 'startPath' for a directory containing the compiled shader 'baseFileName'
    std::filesystem::path FindDirectoryWithShaderBin(nvrhi::GraphicsAPI api, vfs::IFileSystem& fs, const std::filesystem::path& startPath, const std::filesystem::path& relativeFilePath, const std::string& baseFileName, int maxDepth = 5);

	// attempts to locate a media folder in the following sequence:
	//   1. check if the the environment variable (env_donut_media_path) is set and points to
	//      a valid location
	//   2. search updward from the directory containing the application binary for a
	//      directory with 'relativeFilePath'
	static constexpr char const* env_donut_media_path = "DONUT_MEDIA_PATH";
	std::filesystem::path FindMediaFolder(const std::filesystem::path& name = "media");
    
	// parse args for flags: -d3d11, -dx11, -d3d12, -dx12, -vulkan, -vk
    nvrhi::GraphicsAPI GetGraphicsAPIFromCommandLine(int argc, const char* const* argv);

    // searches for a given substring in the list of scenes, returns that name if found;
    // if not found, returns the first scene in the list.
    std::string FindPreferredScene(const std::vector<std::string>& available, const std::string& preferred);
}