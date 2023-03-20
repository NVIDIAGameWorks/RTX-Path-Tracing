//----------------------------------------------------------------------------------
// File:        SLWrapper.h
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
#pragma once

#ifdef STREAMLINE_INTEGRATION

// Donut
#include <donut/engine/View.h>
#include <donut/app/DeviceManager.h>
#include <donut/core/math/basics.h>
#include <nvrhi/nvrhi.h>
#include <donut/core/log.h>

// Streamline
#include <sl.h>
#include <sl_dlss.h>
#include <sl_reflex.h>
#include <sl_dlss_g.h>

static constexpr int APP_ID = 231313132;

// We define a few functions to help with format conversion
inline sl::float2 make_sl_float2(donut::math::float2 donutF) { return sl::float2{ donutF.x, donutF.y }; }
inline sl::float3 make_sl_float3(donut::math::float3 donutF) { return sl::float3{ donutF.x, donutF.y, donutF.z }; }
inline sl::float4 make_sl_float4(donut::math::float4 donutF) { return sl::float4{ donutF.x, donutF.y, donutF.z, donutF.w }; }
inline sl::float4x4 make_sl_float4x4(donut::math::float4x4 donutF4x4) {
    sl::float4x4 outF4x4;
    outF4x4.setRow(0, make_sl_float4(donutF4x4.row0));
    outF4x4.setRow(1, make_sl_float4(donutF4x4.row1));
    outF4x4.setRow(2, make_sl_float4(donutF4x4.row2));
    outF4x4.setRow(3, make_sl_float4(donutF4x4.row3));
    return outF4x4;
}


// This is a wrapper around SL functionality for DLSS. It is seperated to provide focus to the calls specific to NGX for code sample purposes.
class SLWrapper
{
private:

    static bool m_sl_initialised;
    static nvrhi::GraphicsAPI m_api;
    nvrhi::IDevice* m_Device;

    sl::DLSSConstants m_dlss_consts;
    bool m_dlss_available = false;

    sl::DLSSGConstants m_dlssg_consts;
    sl::DLSSGSettings m_dlssg_settings;
    bool m_dlssg_available = false;

    sl::ReflexConstants m_reflex_consts;
    static bool m_reflex_available;
    static bool m_reflex_driverFlashIndicatorEnable;

    static void logFunctionCallback(sl::LogType type, const char* msg);
    static sl::Resource allocateResourceCallback(const sl::ResourceDesc* resDesc, void* device);
    static void releaseResourceCallback(sl::Resource* resource, void* device);

    int m_viewID = 0;

    // SL Interposer Functions
    static PFunSlInit* slInit;
    static PFunSlShutdown* slShutdown;
    static PFunSlSetFeatureEnabled* slSetFeatureEnabled;
    static PFunSlIsFeatureSupported* slIsFeatureSupported;
    static PFunSlSetTag* slSetTag;
    static PFunSlSetConstants* slSetConstants;
    static PFunSlSetFeatureConstants* slSetFeatureConstants;
    static PFunSlGetFeatureSettings* slGetFeatureSettings;
    static PFunSlEvaluateFeature* slEvaluateFeature;
    static PFunSlAllocateResources* slAllocateResources;
    static PFunSlFreeResources* slFreeResources;

public:

    struct DLSSSettings
    {
        donut::math::int2 optimalRenderSize;
        donut::math::int2 minRenderSize;
        donut::math::int2 maxRenderSize;
        float sharpness;
    };

    SLWrapper(nvrhi::IDevice* device);

    static bool Initialize(nvrhi::GraphicsAPI api, const bool& checkSig = true);
    static bool GetSLInitialized() { return m_sl_initialised; }
    static void Shutdown();

    void setViewID(int viewID) { m_viewID = viewID; }
    int getViewID() { return m_viewID; }

    void SetSLConsts(const sl::Constants& consts, const int frameNumber);
    void TagResources(
        nvrhi::ICommandList* commandList,
        const int frameNumber,
        const donut::engine::IView* view,
        nvrhi::ITexture* DLSSOutput,
        nvrhi::ITexture* DLSSInput,
        nvrhi::ITexture* motionVectors,
        nvrhi::ITexture* depth,
        nvrhi::ITexture* finalColorHudless,
        donut::math::int2 renderSize);

    void SetDLSSConsts(const sl::DLSSConstants consts, const int frameNumber);
    bool GetDLSSAvailable() { return m_dlss_available; }
    void QueryDLSSOptimalSettings(DLSSSettings& settings);
    void EvaluateDLSS(nvrhi::ICommandList* commandList, const int frameNumber);

    static bool GetReflexAvailable() { return m_reflex_available; }
    void SetReflexConsts(const sl::ReflexConstants consts, int frameNumber);
    static void ReflexCallback_Sleep_Input_SimStart(donut::app::DeviceManager& manager);
    static void ReflexCallback_SimEnd(donut::app::DeviceManager& manager);
    static void ReflexCallback_RenderStart(donut::app::DeviceManager& manager);
    static void ReflexCallback_RenderEnd(donut::app::DeviceManager& manager);
    static void ReflexCallback_PresentStart(donut::app::DeviceManager& manager);
    static void ReflexCallback_PresentEnd(donut::app::DeviceManager& manager);

    void ReflexTriggerFlash(int frameNumber);
    void ReflexTriggerPcPing(int frameNumber);
    void QueryReflexStats(bool& reflex_lowLatencyAvailable, bool& reflex_flashAvailable, std::string& stats);
    void SetReflexFlashIndicator(bool enabled) { m_reflex_driverFlashIndicatorEnable = enabled; }
    static bool GetReflexFlashIndicatorEnable() { return m_reflex_driverFlashIndicatorEnable; }


    void SetDLSSGConsts(const sl::DLSSGConstants consts, const int frameNumber);
    bool GetDLSSGAvailable() { return m_dlssg_available; }
    void QueryDLSSGSettings(uint64_t& estimatedVRamUsage, float& fps);
};

#endif // #ifdef STREAMLINE_INTEGRATION