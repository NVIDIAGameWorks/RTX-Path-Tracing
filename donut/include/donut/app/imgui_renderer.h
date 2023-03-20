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

/*
License for Dear ImGui

Copyright (c) 2014-2019 Omar Cornut

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include <donut/app/DeviceManager.h>
#include <donut/app/imgui_nvrhi.h>

#include <filesystem>
#include <memory>
#include <optional>

namespace donut::vfs
{
    class IBlob;
    class IFileSystem;
}

namespace donut::engine
{
    class ShaderFactory;
}

namespace donut::app
{
    // base class to build IRenderPass-based UIs using ImGui through NVRHI
    class ImGui_Renderer : public IRenderPass
    {
    protected:

        std::unique_ptr<ImGui_NVRHI> imgui_nvrhi;

        // buffer mouse click and keypress events to make sure we don't lose events which last less than a full frame
        std::array<bool, 3> mouseDown = { false };
        std::array<bool, GLFW_KEY_LAST + 1> keyDown = { false };

    public:
        ImGui_Renderer(DeviceManager *devManager);
        ~ImGui_Renderer();
        bool Init(std::shared_ptr<engine::ShaderFactory> shaderFactory);
        
		ImFont* LoadFont(vfs::IFileSystem& fs, std::filesystem::path const& fontFile, float fontSize);

        virtual bool KeyboardUpdate(int key, int scancode, int action, int mods) override;
        virtual bool KeyboardCharInput(unsigned int unicode, int mods) override;
        virtual bool MousePosUpdate(double xpos, double ypos) override;
        virtual bool MouseScrollUpdate(double xoffset, double yoffset) override;
        virtual bool MouseButtonUpdate(int button, int action, int mods) override;
        virtual void Animate(float elapsedTimeSeconds) override;
        virtual void Render(nvrhi::IFramebuffer* framebuffer) override;
        virtual void BackBufferResizing() override;

    protected:
        // creates the UI in ImGui, updates internal UI state
        virtual void buildUI(void) = 0;

        void BeginFullScreenWindow();
        void DrawScreenCenteredText(const char* text);
        void EndFullScreenWindow();
    };
}