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

#include <donut/core/vfs/VFS.h>

#include <filesystem>
#include <memory>
#include <vector>

namespace donut::app
{
	//
	// A dedicated virtual file system for media assets implementing file access
	// policies as follows:
	//
	//   * all media assets are located under a single 'path' under the 'parent' 
	//     filesystem (typically a physical vfs::NativeFileSystem)
	//
	//   * on creation, the MediaFileSystem scans the media directory for all
	//     package files at the media directory root (in parent file system), and,
	//     where possible, opens them with an appropriate virtual file system 
	//     (ex. vfs::TarFile)
	//
	//   * all file paths relative to the MediaFileSystem are resolved uniquely
	//     in the following order:
	//
	//        1. search the directory structure in the parent file system for
	//           an exact match
	//
	//        2. search package files in descending lexical order
	//           (ex. zap.db => pack2.db => pack1.db => abc.db)
	//
	// note: MediaFileSystem can be mounted under a RootFileSytem
	//
	class MediaFileSystem : public vfs::IFileSystem
	{
	public:

		MediaFileSystem(std::shared_ptr<IFileSystem> parent, const std::filesystem::path& path);

		// searches media directories & packages for scene files & returns a set of unique paths
		std::vector<std::string> GetAvailableScenes() const;
	
	public:

		// VFS overrides

		bool folderExists(const std::filesystem::path& name) override;
		bool fileExists(const std::filesystem::path& name) override;
		std::shared_ptr<vfs::IBlob> readFile(const std::filesystem::path& name) override;
		bool writeFile(const std::filesystem::path& name, const void* data, size_t size) override;
		int enumerateFiles(const std::filesystem::path& path, const std::vector<std::string>& extensions, vfs::enumerate_callback_t callback, bool allowDuplicates = false) override;
		int enumerateDirectories(const std::filesystem::path& path, vfs::enumerate_callback_t callback, bool allowDuplicates = false) override;

	private:
		std::vector<std::shared_ptr<vfs::IFileSystem>> m_FileSystems;
	};
} // end namespace donut::app