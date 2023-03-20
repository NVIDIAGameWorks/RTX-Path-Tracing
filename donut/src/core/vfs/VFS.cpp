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

#include <donut/core/vfs/VFS.h>
#include <donut/core/log.h>
#include <donut/core/string_utils.h>
#include <fstream>
#include <cassert>
#include <algorithm>
#include <utility>
#include <sstream>

#ifdef WIN32
#include <Shlwapi.h>
#else
extern "C" {
#include <glob.h>
}
#endif // _WIN32

using namespace donut::vfs;

Blob::Blob(void* data, size_t size)
    : m_data(data)
    , m_size(size)
{

}

const void* Blob::data() const
{
    return m_data;
}

size_t Blob::size() const
{
    return m_size;
}

Blob::~Blob()
{
    if (m_data)
    {
        free(m_data);
        m_data = nullptr;
    }

    m_size = 0;
}

bool NativeFileSystem::folderExists(const std::filesystem::path& name)
{
	return std::filesystem::exists(name) && std::filesystem::is_directory(name);
}

bool NativeFileSystem::fileExists(const std::filesystem::path& name)
{
    return std::filesystem::exists(name) && std::filesystem::is_regular_file(name);
}

std::shared_ptr<IBlob> NativeFileSystem::readFile(const std::filesystem::path& name)
{
    // TODO: better error reporting

    std::ifstream file(name, std::ios::binary);

    if (!file.is_open())
    {
        // file does not exist or is locked
        return nullptr;
    }

    file.seekg(0, std::ios::end);
    uint64_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    {
        // file larger than size_t
        assert(false);
        return nullptr;
    }

    char* data = static_cast<char*>(malloc(size));

    if (data == nullptr)
    {
        // out of memory
        assert(false);
        return nullptr;
    }

    file.read(data, size);

    if (!file.good())
    {
        // reading error
        assert(false);
        return nullptr;
    }

    return std::make_shared<Blob>(data, size);
}

bool NativeFileSystem::writeFile(const std::filesystem::path& name, const void* data, size_t size)
{
    // TODO: better error reporting

    std::ofstream file(name, std::ios::binary);

    if (!file.is_open())
    {
        // file does not exist or is locked
        return false;
    }

    if (size > 0)
    {
        file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    }

    if (!file.good())
    {
        // writing error
        return false;
    }

    return true;
}

static int enumerateNativeFiles(const char* pattern, bool directories, enumerate_callback_t callback)
{
#ifdef WIN32

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(pattern, &findData);

    if (hFind == INVALID_HANDLE_VALUE)
    {
        if (GetLastError() == ERROR_FILE_NOT_FOUND)
            return 0;

        return status::Failed;
    }

    int numEntries = 0;

    do
    {
        bool isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        bool isDot = strcmp(findData.cFileName, ".") == 0;
        bool isDotDot = strcmp(findData.cFileName, "..") == 0;

        if ((isDirectory == directories) && !isDot && !isDotDot)
        {
            callback(findData.cFileName);
            ++numEntries;
        }
    } while (FindNextFileA(hFind, &findData) != 0);

    FindClose(hFind);

    return numEntries;

#else // WIN32

    glob64_t glob_matches;
    int globResult = glob64(pattern, 0 /*flags*/, nullptr /*errfunc*/, &glob_matches);

    if (globResult == 0)
    {
        int numEntries = 0;

        for (int i=0; i<glob_matches.gl_pathc; ++i)
        {
            const char* globentry = (glob_matches.gl_pathv)[i];
            std::error_code ec, ec2;
            std::filesystem::directory_entry entry(globentry, ec);
            if (!ec)
            {
                if (directories == entry.is_directory(ec2) && !ec2)
                {
                    callback(entry.path().filename().native());
                    ++numEntries;
                }
            }
        }
        globfree64(&glob_matches);

        return numEntries;
    }

    if (globResult == GLOB_NOMATCH)
        return 0;

    return status::Failed;

#endif // WIN32
}

int NativeFileSystem::enumerateFiles(const std::filesystem::path& path, const std::vector<std::string>& extensions, enumerate_callback_t callback, bool allowDuplicates)
{
    (void)allowDuplicates;

    if (extensions.empty())
    {
        std::string pattern = (path / "*").generic_string();
        return enumerateNativeFiles(pattern.c_str(), false, callback);
    }

    int numEntries = 0;
    for (const auto& ext : extensions)
    {
        std::string pattern = (path / ("*" + ext)).generic_string();
        int result = enumerateNativeFiles(pattern.c_str(), false, callback);

        if (result < 0)
            return result;

        numEntries += result;
    }

    return numEntries;
}

int NativeFileSystem::enumerateDirectories(const std::filesystem::path& path, enumerate_callback_t callback, bool allowDuplicates)
{
    (void)allowDuplicates;

    std::string pattern = (path / "*").generic_string();
    return enumerateNativeFiles(pattern.c_str(), true, callback);
}

RelativeFileSystem::RelativeFileSystem(std::shared_ptr<IFileSystem> fs, const std::filesystem::path& basePath)
    : m_UnderlyingFS(std::move(fs))
    , m_BasePath(basePath.lexically_normal())
{
}

bool RelativeFileSystem::folderExists(const std::filesystem::path& name)
{
	return m_UnderlyingFS->folderExists(m_BasePath / name.relative_path());
}

bool RelativeFileSystem::fileExists(const std::filesystem::path& name)
{
    return m_UnderlyingFS->fileExists(m_BasePath / name.relative_path());
}

std::shared_ptr<IBlob> RelativeFileSystem::readFile(const std::filesystem::path& name)
{
    return m_UnderlyingFS->readFile(m_BasePath / name.relative_path());
}

bool RelativeFileSystem::writeFile(const std::filesystem::path& name, const void* data, size_t size)
{
    return m_UnderlyingFS->writeFile(m_BasePath / name.relative_path(), data, size);
}

int RelativeFileSystem::enumerateFiles(const std::filesystem::path& path, const std::vector<std::string>& extensions, enumerate_callback_t callback, bool allowDuplicates)
{
    return m_UnderlyingFS->enumerateFiles(m_BasePath / path.relative_path(), extensions, callback, allowDuplicates);
}

int RelativeFileSystem::enumerateDirectories(const std::filesystem::path& path, enumerate_callback_t callback, bool allowDuplicates)
{
    return m_UnderlyingFS->enumerateDirectories(m_BasePath / path.relative_path(), callback, allowDuplicates);
}

void RootFileSystem::mount(const std::filesystem::path& path, std::shared_ptr<IFileSystem> fs)
{
    if (findMountPoint(path, nullptr, nullptr))
    {
        log::error("Cannot mount a filesystem at %s: there is another FS that includes this path", path.c_str());
        return;
    }

    m_MountPoints.push_back(std::make_pair(path.lexically_normal().generic_string(), fs));
}

void donut::vfs::RootFileSystem::mount(const std::filesystem::path& path, const std::filesystem::path& nativePath)
{
    mount(path, std::make_shared<RelativeFileSystem>(std::make_shared<NativeFileSystem>(), nativePath));
}

bool RootFileSystem::unmount(const std::filesystem::path& path)
{
    std::string spath = path.lexically_normal().generic_string();

    for (size_t index = 0; index < m_MountPoints.size(); index++)
    {
        if (m_MountPoints[index].first == spath)
        {
            m_MountPoints.erase(m_MountPoints.begin() + index);
            return true;
        }
    }

    return false;
}

bool RootFileSystem::findMountPoint(const std::filesystem::path& path, std::filesystem::path* pRelativePath, IFileSystem** ppFS)
{
    std::string spath = path.lexically_normal().generic_string();

    for (auto it : m_MountPoints)
    {
        if (spath.find(it.first, 0) == 0 && ((spath.length() == it.first.length()) || (spath[it.first.length()] == '/')))
        {
            if (pRelativePath)
            {
                std::string relative = spath.substr(it.first.size() + 1);
                *pRelativePath = relative;
            }

            if (ppFS)
            {
                *ppFS = it.second.get();
            }

            return true;
        }
    }

    return false;
}

bool RootFileSystem::folderExists(const std::filesystem::path& name)
{
	std::filesystem::path relativePath;
	IFileSystem* fs = nullptr;

	if (findMountPoint(name, &relativePath, &fs))
	{
		return fs->folderExists(relativePath);
	}

	return false;
}

bool RootFileSystem::fileExists(const std::filesystem::path& name)
{
    std::filesystem::path relativePath;
    IFileSystem* fs = nullptr;

    if (findMountPoint(name, &relativePath, &fs))
    {
        return fs->fileExists(relativePath);
    }

    return false;
}

std::shared_ptr<IBlob> RootFileSystem::readFile(const std::filesystem::path& name)
{
    std::filesystem::path relativePath;
    IFileSystem* fs = nullptr;

    if (findMountPoint(name, &relativePath, &fs))
    {
        return fs->readFile(relativePath);
    }

    return nullptr;
}

bool RootFileSystem::writeFile(const std::filesystem::path& name, const void* data, size_t size)
{
    std::filesystem::path relativePath;
    IFileSystem* fs = nullptr;

    if (findMountPoint(name, &relativePath, &fs))
    {
        return fs->writeFile(relativePath, data, size);
    }

    return false;
}

int RootFileSystem::enumerateFiles(const std::filesystem::path& path, const std::vector<std::string>& extensions, enumerate_callback_t callback, bool allowDuplicates)
{
    std::filesystem::path relativePath;
    IFileSystem* fs = nullptr;

    if (findMountPoint(path, &relativePath, &fs))
    {
        return fs->enumerateFiles(relativePath, extensions, callback, allowDuplicates);
    }

    return status::PathNotFound;
}

int RootFileSystem::enumerateDirectories(const std::filesystem::path& path, enumerate_callback_t callback, bool allowDuplicates)
{
    std::filesystem::path relativePath;
    IFileSystem* fs = nullptr;

    if (findMountPoint(path, &relativePath, &fs))
    {
        return fs->enumerateDirectories(relativePath, callback, allowDuplicates);
    }

    return status::PathNotFound;
}

static void appendPatternToRegex(const std::string& pattern, std::stringstream& regex)
{
    for (char c : pattern)
    {
        switch (c)
        {
        case '?': regex << "[^/]?"; break;
        case '*': regex << "[^/]+"; break;
        case '.': regex << "\\."; break;
        default: regex << c;
        }
    }
}

std::string donut::vfs::getFileSearchRegex(const std::filesystem::path& path, const std::vector<std::string>& extensions)
{
    std::filesystem::path normalizedPath = path.lexically_normal();
    std::string normalizedPathStr = normalizedPath.generic_string();

    std::stringstream regex;
    appendPatternToRegex(normalizedPathStr, regex);
    if (!string_utils::ends_with(normalizedPathStr, "/") && !normalizedPath.empty())
        regex << '/';
    regex << "[^/]+";

    if (!extensions.empty())
    {
        regex << '(';
        bool first = true;
        for (const auto& ext : extensions)
        {
            if (!first) regex << '|';
            appendPatternToRegex(ext, regex);
            first = false;
        }
        regex << ')';
    }

    return regex.str();
}
