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
#include <utility>

namespace donut::vfs
{
    /* 
    Transparent compression and decompression layer for the virtual file system.
    Currently, it only supports LZ4 frame compression.

    Behavior:
    
    The readFile function tries to read the file with an extra '.lz4' extension
    appended first. If such file exists, it will be decompressed and returned.
    If no .lz4 file exists, the compression layer will read and return the file
    with the exact name requested.

    The writeFile function will compress the input data if the provided file name
    has an '.lz4' extension. If no such extension is present, the file will be 
    written uncompressed.

    The enumerateFiles function will search for files with the requested extensions
    and with extra '.lz4' extensions. The .lz4 extensions will be removed from 
    the returned file names and de-duplicated in case the same file exists in both
    compressed and uncompressed forms.

    Intended usage:

    The compression layer is designed to allow storing assets (shaders, models, textures)
    for an application in a package. It works well together with the TarFile layer.
    This combination - CompressionLayer on top of TarFile - enables parallel and
    very fast decompression of individual .lz4 compressed files within a tar archive.
    To create such an archive, one can use the existing tar and lz4 Unix utilities,
    or the 'scripts/lz4_tar.py' Python script provided with Donut.
    */
    
    class CompressionLayer : public IFileSystem
    {
    private:
        std::shared_ptr<IFileSystem> m_fs;
        int m_CompressionLevel = 5;

    public:
        explicit CompressionLayer(std::shared_ptr<IFileSystem> fs)
            : m_fs(std::move(fs))
        { }

        void setCompressionLevel(int level) { m_CompressionLevel = level; }
        
        bool folderExists(const std::filesystem::path& name) override;
        bool fileExists(const std::filesystem::path& name) override;
        std::shared_ptr<IBlob> readFile(const std::filesystem::path& name) override;
        bool writeFile(const std::filesystem::path& name, const void* data, size_t size) override;
        int enumerateFiles(const std::filesystem::path& path, const std::vector<std::string>& extensions, enumerate_callback_t callback, bool allowDuplicates = false) override;
        int enumerateDirectories(const std::filesystem::path& path, enumerate_callback_t callback, bool allowDuplicates = false) override;
    };
}
