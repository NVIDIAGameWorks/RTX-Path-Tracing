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

#include "PathTracer/Config.h"

#include "SampleUI.h"

#include <donut/app/ApplicationBase.h>
#include <donut/core/vfs/VFS.h>
#include <donut/render/GBufferFillPass.h>
#include <donut/render/PixelReadbackPass.h>
#include <donut/render/DrawStrategy.h>
#include <donut/app/Camera.h>

#include "RTXDI/RtxdiPass.h"
#include "NRD/NrdIntegration.h"
#include "OpacityMicroMap/OmmBuildQueue.h"
#include "PathTracer/StablePlanes.hlsli"
#include "Streamline/SLWrapper.h"

#include "RenderTargets.h"
#include "PostProcess.h"
#include "SampleConstantBuffer.h"
#include "AccumulationPass.h"
#include "ExtendedScene.h"

// should we use donut::pt_sdk for all our path tracing stuff?

// can be upgraded for special normalmap type (i.e. DXGI_FORMAT_BC5_UNORM) or single channel masks (i.e. DXGI_FORMAT_BC4_UNORM)
enum class TextureCompressionType
{
    Normalmap,
    GenericSRGB,        // maps to BC7_UNORM_SRGB
    GenericLinear,      // maps to BC7_UNORM
};

struct MaterialShadingProperties
{
    bool AlphaTest;
    bool HasTransmission;
    bool NoTransmission;
    bool FullyTransmissive;
    bool OnlyDeltaLobes;
    bool NoTextures;

    bool operator==(const MaterialShadingProperties& other) const { return AlphaTest==other.AlphaTest && HasTransmission==other.HasTransmission && NoTransmission==other.NoTransmission && FullyTransmissive==other.FullyTransmissive && OnlyDeltaLobes==other.OnlyDeltaLobes && NoTextures==other.NoTextures; };
    bool operator!=(const MaterialShadingProperties& other) const { return !(*this==other); }

    static MaterialShadingProperties Compute(const donut::engine::Material& material);
};

class Sample : public donut::app::ApplicationBase
{
    static constexpr uint32_t               c_PathTracerVariants   = 6; // see shaders.cfg and CreatePTPipeline for details on variants

private:
    std::shared_ptr<donut::vfs::RootFileSystem> m_RootFS;

    // scene
    std::vector<std::string>                        m_SceneFilesAvailable;
    std::string                                     m_CurrentSceneName;
    std::shared_ptr<donut::engine::ExtendedScene>   m_Scene;
    double                                          m_SceneTime = 0.;
    uint                                            m_SelectedCameraIndex = 0;  // 0 is first person camera, the rest (if any) are scene cameras

    // device setup
    std::shared_ptr<donut::engine::ShaderFactory> m_ShaderFactory;
    std::shared_ptr<donut::engine::DescriptorTableManager> m_DescriptorTable;
    std::unique_ptr<donut::engine::BindingCache> m_BindingCache;
    nvrhi::CommandListHandle                    m_CommandList;
    nvrhi::BindingLayoutHandle                  m_BindingLayout;
    nvrhi::BindingSetHandle                     m_BindingSet;
    nvrhi::BindingLayoutHandle                  m_BindlessLayout;

    std::unique_ptr<donut::render::TemporalAntiAliasingPass> m_TemporalAntiAliasingPass;

    // rendering
    std::unique_ptr<RenderTargets>              m_RenderTargets;
    std::vector <std::shared_ptr<donut::engine::Light>> m_Lights;
    std::unique_ptr<ToneMappingPass>      m_ToneMappingPass;
    std::shared_ptr<donut::render::InstancedOpaqueDrawStrategy> m_OpaqueDrawStrategy;
    std::shared_ptr<donut::render::TransparentDrawStrategy> m_TransparentDrawStrategy;
    std::shared_ptr<EnvironmentMap>             m_EnvironmentMap;
    nvrhi::BufferHandle                         m_ConstantBuffer;
    nvrhi::BufferHandle                         m_SubInstanceBuffer;            // per-instance-geometry data, indexed with (InstanceID()+GeometryIndex())
    uint                                        m_SubInstanceCount;

#if USE_PRECOMPUTED_SOBOL_BUFFER
    nvrhi::BufferHandle                         m_PrecomputedSobolBuffer;
#endif
    
    // raytracing basics
    std::unique_ptr< OmmBuildQueue >            m_OmmBuildQueue;
    nvrhi::rt::AccelStructHandle                m_TopLevelAS;

    // camera
    donut::app::FirstPersonCamera               m_Camera;
    std::shared_ptr<donut::engine::PlanarView>  m_View;
    std::shared_ptr<donut::engine::PlanarView>  m_ViewPrevious;
    float                                       m_CameraVerticalFOV = 60.0f;
    float                                       m_CameraZNear = 0.001f;
    dm::float3                                  m_LastCamPos = { 0,0,0 };
    dm::float3                                  m_LastCamDir = { 0,0,0 };
    dm::float3                                  m_LastCamUp = { 0,0,0 };


    std::chrono::high_resolution_clock::time_point       m_BenchStart = std::chrono::high_resolution_clock::now();
    std::chrono::high_resolution_clock::time_point       m_BenchLast = std::chrono::high_resolution_clock::now();
    int                                         m_BenchFrames = 0;

    std::shared_ptr<PostProcess>                m_PostProcess;

    //Debugging and debug viz
    //nvrhi::BufferHandle                         m_DebugConstantBuffer;
    nvrhi::BufferHandle                         m_Feedback_Buffer_Gpu;
    nvrhi::BufferHandle                         m_Feedback_Buffer_Cpu;
    nvrhi::BufferHandle                         m_DebugLineBufferCapture;
    nvrhi::BufferHandle                         m_DebugLineBufferDisplay;
    //nvrhi::BufferHandle                         m_DebugLineBufferDisplayAux;
    //nvrhi::BufferHandle                         m_DebugLineAdditionalBuffer;
    nvrhi::ShaderHandle                         m_LinesVertexShader;
    nvrhi::ShaderHandle                         m_LinesPixelShader;
    // nvrhi::ShaderHandle                         m_LinesAddExtraComputeShader;
    // nvrhi::ComputePipelineHandle                m_LinesAddExtraPipeline;

    std::vector<DebugLineStruct>                m_CPUSideDebugLines;

    nvrhi::InputLayoutHandle                    m_LinesInputLayout;
    nvrhi::GraphicsPipelineHandle               m_LinesPipeline;
    nvrhi::BindingLayoutHandle                  m_LinesBindingLayout;
    nvrhi::BindingSetHandle                     m_LinesBindingSet;
    uint2                                       m_PickPosition = 0u;
    bool                                        m_Pick = false;         // this is both for pixel and material debugging
    DebugFeedbackStruct                         m_FeedbackData;
    
    DeltaTreeVizPathVertex                         m_DebugDeltaPathTree[cDeltaTreeVizMaxVertices];
    nvrhi::BufferHandle                         m_DebugDeltaPathTree_Gpu;
    nvrhi::BufferHandle                         m_DebugDeltaPathTree_Cpu;
    nvrhi::BufferHandle                         m_DebugDeltaPathTreeSearchStack;

    // all UI-tweakable settings are here
    SampleUIData &                              m_ui;

    // path tracing
    nvrhi::ShaderLibraryHandle                  m_PTShaderLibrary[c_PathTracerVariants];
    nvrhi::rt::PipelineHandle                   m_PTPipeline[c_PathTracerVariants];
    nvrhi::rt::ShaderTableHandle                m_PTShaderTable[c_PathTracerVariants];
    int                                         m_AccumulationSampleIndex = 0;  // accumulated so far in the past, so if 0 this is the first.
    int                                         m_AccumulationSampleTarget = 0; // the target to how many we want accumulated (set by UI)

    uint64_t                                    m_frameIndex = 0;
    uint                                        m_sampleIndex = 0;            // per-frame sampling index; same as m_AccumulationSampleIndex in accumulation mode, otherwise in realtime based on frameIndex%something 
    SampleConstants                             m_currentConstants = {};

    std::unique_ptr<NrdIntegration>             m_NRD[cStablePlaneCount];       // reminder: when switching between ReLAX/ReBLUR, change settings, reset these to 0 and they'll get re-created in CreateRenderPasses!
    std::unique_ptr<RtxdiPass>                  m_RtxdiPass;
    std::unique_ptr<AccumulationPass>           m_AccumulationPass;

    nvrhi::ShaderHandle                         m_ExportVBufferCS;
    nvrhi::ComputePipelineHandle                m_ExportVBufferPSO;

    // texture compression: used but not compressed textures
    std::map<std::shared_ptr<donut::engine::LoadedTexture>, TextureCompressionType> m_UncompressedTextures;

#ifdef STREAMLINE_INTEGRATION
    std::unique_ptr<SLWrapper>                  m_SLWrapper;
    SLWrapper::DLSSSettings                     m_RecommendedDLSSSettings = {};
#endif
    int2                                        m_DisplaySize;
    int2                                        m_RenderSize;

    std::string                                 m_FPSInfo;

public:
    using ApplicationBase::ApplicationBase;

    Sample(donut::app::DeviceManager* deviceManager, SampleUIData& ui);

    //std::shared_ptr<donut::vfs::IFileSystem> GetRootFs() const                      { return m_RootFS; }
    std::shared_ptr<donut::engine::ShaderFactory> GetShaderFactory() const          { return m_ShaderFactory; }
    std::shared_ptr<donut::engine::Scene>   GetScene() const                        { return m_Scene; }
    std::vector<std::string> const &        GetAvailableScenes() const              { return m_SceneFilesAvailable; }
    std::string                             GetCurrentSceneName() const             { return m_CurrentSceneName; }
    const DebugFeedbackStruct &             GetFeedbackData() const                 { return m_FeedbackData; }
    const DeltaTreeVizPathVertex *             GetDebugDeltaPathTree() const           { return m_DebugDeltaPathTree; }
    uint                                    GetSceneCameraCount() const             { return (uint)m_Scene->GetSceneGraph()->GetCameras().size() + 1; }
    uint &                                  SelectedCameraIndex()                   { return m_SelectedCameraIndex; }   // 0 is default fps free flight, above (if any) will just use current scene camera
    
    void                                    SetUIPick()                             { m_Pick = true; }

    std::shared_ptr<donut::engine::Material> FindMaterial( int materialID ) const;
    
    uint                                    UncompressedTextureCount() const        { return (uint)m_UncompressedTextures.size(); }
    bool                                    CompressTextures();
    void                                    SaveCurrentCamera();
    void                                    LoadCurrentCamera();

    float                                   GetCameraVerticalFOV() const            { return m_CameraVerticalFOV; }
    void                                    SetCameraVerticalFOV(float cameraFOV)   { m_CameraVerticalFOV = cameraFOV; }

    float                                   GetAvgTimePerFrame() const;

    bool                                    Init(const std::string& preferredScene);
    void                                    SetCurrentScene(const std::string& sceneName, bool forceReload = false);

    virtual void                            SceneUnloading() override;
    virtual bool                            LoadScene(std::shared_ptr<donut::vfs::IFileSystem> fs, const std::filesystem::path& sceneFileName) override;
    virtual void                            SceneLoaded() override;
    virtual bool                            KeyboardUpdate(int key, int scancode, int action, int mods) override;
    virtual bool                            MousePosUpdate(double xpos, double ypos) override;
    virtual bool                            MouseButtonUpdate(int button, int action, int mods) override;
    virtual bool                            MouseScrollUpdate(double xoffset, double yoffset) override;
    virtual void                            Animate(float fElapsedTimeSeconds) override;

    bool                                    CreatePTPipeline(donut::engine::ShaderFactory& shaderFactory);
    void                                    DestroyOpacityMicromaps(nvrhi::ICommandList* commandList);
    void                                    CreateOpacityMicromaps();
    void                                    CreateBlases(nvrhi::ICommandList* commandList);
    void                                    CreateTlas(nvrhi::ICommandList* commandList);
    void                                    CreateAccelStructs(nvrhi::ICommandList* commandList);
    void                                    UpdateAccelStructs(nvrhi::ICommandList* commandList);
    void                                    BuildOpacityMicromaps(nvrhi::ICommandList* commandList, uint32_t frameIndex);
    void                                    BuildTLAS(nvrhi::ICommandList* commandList, uint32_t frameIndex) const;
    void                                    TransitionMeshBuffersToReadOnly(nvrhi::ICommandList* commandList);
    void                                    BackBufferResizing() override;
    void                                    CreateRenderPasses(bool& exposureResetRequired);
    void                                    PreUpdatePathTracing(bool resetAccum, nvrhi::CommandListHandle commandList);
    void                                    PostUpdatePathTracing();
    void                                    UpdatePathTracerConstants( PathTracerConstants & constants );
    void                                    RtxdiBeginFrame(nvrhi::IFramebuffer* framebuffer, PathTracerCameraData cameraData, bool needNewPasses, uint2 renderDims);

    void                                    Denoise(nvrhi::IFramebuffer* framebuffer);
    void                                    PathTrace(nvrhi::IFramebuffer* framebuffer, const SampleConstants & constants);
    void                                    Render(nvrhi::IFramebuffer* framebuffer) override;
    void                                    PostProcessAA(nvrhi::IFramebuffer* framebuffer);

    donut::math::float2                     ComputeCameraJitter( uint frameIndex );

    std::string                             GetResolutionInfo() const;
    std::string                             GetFPSInfo() const              { return m_FPSInfo; }

    void                                    DebugDrawLine( float3 start, float3 stop, float4 col1, float4 col2 );
    const donut::app::FirstPersonCamera &   GetCurrentCamera( ) const { return m_Camera; }

    void                                    ResetSceneTime( ) { m_SceneTime = 0.; }

private:
    void                                    UpdateCameraFromScene( const std::shared_ptr<donut::engine::PerspectiveCamera> & sceneCamera );
    void                                    UpdateViews( nvrhi::IFramebuffer* framebuffer );
    void                                    DenoisedScreenshot( nvrhi::ITexture * framebufferTexture ) const;
};
