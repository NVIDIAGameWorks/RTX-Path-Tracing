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
#include <vector>

#include <donut/app/DeviceManager.h>
#include <donut/core/log.h>

#include <Windows.h>
#include <dxgi1_5.h>
#include <dxgidebug.h>

#include <nvrhi/d3d12.h>
#include <nvrhi/validation.h>

#include <sstream>

#ifdef STREAMLINE_INTEGRATION
#include "../../../../pt_sdk/Streamline/SLWrapper.h"
#endif

using nvrhi::RefCountPtr;

using namespace donut::app;

#define HR_RETURN(hr) if(FAILED(hr)) return false;

class DeviceManager_DX12 : public DeviceManager
{
    RefCountPtr<ID3D12Device>                   m_Device12;
    RefCountPtr<ID3D12CommandQueue>             m_GraphicsQueue;
    RefCountPtr<ID3D12CommandQueue>             m_ComputeQueue;
    RefCountPtr<ID3D12CommandQueue>             m_CopyQueue;
    RefCountPtr<IDXGISwapChain3>                m_SwapChain;
    DXGI_SWAP_CHAIN_DESC1                       m_SwapChainDesc{};
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC             m_FullScreenDesc{};
    RefCountPtr<IDXGIAdapter>                   m_DxgiAdapter;
    HWND                                        m_hWnd = nullptr;
    bool                                        m_TearingSupported = false;

    std::vector<RefCountPtr<ID3D12Resource>>    m_SwapChainBuffers;
    std::vector<nvrhi::TextureHandle>           m_RhiSwapChainBuffers;
    RefCountPtr<ID3D12Fence>                    m_FrameFence;
    std::vector<HANDLE>                         m_FrameFenceEvents;

    UINT64                                      m_FrameCount = 1;

    nvrhi::DeviceHandle                         m_NvrhiDevice;

    std::string                                 m_RendererString;

public:
    const char *GetRendererString() const override
    {
        return m_RendererString.c_str();
    }

    nvrhi::IDevice *GetDevice() const override
    {
        return m_NvrhiDevice;
    }

    void ReportLiveObjects() override;

    nvrhi::GraphicsAPI GetGraphicsAPI() const override
    {
        return nvrhi::GraphicsAPI::D3D12;
    }

protected:
    bool CreateDeviceAndSwapChain() override;
    void DestroyDeviceAndSwapChain() override;
    void ResizeSwapChain() override;
    nvrhi::ITexture* GetCurrentBackBuffer() override;
    nvrhi::ITexture* GetBackBuffer(uint32_t index) override;
    uint32_t GetCurrentBackBufferIndex() override;
    uint32_t GetBackBufferCount() override;
    void BeginFrame() override;
    void Present() override;

private:
    bool CreateRenderTargets();
    void ReleaseRenderTargets();
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

void DeviceManager_DX12::ReportLiveObjects()
{
    nvrhi::RefCountPtr<IDXGIDebug> pDebug;
    DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug));

    if (pDebug)
        pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_IGNORE_INTERNAL);
}

bool DeviceManager_DX12::CreateDeviceAndSwapChain()
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
            std::wstring adapterNameStr(m_DeviceParams.adapterNameSubstring.begin(), m_DeviceParams.adapterNameSubstring.end());

            donut::log::error("Could not find an adapter matching %s\n", adapterNameStr.c_str());
            return false;
        }
    }

    {
        DXGI_ADAPTER_DESC aDesc;
        targetAdapter->GetDesc(&aDesc);

        std::wstring adapterName = aDesc.Description;

        // A stupid but non-deprecated and portable way of converting a wstring to a string
        std::stringstream ss;
        std::wstringstream wss;
        for (auto c : adapterName)
            ss << wss.narrow(c, '?');
        m_RendererString = ss.str();

        m_IsNvidia = IsNvDeviceID(aDesc.VendorId);
    }

    if (MoveWindowOntoAdapter(targetAdapter, rect))
    {
        glfwSetWindowPos(m_Window, rect.left, rect.top);
    }

    m_hWnd = glfwGetWin32Window(m_Window);

    HRESULT hr = E_FAIL;

    RECT clientRect;
    GetClientRect(m_hWnd, &clientRect);
    UINT width = clientRect.right - clientRect.left;
    UINT height = clientRect.bottom - clientRect.top;

    ZeroMemory(&m_SwapChainDesc, sizeof(m_SwapChainDesc));
    m_SwapChainDesc.Width = width;
    m_SwapChainDesc.Height = height;
    m_SwapChainDesc.SampleDesc.Count = m_DeviceParams.swapChainSampleCount;
    m_SwapChainDesc.SampleDesc.Quality = 0;
    m_SwapChainDesc.BufferUsage = m_DeviceParams.swapChainUsage;
    m_SwapChainDesc.BufferCount = m_DeviceParams.swapChainBufferCount;
    m_SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    m_SwapChainDesc.Flags = m_DeviceParams.allowModeSwitch ? DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH : 0;

    // Special processing for sRGB swap chain formats.
    // DXGI will not create a swap chain with an sRGB format, but its contents will be interpreted as sRGB.
    // So we need to use a non-sRGB format here, but store the true sRGB format for later framebuffer creation.
    switch (m_DeviceParams.swapChainFormat)  // NOLINT(clang-diagnostic-switch-enum)
    {
    case nvrhi::Format::SRGBA8_UNORM:
        m_SwapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;
    case nvrhi::Format::SBGRA8_UNORM:
        m_SwapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        break;
    default:
        m_SwapChainDesc.Format = nvrhi::d3d12::convertFormat(m_DeviceParams.swapChainFormat);
        break;
    }

    if (m_DeviceParams.enableDebugRuntime)
    {
        RefCountPtr<ID3D12Debug> pDebug;
        hr = D3D12GetDebugInterface(IID_PPV_ARGS(&pDebug));
        HR_RETURN(hr)

        pDebug->EnableDebugLayer();
    }

    RefCountPtr<IDXGIFactory2> pDxgiFactory;
    UINT dxgiFactoryFlags = m_DeviceParams.enableDebugRuntime ? DXGI_CREATE_FACTORY_DEBUG : 0;
    hr = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&pDxgiFactory));
    HR_RETURN(hr)

    RefCountPtr<IDXGIFactory5> pDxgiFactory5;
    if (SUCCEEDED(pDxgiFactory->QueryInterface(IID_PPV_ARGS(&pDxgiFactory5))))
    {
        BOOL supported = 0;
        if (SUCCEEDED(pDxgiFactory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &supported, sizeof(supported))))
            m_TearingSupported = (supported != 0);
    }

    if (m_TearingSupported)
    {
        m_SwapChainDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }

    hr = D3D12CreateDevice(
        targetAdapter,
        m_DeviceParams.featureLevel,
        IID_PPV_ARGS(&m_Device12));
    HR_RETURN(hr)

#ifdef STREAMLINE_INTEGRATION
    void* nativeDeviceHandle = NULL;
    SLWrapper::Get().ProxyToNative(m_Device12, (void**)&nativeDeviceHandle);
    SLWrapper::Get().SetDevice_raw(nativeDeviceHandle);
#endif

    if (m_DeviceParams.enableDebugRuntime)
    {
        RefCountPtr<ID3D12InfoQueue> pInfoQueue;
        m_Device12->QueryInterface(&pInfoQueue);

        if (pInfoQueue)
        {
#ifdef _DEBUG
            pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
            pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
            pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
#endif

            std::vector< D3D12_MESSAGE_ID> disableMessageIDs = {
                D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
                D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
                D3D12_MESSAGE_ID_COMMAND_LIST_STATIC_DESCRIPTOR_RESOURCE_DIMENSION_MISMATCH, // descriptor validation doesn't understand acceleration structures
                
            };

            // D3D12 WARNING: ID3D12Device::CreateCommittedResource: Ignoring InitialState X. Buffers are effectively created in state D3D12_RESOURCE_STATE_COMMON.
            {   
                // Sorry: this is just some convoluted logic to account for d3d12.h headers where D3D12_MESSAGE_ID_CREATERESOURCE_STATE_IGNORED is not defined, 
                // the debug layer not triggering this warning in those cases anyway.
                static constexpr D3D12_MESSAGE_ID D3D12_MESSAGE_ID_CREATERESOURCE_STATE_IGNORED_VALUE = (D3D12_MESSAGE_ID)1328;
                if constexpr (D3D12_MESSAGE_ID_CREATERESOURCE_STATE_IGNORED_VALUE < D3D12_MESSAGE_ID_D3D12_MESSAGES_END)
                {
                    disableMessageIDs.push_back(D3D12_MESSAGE_ID_CREATERESOURCE_STATE_IGNORED_VALUE);
                }
            }
            
            D3D12_INFO_QUEUE_FILTER filter = {};
            filter.DenyList.pIDList = disableMessageIDs.data();
            filter.DenyList.NumIDs = (uint32_t)disableMessageIDs.size();
            pInfoQueue->AddStorageFilterEntries(&filter);
        }
    }

    m_DxgiAdapter = targetAdapter;

    D3D12_COMMAND_QUEUE_DESC queueDesc;
    ZeroMemory(&queueDesc, sizeof(queueDesc));
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.NodeMask = 1;
    hr = m_Device12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_GraphicsQueue));
    HR_RETURN(hr)
    m_GraphicsQueue->SetName(L"Graphics Queue");

    if (m_DeviceParams.enableComputeQueue)
    {
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        hr = m_Device12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_ComputeQueue));
        HR_RETURN(hr)
        m_ComputeQueue->SetName(L"Compute Queue");
    }

    if (m_DeviceParams.enableCopyQueue)
    {
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
        hr = m_Device12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_CopyQueue));
        HR_RETURN(hr)
        m_CopyQueue->SetName(L"Copy Queue");
    }

    m_FullScreenDesc = {};
    m_FullScreenDesc.RefreshRate.Numerator = m_DeviceParams.refreshRate;
    m_FullScreenDesc.RefreshRate.Denominator = 1;
    m_FullScreenDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
    m_FullScreenDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    m_FullScreenDesc.Windowed = !m_DeviceParams.startFullscreen;
    
    RefCountPtr<IDXGISwapChain1> pSwapChain1;
    hr = pDxgiFactory->CreateSwapChainForHwnd(m_GraphicsQueue, m_hWnd, &m_SwapChainDesc, &m_FullScreenDesc, nullptr, &pSwapChain1);
    HR_RETURN(hr)

	hr = pSwapChain1->QueryInterface(IID_PPV_ARGS(&m_SwapChain));
	HR_RETURN(hr)

    nvrhi::d3d12::DeviceDesc deviceDesc;
    deviceDesc.errorCB = &DefaultMessageCallback::GetInstance();
    deviceDesc.pDevice = m_Device12;
    deviceDesc.pGraphicsCommandQueue = m_GraphicsQueue;
    deviceDesc.pComputeCommandQueue = m_ComputeQueue;
    deviceDesc.pCopyCommandQueue = m_CopyQueue;

    m_NvrhiDevice = nvrhi::d3d12::createDevice(deviceDesc);
    
    if (m_DeviceParams.enableNvrhiValidationLayer)
    {
        m_NvrhiDevice = nvrhi::validation::createValidationLayer(m_NvrhiDevice);
    }

    if (!CreateRenderTargets())
        return false;

    hr = m_Device12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_FrameFence));
    HR_RETURN(hr)

    for(UINT bufferIndex = 0; bufferIndex < m_SwapChainDesc.BufferCount; bufferIndex++)
    {
        m_FrameFenceEvents.push_back( CreateEvent(nullptr, false, true, NULL) );
    }
    
    //m_Device12->SetStablePowerState( TRUE );

    return true;
}

void DeviceManager_DX12::DestroyDeviceAndSwapChain()
{
    m_RhiSwapChainBuffers.clear();
    m_RendererString.clear();

    ReleaseRenderTargets();

    m_NvrhiDevice = nullptr;

    for (auto fenceEvent : m_FrameFenceEvents)
    {
        WaitForSingleObject(fenceEvent, INFINITE);
        CloseHandle(fenceEvent);
    }

    m_FrameFenceEvents.clear();

    if (m_SwapChain)
    {
        m_SwapChain->SetFullscreenState(false, nullptr);
    }

    m_SwapChainBuffers.clear();

    m_FrameFence = nullptr;
    m_SwapChain = nullptr;
    m_GraphicsQueue = nullptr;
    m_ComputeQueue = nullptr;
    m_CopyQueue = nullptr;
    m_Device12 = nullptr;
    m_DxgiAdapter = nullptr;
}

bool DeviceManager_DX12::CreateRenderTargets()
{
    m_SwapChainBuffers.resize(m_SwapChainDesc.BufferCount);
    m_RhiSwapChainBuffers.resize(m_SwapChainDesc.BufferCount);

    for(UINT n = 0; n < m_SwapChainDesc.BufferCount; n++)
    {
        const HRESULT hr = m_SwapChain->GetBuffer(n, IID_PPV_ARGS(&m_SwapChainBuffers[n]));
        HR_RETURN(hr)

        nvrhi::TextureDesc textureDesc;
        textureDesc.width = m_DeviceParams.backBufferWidth;
        textureDesc.height = m_DeviceParams.backBufferHeight;
        textureDesc.sampleCount = m_DeviceParams.swapChainSampleCount;
        textureDesc.sampleQuality = m_DeviceParams.swapChainSampleQuality;
        textureDesc.format = m_DeviceParams.swapChainFormat;
        textureDesc.debugName = "SwapChainBuffer";
        textureDesc.isRenderTarget = true;
        textureDesc.isUAV = false;
        textureDesc.initialState = nvrhi::ResourceStates::Present;
        textureDesc.keepInitialState = true;

        m_RhiSwapChainBuffers[n] = m_NvrhiDevice->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object(m_SwapChainBuffers[n]), textureDesc);
    }

    return true;
}

void DeviceManager_DX12::ReleaseRenderTargets()
{
    // Make sure that all frames have finished rendering
    m_NvrhiDevice->waitForIdle();

    // Release all in-flight references to the render targets
    m_NvrhiDevice->runGarbageCollection();

    // Set the events so that WaitForSingleObject in OneFrame will not hang later
    for(auto e : m_FrameFenceEvents)
        SetEvent(e);

    // Release the old buffers because ResizeBuffers requires that
    m_RhiSwapChainBuffers.clear();
    m_SwapChainBuffers.clear();
}

void DeviceManager_DX12::ResizeSwapChain()
{
    ReleaseRenderTargets();

    if (!m_NvrhiDevice)
        return;

    if (!m_SwapChain)
        return;

    const HRESULT hr = m_SwapChain->ResizeBuffers(m_DeviceParams.swapChainBufferCount,
                                            m_DeviceParams.backBufferWidth,
                                            m_DeviceParams.backBufferHeight,
                                            m_SwapChainDesc.Format,
                                            m_SwapChainDesc.Flags);

    if (FAILED(hr))
    {
        donut::log::fatal("ResizeBuffers failed");
    }

    bool ret = CreateRenderTargets();
    if (!ret)
    {
        donut::log::fatal("CreateRenderTarget failed");
    }
}

void DeviceManager_DX12::BeginFrame()
{
    DXGI_SWAP_CHAIN_DESC1 newSwapChainDesc;
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC newFullScreenDesc;
    if (SUCCEEDED(m_SwapChain->GetDesc1(&newSwapChainDesc)) && SUCCEEDED(m_SwapChain->GetFullscreenDesc(&newFullScreenDesc)))
    {
        if (m_FullScreenDesc.Windowed != newFullScreenDesc.Windowed)
        {
            BackBufferResizing();
            
            m_FullScreenDesc = newFullScreenDesc;
            m_SwapChainDesc = newSwapChainDesc;
            m_DeviceParams.backBufferWidth = newSwapChainDesc.Width;
            m_DeviceParams.backBufferHeight = newSwapChainDesc.Height;

            if(newFullScreenDesc.Windowed)
                glfwSetWindowMonitor(m_Window, nullptr, 50, 50, newSwapChainDesc.Width, newSwapChainDesc.Height, 0);

            ResizeSwapChain();
            BackBufferResized();
        }

    }

    auto bufferIndex = m_SwapChain->GetCurrentBackBufferIndex();

    WaitForSingleObject(m_FrameFenceEvents[bufferIndex], INFINITE);
}

nvrhi::ITexture* DeviceManager_DX12::GetCurrentBackBuffer()
{
    return m_RhiSwapChainBuffers[m_SwapChain->GetCurrentBackBufferIndex()];
}

nvrhi::ITexture* DeviceManager_DX12::GetBackBuffer(uint32_t index)
{
    if (index < m_RhiSwapChainBuffers.size())
        return m_RhiSwapChainBuffers[index];
    return nullptr;
}

uint32_t DeviceManager_DX12::GetCurrentBackBufferIndex()
{
    return m_SwapChain->GetCurrentBackBufferIndex();
}

uint32_t DeviceManager_DX12::GetBackBufferCount()
{
    return m_SwapChainDesc.BufferCount;
}

void DeviceManager_DX12::Present()
{
    if (!m_windowVisible)
        return;

    auto bufferIndex = m_SwapChain->GetCurrentBackBufferIndex();

    UINT presentFlags = 0;
    if (!m_DeviceParams.vsyncEnabled && m_FullScreenDesc.Windowed && m_TearingSupported)
        presentFlags |= DXGI_PRESENT_ALLOW_TEARING;

    m_SwapChain->Present(m_DeviceParams.vsyncEnabled ? 1 : 0, presentFlags);

    m_FrameFence->SetEventOnCompletion(m_FrameCount, m_FrameFenceEvents[bufferIndex]);
    m_GraphicsQueue->Signal(m_FrameFence, m_FrameCount);
    m_FrameCount++;
}

DeviceManager *DeviceManager::CreateD3D12(void)
{
    return new DeviceManager_DX12();
}
