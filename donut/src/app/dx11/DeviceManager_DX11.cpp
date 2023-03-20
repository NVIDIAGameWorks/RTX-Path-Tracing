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

#include <string>
#include <algorithm>
#include <locale>

#include <donut/app/DeviceManager.h>
#include <donut/core/log.h>

#include <Windows.h>
#include <dxgi1_3.h>
#include <dxgidebug.h>

#include <nvrhi/d3d11.h>
#include <nvrhi/validation.h>

using nvrhi::RefCountPtr;

using namespace donut::app;

class DeviceManager_DX11 : public DeviceManager
{
    RefCountPtr<ID3D11Device> m_Device;
    RefCountPtr<ID3D11DeviceContext> m_ImmediateContext;
    RefCountPtr<IDXGISwapChain> m_SwapChain;
    DXGI_SWAP_CHAIN_DESC m_SwapChainDesc{};
    HWND m_hWnd = nullptr;

    nvrhi::DeviceHandle m_NvrhiDevice;
    nvrhi::TextureHandle m_RhiBackBuffer;
    RefCountPtr<ID3D11Texture2D> m_D3D11BackBuffer;

    std::string m_RendererString;

public:
    [[nodiscard]] const char* GetRendererString() const override
    {
        return m_RendererString.c_str();
    }

    [[nodiscard]] nvrhi::IDevice* GetDevice() const override
    {
        return m_NvrhiDevice;
    }

    void BeginFrame() override;

    void ReportLiveObjects() override;

    [[nodiscard]] nvrhi::GraphicsAPI GetGraphicsAPI() const override
    {
        return nvrhi::GraphicsAPI::D3D11;
    }
protected:
    bool CreateDeviceAndSwapChain() override;
    void DestroyDeviceAndSwapChain() override;
    void ResizeSwapChain() override;

    nvrhi::ITexture* GetCurrentBackBuffer() override
    {
        return m_RhiBackBuffer;
    }

    nvrhi::ITexture* GetBackBuffer(uint32_t index) override
    {
        if (index == 0)
            return m_RhiBackBuffer;

        return nullptr;
    }

    uint32_t GetCurrentBackBufferIndex() override
    {
        return 0;
    }

    uint32_t GetBackBufferCount() override
    {
        return 1;
    }

    void Present() override;


private:
    bool CreateRenderTarget();
    void ReleaseRenderTarget();
};

static bool IsNvDeviceID(UINT id)
{
    return id == 0x10DE;
}

// Find an adapter whose name contains the given string.
static RefCountPtr<IDXGIAdapter> FindAdapter(const std::wstring& targetName)
{
    RefCountPtr<IDXGIAdapter> targetAdapter;
    RefCountPtr<IDXGIFactory1> DXGIFactory;
    HRESULT hres = CreateDXGIFactory1(IID_PPV_ARGS(&DXGIFactory));
    if (hres != S_OK)
    {
        donut::log::error("ERROR in CreateDXGIFactory.\n"
            "For more info, get log from debug D3D runtime: (1) Install DX SDK, and enable Debug D3D from DX Control Panel Utility. (2) Install and start DbgView. (3) Try running the program again.\n");
        return targetAdapter;
    }

    unsigned int adapterNo = 0;
    while (SUCCEEDED(hres))
    {
        RefCountPtr<IDXGIAdapter> pAdapter;
        hres = DXGIFactory->EnumAdapters(adapterNo, &pAdapter);

        if (SUCCEEDED(hres))
        {
            DXGI_ADAPTER_DESC aDesc;
            pAdapter->GetDesc(&aDesc);

            // If no name is specified, return the first adapater.  This is the same behaviour as the
            // default specified for D3D11CreateDevice when no adapter is specified.
            if (targetName.length() == 0)
            {
                targetAdapter = pAdapter;
                break;
            }

            std::wstring aName = aDesc.Description;

            if (aName.find(targetName) != std::string::npos)
            {
                targetAdapter = pAdapter;
                break;
            }
        }

        adapterNo++;
    }

    return targetAdapter;
}

// Adjust window rect so that it is centred on the given adapter.  Clamps to fit if it's too big.
static bool MoveWindowOntoAdapter(IDXGIAdapter* targetAdapter, RECT& rect)
{
    assert(targetAdapter != NULL);

    HRESULT hres = S_OK;
    unsigned int outputNo = 0;
    while (SUCCEEDED(hres))
    {
        IDXGIOutput* pOutput = nullptr;
        hres = targetAdapter->EnumOutputs(outputNo++, &pOutput);

        if (SUCCEEDED(hres) && pOutput)
        {
            DXGI_OUTPUT_DESC OutputDesc;
            pOutput->GetDesc(&OutputDesc);
            const RECT desktop = OutputDesc.DesktopCoordinates;
            const int centreX = (int)desktop.left + (int)(desktop.right - desktop.left) / 2;
            const int centreY = (int)desktop.top + (int)(desktop.bottom - desktop.top) / 2;
            const int winW = rect.right - rect.left;
            const int winH = rect.bottom - rect.top;
            const int left = centreX - winW / 2;
            const int right = left + winW;
            const int top = centreY - winH / 2;
            const int bottom = top + winH;
            rect.left = std::max(left, (int)desktop.left);
            rect.right = std::min(right, (int)desktop.right);
            rect.bottom = std::min(bottom, (int)desktop.bottom);
            rect.top = std::max(top, (int)desktop.top);

            // If there is more than one output, go with the first found.  Multi-monitor support could go here.
            return true;
        }
    }

    return false;
}

void DeviceManager_DX11::BeginFrame()
{
    DXGI_SWAP_CHAIN_DESC newSwapChainDesc;
    if (SUCCEEDED(m_SwapChain->GetDesc(&newSwapChainDesc)))
    {
        if (m_SwapChainDesc.Windowed != newSwapChainDesc.Windowed)
        {
            BackBufferResizing();

            m_SwapChainDesc = newSwapChainDesc;
            m_DeviceParams.backBufferWidth = newSwapChainDesc.BufferDesc.Width;
            m_DeviceParams.backBufferHeight = newSwapChainDesc.BufferDesc.Height;

            if (newSwapChainDesc.Windowed)
                glfwSetWindowMonitor(m_Window, nullptr, 50, 50, newSwapChainDesc.BufferDesc.Width, newSwapChainDesc.BufferDesc.Height, 0);

            ResizeSwapChain();
            BackBufferResized();
        }

    }
}

void DeviceManager_DX11::ReportLiveObjects()
{
    nvrhi::RefCountPtr<IDXGIDebug> pDebug;
    DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug));

    if (pDebug)
        pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_DETAIL);
}

bool DeviceManager_DX11::CreateDeviceAndSwapChain()
{
    UINT windowStyle = m_DeviceParams.startFullscreen
        ? (WS_POPUP | WS_SYSMENU | WS_VISIBLE)
        : m_DeviceParams.startMaximized
            ? (WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_MAXIMIZE)
            : (WS_OVERLAPPEDWINDOW | WS_VISIBLE);

    RECT rect = { 0, 0, LONG(m_DeviceParams.backBufferWidth), LONG(m_DeviceParams.backBufferHeight) };
    AdjustWindowRect(&rect, windowStyle, FALSE);

    RefCountPtr<IDXGIAdapter> targetAdapter;
    
    if (m_DeviceParams.adapter)
    {
        targetAdapter = m_DeviceParams.adapter;
    }
    else
    {
        targetAdapter = FindAdapter(m_DeviceParams.adapterNameSubstring);

        if (!targetAdapter)
        {
#pragma warning(push)
#pragma warning(disable: 4244) // warning C4244: 'argument': conversion from 'wchar_t' to 'const _Elem', possible loss of data
                               // There is no standard way to do the conversion safely in c++17, std::codecvt is deprecated.
            std::string adapterNameStr(m_DeviceParams.adapterNameSubstring.begin(), m_DeviceParams.adapterNameSubstring.end());
#pragma warning(pop)

            donut::log::error("Could not find an adapter matching %s\n", adapterNameStr.c_str());
            return false;
        }
    }

    {
        DXGI_ADAPTER_DESC aDesc;
        targetAdapter->GetDesc(&aDesc);

        std::wstring adapterName = aDesc.Description;
        m_RendererString = std::string(adapterName.begin(), adapterName.end());

        m_IsNvidia = IsNvDeviceID(aDesc.VendorId);
    }

    if (MoveWindowOntoAdapter(targetAdapter, rect))
    {
        glfwSetWindowPos(m_Window, rect.left, rect.top);
    }

    m_hWnd = glfwGetWin32Window(m_Window);

    RECT clientRect;
    GetClientRect(m_hWnd, &clientRect);
    UINT width = clientRect.right - clientRect.left;
    UINT height = clientRect.bottom - clientRect.top;

    ZeroMemory(&m_SwapChainDesc, sizeof(m_SwapChainDesc));
    m_SwapChainDesc.BufferCount = m_DeviceParams.swapChainBufferCount;
    m_SwapChainDesc.BufferDesc.Width = width;
    m_SwapChainDesc.BufferDesc.Height = height;
    m_SwapChainDesc.BufferDesc.RefreshRate.Numerator = m_DeviceParams.refreshRate;
    m_SwapChainDesc.BufferDesc.RefreshRate.Denominator = 0;
    m_SwapChainDesc.BufferUsage = m_DeviceParams.swapChainUsage;
    m_SwapChainDesc.OutputWindow = m_hWnd;
    m_SwapChainDesc.SampleDesc.Count = m_DeviceParams.swapChainSampleCount;
    m_SwapChainDesc.SampleDesc.Quality = m_DeviceParams.swapChainSampleQuality;
    m_SwapChainDesc.Windowed = !m_DeviceParams.startFullscreen;
    m_SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    m_SwapChainDesc.Flags = m_DeviceParams.allowModeSwitch ? DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH : 0;

    // Special processing for sRGB swap chain formats.
    // DXGI will not create a swap chain with an sRGB format, but its contents will be interpreted as sRGB.
    // So we need to use a non-sRGB format here, but store the true sRGB format for later framebuffer creation.
    switch (m_DeviceParams.swapChainFormat)  // NOLINT(clang-diagnostic-switch-enum)
    {
    case nvrhi::Format::SRGBA8_UNORM:
        m_SwapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;
    case nvrhi::Format::SBGRA8_UNORM:
        m_SwapChainDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        break;
    default:
        m_SwapChainDesc.BufferDesc.Format = nvrhi::d3d11::convertFormat(m_DeviceParams.swapChainFormat);
        break;
    }

    UINT createFlags = 0;
    if (m_DeviceParams.enableDebugRuntime)
        createFlags |= D3D11_CREATE_DEVICE_DEBUG;

    const HRESULT hr = D3D11CreateDeviceAndSwapChain(
        targetAdapter, // pAdapter
        D3D_DRIVER_TYPE_UNKNOWN, // DriverType
        nullptr, // Software
        createFlags, // Flags
        &m_DeviceParams.featureLevel, // pFeatureLevels
        1, // FeatureLevels
        D3D11_SDK_VERSION, // SDKVersion
        &m_SwapChainDesc, // pSwapChainDesc
        &m_SwapChain, // ppSwapChain
        &m_Device, // ppDevice
        nullptr, // pFeatureLevel
        &m_ImmediateContext // ppImmediateContext
    );
    
    if(FAILED(hr))
    {
        return false;
    }

    nvrhi::d3d11::DeviceDesc deviceDesc;
    deviceDesc.messageCallback = &DefaultMessageCallback::GetInstance();
    deviceDesc.context = m_ImmediateContext;

    m_NvrhiDevice = nvrhi::d3d11::createDevice(deviceDesc);

    if (m_DeviceParams.enableNvrhiValidationLayer)
    {
        m_NvrhiDevice = nvrhi::validation::createValidationLayer(m_NvrhiDevice);
    }

    bool ret = CreateRenderTarget();

    if(!ret)
    {
        return false;
    }

    return true;
}

void DeviceManager_DX11::DestroyDeviceAndSwapChain()
{
    m_RhiBackBuffer = nullptr;
    m_NvrhiDevice = nullptr;

    if (m_SwapChain)
    {
        m_SwapChain->SetFullscreenState(false, nullptr);
    }

    ReleaseRenderTarget();

    m_SwapChain = nullptr;
    m_ImmediateContext = nullptr;
    m_Device = nullptr;
}

bool DeviceManager_DX11::CreateRenderTarget()
{
    ReleaseRenderTarget();

    const HRESULT hr = m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&m_D3D11BackBuffer);  // NOLINT(clang-diagnostic-language-extension-token)
    if (FAILED(hr))
    {
        return false;
    }

    nvrhi::TextureDesc textureDesc;
    textureDesc.width = m_DeviceParams.backBufferWidth;
    textureDesc.height = m_DeviceParams.backBufferHeight;
    textureDesc.sampleCount = m_DeviceParams.swapChainSampleCount;
    textureDesc.sampleQuality = m_DeviceParams.swapChainSampleQuality;
    textureDesc.format = m_DeviceParams.swapChainFormat;
    textureDesc.debugName = "SwapChainBuffer";
    textureDesc.isRenderTarget = true;
    textureDesc.isUAV = false;

    m_RhiBackBuffer = m_NvrhiDevice->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D11_Resource, static_cast<ID3D11Resource*>(m_D3D11BackBuffer.Get()), textureDesc);

    if (FAILED(hr))
    {
        return false;
    }

    return true;
}

void DeviceManager_DX11::ReleaseRenderTarget()
{
    m_RhiBackBuffer = nullptr;
    m_D3D11BackBuffer = nullptr;
}

void DeviceManager_DX11::ResizeSwapChain()
{
    ReleaseRenderTarget();

    if (!m_SwapChain)
        return;

    const HRESULT hr = m_SwapChain->ResizeBuffers(m_DeviceParams.swapChainBufferCount,
                                            m_DeviceParams.backBufferWidth,
                                            m_DeviceParams.backBufferHeight,
                                            m_SwapChainDesc.BufferDesc.Format,
                                            m_SwapChainDesc.Flags);

    if (FAILED(hr))
    {
        donut::log::fatal("ResizeBuffers failed");
    }

    const bool ret = CreateRenderTarget();
    if (!ret)
    {
        donut::log::fatal("CreateRenderTarget failed");
    }
}

void DeviceManager_DX11::Present()
{
    m_SwapChain->Present(m_DeviceParams.vsyncEnabled ? 1 : 0, 0);
}

DeviceManager *DeviceManager::CreateD3D11()
{
    return new DeviceManager_DX11();
}
