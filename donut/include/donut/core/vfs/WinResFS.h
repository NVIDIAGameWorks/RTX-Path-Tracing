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

namespace donut::vfs
{
    /*

    File system interface for Windows module (EXE or DLL) resources.
    Automatically included into Donut builds for WIN32.

    Supports enumerating and reading resources of a given type, "BINARY" by default.
    Resource names are case insensitive, and all resource names are stored in the 
    modules in uppercase, and reported by enumerate(...) also in uppercase.

    To add a resource to the application, use a .rc file, with lines like this one:

        resource_name BINARY "real_file_path"

    The <resource_name> part is interpreted by this interface as a virtual file path,
    and it can include slashes. The <real_file_path> part is path to the actual file to
    be embedded, and it should be enclosed in quotes.

    */
    class WinResFileSystem : public IFileSystem
    {
    private:
        const void* m_hModule;
        std::string m_Type;
        std::vector<std::string> m_ResourceNames;

    public:
        WinResFileSystem(const void* hModule, const char* type = "BINARY");

        bool folderExists(const std::filesystem::path& name) override;
        bool fileExists(const std::filesystem::path& name) override;
        std::shared_ptr<IBlob> readFile(const std::filesystem::path& name) override;
        bool writeFile(const std::filesystem::path& name, const void* data, size_t size) override;
        int enumerateFiles(const std::filesystem::path& path, const std::vector<std::string>& extensions, enumerate_callback_t callback, bool allowDuplicates = false) override;
        int enumerateDirectories(const std::filesystem::path& path, enumerate_callback_t callback, bool allowDuplicates = false) override;
    };
}