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
License for glfw

Copyright (c) 2002-2006 Marcus Geelnard

Copyright (c) 2006-2019 Camilla Lowy

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would
   be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not
   be misrepresented as being the original software.

3. This notice may not be removed or altered from any source
   distribution.
*/

#include <donut/app/DeviceManager.h>
#include <donut/core/math/math.h>
#include <donut/core/log.h>
#include <nvrhi/utils.h>

#include <cstdio>
#include <iomanip>
#include <thread>
#include <sstream>

#if USE_DX11
#include <d3d11.h>
#endif

#if USE_DX12
#include <d3d12.h>
#endif

#ifdef _WINDOWS
#include <ShellScalingApi.h>
#pragma comment(lib, "shcore.lib")
#endif

using namespace donut::app;

// The joystick interface in glfw is not per-window like the keys, mouse, etc. The joystick callbacks
// don't take a window arg. So glfw's model is a global joystick shared by all windows. Hence, the equivalent 
// is a singleton class that all DeviceManager instances can use.
class JoyStickManager
{
public:
	static JoyStickManager& Singleton()
	{
		static JoyStickManager singleton;
		return singleton;
	}

	void UpdateAllJoysticks(const std::list<IRenderPass*>& passes);
	
	void EraseDisconnectedJoysticks();
	void EnumerateJoysticks();

	void ConnectJoystick(int id);
	void DisconnectJoystick(int id);

private:
	JoyStickManager() {}
	void UpdateJoystick(int j, const std::list<IRenderPass*>& passes);

	std::list<int> m_JoystickIDs, m_RemovedJoysticks;
};

static void ErrorCallback_GLFW(int error, const char *description)
{
    fprintf(stderr, "GLFW error: %s\n", description);
    exit(1);
}

static void WindowIconifyCallback_GLFW(GLFWwindow *window, int iconified)
{
    DeviceManager *manager = reinterpret_cast<DeviceManager *>(glfwGetWindowUserPointer(window));
    manager->WindowIconifyCallback(iconified);
}

static void WindowFocusCallback_GLFW(GLFWwindow *window, int focused)
{
    DeviceManager *manager = reinterpret_cast<DeviceManager *>(glfwGetWindowUserPointer(window));
    manager->WindowFocusCallback(focused);
}

static void WindowRefreshCallback_GLFW(GLFWwindow *window)
{
    DeviceManager *manager = reinterpret_cast<DeviceManager *>(glfwGetWindowUserPointer(window));
    manager->WindowRefreshCallback();
}

static void WindowCloseCallback_GLFW(GLFWwindow *window)
{
    DeviceManager *manager = reinterpret_cast<DeviceManager *>(glfwGetWindowUserPointer(window));
    manager->WindowCloseCallback();
}

static void WindowPosCallback_GLFW(GLFWwindow *window, int xpos, int ypos)
{
    DeviceManager *manager = reinterpret_cast<DeviceManager *>(glfwGetWindowUserPointer(window));
    manager->WindowPosCallback(xpos, ypos);
}

static void KeyCallback_GLFW(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    DeviceManager *manager = reinterpret_cast<DeviceManager *>(glfwGetWindowUserPointer(window));
    manager->KeyboardUpdate(key, scancode, action, mods);
}

static void CharModsCallback_GLFW(GLFWwindow *window, unsigned int unicode, int mods)
{
    DeviceManager *manager = reinterpret_cast<DeviceManager *>(glfwGetWindowUserPointer(window));
    manager->KeyboardCharInput(unicode, mods);
}

static void MousePosCallback_GLFW(GLFWwindow *window, double xpos, double ypos)
{
    DeviceManager *manager = reinterpret_cast<DeviceManager *>(glfwGetWindowUserPointer(window));
    manager->MousePosUpdate(xpos, ypos);
}

static void MouseButtonCallback_GLFW(GLFWwindow *window, int button, int action, int mods)
{
    DeviceManager *manager = reinterpret_cast<DeviceManager *>(glfwGetWindowUserPointer(window));
    manager->MouseButtonUpdate(button, action, mods);
}

static void MouseScrollCallback_GLFW(GLFWwindow *window, double xoffset, double yoffset)
{
    DeviceManager *manager = reinterpret_cast<DeviceManager *>(glfwGetWindowUserPointer(window));
    manager->MouseScrollUpdate(xoffset, yoffset);
}

static void JoystickConnectionCallback_GLFW(int joyId, int connectDisconnect)
{
	if (connectDisconnect == GLFW_CONNECTED)
		JoyStickManager::Singleton().ConnectJoystick(joyId);
	if (connectDisconnect == GLFW_DISCONNECTED)
		JoyStickManager::Singleton().DisconnectJoystick(joyId);
}

static const struct
{
    nvrhi::Format format;
    uint32_t redBits;
    uint32_t greenBits;
    uint32_t blueBits;
    uint32_t alphaBits;
    uint32_t depthBits;
    uint32_t stencilBits;
} formatInfo[] = {
    { nvrhi::Format::UNKNOWN,            0,  0,  0,  0,  0,  0, },
    { nvrhi::Format::R8_UINT,            8,  0,  0,  0,  0,  0, },
    { nvrhi::Format::RG8_UINT,           8,  8,  0,  0,  0,  0, },
    { nvrhi::Format::RG8_UNORM,          8,  8,  0,  0,  0,  0, },
    { nvrhi::Format::R16_UINT,          16,  0,  0,  0,  0,  0, },
    { nvrhi::Format::R16_UNORM,         16,  0,  0,  0,  0,  0, },
    { nvrhi::Format::R16_FLOAT,         16,  0,  0,  0,  0,  0, },
    { nvrhi::Format::RGBA8_UNORM,        8,  8,  8,  8,  0,  0, },
    { nvrhi::Format::RGBA8_SNORM,        8,  8,  8,  8,  0,  0, },
    { nvrhi::Format::BGRA8_UNORM,        8,  8,  8,  8,  0,  0, },
    { nvrhi::Format::SRGBA8_UNORM,       8,  8,  8,  8,  0,  0, },
    { nvrhi::Format::SBGRA8_UNORM,       8,  8,  8,  8,  0,  0, },
    { nvrhi::Format::R10G10B10A2_UNORM, 10, 10, 10,  2,  0,  0, },
    { nvrhi::Format::R11G11B10_FLOAT,   11, 11, 10,  0,  0,  0, },
    { nvrhi::Format::RG16_UINT,         16, 16,  0,  0,  0,  0, },
    { nvrhi::Format::RG16_FLOAT,        16, 16,  0,  0,  0,  0, },
    { nvrhi::Format::R32_UINT,          32,  0,  0,  0,  0,  0, },
    { nvrhi::Format::R32_FLOAT,         32,  0,  0,  0,  0,  0, },
    { nvrhi::Format::RGBA16_FLOAT,      16, 16, 16, 16,  0,  0, },
    { nvrhi::Format::RGBA16_UNORM,      16, 16, 16, 16,  0,  0, },
    { nvrhi::Format::RGBA16_SNORM,      16, 16, 16, 16,  0,  0, },
    { nvrhi::Format::RG32_UINT,         32, 32,  0,  0,  0,  0, },
    { nvrhi::Format::RG32_FLOAT,        32, 32,  0,  0,  0,  0, },
    { nvrhi::Format::RGB32_UINT,        32, 32, 32,  0,  0,  0, },
    { nvrhi::Format::RGB32_FLOAT,       32, 32, 32,  0,  0,  0, },
    { nvrhi::Format::RGBA32_UINT,       32, 32, 32, 32,  0,  0, },
    { nvrhi::Format::RGBA32_FLOAT,      32, 32, 32, 32,  0,  0, },
};

bool DeviceManager::CreateWindowDeviceAndSwapChain(const DeviceCreationParameters& params, const char *windowTitle)
{
#ifdef _WINDOWS
    if (params.enablePerMonitorDPI)
    {
        // this needs to happen before glfwInit in order to override GLFW behavior
        SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    } else {
        SetProcessDpiAwareness(PROCESS_DPI_UNAWARE);
    }
#endif

    if (!glfwInit())
    {
        return false;
    }

    this->m_DeviceParams = params;
    m_RequestedVSync = params.vsyncEnabled;

    glfwSetErrorCallback(ErrorCallback_GLFW);

    glfwDefaultWindowHints();

    bool foundFormat = false;
    for (const auto& info : formatInfo)
    {
        if (info.format == params.swapChainFormat)
        {
            glfwWindowHint(GLFW_RED_BITS, info.redBits);
            glfwWindowHint(GLFW_GREEN_BITS, info.greenBits);
            glfwWindowHint(GLFW_BLUE_BITS, info.blueBits);
            glfwWindowHint(GLFW_ALPHA_BITS, info.alphaBits);
            glfwWindowHint(GLFW_DEPTH_BITS, info.depthBits);
            glfwWindowHint(GLFW_STENCIL_BITS, info.stencilBits);
            foundFormat = true;
            break;
        }
    }

    assert(foundFormat);

    glfwWindowHint(GLFW_SAMPLES, params.swapChainSampleCount);
    glfwWindowHint(GLFW_REFRESH_RATE, params.refreshRate);

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);   // Ignored for fullscreen

    m_Window = glfwCreateWindow(params.backBufferWidth, params.backBufferHeight,
                                windowTitle ? windowTitle : "",
                                params.startFullscreen ? glfwGetPrimaryMonitor() : nullptr,
                                nullptr);

    if (m_Window == nullptr)
    {
        return false;
    }

    if (params.startFullscreen)
    {
        glfwSetWindowMonitor(m_Window, glfwGetPrimaryMonitor(), 0, 0,
            m_DeviceParams.backBufferWidth, m_DeviceParams.backBufferHeight, m_DeviceParams.refreshRate);
    }
    else
    {
        int fbWidth = 0, fbHeight = 0;
        glfwGetFramebufferSize(m_Window, &fbWidth, &fbHeight);
        m_DeviceParams.backBufferWidth = fbWidth;
        m_DeviceParams.backBufferHeight = fbHeight;
    }

    if (windowTitle)
        m_WindowTitle = windowTitle;

    glfwSetWindowUserPointer(m_Window, this);

    if (params.windowPosX != -1 && params.windowPosY != -1)
    {
        glfwSetWindowPos(m_Window, params.windowPosX, params.windowPosY);
    }

    if (params.startMaximized)
    {
        glfwMaximizeWindow(m_Window);
    }

    glfwSetWindowPosCallback(m_Window, WindowPosCallback_GLFW);
    glfwSetWindowCloseCallback(m_Window, WindowCloseCallback_GLFW);
    glfwSetWindowRefreshCallback(m_Window, WindowRefreshCallback_GLFW);
    glfwSetWindowFocusCallback(m_Window, WindowFocusCallback_GLFW);
    glfwSetWindowIconifyCallback(m_Window, WindowIconifyCallback_GLFW);
    glfwSetKeyCallback(m_Window, KeyCallback_GLFW);
    glfwSetCharModsCallback(m_Window, CharModsCallback_GLFW);
    glfwSetCursorPosCallback(m_Window, MousePosCallback_GLFW);
    glfwSetMouseButtonCallback(m_Window, MouseButtonCallback_GLFW);
    glfwSetScrollCallback(m_Window, MouseScrollCallback_GLFW);
	  glfwSetJoystickCallback(JoystickConnectionCallback_GLFW);

	  // If there are multiple device managers, then this would be called by each one which isn't necessary
	  // but should not hurt.
	  JoyStickManager::Singleton().EnumerateJoysticks();

    if (!CreateDeviceAndSwapChain())
        return false;

    glfwShowWindow(m_Window);

    // reset the back buffer size state to enforce a resize event
    m_DeviceParams.backBufferWidth = 0;
    m_DeviceParams.backBufferHeight = 0;

    UpdateWindowSize();

    return true;
}

bool DeviceManager::CreateDeviceAndSwapChain(const DeviceCreationParameters& params)
{
    m_RequestedVSync = params.vsyncEnabled;

    m_DeviceParams.backBufferWidth = params.backBufferWidth;
    m_DeviceParams.backBufferHeight = params.backBufferHeight;

    if (!CreateDeviceAndSwapChain())
        return false;

    UpdateWindowSize();

    return true;
}

void DeviceManager::AddRenderPassToFront(IRenderPass *pRenderPass)
{
    m_vRenderPasses.remove(pRenderPass);
    m_vRenderPasses.push_front(pRenderPass);

    pRenderPass->BackBufferResizing();
    pRenderPass->BackBufferResized(
        m_DeviceParams.backBufferWidth,
        m_DeviceParams.backBufferHeight,
        m_DeviceParams.swapChainSampleCount);
}

void DeviceManager::AddRenderPassToBack(IRenderPass *pRenderPass)
{
    m_vRenderPasses.remove(pRenderPass);
    m_vRenderPasses.push_back(pRenderPass);

    pRenderPass->BackBufferResizing();
    pRenderPass->BackBufferResized(
        m_DeviceParams.backBufferWidth,
        m_DeviceParams.backBufferHeight,
        m_DeviceParams.swapChainSampleCount);
}

void DeviceManager::RemoveRenderPass(IRenderPass *pRenderPass)
{
    m_vRenderPasses.remove(pRenderPass);
}

void DeviceManager::BackBufferResizing()
{
    m_SwapChainFramebuffers.clear();

    for (auto it : m_vRenderPasses)
    {
        it->BackBufferResizing();
    }
}

void DeviceManager::BackBufferResized()
{
    for(auto it : m_vRenderPasses)
    {
        it->BackBufferResized(m_DeviceParams.backBufferWidth,
                              m_DeviceParams.backBufferHeight,
                              m_DeviceParams.swapChainSampleCount);
    }

    uint32_t backBufferCount = GetBackBufferCount();
    m_SwapChainFramebuffers.resize(backBufferCount);
    for (uint32_t index = 0; index < backBufferCount; index++)
    {
        m_SwapChainFramebuffers[index] = GetDevice()->createFramebuffer(
            nvrhi::FramebufferDesc().addColorAttachment(GetBackBuffer(index)));
    }
}

void DeviceManager::Animate(double elapsedTime)
{
    for(auto it : m_vRenderPasses)
    {
        it->Animate(float(elapsedTime));
    }
}

void DeviceManager::Render()
{
    BeginFrame();
    
    nvrhi::IFramebuffer* framebuffer = m_SwapChainFramebuffers[GetCurrentBackBufferIndex()];

    for (auto it : m_vRenderPasses)
    {
        it->Render(framebuffer);
    }
}

void DeviceManager::UpdateAverageFrameTime(double elapsedTime)
{
    m_FrameTimeSum += elapsedTime;
    m_NumberOfAccumulatedFrames += 1;
    
    if (m_FrameTimeSum > m_AverageTimeUpdateInterval && m_NumberOfAccumulatedFrames > 0)
    {
        m_AverageFrameTime = m_FrameTimeSum / double(m_NumberOfAccumulatedFrames);
        m_NumberOfAccumulatedFrames = 0;
        m_FrameTimeSum = 0.0;
    }
}

void DeviceManager::RunMessageLoop()
{
    m_PreviousFrameTimestamp = glfwGetTime();

    while(!m_Window || !glfwWindowShouldClose(m_Window))
    {

        if (m_callbacks.beforeFrame != nullptr) m_callbacks.beforeFrame(*this);

        glfwPollEvents();

        UpdateWindowSize();

        double curTime = glfwGetTime();
        double elapsedTime = curTime - m_PreviousFrameTimestamp;

		JoyStickManager::Singleton().EraseDisconnectedJoysticks();
		JoyStickManager::Singleton().UpdateAllJoysticks(m_vRenderPasses);

        if (m_windowVisible)
        {
            if (m_callbacks.beforeAnimate) m_callbacks.beforeAnimate(*this);
            Animate(elapsedTime);
            if (m_callbacks.afterAnimate) m_callbacks.afterAnimate(*this);
            if (m_callbacks.beforeRender) m_callbacks.beforeRender(*this);
            Render();
            if (m_callbacks.afterRender) m_callbacks.afterRender(*this);
            if (m_callbacks.beforePresent) m_callbacks.beforePresent(*this);
            Present();
            if (m_callbacks.afterPresent) m_callbacks.afterPresent(*this);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(0));

        GetDevice()->runGarbageCollection();

        UpdateAverageFrameTime(elapsedTime);
        m_PreviousFrameTimestamp = curTime;

        ++m_FrameIndex;
    }

    GetDevice()->waitForIdle();
}

void DeviceManager::GetWindowDimensions(int& width, int& height)
{
    width = m_DeviceParams.backBufferWidth;
    height = m_DeviceParams.backBufferHeight;
}

const DeviceCreationParameters& DeviceManager::GetDeviceParams()
{
    return m_DeviceParams;
}

void DeviceManager::UpdateWindowSize()
{
    int width;
    int height;
    if (m_Window)
    {
        glfwGetWindowSize(m_Window, &width, &height);

        if (width == 0 || height == 0)
        {
            // window is minimized
            m_windowVisible = false;
            return;
        }
    }
    else
    {
        width = m_DeviceParams.backBufferWidth;
        height = m_DeviceParams.backBufferHeight;
    }

    m_windowVisible = true;

    if (int(m_DeviceParams.backBufferWidth) != width || 
        int(m_DeviceParams.backBufferHeight) != height ||
        (m_DeviceParams.vsyncEnabled != m_RequestedVSync && GetGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN) ||
        GetBackBufferCount() != m_SwapChainFramebuffers.size())
    {
        // window is not minimized, and the size has changed

        BackBufferResizing();

        m_DeviceParams.backBufferWidth = width;
        m_DeviceParams.backBufferHeight = height;
        m_DeviceParams.vsyncEnabled = m_RequestedVSync;

        ResizeSwapChain();
        BackBufferResized();

        assert(GetBackBufferCount() == m_SwapChainFramebuffers.size());
    }

    m_DeviceParams.vsyncEnabled = m_RequestedVSync;
}

void DeviceManager::WindowPosCallback(int x, int y)
{
#ifdef _WINDOWS
    if (m_DeviceParams.enablePerMonitorDPI)
    {
        HWND hwnd = glfwGetWin32Window(m_Window);
        auto monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

        unsigned int dpiX;
        unsigned int dpiY;
        GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);

        m_DPIScaleFactorX = dpiX / 96.f;
        m_DPIScaleFactorY = dpiY / 96.f;
    }
#endif
}

void DeviceManager::KeyboardUpdate(int key, int scancode, int action, int mods)
{
    if (key == -1)
    {
        // filter unknown keys
        return;
    }

    for (auto it = m_vRenderPasses.crbegin(); it != m_vRenderPasses.crend(); it++)
    {
        bool ret = (*it)->KeyboardUpdate(key, scancode, action, mods);
        if (ret)
            break;
    }
}

void DeviceManager::KeyboardCharInput(unsigned int unicode, int mods)
{
    for (auto it = m_vRenderPasses.crbegin(); it != m_vRenderPasses.crend(); it++)
    {
        bool ret = (*it)->KeyboardCharInput(unicode, mods);
        if (ret)
            break;
    }
}

void DeviceManager::MousePosUpdate(double xpos, double ypos)
{
    xpos /= m_DPIScaleFactorX;
    ypos /= m_DPIScaleFactorY;

    for (auto it = m_vRenderPasses.crbegin(); it != m_vRenderPasses.crend(); it++)
    {
        bool ret = (*it)->MousePosUpdate(xpos, ypos);
        if (ret)
            break;
    }
}

void DeviceManager::MouseButtonUpdate(int button, int action, int mods)
{
    for (auto it = m_vRenderPasses.crbegin(); it != m_vRenderPasses.crend(); it++)
    {
        bool ret = (*it)->MouseButtonUpdate(button, action, mods);
        if (ret)
            break;
    }
}

void DeviceManager::MouseScrollUpdate(double xoffset, double yoffset)
{
    for (auto it = m_vRenderPasses.crbegin(); it != m_vRenderPasses.crend(); it++)
    {
        bool ret = (*it)->MouseScrollUpdate(xoffset, yoffset);
        if (ret)
            break;
    }
}

void JoyStickManager::EnumerateJoysticks()
{
	// The glfw header says nothing about what values to expect for joystick IDs. Empirically, having connected two
	// simultaneously, glfw just seems to number them starting at 0.
	for (int i = 0; i != 10; ++i)
		if (glfwJoystickPresent(i))
			m_JoystickIDs.push_back(i);
}

void JoyStickManager::EraseDisconnectedJoysticks()
{
	while (!m_RemovedJoysticks.empty())
	{
		auto id = m_RemovedJoysticks.back();
		m_RemovedJoysticks.pop_back();

		auto it = std::find(m_JoystickIDs.begin(), m_JoystickIDs.end(), id);
		if (it != m_JoystickIDs.end())
			m_JoystickIDs.erase(it);
	}
}

void JoyStickManager::ConnectJoystick(int id)
{
	m_JoystickIDs.push_back(id);
}

void JoyStickManager::DisconnectJoystick(int id)
{
	// This fn can be called from inside glfwGetJoystickAxes below (similarly for buttons, I guess).
	// We can't call m_JoystickIDs.erase() here and now. Save them for later. Forunately, glfw docs
	// say that you can query a joystick ID that isn't present.
	m_RemovedJoysticks.push_back(id);
}

void JoyStickManager::UpdateAllJoysticks(const std::list<IRenderPass*>& passes)
{
	for (auto j = m_JoystickIDs.begin(); j != m_JoystickIDs.end(); ++j)
		UpdateJoystick(*j, passes);
}

static void ApplyDeadZone(dm::float2& v, const float deadZone = 0.1f)
{
    v *= std::max(dm::length(v) - deadZone, 0.f) / (1.f - deadZone);
}

void JoyStickManager::UpdateJoystick(int j, const std::list<IRenderPass*>& passes)
{
    GLFWgamepadstate gamepadState;
    glfwGetGamepadState(j, &gamepadState);

	float* axisValues = gamepadState.axes;

    auto updateAxis = [&] (int axis, float axisVal)
    {
		for (auto it = passes.crbegin(); it != passes.crend(); it++)
		{
			bool ret = (*it)->JoystickAxisUpdate(axis, axisVal);
			if (ret)
				break;
		}
    };

    {
        dm::float2 v(axisValues[GLFW_GAMEPAD_AXIS_LEFT_X], axisValues[GLFW_GAMEPAD_AXIS_LEFT_Y]);
        ApplyDeadZone(v);
        updateAxis(GLFW_GAMEPAD_AXIS_LEFT_X, v.x);
        updateAxis(GLFW_GAMEPAD_AXIS_LEFT_Y, v.y);
    }

    {
        dm::float2 v(axisValues[GLFW_GAMEPAD_AXIS_RIGHT_X], axisValues[GLFW_GAMEPAD_AXIS_RIGHT_Y]);
        ApplyDeadZone(v);
        updateAxis(GLFW_GAMEPAD_AXIS_RIGHT_X, v.x);
        updateAxis(GLFW_GAMEPAD_AXIS_RIGHT_Y, v.y);
    }

    updateAxis(GLFW_GAMEPAD_AXIS_LEFT_TRIGGER, axisValues[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER]);
    updateAxis(GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER, axisValues[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER]);

	for (int b = 0; b != GLFW_GAMEPAD_BUTTON_LAST; ++b)
	{
		auto buttonVal = gamepadState.buttons[b];
		for (auto it = passes.crbegin(); it != passes.crend(); it++)
		{
			bool ret = (*it)->JoystickButtonUpdate(b, buttonVal == GLFW_PRESS);
			if (ret)
				break;
		}
	}
}

void DeviceManager::Shutdown()
{
    m_SwapChainFramebuffers.clear();

    DestroyDeviceAndSwapChain();

    if (m_Window)
    {
        glfwDestroyWindow(m_Window);
        m_Window = nullptr;
    }

    glfwTerminate();
}

nvrhi::IFramebuffer* donut::app::DeviceManager::GetCurrentFramebuffer()
{
    return GetFramebuffer(GetCurrentBackBufferIndex());
}

nvrhi::IFramebuffer* donut::app::DeviceManager::GetFramebuffer(uint32_t index)
{
    if (index < m_SwapChainFramebuffers.size())
        return m_SwapChainFramebuffers[index];

    return nullptr;
}

void DeviceManager::SetWindowTitle(const char* title)
{
    if (!m_Window)
        return;

    assert(title);
    if (m_WindowTitle == title)
        return;

    glfwSetWindowTitle(m_Window, title);

    m_WindowTitle = title;
}

void DeviceManager::SetInformativeWindowTitle(const char* applicationName, bool includeFramerate, const char* extraInfo)
{
    std::stringstream ss;
    ss << applicationName;
    ss << " (" << nvrhi::utils::GraphicsAPIToString(GetDevice()->getGraphicsAPI());

    if (m_DeviceParams.enableDebugRuntime)
    {
        if (GetGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN)
            ss << ", VulkanValidationLayer";
        else
            ss << ", DebugRuntime";
    }

    if (m_DeviceParams.enableNvrhiValidationLayer)
    {
        ss << ", NvrhiValidationLayer";
    }

    ss << ")";

    if (includeFramerate)
    {
        double frameTime = GetAverageFrameTimeSeconds();
        if (frameTime > 0)
        {
            ss << " - " << std::setprecision(4) << (1.0 / frameTime) << " FPS ";
        }
    }

    if (extraInfo)
        ss << extraInfo;

    SetWindowTitle(ss.str().c_str());
}

donut::app::DeviceManager* donut::app::DeviceManager::Create(nvrhi::GraphicsAPI api)
{
    switch (api)
    {
#if USE_DX11
    case nvrhi::GraphicsAPI::D3D11:
        return CreateD3D11();
#endif
#if USE_DX12
    case nvrhi::GraphicsAPI::D3D12:
        return CreateD3D12();
#endif
#if USE_VK
    case nvrhi::GraphicsAPI::VULKAN:
        return CreateVK();
#endif
    default:
        log::error("DeviceManager::Create: Unsupported Graphics API (%d)", api);
        return nullptr;
    }
}

DefaultMessageCallback& DefaultMessageCallback::GetInstance()
{
    static DefaultMessageCallback Instance;
    return Instance;
}

void DefaultMessageCallback::message(nvrhi::MessageSeverity severity, const char* messageText)
{
    donut::log::Severity donutSeverity = donut::log::Severity::Info;
    switch (severity)
    {
    case nvrhi::MessageSeverity::Info:
        donutSeverity = donut::log::Severity::Info;
        break;
    case nvrhi::MessageSeverity::Warning:
        donutSeverity = donut::log::Severity::Warning;
        break;
    case nvrhi::MessageSeverity::Error:
        donutSeverity = donut::log::Severity::Error;
        break;
    case nvrhi::MessageSeverity::Fatal:
        donutSeverity = donut::log::Severity::Fatal;
        break;
    }
    
    donut::log::message(donutSeverity, "%s", messageText);
}
