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

#include <memory>
#include <filesystem>

namespace donut::vfs
{
    class IBlob;
    class IFileSystem;
}

namespace donut::engine
{
    struct SceneImportResult;
    struct SceneLoadingStats;
    class TextureCache;
    class SceneGraphNode;
    class SceneTypeFactory;
    class SceneGraphAnimation;
}

namespace tf
{
    class Executor;
}

namespace donut::engine
{
    class GltfImporter
    {   
    protected:
        std::shared_ptr<vfs::IFileSystem> m_fs;
        std::shared_ptr<SceneTypeFactory> m_SceneTypeFactory;
        
    public:
        explicit GltfImporter(std::shared_ptr<vfs::IFileSystem> fs, std::shared_ptr<SceneTypeFactory> sceneTypeFactory);
        
        bool Load(
            const std::filesystem::path& fileName,
            TextureCache& textureCache,
            SceneLoadingStats& stats,
            tf::Executor* executor,
            SceneImportResult& result) const;
    };
}
