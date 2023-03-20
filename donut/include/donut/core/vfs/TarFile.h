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

#include <donut/core/vfs/VFS.h>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace donut::vfs
{
    /* 
    A read-only file system that provides access to files in a tar archive.
    The archive is partially read to enumerate the files when TarFile is created.
    TarFile can only operate on real files, i.e. underlying virtual file systems are not supported.
    Designed to work in combination with CompressionLayer to store packaged assets.
    */
    class TarFile : public IFileSystem
    {
    private:
        std::string m_ArchivePath;
        std::mutex m_Mutex;
        FILE* m_ArchiveFile = nullptr;

        struct FileEntry
        {
            size_t offset = 0;
            size_t size = 0;
        };

        std::unordered_map<std::string, FileEntry> m_Files;
        std::unordered_set<std::string> m_Directories;
        
    public:
        TarFile(const std::filesystem::path& archivePath);
        ~TarFile() override;

        [[nodiscard]] bool isOpen() const;
        
        bool folderExists(const std::filesystem::path& name) override;
        bool fileExists(const std::filesystem::path& name) override;
        std::shared_ptr<IBlob> readFile(const std::filesystem::path& name) override;
        bool writeFile(const std::filesystem::path& name, const void* data, size_t size) override;
        int enumerateFiles(const std::filesystem::path& path, const std::vector<std::string>& extensions, enumerate_callback_t callback, bool allowDuplicates = false) override;
        int enumerateDirectories(const std::filesystem::path& path, enumerate_callback_t callback, bool allowDuplicates = false) override;
    };
}
