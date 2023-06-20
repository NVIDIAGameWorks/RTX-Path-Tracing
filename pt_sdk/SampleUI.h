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

#include <donut/engine/Scene.h>

#include <donut/app/imgui_renderer.h>
#include <donut/app/imgui_console.h>

#include "Lights/EnvironmentMapImportanceSampling.h"
#include "ToneMapper/ToneMappingPasses.h"

#include "PathTracer/ShaderDebug.hlsli"
#include "RTXDI/RtxdiPass.h"

#include <donut/render/TemporalAntiAliasingPass.h>

#if ENABLE_DEBUG_DELTA_TREE_VIZUALISATION
#include "ImNodesEz.h"
#endif

#ifdef STREAMLINE_INTEGRATION
#include "sl.h"
#include "sl_dlss.h"
#include "sl_reflex.h"
#include<sl_dlss_g.h>
#endif

#include "NRD/NrdConfig.h"

// should we use donut::pt_sdk for all our path tracing stuff?

namespace donut::engine
{
    class SceneGraphNode;
}

struct TogglableNode
{
    donut::engine::SceneGraphNode * SceneNode;
    dm::double3                     OriginalTranslation;
    std::string                     UIName;
    bool                            IsSelected() const;
    void                            SetSelected( bool selected ) ;
};

struct OpacityMicroMapUIData
{
    struct BuildState
    {
        // ~~ Application is expected to tweak these settings ~~ 
        int MaxSubdivisionLevel = 12;
        bool EnableDynamicSubdivision = true;
        float DynamicSubdivisionScale = 1.f;
        nvrhi::rt::OpacityMicromapBuildFlags Flag = nvrhi::rt::OpacityMicromapBuildFlags::FastTrace;
        nvrhi::rt::OpacityMicromapFormat Format = nvrhi::rt::OpacityMicromapFormat::OC1_4_State;

        // ~~ Debug settings, application is expected to leave to default ~~ 
        bool ComputeOnly = true;
        bool LevelLineIntersection = true;
        bool EnableTexCoordDeduplication = true;
        bool Force32BitIndices = false;
        bool EnableNsightDebugMode = false;
        bool EnableSpecialIndices = true;
        int MaxOmmArrayDataSizeInMB = 100;

        bool operator == (const BuildState& o) const {
            return
                MaxSubdivisionLevel == o.MaxSubdivisionLevel &&
                EnableDynamicSubdivision == o.EnableDynamicSubdivision &&
                DynamicSubdivisionScale == o.DynamicSubdivisionScale &&
                Flag == o.Flag &&
                Format == o.Format &&
                ComputeOnly == o.ComputeOnly &&
                LevelLineIntersection == o.LevelLineIntersection &&
                EnableTexCoordDeduplication == o.EnableTexCoordDeduplication &&
                Force32BitIndices == o.Force32BitIndices &&
                EnableNsightDebugMode == o.EnableNsightDebugMode &&
                EnableSpecialIndices == o.EnableSpecialIndices &&
                MaxOmmArrayDataSizeInMB == o.MaxOmmArrayDataSizeInMB
                ;
        }
    };

    bool                                Enable = true;
    bool                                Force2State = false;
    bool                                OnlyOMMs = false;

    // Amortize the builds over multiple frames
    std::optional<BuildState>           ActiveState;
    BuildState                          DesiredState;
    bool                                TriggerRebuild = true;

    // --- Stats --- 
    // build progress of active tasks
    uint32_t                            BuildsLeftInQueue = 0;
    uint32_t                            BuildsQueued = 0;
};

struct AccelerationStructureUIData
{
    // Instance settings (no rebuild required)
    bool                                ForceOpaque = false;

    // BVH settings (require rebuild to take effect)
    bool                                ExcludeTransmissive = false;

    bool                                IsDirty = false;
};

struct SampleUIData
{
    bool                                ShowUI = true;
    int                                 FPSLimiter = 0; // 0 - no limit, otherwise limit fps to FPSLimiter and fix scene update deltaTime to 1./FPSLimiter
    bool                                ShowConsole = false;
    bool                                EnableAnimations = false;
    bool                                EnableVsync = false;
    std::shared_ptr<donut::engine::Material> SelectedMaterial;
    bool                                ShaderReloadRequested = false;
    float                               ShaderReloadDelayedRequest = 0.0f;
    std::string                         ScreenshotFileName;
    std::string                         ScreenshotSequencePath = "D:/AnimSequence/";
    bool                                ScreenshotSequenceCaptureActive = false;
    int                                 ScreenshotSequenceCaptureIndex = -64; // -x means x warmup frames for recording to stabilize denoiser
    bool                                LoopLongestAnimation = false; // some animation sequences want to loop only the longest, but some want to loop each independently
    bool                                ExperimentalPhotoModeScreenshot = false;

    bool                                UseStablePlanes = false; // only determines whether UseStablePlanes is used in Accumulate mode (for testing correctness and enabling RTXDI) - in Realtime mode or when using RTXDI UseStablePlanes are necessary
    bool                                AllowRTXDIInReferenceMode = false; // allows use of RTXDI even in reference mode
    bool                                UseReSTIR = false;  // should be renamed as ReSTIRDI?
    bool                                UseReSTIRGI = false;
    bool                                RealtimeMode = false;
    bool                                RealtimeNoise = true;
    bool                                RealtimeDenoiser = true;
    bool                                ResetAccumulation = false;
    int                                 BounceCount = 30;
    int                                 ReferenceDiffuseBounceCount = 6;
    int                                 RealtimeDiffuseBounceCount = 3;
    int                                 AccumulationTarget = 4096;
    int                                 AccumulationIndex = 0;  // only for info
    bool                                AccumulationAA = true;
    int                                 RealtimeAA = 2;         // 0 - no AA, 1 - TAA, 2 - DLSS,  3 - DLAA
    float                               CameraAperture = 0.0f;
    float                               CameraFocalDistance = 10000.0f;
    float                               CameraMoveSpeed = 2.0f;
    float                               TexLODBias = -1.0f;     // as small as possible without reducing performance!
    bool                                SuppressPrimaryNEE = false;

    donut::render::TemporalAntiAliasingParameters TemporalAntiAliasingParams;
    donut::render::TemporalAntiAliasingJitter     TemporalAntiAliasingJitter = donut::render::TemporalAntiAliasingJitter::R2;

    bool                                ContinuousDebugFeedback = false;
    bool                                ShowDebugLines = false;
    donut::math::uint2                  DebugPixel = { 0, 0 };
    donut::math::uint2                  MousePos = { 0, 0 };
    float                               DebugLineScale = 0.2f;

    bool                                ShowSceneTweakerWindow = false;

    EnvironmentMapImportanceSamplingParameters EnvironmentMapParams;

    bool                                EnableToneMapping = true;
    ToneMappingParameters               ToneMappingParams;

    DebugViewType                       DebugView = DebugViewType::Disabled;
    int                                 DebugViewStablePlaneIndex = -1;
    bool                                ShowWireframe;

    bool                                ReferenceFireflyFilterEnabled = true;
    float                               ReferenceFireflyFilterThreshold = 2.5f;
    bool                                RealtimeFireflyFilterEnabled = true;
    float                               RealtimeFireflyFilterThreshold = 0.25f;

    float                               DenoiserRadianceClampK = 16.0f;

    bool                                EnableRussianRoulette = true;

    bool                                DXRHitObjectExtension = true;
    bool                                ShaderExecutionReordering = true;
    OpacityMicroMapUIData               OpacityMicroMaps;
    AccelerationStructureUIData         AS;

    RtxdiUserSettings                   RTXDI;
    
    bool                                ShowDeltaTree = false;
    bool                                ShowMaterialEditor = true;  // this makes material editor default right click option

#ifdef STREAMLINE_INTEGRATION
    float                               DLSS_Sharpness = 0.f;
    bool                                DLSS_Supported = false;
    static constexpr sl::DLSSMode       DLSS_ModeDefault = sl::DLSSMode::eMaxQuality;
    sl::DLSSMode                        DLSS_Mode = DLSS_ModeDefault;
    bool                                DLSS_Dynamic_Res_change = true;
    donut::math::int2                   DLSS_Last_DisplaySize = { 0,0 };
    sl::DLSSMode                        DLSS_Last_Mode = sl::DLSSMode::eOff;
    int                                 DLSS_Last_RealtimeAA = 0;
    bool                                DLSS_DebugShowFullRenderingBuffer = false;
    bool                                DLSS_lodbias_useoveride = false;
    float                               DLSS_lodbias_overide = 0.f;
    bool                                DLSS_always_use_extents = false;

    // LATENCY specific parameters
    bool                                REFLEX_Supported = false;
    bool                                REFLEX_LowLatencyAvailable = false;
    int                                 REFLEX_Mode = sl::ReflexMode::eOff;
    int                                 REFLEX_CapedFPS = 0;
    std::string                         REFLEX_Stats = "";
    bool                                REFLEX_ShowStats = false;
    int                                 FpsCap = 60;

    // DLFG specific parameters
    bool                                DLSSG_Supported = false;
    sl::DLSSGMode                       DLSSG_mode = sl::DLSSGMode::eOff;
    int                                 DLSSG_multiplier = 1;
#endif

    int                                 StablePlanesActiveCount             = cStablePlaneCount;
    int                                 StablePlanesMaxVertexDepth          = std::min(14u, cStablePlaneMaxVertexIndex);
    float                               StablePlanesSplitStopThreshold      = 0.95f;
    float                               StablePlanesMinRoughness            = 0.07f;
    bool                                AllowPrimarySurfaceReplacement      = true;
    bool                                StablePlanesSuppressPrimaryIndirectSpecular = true;
    float                               StablePlanesSuppressPrimaryIndirectSpecularK = 0.4f;
    float                               StablePlanesAntiAliasingFallthrough = 0.6f;
    //bool                                StablePlanesSkipIndirectNoisePlane0 = false;

    std::shared_ptr<std::vector<TogglableNode>> TogglableNodes = nullptr;

    bool                                ActualUseStablePlanes() const               { return UseStablePlanes || RealtimeMode || ActualUseRTXDIPasses(); }
    //bool                                ActualSkipIndirectNoisePlane0() const       { return StablePlanesSkipIndirectNoisePlane0 && StablePlanesActiveCount > 2; }

    bool                                ActualUseRTXDIPasses() const                { return (RealtimeMode || AllowRTXDIInReferenceMode) && (UseReSTIR || UseReSTIRGI); }
    bool                                ActualUseReSTIRDI() const                   { return (RealtimeMode || AllowRTXDIInReferenceMode) && (UseReSTIR); }
    bool                                ActualUseReSTIRGI() const                   { return (RealtimeMode || AllowRTXDIInReferenceMode) && (UseReSTIRGI); }

    // Denoiser
    bool                                NRDModeChanged = false;
    NrdConfig::DenoiserMethod           NRDMethod = NrdConfig::DenoiserMethod::RELAX;
    float                               NRDDisocclusionThreshold = 0.01f;
    bool                                NRDUseAlternateDisocclusionThresholdMix = true;
    float                               NRDDisocclusionThresholdAlternate = 0.1f;
    nrd::RelaxDiffuseSpecularSettings   RelaxSettings;
    nrd::ReblurSettings                 ReblurSettings;

};

class SampleUI : public donut::app::ImGui_Renderer
{
private:
    class Sample & m_app;

    ImFont* m_FontDroidMono = nullptr;
    std::pair<ImFont*, float>   m_scaledFonts[14];
    int                         m_currentFontScaleIndex = -1;
    float                       m_currentScale = 1.0f;
    ImGuiStyle                  m_defaultStyle;

    float                       m_showSceneWidgets = 0.0f;

    std::unique_ptr<donut::app::ImGui_Console> m_console;
    std::shared_ptr<donut::engine::Light> m_SelectedLight;

    SampleUIData& m_ui;
    nvrhi::CommandListHandle m_CommandList;

    const bool m_SERSupported;
    const bool m_OMMSupported;

#if ENABLE_DEBUG_DELTA_TREE_VIZUALISATION
    ImNodes::Ez::Context* m_ImNodesContext;
#endif

public:
    SampleUI(donut::app::DeviceManager* deviceManager, class Sample & app, SampleUIData& ui, bool SERSupported, bool OMMSupported);
    virtual ~SampleUI();
protected:
    virtual void buildUI(void) override;
private:
    void buildDeltaTreeViz();

    virtual void Animate(float elapsedTimeSeconds) override;
    virtual bool MousePosUpdate(double xpos, double ypos) override;

    int FindBestScaleFontIndex(float scale);
};

void UpdateTogglableNodes(std::vector<TogglableNode>& TogglableNodes, donut::engine::SceneGraphNode* node);
