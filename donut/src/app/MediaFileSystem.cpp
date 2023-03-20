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

#include <donut/app/MediaFileSystem.h>
#include <donut/app/ApplicationBase.h>
#include <donut/core/log.h>
#include <donut/core/string_utils.h>
#include <donut/core/vfs/TarFile.h>
#include <donut/core/vfs/Compression.h>
#ifdef DONUT_WITH_MINIZ
#include <donut/core/vfs/ZipFile.h>
#endif

#include <unordered_set>

using namespace donut::vfs;
using namespace donut::app;

MediaFileSystem::MediaFileSystem(
	std::shared_ptr<IFileSystem> parent, 
	const std::filesystem::path& mediaFolder)
{
	// always seach media folder vfs first
	auto mediafs = std::make_shared<RelativeFileSystem>(parent, mediaFolder);
	auto compressionLayer = std::make_shared<CompressionLayer>(mediafs);
	m_FileSystems.push_back(compressionLayer);

	// open package files & add a vfs for each
	NativeFileSystem* nativeFS = dynamic_cast<NativeFileSystem*>(parent.get());
	if (nativeFS)
	{
		std::vector<std::string> packs;
		if (mediafs->enumerateFiles("", { ".tar", ".zip", ".pkz" }, vfs::enumerate_to_vector(packs)) > 0)
		{
			// sort the packs in reverse because want to search
			// from 'highest revision' of a pack file down (ex: pack2.pkz is
			// searched before pack1.db)
			std::sort(packs.rbegin(), packs.rend());

			for (auto const& fileName : packs)
			{
				std::filesystem::path filePath = mediaFolder / fileName;

				bool mounted = false;
				if (string_utils::ends_with(fileName, ".tar"))
				{
					if (auto packfs = std::make_shared<TarFile>(filePath); packfs->isOpen())
					{
						auto tarDecompressionLayer = std::make_shared<CompressionLayer>(packfs);
						m_FileSystems.push_back(tarDecompressionLayer);
						mounted = true;
					}
				}
#ifdef DONUT_WITH_MINIZ
				else if (string_utils::ends_with(fileName, ".zip") || string_utils::ends_with(fileName, ".pkz"))
				{
					if (auto packfs = std::make_shared<ZipFile>(filePath); packfs->isOpen())
					{
						m_FileSystems.push_back(packfs);
						mounted = true;
					}
				}
#endif // DONUT_WITH_MINIZ
				else
				{
					log::warning("Cannot mount '%s': unsupported format. Skipping.", filePath.string().c_str());
					continue;
				}

				if (!mounted)
				{
					log::warning("Failed to mount '%s' (see above for errors). Skipping.", filePath.string().c_str());
				}
			}
		}
	}
}

std::vector<std::string> MediaFileSystem::GetAvailableScenes() const
{
	std::unordered_set<std::string> resultSet;
	for (auto fs : m_FileSystems)
	{
		if (auto scenes = FindScenes(*fs, "/"); !scenes.empty())
			resultSet.insert(scenes.begin(), scenes.end());
	}

	std::vector result(resultSet.begin(), resultSet.end());
	std::sort(result.begin(), result.end());
	return result;
}

// file system virtual overrides

bool MediaFileSystem::folderExists(const std::filesystem::path & path)
{
	for (const auto& fs : m_FileSystems)
		if (fs->folderExists(path))
			return true;
	return false;
}

bool MediaFileSystem::fileExists(const std::filesystem::path & path)
{
	for (const auto& fs : m_FileSystems)
		if (fs->fileExists(path))
			return true;
	return false;
}

std::shared_ptr<IBlob> MediaFileSystem::readFile(const std::filesystem::path & name)
{
	for (const auto& fs : m_FileSystems)
		if (std::shared_ptr<vfs::IBlob> blob = fs->readFile(name))
			return blob;
	return nullptr;
}

bool MediaFileSystem::writeFile(const std::filesystem::path & name, const void* data, size_t size)
{
	for (const auto& fs : m_FileSystems)
		if (fs->writeFile(name, data, size))
			return true;
	return false;
}

int MediaFileSystem::enumerateFiles(const std::filesystem::path& path, const std::vector<std::string>& extensions, enumerate_callback_t callback, bool allowDuplicates)
{
	int numRawResults = 0;
	std::unordered_set<std::string> resultSet;
	for (const auto& fs : m_FileSystems)
	{
		if (allowDuplicates)
		{
			int result = fs->enumerateFiles(path, extensions, callback, true);
			if (result >= 0)
				numRawResults += result;
		}
		else
		{
			fs->enumerateFiles(path, extensions,
				[&resultSet](std::string_view name)
				{
					resultSet.insert(std::string(name));
				}, true);
		}
	}

	if (!allowDuplicates)
	{
		// pass the deduplicated names to the caller
		std::for_each(resultSet.begin(), resultSet.end(), callback);
		return int(resultSet.size());
	}

	return numRawResults;
}

int MediaFileSystem::enumerateDirectories(const std::filesystem::path& path, enumerate_callback_t callback, bool allowDuplicates)
{
	int numRawResults = 0;
	std::unordered_set<std::string> resultSet;
	for (const auto& fs : m_FileSystems)
	{
		if (allowDuplicates)
		{
			int result = fs->enumerateDirectories(path, callback, true);
			if (result >= 0)
				numRawResults += result;
		}
		else
		{
			fs->enumerateDirectories(path,
				[&resultSet](std::string_view name)
				{
					resultSet.insert(std::string(name));
				}, true);
		}
	}

	if (!allowDuplicates)
	{
		// pass the deduplicated names to the caller
		std::for_each(resultSet.begin(), resultSet.end(), callback);
		return int(resultSet.size());
	}

	return numRawResults;
}
