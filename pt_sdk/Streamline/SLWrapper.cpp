//----------------------------------------------------------------------------------
// File:        SLWrapper.cpp
// SDK Version: 2.0
// Email:       StreamlineSupport@nvidia.com
// Site:        http://developer.nvidia.com/
//
// Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//----------------------------------------------------------------------------------

#ifdef STREAMLINE_INTEGRATION

#include "SLWrapper.h" 

#include <donut/core/log.h>
#include <filesystem>

#if USE_DX11
#include <d3d11.h>
#include <nvrhi/d3d11.h>
#endif
#if USE_DX12
#include <d3d12.h>
#include <nvrhi/d3d12.h>
#endif
#if USE_VK
#include <vulkan/vulkan.h>
#include <nvrhi/vulkan.h>
#include <../src/vulkan/vulkan-backend.h>
#endif

#include "secureLoadLibrary.h"

using namespace donut;
using namespace donut::math;
using namespace donut::engine;

bool SLWrapper::m_sl_initialised = false;
bool SLWrapper::m_reflex_available = false;
bool SLWrapper::m_reflex_driverFlashIndicatorEnable = false;
nvrhi::GraphicsAPI SLWrapper::m_api = nvrhi::GraphicsAPI::D3D12;

// SL Interposer Functions
PFunSlInit* SLWrapper::slInit{};
PFunSlShutdown* SLWrapper::slShutdown{};
PFunSlSetFeatureEnabled* SLWrapper::slSetFeatureEnabled{};
PFunSlIsFeatureSupported* SLWrapper::slIsFeatureSupported{};
PFunSlSetTag* SLWrapper::slSetTag{};
PFunSlSetConstants* SLWrapper::slSetConstants{};
PFunSlSetFeatureConstants* SLWrapper::slSetFeatureConstants{};
PFunSlGetFeatureSettings* SLWrapper::slGetFeatureSettings{};
PFunSlEvaluateFeature* SLWrapper::slEvaluateFeature{};
PFunSlAllocateResources* SLWrapper::slAllocateResources{};
PFunSlFreeResources* SLWrapper::slFreeResources{};

SLWrapper::SLWrapper(nvrhi::IDevice* device)
{
    m_Device = device;

    if (!m_sl_initialised) log::error("Must initialise SL before creating the wrapper.");

    m_dlss_available = slIsFeatureSupported(sl::Feature::eFeatureDLSS, nullptr);
    if (m_dlss_available) log::info("DLSS is supported on this system.");
    else log::warning("DLSS is not supported on this system.");

    m_reflex_available = slIsFeatureSupported(sl::Feature::eFeatureReflex, nullptr);
    if (m_reflex_available) log::info("Reflex is supported on this system.");
    else log::warning("Reflex is not supported on this system.");

    m_dlssg_available = slIsFeatureSupported(sl::Feature::eFeatureDLSS_G, nullptr);
    if (m_dlssg_available) log::info("DLSS-G is supported on this system.");
    else log::warning("DLSS-G is not supported on this system.");
}

void SLWrapper::logFunctionCallback(sl::LogType type, const char* msg) {

    if (type == sl::LogType::eLogTypeError) {
        // Add a breakpoint here to break on errors
        donut::log::error(msg);
    }
    if (type == sl::LogType::eLogTypeWarn) {
        // Add a breakpoint here to break on warnings
        donut::log::warning(msg);
    }
    else {
        donut::log::info(msg);
    }
}

#ifndef _WIN32
#include <unistd.h>
#include <cstdio>
#include <climits>
#else
#define PATH_MAX MAX_PATH
#endif // _WIN32

std::wstring GetSlInterposerDllLocation() {

    char path[PATH_MAX] = { 0 };
#ifdef _WIN32
    if (GetModuleFileNameA(nullptr, path, dim(path)) == 0)
        return std::wstring();
#else // _WIN32
    // /proc/self/exe is mostly linux-only, but can't hurt to try it elsewhere
    if (readlink("/proc/self/exe", path, std::size(path)) <= 0)
    {
        // portable but assumes executable dir == cwd
        if (!getcwd(path, std::size(path)))
            return ""; // failure
    }
#endif // _WIN32

    auto basePath = std::filesystem::path(path).parent_path();
    auto dllPath = basePath.wstring().append(L"\\sl.interposer.dll");
    return dllPath;
}

bool SLWrapper::Initialize(nvrhi::GraphicsAPI api, const bool& checkSig)
{

    if (m_sl_initialised) {
        log::info("SLWrapper is already initialised.");
        return true;
    }

    sl::Preferences pref;

    m_api = api;

    pref.allocateCallback = &allocateResourceCallback;
    pref.releaseCallback = &releaseResourceCallback;

#if _DEBUG
    pref.showConsole = true;
    pref.logMessageCallback = &logFunctionCallback;
    pref.logLevel = sl::LogLevel::eLogLevelDefault;
    // pref.pathToLogsAndData = baseDir.c_str();
#else
    pref.logLevel = sl::LogLevel::eLogLevelOff;
#endif

    sl::Feature featuresToLoad[] = { sl::Feature::eFeatureDLSS,
        sl::Feature::eFeatureDLSS_G,
        sl::Feature::eFeatureReflex,
    };
    pref.featuresToLoad = featuresToLoad;
    pref.numFeaturesToLoad = _countof(featuresToLoad);

    // Enable CL state tracking.
    // pref.flags = (sl::PreferenceFlags) 0;

    auto pathDll = GetSlInterposerDllLocation();
    const wchar_t* rawpathDll = pathDll.c_str();
    HMODULE interposer = sl::security::loadLibrary(rawpathDll, checkSig);
    //HMODULE interposer = LoadLibraryW(rawpathDll);
    if (!interposer)
    {
        donut::log::error("Unable to load Streamline Interposer");
        return false;
    }

    // Hook up all of the functions exported by the SL Interposer Library
    slInit = (PFunSlInit*)GetProcAddress(interposer, "slInit");
    slShutdown = (PFunSlShutdown*)GetProcAddress(interposer, "slShutdown");
    slSetFeatureEnabled = (PFunSlSetFeatureEnabled*)GetProcAddress(interposer, "slSetFeatureEnabled");
    slIsFeatureSupported = (PFunSlIsFeatureSupported*)GetProcAddress(interposer, "slIsFeatureSupported");
    slSetTag = (PFunSlSetTag*)GetProcAddress(interposer, "slSetTag");
    slSetConstants = (PFunSlSetConstants*)GetProcAddress(interposer, "slSetConstants");
    slSetFeatureConstants = (PFunSlSetFeatureConstants*)GetProcAddress(interposer, "slSetFeatureConstants");
    slGetFeatureSettings = (PFunSlGetFeatureSettings*)GetProcAddress(interposer, "slGetFeatureSettings");
    slEvaluateFeature = (PFunSlEvaluateFeature*)GetProcAddress(interposer, "slEvaluateFeature");
    slAllocateResources = (PFunSlAllocateResources*)GetProcAddress(interposer, "slAllocateResources");
    slFreeResources = (PFunSlFreeResources*)GetProcAddress(interposer, "slFreeResources");

    m_sl_initialised = slInit(pref, APP_ID);
    if (!m_sl_initialised) {
        log::error("Failed to initialse SL.");
        return false;
    }

    return true;

}

void SLWrapper::Shutdown()
{
    if (m_sl_initialised) {
        if (!slShutdown()) log::error("Failed to shutdown SL properly.");
        m_sl_initialised = false;
    }
}

void SLWrapper::SetSLConsts(const sl::Constants& consts, const int frameNumber) {
    if (!m_sl_initialised) {
        log::warning("SL not initialised.");
        return;
    }
    if (!slSetConstants(consts, frameNumber, m_viewID)) log::warning("Failed to set SL constants.");
}

void SLWrapper::SetDLSSConsts(const sl::DLSSConstants consts, const int frameNumber)
{
    if (!m_sl_initialised || !m_dlss_available) {
        log::warning("SL not initialised or DLSS not available.");
        return;
    }

    m_dlss_consts = consts;
    if (!slSetFeatureConstants(sl::Feature::eFeatureDLSS, &consts, frameNumber, m_viewID)) log::warning("Failed to set DLSS constants.");

}

void SLWrapper::QueryDLSSOptimalSettings(DLSSSettings& settings) {
    if (!m_sl_initialised || !m_dlss_available) {
        log::warning("SL not initialised or DLSS not available.");
        settings = DLSSSettings{};
        return;
    }

    sl::DLSSSettings dlssSettings = {};
    sl::DLSSSettings1 dlssSettings1 = {};
    dlssSettings.ext = &dlssSettings1;
    if (!slGetFeatureSettings(sl::Feature::eFeatureDLSS, &m_dlss_consts, &dlssSettings)) log::error("Failed to get DLSS optimal settings.");

    settings.optimalRenderSize.x = static_cast<int>(dlssSettings.optimalRenderWidth);
    settings.optimalRenderSize.y = static_cast<int>(dlssSettings.optimalRenderHeight);
    settings.sharpness = dlssSettings.optimalSharpness;

    settings.minRenderSize.x = dlssSettings1.renderWidthMin;
    settings.minRenderSize.y = dlssSettings1.renderHeightMin;
    settings.maxRenderSize.x = dlssSettings1.renderWidthMax;
    settings.maxRenderSize.y = dlssSettings1.renderHeightMax;
}

void SLWrapper::SetDLSSGConsts(const sl::DLSSGConstants consts, const int frameNumber) {
    if (!m_sl_initialised || !m_dlssg_available) {
        log::warning("SL not initialised or DLSSG not available.");
        return;
    }

    m_dlssg_consts = consts;
    if (!slSetFeatureConstants(sl::Feature::eFeatureDLSS_G, &consts, frameNumber, m_viewID)) log::warning("Failed to set DLSS-G constants.");
}

void SLWrapper::QueryDLSSGSettings(uint64_t& estimatedVRamUsage, float& fps) {
    if (!m_sl_initialised || !m_dlssg_available) {
        log::warning("SL not initialised or DLSSG not available.");
        return;
    }

    if (!slGetFeatureSettings(sl::Feature::eFeatureDLSS_G, &m_dlssg_consts, &m_dlssg_settings)) log::error("Failed to query DLSS-G settings.");

    estimatedVRamUsage = m_dlssg_settings.estimatedVRAMUsageInBytes;
    fps = 1000.f / m_dlssg_settings.actualFrameTimeMs;
}


sl::Resource SLWrapper::allocateResourceCallback(const sl::ResourceDesc* resDesc, void* device) {

    sl::Resource res = {};

    if (device == nullptr) {
        log::warning("No device available for allocation.");
        return res;
    }

    bool isBuffer = (resDesc->type == sl::ResourceType::eResourceTypeBuffer);

    if (isBuffer) {

#ifdef USE_DX11

        if (m_api == nvrhi::GraphicsAPI::D3D11)
        {
            D3D11_BUFFER_DESC* desc = (D3D11_BUFFER_DESC*)resDesc->desc;
            ID3D11Device* pd3d11Device = (ID3D11Device*)device;
            ID3D11Buffer* pbuffer;
            bool success = SUCCEEDED(pd3d11Device->CreateBuffer(desc, nullptr, &pbuffer));
            if (!success) log::error("Failed to create buffer in SL allocation callback");
            res.type = resDesc->type;
            res.native = pbuffer;

        }
#endif

#ifdef USE_DX12
        if (m_api == nvrhi::GraphicsAPI::D3D12)
        {
            D3D12_RESOURCE_DESC* desc = (D3D12_RESOURCE_DESC*)resDesc->desc;
            D3D12_HEAP_PROPERTIES* heap = (D3D12_HEAP_PROPERTIES*)resDesc->heap;
            D3D12_RESOURCE_STATES state = (D3D12_RESOURCE_STATES)resDesc->state;
            ID3D12Device* pd3d12Device = (ID3D12Device*)device;
            ID3D12Resource* pbuffer;
            bool success = SUCCEEDED(pd3d12Device->CreateCommittedResource(heap, D3D12_HEAP_FLAG_NONE, desc, state, nullptr, IID_PPV_ARGS(&pbuffer)));
            if (!success) log::error("Failed to create buffer in SL allocation callback");
            res.type = resDesc->type;
            res.native = pbuffer;
        }
#endif

    }

    else {

#ifdef USE_DX11

        if (m_api == nvrhi::GraphicsAPI::D3D11)
        {
            D3D11_TEXTURE2D_DESC* desc = (D3D11_TEXTURE2D_DESC*)resDesc->desc;
            ID3D11Device* pd3d11Device = (ID3D11Device*)device;
            ID3D11Texture2D* ptexture;
            bool success = SUCCEEDED(pd3d11Device->CreateTexture2D(desc, nullptr, &ptexture));
            if (!success) log::error("Failed to create texture in SL allocation callback");
            res.type = resDesc->type;
            res.native = ptexture;

        }
#endif

#ifdef USE_DX12
        if (m_api == nvrhi::GraphicsAPI::D3D12)
        {
            D3D12_RESOURCE_DESC* desc = (D3D12_RESOURCE_DESC*)resDesc->desc;
            D3D12_RESOURCE_STATES state = (D3D12_RESOURCE_STATES)resDesc->state;
            D3D12_HEAP_PROPERTIES* heap = (D3D12_HEAP_PROPERTIES*)resDesc->heap;
            ID3D12Device* pd3d12Device = (ID3D12Device*)device;
            ID3D12Resource* ptexture;
            bool success = SUCCEEDED(pd3d12Device->CreateCommittedResource(heap, D3D12_HEAP_FLAG_NONE, desc, state, nullptr, IID_PPV_ARGS(&ptexture)));
            if (!success) log::error("Failed to create texture in SL allocation callback");
            res.type = resDesc->type;
            res.native = ptexture;
        }
#endif

    }
    return res;

}

void SLWrapper::releaseResourceCallback(sl::Resource* resource, void* device)
{
    if (resource)
    {
        auto i = (IUnknown*)resource->native;
        i->Release();
    }
};

void SLWrapper::TagResources(
    nvrhi::ICommandList* commandList,
    const int frameNumber,
    const donut::engine::IView* view,
    nvrhi::ITexture* DLSSOutput,
    nvrhi::ITexture* DLSSInput,
    nvrhi::ITexture* motionVectors,
    nvrhi::ITexture* depth,
    nvrhi::ITexture* finalColorHudless,
    donut::math::int2 renderSize)
{
    if (!m_sl_initialised) {
        log::warning("Streamline not initialised.");
        return;
    }
    if (m_Device == nullptr) {
        log::error("No device available.");
        return;
    }

    bool success = true;
    sl::Extent renderExtent{ 0, 0, (uint32_t)renderSize.x, (uint32_t)renderSize.y };
    sl::Extent fullExtent{ 0, 0, DLSSOutput->getDesc().width, DLSSOutput->getDesc().height };

#if USE_DX11
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D11)
    {
        sl::Resource unresolvedColorResource = sl::Resource{ sl::ResourceType::eResourceTypeTex2d, DLSSOutput->getNativeObject(nvrhi::ObjectTypes::D3D11_Resource) };
        sl::Resource motionVectorsResource = sl::Resource{ sl::ResourceType::eResourceTypeTex2d, motionVectors->getNativeObject(nvrhi::ObjectTypes::D3D11_Resource) };
        sl::Resource resolvedColorResource = sl::Resource{ sl::ResourceType::eResourceTypeTex2d, DLSSInput->getNativeObject(nvrhi::ObjectTypes::D3D11_Resource) };
        sl::Resource depthResource = sl::Resource{ sl::ResourceType::eResourceTypeTex2d, depth->getNativeObject(nvrhi::ObjectTypes::D3D11_Resource) };
        sl::Resource finalColorHudlessResource = sl::Resource{ sl::ResourceType::eResourceTypeTex2d, finalColorHudless->getNativeObject(nvrhi::ObjectTypes::D3D11_Resource) };

        success = success && slSetTag(&resolvedColorResource, sl::eBufferTypeScalingInputColor, 0, &renderExtent);
        success = success && slSetTag(&unresolvedColorResource, sl::eBufferTypeScalingOutputColor, 0, &fullExtent);
        success = success && slSetTag(&motionVectorsResource, sl::eBufferTypeMVec, 0, &renderExtent);
        success = success && slSetTag(&depthResource, sl::eBufferTypeDepth, 0, &renderExtent);
        success = success && slSetTag(&finalColorHudlessResource, sl::eBufferTypeHUDLessColor, 0, &fullExtent);
    }
#endif

#ifdef USE_DX12
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
    {
        sl::Resource unresolvedColorResource = sl::Resource{ sl::ResourceType::eResourceTypeTex2d, DLSSOutput->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource), nullptr, nullptr, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr };
        sl::Resource motionVectorsResource = sl::Resource{ sl::ResourceType::eResourceTypeTex2d, motionVectors->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource), nullptr, nullptr, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr };
        sl::Resource resolvedColorResource = sl::Resource{ sl::ResourceType::eResourceTypeTex2d, DLSSInput->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource), nullptr, nullptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr };
        sl::Resource depthResource = sl::Resource{ sl::ResourceType::eResourceTypeTex2d, depth->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource), nullptr, nullptr, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr };
        sl::Resource finalColorHudlessResource = sl::Resource{ sl::ResourceType::eResourceTypeTex2d, finalColorHudless->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource), nullptr, nullptr, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr };

        success = success && slSetTag(&resolvedColorResource, sl::eBufferTypeScalingInputColor, m_viewID, &renderExtent);
        success = success && slSetTag(&unresolvedColorResource, sl::eBufferTypeScalingOutputColor, m_viewID, &fullExtent);
        success = success && slSetTag(&motionVectorsResource, sl::eBufferTypeMVec, m_viewID, &renderExtent);
        success = success && slSetTag(&depthResource, sl::eBufferTypeDepth, m_viewID, &renderExtent);
        success = success && slSetTag(&finalColorHudlessResource, sl::eBufferTypeHUDLessColor, m_viewID, &fullExtent);
    }
#endif

#ifdef USE_VK
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN)
    {
        nvrhi::TextureSubresourceSet subresources = view->GetSubresources();

        sl::Resource unresolvedColorResource = sl::Resource{ sl::ResourceType::eResourceTypeTex2d, DLSSOutput->getNativeObject(nvrhi::ObjectTypes::VK_Image),         nullptr,        DLSSOutput->getNativeView(nvrhi::ObjectTypes::VK_ImageView, nvrhi::Format::UNKNOWN,        subresources) };
        sl::Resource motionVectorsResource = sl::Resource{ sl::ResourceType::eResourceTypeTex2d, motionVectors->getNativeObject(nvrhi::ObjectTypes::VK_Image),      nullptr,        motionVectors->getNativeView(nvrhi::ObjectTypes::VK_ImageView, nvrhi::Format::UNKNOWN,     subresources) };
        sl::Resource resolvedColorResource = sl::Resource{ sl::ResourceType::eResourceTypeTex2d, DLSSInput->getNativeObject(nvrhi::ObjectTypes::VK_Image),          nullptr,        DLSSInput->getNativeView(nvrhi::ObjectTypes::VK_ImageView, nvrhi::Format::UNKNOWN,         subresources) };
        sl::Resource depthResource = sl::Resource{ sl::ResourceType::eResourceTypeTex2d, depth->getNativeObject(nvrhi::ObjectTypes::VK_Image),              nullptr,        depth->getNativeView(nvrhi::ObjectTypes::VK_ImageView, nvrhi::Format::UNKNOWN,             subresources) };
        sl::Resource finalColorHudlessResource = sl::Resource{ sl::ResourceType::eResourceTypeTex2d, finalColorHudless->getNativeObject(nvrhi::ObjectTypes::VK_Image), nullptr,        finalColorHudless->getNativeView(nvrhi::ObjectTypes::VK_ImageView, nvrhi::Format::UNKNOWN, subresources) };

        success = success && slSetTag(&resolvedColorResource, sl::eBufferTypeScalingInputColor, m_viewID, &renderExtent);
        success = success && slSetTag(&unresolvedColorResource, sl::eBufferTypeScalingOutputColor, m_viewID, &fullExtent);
        success = success && slSetTag(&motionVectorsResource, sl::eBufferTypeMVec, m_viewID, &renderExtent);
        success = success && slSetTag(&depthResource, sl::eBufferTypeDepth, m_viewID, &renderExtent);
        success = success && slSetTag(&finalColorHudlessResource, sl::eBufferTypeHUDLessColor, 0, &fullExtent);
    }
#endif

    if (!success) {
        log::warning("Failed Streamline tag setting");
        return;
    }
}

void SLWrapper::EvaluateDLSS(nvrhi::ICommandList* commandList, const int frameNumber) {

    void* context = nullptr;

#if USE_DX11
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D11)
    {
        context = m_Device->getNativeObject(nvrhi::ObjectTypes::D3D11_DeviceContext);
    }
#endif

#ifdef USE_DX12
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
    {
        context = commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
    }
#endif

#ifdef USE_VK
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN)
    {
        context = commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer);
    }
#endif

    if (context == nullptr) {
        log::warning("Failed to retrieve context");
        return;
    }

    if (!slEvaluateFeature(context, sl::Feature::eFeatureDLSS, frameNumber, m_viewID)) log::warning("Failed DLSS evaluation");

    return;
}

void SLWrapper::SetReflexConsts(const sl::ReflexConstants consts, int frameNumber)
{
    if (!m_sl_initialised || !m_reflex_available)
    {
        log::warning("SL not initialised or Reflex not available.");
        return;
    }

    m_reflex_consts = consts;
    if (!slSetFeatureConstants(sl::Feature::eFeatureReflex, &m_reflex_consts, frameNumber, m_viewID)) log::warning("Failed to set Reflex constants.");

    return;
}

void SLWrapper::ReflexCallback_Sleep_Input_SimStart(donut::app::DeviceManager& manager) {

    if (SLWrapper::GetReflexAvailable()) {
        slEvaluateFeature(nullptr, sl::Feature::eFeatureReflex, 0, sl::ReflexMarker::eReflexMarkerSleep);

        int frameNumber = manager.GetFrameIndex();
        slEvaluateFeature(nullptr, sl::Feature::eFeatureReflex, frameNumber, sl::ReflexMarker::eReflexMarkerInputSample);
        slEvaluateFeature(nullptr, sl::Feature::eFeatureReflex, frameNumber, sl::ReflexMarker::eReflexMarkerSimulationStart);
    }
}

void SLWrapper::ReflexCallback_SimEnd(donut::app::DeviceManager& manager) {

    if (SLWrapper::GetReflexAvailable()) {
        int frameNumber = manager.GetFrameIndex();
        slEvaluateFeature(nullptr, sl::Feature::eFeatureReflex, frameNumber, sl::ReflexMarker::eReflexMarkerSimulationEnd);
    }
}

void SLWrapper::ReflexCallback_RenderStart(donut::app::DeviceManager& manager) {

    if (SLWrapper::GetReflexAvailable()) {
        int frameNumber = manager.GetFrameIndex();
        slEvaluateFeature(nullptr, sl::Feature::eFeatureReflex, frameNumber, sl::ReflexMarker::eReflexMarkerRenderSubmitStart);
    }
}

void SLWrapper::ReflexCallback_RenderEnd(donut::app::DeviceManager& manager) {

    if (SLWrapper::GetReflexAvailable()) {
        int frameNumber = manager.GetFrameIndex();
        slEvaluateFeature(nullptr, sl::Feature::eFeatureReflex, frameNumber, sl::ReflexMarker::eReflexMarkerRenderSubmitEnd);
    }
}

void SLWrapper::ReflexCallback_PresentStart(donut::app::DeviceManager& manager) {

    if (SLWrapper::GetReflexAvailable()) {
        int frameNumber = manager.GetFrameIndex();
        slEvaluateFeature(nullptr, sl::Feature::eFeatureReflex, frameNumber, sl::ReflexMarker::eReflexMarkerPresentStart);
    }
}

void SLWrapper::ReflexCallback_PresentEnd(donut::app::DeviceManager& manager) {

    if (SLWrapper::GetReflexAvailable()) {
        int frameNumber = manager.GetFrameIndex();
        slEvaluateFeature(nullptr, sl::Feature::eFeatureReflex, frameNumber, sl::ReflexMarker::eReflexMarkerPresentEnd);
    }
}

void SLWrapper::ReflexTriggerFlash(int frameNumber) {
    if (SLWrapper::GetReflexAvailable() && SLWrapper::GetReflexFlashIndicatorEnable())
        slEvaluateFeature(nullptr, sl::Feature::eFeatureReflex, frameNumber, sl::ReflexMarker::eReflexMarkerTriggerFlash);
}

void SLWrapper::ReflexTriggerPcPing(int frameNumber) {
    if (SLWrapper::GetReflexAvailable())
        slEvaluateFeature(nullptr, sl::Feature::eFeatureReflex, frameNumber, sl::ReflexMarker::eReflexMarkerPCLatencyPing);
}


void SLWrapper::QueryReflexStats(bool& reflex_lowLatencyAvailable, bool& reflex_flashAvailable, std::string& stats) {
    if (SLWrapper::GetReflexAvailable()) {
        sl::ReflexSettings settings;
        slGetFeatureSettings(sl::Feature::eFeatureReflex, &m_reflex_consts, &settings);

        reflex_lowLatencyAvailable = settings.lowLatencyAvailable;
        reflex_flashAvailable = settings.flashIndicatorDriverControlled;

        auto rep = settings.frameReport[63];
        if (settings.latencyReportAvailable && rep.gpuRenderEndTime != 0) {

            auto frameID = rep.frameID;
            auto totalGameToRenderLatencyUs = rep.gpuRenderEndTime - rep.inputSampleTime;
            auto simDeltaUs = rep.simEndTime - rep.simStartTime;
            auto renderDeltaUs = rep.renderSubmitEndTime - rep.renderSubmitStartTime;
            auto presentDeltaUs = rep.presentEndTime - rep.presentStartTime;
            auto driverDeltaUs = rep.driverEndTime - rep.driverStartTime;
            auto osRenderQueueDeltaUs = rep.osRenderQueueEndTime - rep.osRenderQueueStartTime;
            auto gpuRenderDeltaUs = rep.gpuRenderEndTime - rep.gpuRenderStartTime;

            stats = "frameID: " + std::to_string(frameID);
            stats += "\ntotalGameToRenderLatencyUs: " + std::to_string(totalGameToRenderLatencyUs);
            stats += "\nsimDeltaUs: " + std::to_string(simDeltaUs);
            stats += "\nrenderDeltaUs: " + std::to_string(renderDeltaUs);
            stats += "\npresentDeltaUs: " + std::to_string(presentDeltaUs);
            stats += "\ndriverDeltaUs: " + std::to_string(driverDeltaUs);
            stats += "\nosRenderQueueDeltaUs: " + std::to_string(osRenderQueueDeltaUs);
            stats += "\ngpuRenderDeltaUs: " + std::to_string(gpuRenderDeltaUs);
        }
        else {
            stats = "Latency Report Unavailable";
        }
    }

}

#endif // #ifdef STREAMLINE_INTEGRATION
