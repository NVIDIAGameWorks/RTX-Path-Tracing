/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#include <map>

// Donut
#include <donut/engine/View.h>
#include <donut/app/DeviceManager.h>
#include <donut/core/math/basics.h>
#include <nvrhi/nvrhi.h>
#include <donut/core/log.h>

// Streamline Core
#include <sl.h>
#include <sl_consts.h>
#include <sl_hooks.h>
#include <sl_version.h>

// Streamline Features
#include <sl_dlss.h>
#include <sl_reflex.h>
#include <sl_nis.h>
#include <sl_dlss_g.h>


static constexpr int APP_ID = 231313132;

// Set this to a game's specific sdk version
static constexpr uint64_t SDK_VERSION = sl::kSDKVersion;

// We define a few functions to help with format conversion
inline sl::float2 make_sl_float2(donut::math::float2 donutF) { return sl::float2{ donutF.x, donutF.y }; }
inline sl::float3 make_sl_float3(donut::math::float3 donutF) { return sl::float3{ donutF.x, donutF.y, donutF.z }; }
inline sl::float4 make_sl_float4(donut::math::float4 donutF) {  return sl::float4{ donutF.x, donutF.y, donutF.z, donutF.w }; }
inline sl::float4x4 make_sl_float4x4(donut::math::float4x4 donutF4x4) {
    sl::float4x4 outF4x4;
    outF4x4.setRow(0, make_sl_float4(donutF4x4.row0));
    outF4x4.setRow(1, make_sl_float4(donutF4x4.row1));
    outF4x4.setRow(2, make_sl_float4(donutF4x4.row2));
    outF4x4.setRow(3, make_sl_float4(donutF4x4.row3));
    return outF4x4;
}


void logFunctionCallback(sl::LogType type, const char* msg);

bool successCheck(sl::Result result, const char* location = nullptr);


// This is a wrapper around SL functionality for DLSS. It is seperated to provide focus to the calls specific to NGX for code sample purposes.
class SLWrapper
{
private:

    SLWrapper() {}

    bool m_sl_initialised = false;
    nvrhi::GraphicsAPI m_api = nvrhi::GraphicsAPI::D3D12;
    nvrhi::IDevice* m_Device = nullptr;

#ifdef USE_DX11
    LUID m_d3d11Luid;
#endif

    bool m_dlss_available = false;
    sl::DLSSOptions m_dlss_consts{};

    bool m_nis_available = false;
    sl::NISOptions m_nis_consts{};

    bool m_dlssg_available = false;
    bool m_dlssg_triggerswapchainRecreation = false;
    bool m_dlssg_shoudLoad = false;
    sl::DLSSGOptions m_dlssg_consts{};
    sl::DLSSGState m_dlssg_settings{};

    bool m_reflex_available = false;
    sl::ReflexOptions m_reflex_consts{};
    bool m_reflex_driverFlashIndicatorEnable = false;

    static sl::Resource allocateResourceCallback(const sl::ResourceAllocationDesc* resDesc, void* device);
    static void releaseResourceCallback(sl::Resource* resource, void* device);

    sl::FrameToken* m_currentFrame;
    sl::ViewportHandle m_viewport = {0};

public:

    static SLWrapper& Get();
    SLWrapper(const SLWrapper&) = delete;
    SLWrapper(SLWrapper&&) = delete;
    SLWrapper& operator=(const SLWrapper&) = delete;
    SLWrapper& operator=(SLWrapper&&) = delete;
   
    bool Initialize_preDevice(nvrhi::GraphicsAPI api, const bool& checkSig = true, const bool& SLlog = false);
    bool Initialize_postDevice();

    bool GetSLInitialized() { return m_sl_initialised; }
    void SetDevice_raw(void* device_ptr);
    void SetDevice_nvrhi(nvrhi::IDevice* device);
    void UpdateFeatureAvailable(donut::app::DeviceManager* adapter);
    void Shutdown();
    nvrhi::GraphicsAPI getAPI() { return m_api; }
    void ProxyToNative(void* proxy, void** native);
    void NativeToProxy(void* proxy, void** native);
    void FindAdapter(void*& adapterPtr, void* vkDevices = nullptr);
#ifdef USE_DX11
    LUID& getD3D11LUID() { return m_d3d11Luid; }
#endif

    sl::FeatureRequirements GetFeatureRequirements(sl::Feature feature);
    sl::FeatureVersion GetFeatureVersion(sl::Feature feature);


    void SetSLConsts(const sl::Constants& consts);
    void FeatureLoad(sl::Feature feature, const bool turn_on);
    void TagResources_General(
        nvrhi::ICommandList* commandList,
        const donut::engine::IView* view,
        nvrhi::ITexture* motionVectors,
        nvrhi::ITexture* depth,
        nvrhi::ITexture* finalColorHudless);

    void TagResources_DLSS_NIS(
        nvrhi::ICommandList* commandList,
        const donut::engine::IView* view,
        nvrhi::ITexture* output,
        nvrhi::ITexture* input);
    
    struct DLSSSettings
    {
        donut::math::int2 optimalRenderSize;
        donut::math::int2 minRenderSize;
        donut::math::int2 maxRenderSize;
        float sharpness;
    };
    void SetDLSSOptions(const sl::DLSSOptions consts);
    bool GetDLSSAvailable() { return m_dlss_available; }
    bool GetDLSSLastEnable() { return m_dlss_consts.mode != sl::DLSSMode::eOff; }
    void QueryDLSSOptimalSettings(DLSSSettings& settings);
    void EvaluateDLSS(nvrhi::ICommandList* commandList);
    void CleanupDLSS();

    void SetNISOptions(const sl::NISOptions consts);
    bool GetNISAvailable() { return m_nis_available; }
    bool GetNISLastEnable() { return m_nis_consts.mode != sl::NISMode::eOff; }
    void EvaluateNIS(nvrhi::ICommandList* commandList);
    void CleanupNIS();

    bool GetReflexAvailable() { return m_reflex_available; }
    void SetReflexConsts(const sl::ReflexOptions consts);
    static void Callback_FrameCount_Reflex_Sleep_Input_SimStart(donut::app::DeviceManager& manager);
    static void ReflexCallback_SimEnd(donut::app::DeviceManager& manager);
    static void ReflexCallback_RenderStart(donut::app::DeviceManager& manager);
    static void ReflexCallback_RenderEnd(donut::app::DeviceManager& manager);
    static void ReflexCallback_PresentStart(donut::app::DeviceManager& manager);
    static void ReflexCallback_PresentEnd(donut::app::DeviceManager& manager);

    void ReflexTriggerFlash(int frameNumber);
    void ReflexTriggerPcPing(int frameNumber);
    void QueryReflexStats(bool& reflex_lowLatencyAvailable, bool& reflex_flashAvailable, std::string& stats);
    void SetReflexFlashIndicator(bool enabled) {m_reflex_driverFlashIndicatorEnable = enabled; }
    bool GetReflexFlashIndicatorEnable() { return m_reflex_driverFlashIndicatorEnable; }

    void SetDLSSGOptions(const sl::DLSSGOptions consts);
    bool GetDLSSGAvailable() { return m_dlssg_available; }
    bool GetDLSSGLastEnable() { return m_dlssg_consts.mode == sl::DLSSGMode::eOn; }
    bool QueryDLSSGState(uint64_t& estimatedVRamUsage, int& fps_multiplier, sl::DLSSGStatus& status, int& minSize);
    void Set_DLSSG_SwapChainRecreation(bool on) { m_dlssg_triggerswapchainRecreation = true; m_dlssg_shoudLoad = on; }
    bool Get_DLSSG_SwapChainRecreation(bool& turn_on) const;
    void Quiet_DLSSG_SwapChainRecreation() { m_dlssg_triggerswapchainRecreation = false; }
    void CleanupDLSSG();
};

