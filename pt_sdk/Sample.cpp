/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "Sample.h"

#include <donut/engine/ShaderFactory.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/TextureCache.h>
#include <donut/engine/BindingCache.h>
#include <donut/app/DeviceManager.h>
#include <donut/core/log.h>
#include <donut/core/json.h>
#include <donut/core/math/math.h>
#include <nvrhi/utils.h>
#include <nvrhi/common/misc.h>

#include "PathTracer/StablePlanes.hlsli"
#include "AccelerationStructureUtil.h"

#include "PathTracer/NoiseAndSequences.hlsli"

#include "Lighting/Distant/EnvMapImportanceSamplingBaker.h"

#include "LocalConfig.h"
#include "CommandLine.h"

using namespace donut;
using namespace donut::math;
using namespace donut::app;
using namespace donut::vfs;
using namespace donut::engine;
using namespace donut::render;

#include <fstream>
#include <iostream>

#include <thread>

static const int c_SwapchainCount = 3;

static const char* g_WindowTitle = "Path Tracing SDK v1.3.0";

const float c_EnvMapRadianceScale = 1.0f / 4.0f; // used to make input 32bit float radiance fit into 16bit float range that baker supports; going lower than 1/4 causes issues with current BC6U compression algorithm when used

// Temp helper used to reduce FPS to specified target (i.e.) 30 - useful to avoid overheating the office :) but not intended for precise fps control
class FPSLimiter
{
private:
    std::chrono::high_resolution_clock::time_point   m_lastTimestamp = std::chrono::high_resolution_clock::now();
    double                                  m_prevError     = 0.0;

public:
    void                FramerateLimit( int fpsTarget )
    {
        std::chrono::high_resolution_clock::time_point   nowTimestamp = std::chrono::high_resolution_clock::now();
        double deltaTime = std::chrono::duration<double>(nowTimestamp - m_lastTimestamp).count();
        double targetDeltaTime = 1.0 / (double)fpsTarget;
        double diffFromTarget = targetDeltaTime - deltaTime + m_prevError;
        if (diffFromTarget > 0.0f)
        {
            size_t sleepInMs = std::min(1000, (int)(diffFromTarget * 1000));
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepInMs));
        }

        auto prevTime = m_lastTimestamp;
        m_lastTimestamp = std::chrono::high_resolution_clock::now();
        double deltaError = targetDeltaTime - std::chrono::duration<double>( m_lastTimestamp - prevTime ).count();
        m_prevError = deltaError * 0.9 + m_prevError * 0.1;     // dampen the spring-like effect, but still remain accurate to any positive/negative creep induced by our sleep mechanism
        // clamp error handling to 1 frame length
        if( m_prevError > targetDeltaTime )
            m_prevError = targetDeltaTime;
        if( m_prevError < -targetDeltaTime )
            m_prevError = -targetDeltaTime;
        // shift last time by error to compensate
        m_lastTimestamp += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(m_prevError));
    }
};

template<typename ... Args>
std::string string_format(const std::string& format, Args ... args)
{
    int size_s = std::snprintf(nullptr, 0, format.c_str(), args ...) + 1; // Extra space for '\0'
    if (size_s <= 0) { throw std::runtime_error("Error during formatting."); }
    auto size = static_cast<size_t>(size_s);
    std::unique_ptr<char[]> buf(new char[size]);
    std::snprintf(buf.get(), size, format.c_str(), args ...);
    return std::string(buf.get(), buf.get() + size - 1); // We don't want the '\0' inside
}

static FPSLimiter g_FPSLimiter;

std::filesystem::path GetLocalPath(std::string subfolder)
{
    static std::filesystem::path oneChoice;
    // if( oneChoice.empty() )
    {
        std::filesystem::path candidateA = app::GetDirectoryWithExecutable( ) / subfolder;
        std::filesystem::path candidateB = app::GetDirectoryWithExecutable( ).parent_path( ) / subfolder;
        if (std::filesystem::exists(candidateA))
            oneChoice = candidateA;
        else
            oneChoice = candidateB;
    }
    return oneChoice;
}

Sample::Sample( donut::app::DeviceManager * deviceManager, CommandLineOptions& cmdLine, SampleUIData & ui )
    : app::ApplicationBase( deviceManager ), m_cmdLine(cmdLine), m_ui( ui )
{
    deviceManager->SetFrameTimeUpdateInterval(1.0f);

    std::filesystem::path frameworkShaderPath = app::GetDirectoryWithExecutable( ) / "shaders/framework" / app::GetShaderTypeName( GetDevice( )->getGraphicsAPI( ) );
    std::filesystem::path appShaderPath = app::GetDirectoryWithExecutable() / "shaders/pt_sdk" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());
    std::filesystem::path nrdShaderPath = app::GetDirectoryWithExecutable() / "shaders/nrd" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());
    std::filesystem::path ommShaderPath = app::GetDirectoryWithExecutable( ) / "shaders/omm" / app::GetShaderTypeName( GetDevice( )->getGraphicsAPI( ) );

    m_RootFS = std::make_shared<vfs::RootFileSystem>( );
    m_RootFS->mount( "/shaders/donut", frameworkShaderPath );
    m_RootFS->mount( "/shaders/app", appShaderPath);
    m_RootFS->mount("/shaders/nrd", nrdShaderPath);
    m_RootFS->mount( "/shaders/omm", ommShaderPath);

    m_ShaderFactory = std::make_shared<engine::ShaderFactory>( GetDevice( ), m_RootFS, "/shaders" );
    m_CommonPasses = std::make_shared<engine::CommonRenderPasses>( GetDevice( ), m_ShaderFactory );
    m_BindingCache = std::make_unique<engine::BindingCache>( GetDevice( ) );

    m_OpaqueDrawStrategy = std::make_shared<InstancedOpaqueDrawStrategy>( );
    m_TransparentDrawStrategy = std::make_shared<TransparentDrawStrategy>( );

    m_Camera.SetRotateSpeed(.003f);

#ifdef STREAMLINE_INTEGRATION
    if (!cmdLine.noStreamline)
    {
        m_ui.DLSS_Supported = SLWrapper::Get().GetDLSSAvailable();
        m_ui.REFLEX_Supported = SLWrapper::Get().GetReflexAvailable();
        m_ui.DLSSG_Supported = SLWrapper::Get().GetDLSSGAvailable();

        // Set the callbacks for Reflex
        deviceManager->m_callbacks.beforeFrame = SLWrapper::Callback_FrameCount_Reflex_Sleep_Input_SimStart;
        deviceManager->m_callbacks.afterAnimate = SLWrapper::ReflexCallback_SimEnd;
        deviceManager->m_callbacks.beforeRender = SLWrapper::ReflexCallback_RenderStart;
        deviceManager->m_callbacks.afterRender = SLWrapper::ReflexCallback_RenderEnd;
        deviceManager->m_callbacks.beforePresent = SLWrapper::ReflexCallback_PresentStart;
        deviceManager->m_callbacks.afterPresent = SLWrapper::ReflexCallback_PresentEnd;
    }
#endif
}

void Sample::DebugDrawLine( float3 start, float3 stop, float4 col1, float4 col2 )
{
    if( int(m_CPUSideDebugLines.size())+2 >= MAX_DEBUG_LINES )
        return;
    DebugLineStruct dls = { float4(start, 1), col1 }, dle = { float4(stop, 1), col2 };
    m_CPUSideDebugLines.push_back(dls);
    m_CPUSideDebugLines.push_back(dle);
}

bool Sample::Init(const std::string& preferredScene)
{
    nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
    bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
    bindlessLayoutDesc.firstSlot = 0;
    bindlessLayoutDesc.maxCapacity = 1024;
    bindlessLayoutDesc.registerSpaces = {
        nvrhi::BindingLayoutItem::RawBuffer_SRV(1),
        nvrhi::BindingLayoutItem::Texture_SRV(2)
    };
    m_BindlessLayout = GetDevice()->createBindlessLayout(bindlessLayoutDesc);

    nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
    globalBindingLayoutDesc.visibility = nvrhi::ShaderType::All;
    globalBindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::PushConstants(1, sizeof(SampleMiniConstants)),
        nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(4),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(5),
        nvrhi::BindingLayoutItem::Texture_SRV(6),
        nvrhi::BindingLayoutItem::Texture_SRV(7),
        nvrhi::BindingLayoutItem::TypedBuffer_SRV(8),
        //nvrhi::BindingLayoutItem::Texture_SRV(10),
#if USE_PRECOMPUTED_SOBOL_BUFFER
        nvrhi::BindingLayoutItem::TypedBuffer_SRV(42),
#endif
        nvrhi::BindingLayoutItem::Sampler(0),
        nvrhi::BindingLayoutItem::Sampler(1),
        nvrhi::BindingLayoutItem::Sampler(2),
        nvrhi::BindingLayoutItem::Texture_UAV(0),
        nvrhi::BindingLayoutItem::Texture_UAV(4),           // u_Throughput
        nvrhi::BindingLayoutItem::Texture_UAV(5),           // u_MotionVectors
        nvrhi::BindingLayoutItem::Texture_UAV(6),           // u_Depth        
        // denoising slots go from 30-39
        //nvrhi::BindingLayoutItem::StructuredBuffer_UAV(30), // denoiser 'control buffer' (might be removed, might be reused)
        nvrhi::BindingLayoutItem::Texture_UAV(31),          // RWTexture2D<float>  u_DenoiserViewspaceZ         
        nvrhi::BindingLayoutItem::Texture_UAV(32),          // RWTexture2D<float4> u_DenoiserMotionVectors      
        nvrhi::BindingLayoutItem::Texture_UAV(33),          // RWTexture2D<float4> u_DenoiserNormalRoughness    
        nvrhi::BindingLayoutItem::Texture_UAV(34),          // RWTexture2D<float4> u_DenoiserDiffRadianceHitDist
        nvrhi::BindingLayoutItem::Texture_UAV(35),          // RWTexture2D<float4> u_DenoiserSpecRadianceHitDist
        nvrhi::BindingLayoutItem::Texture_UAV(36),          // RWTexture2D<float4> u_DenoiserDisocclusionThresholdMix
        nvrhi::BindingLayoutItem::Texture_UAV(37),          // RWTexture2D<float4> u_CombinedHistoryClampRelax
        // debugging slots go from 50-59
        nvrhi::BindingLayoutItem::Texture_UAV(50),
        nvrhi::BindingLayoutItem::StructuredBuffer_UAV(51),
        nvrhi::BindingLayoutItem::StructuredBuffer_UAV(52),
        nvrhi::BindingLayoutItem::StructuredBuffer_UAV(53),
        nvrhi::BindingLayoutItem::StructuredBuffer_UAV(54),
        // ReSTIR GI
        nvrhi::BindingLayoutItem::Texture_UAV(60), // u_SecondarySurfacePositionNormal
        nvrhi::BindingLayoutItem::Texture_UAV(61)  // u_SecondarySurfaceRadiance

        // RTXDI for Local light sampling
        , nvrhi::BindingLayoutItem::TypedBuffer_UAV(62)			    // u_LL_RisLightDataBuffer
        , nvrhi::BindingLayoutItem::StructuredBuffer_SRV(62)        // t_LL_LightDataBuffer
        , nvrhi::BindingLayoutItem::TypedBuffer_UAV(63)             // u_LL_RisBuffer
        , nvrhi::BindingLayoutItem::VolatileConstantBuffer(2),	    // g_LL_RtxdiBridgeConst
    };

    // NV HLSL extensions - DX12 only
    if (GetDevice()->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
    {
        globalBindingLayoutDesc.bindings.push_back(
            nvrhi::BindingLayoutItem::TypedBuffer_UAV(NV_SHADER_EXTN_SLOT_NUM));
    }

    // stable planes buffers -- must be last because these items are appended to the BindingSetDesc after the main list
    globalBindingLayoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::Texture_UAV(40));
    globalBindingLayoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(42));
    globalBindingLayoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::Texture_UAV(44));
    globalBindingLayoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(45));

    m_BindingLayout = GetDevice()->createBindingLayout(globalBindingLayoutDesc);

    m_DescriptorTable = std::make_shared<engine::DescriptorTableManager>(GetDevice(), m_BindlessLayout);

    auto nativeFS = std::make_shared<vfs::NativeFileSystem>();
    m_TextureCache = std::make_shared<engine::TextureCache>(GetDevice(), nativeFS, m_DescriptorTable);

    memset( &m_FeedbackData, 0, sizeof(DebugFeedbackStruct) * 1 );
    memset( &m_DebugDeltaPathTree, 0, sizeof(DeltaTreeVizPathVertex) * cDeltaTreeVizMaxVertices );

    //Draw lines from the feedback buffer
    {
        std::vector<ShaderMacro> drawLinesMacro = { ShaderMacro("DRAW_LINES_SHADERS", "1") };
        m_LinesVertexShader = m_ShaderFactory->CreateShader("app/DebugLines.hlsl", "main_vs", &drawLinesMacro, nvrhi::ShaderType::Vertex);
        m_LinesPixelShader = m_ShaderFactory->CreateShader("app/DebugLines.hlsl", "main_ps", &drawLinesMacro, nvrhi::ShaderType::Pixel);

        // no longer using this approach but separate draw; leaving in for now
        // shaders.cfg: DebugLines.hlsl -T cs_6_3 -E AddExtraLinesCS -D UPDATE_LINES_SHADERS=1
        // std::vector<ShaderMacro> updateLinesMacro = { ShaderMacro("UPDATE_LINES_SHADERS", "1") };
        // m_LinesAddExtraComputeShader = m_ShaderFactory->CreateShader("app/DebugLines.hlsl", "AddExtraLinesCS", &updateLinesMacro, nvrhi::ShaderType::Compute);

        nvrhi::VertexAttributeDesc attributes[] = {
            nvrhi::VertexAttributeDesc()
                .setName("POSITION")
                .setFormat(nvrhi::Format::RGBA32_FLOAT)
                .setOffset(0)
                .setElementStride(sizeof(DebugLineStruct)),
                nvrhi::VertexAttributeDesc()
                .setName("COLOR")
                .setFormat(nvrhi::Format::RGBA32_FLOAT)
                .setOffset(offsetof(DebugLineStruct, col))
                .setElementStride(sizeof(DebugLineStruct)),
        };
        m_LinesInputLayout = GetDevice()->createInputLayout(attributes, uint32_t(std::size(attributes)), m_LinesVertexShader);

        nvrhi::BindingLayoutDesc linesBindingLayoutDesc;
        linesBindingLayoutDesc.visibility = nvrhi::ShaderType::All;
        linesBindingLayoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0)
        };

        m_LinesBindingLayout = GetDevice()->createBindingLayout(linesBindingLayoutDesc);

        // debug stuff!
        {
            nvrhi::BufferDesc bufferDesc;
            bufferDesc.byteSize = sizeof(DebugFeedbackStruct) * 1;
            bufferDesc.isConstantBuffer = false;
            bufferDesc.isVolatile = false;
            bufferDesc.canHaveUAVs = true;
            bufferDesc.cpuAccess = nvrhi::CpuAccessMode::None;
            bufferDesc.maxVersions = engine::c_MaxRenderPassConstantBufferVersions;
            bufferDesc.structStride = sizeof(DebugFeedbackStruct);
            bufferDesc.keepInitialState = true;
            bufferDesc.initialState = nvrhi::ResourceStates::Common;
            bufferDesc.debugName = "Feedback_Buffer_Gpu";
            m_Feedback_Buffer_Gpu = GetDevice()->createBuffer(bufferDesc);

            bufferDesc.canHaveUAVs = false;
            bufferDesc.cpuAccess = nvrhi::CpuAccessMode::Read;
            bufferDesc.structStride = 0;
            bufferDesc.keepInitialState = false;
            bufferDesc.initialState = nvrhi::ResourceStates::Unknown;
            bufferDesc.debugName = "Feedback_Buffer_Cpu";
            m_Feedback_Buffer_Cpu = GetDevice()->createBuffer(bufferDesc);

            bufferDesc.byteSize = sizeof(DebugLineStruct) * MAX_DEBUG_LINES;
            bufferDesc.isVertexBuffer = true;
            bufferDesc.isConstantBuffer = false;
            bufferDesc.isVolatile = false;
            bufferDesc.canHaveUAVs = true;
            bufferDesc.cpuAccess = nvrhi::CpuAccessMode::None;
            bufferDesc.structStride = sizeof(DebugLineStruct);
            bufferDesc.keepInitialState = true;
            bufferDesc.initialState = nvrhi::ResourceStates::Common;
            bufferDesc.debugName = "DebugLinesCapture";
            m_DebugLineBufferCapture    = GetDevice()->createBuffer(bufferDesc);
            bufferDesc.debugName = "DebugLinesDisplay";
            m_DebugLineBufferDisplay    = GetDevice()->createBuffer(bufferDesc);
            // bufferDesc.debugName = "DebugLinesDisplayAux";
            // m_DebugLineBufferDisplayAux = GetDevice()->createBuffer(bufferDesc);


            bufferDesc.byteSize = sizeof(DeltaTreeVizPathVertex) * cDeltaTreeVizMaxVertices;
            bufferDesc.isConstantBuffer = false;
            bufferDesc.isVolatile = false;
            bufferDesc.canHaveUAVs = true;
            bufferDesc.cpuAccess = nvrhi::CpuAccessMode::None;
            bufferDesc.maxVersions = engine::c_MaxRenderPassConstantBufferVersions;
            bufferDesc.structStride = sizeof(DeltaTreeVizPathVertex);
            bufferDesc.keepInitialState = true;
            bufferDesc.initialState = nvrhi::ResourceStates::Common;
            bufferDesc.debugName = "Feedback_PathDecomp_Gpu";
            m_DebugDeltaPathTree_Gpu = GetDevice()->createBuffer(bufferDesc);

            bufferDesc.canHaveUAVs = false;
            bufferDesc.cpuAccess = nvrhi::CpuAccessMode::Read;
            bufferDesc.structStride = 0;
            bufferDesc.keepInitialState = false;
            bufferDesc.initialState = nvrhi::ResourceStates::Unknown;
            bufferDesc.debugName = "Feedback_PathDecomp_Cpu";
            m_DebugDeltaPathTree_Cpu = GetDevice()->createBuffer(bufferDesc);


            bufferDesc.byteSize = sizeof(PathPayload) * cDeltaTreeVizMaxStackSize;
            bufferDesc.isConstantBuffer = false;
            bufferDesc.isVolatile = false;
            bufferDesc.canHaveUAVs = true;
            bufferDesc.cpuAccess = nvrhi::CpuAccessMode::None;
            bufferDesc.maxVersions = engine::c_MaxRenderPassConstantBufferVersions;
            bufferDesc.structStride = sizeof(PathPayload);
            bufferDesc.keepInitialState = true;
            bufferDesc.initialState = nvrhi::ResourceStates::Common;
            bufferDesc.debugName = "DebugDeltaPathTreeSearchStack";
            m_DebugDeltaPathTreeSearchStack = GetDevice()->createBuffer(bufferDesc);
        }
    }

    if (GetDevice()->queryFeatureSupport(nvrhi::Feature::RayTracingOpacityMicromap))
    {
        nvrhi::DeviceHandle device = GetDevice();
        m_OmmBuildQueue = std::make_unique<OmmBuildQueue>(device, m_DescriptorTable, m_ShaderFactory);
    }

    // Main constant buffer
    m_ConstantBuffer = GetDevice()->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
        sizeof(SampleConstants), "SampleConstants", engine::c_MaxRenderPassConstantBufferVersions*2));	// *2 because in some cases we update twice per frame

    // Command list!
    m_CommandList = GetDevice()->createCommandList();

    // Setup OMM baker.
    if (m_OmmBuildQueue)
    {
        nvrhi::DeviceHandle device = GetDevice();
        m_CommandList->open();
        m_OmmBuildQueue->Initialize(m_CommandList);
        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);
        GetDevice()->waitForIdle();
    }

    // Setup precomputed Sobol' buffer.
#if USE_PRECOMPUTED_SOBOL_BUFFER
    {
        const uint precomputedSobolDimensions = SOBOL_MAX_DIMENSIONS; const uint precomputedSobolIndexCount = SOBOL_PRECOMPUTED_INDEX_COUNT;
        const uint precomputedSobolBufferCount = precomputedSobolIndexCount * precomputedSobolDimensions;

        // buffer that stores pre-generated samples which get updated once per frame
        nvrhi::BufferDesc buffDesc;
        buffDesc.byteSize = sizeof(uint) * precomputedSobolBufferCount;
        buffDesc.format = nvrhi::Format::R32_UINT;
        buffDesc.canHaveTypedViews = true;
        buffDesc.initialState = nvrhi::ResourceStates::ShaderResource;
        buffDesc.keepInitialState = true;
        buffDesc.debugName = "PresampledEnvironmentSamples";
        buffDesc.canHaveUAVs = false;
        m_PrecomputedSobolBuffer = GetDevice()->createBuffer(buffDesc);

        uint * dataBuffer = new uint[precomputedSobolBufferCount];
        PrecomputeSobol(dataBuffer);

        nvrhi::DeviceHandle device = GetDevice();
        m_CommandList->open();
        m_CommandList->writeBuffer(m_PrecomputedSobolBuffer, dataBuffer, precomputedSobolBufferCount * sizeof(uint));
        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);
        GetDevice()->waitForIdle();

        delete[] dataBuffer;
    }
#endif

    // Get all scenes in "media" folder
    const std::string mediaExt = ".scene.json";
    for (const auto& file : std::filesystem::directory_iterator(GetLocalPath("media")))
    {
        if (!file.is_regular_file()) continue;
        std::string fileName = file.path().filename().string();
        std::string longExt = (fileName.size()<=mediaExt.length())?(""):(fileName.substr(fileName.length()-mediaExt.length()));
        if ( longExt == mediaExt )
            m_SceneFilesAvailable.push_back( file.path().filename().string() );
    }

    std::string scene = FindPreferredScene(m_SceneFilesAvailable, preferredScene);
    
    // Select initial scene
    SetCurrentScene(scene);

    return true;
}

void Sample::SetCurrentScene( const std::string & sceneName, bool forceReload )
{
    if( m_CurrentSceneName == sceneName && !forceReload )
        return;
    m_CurrentSceneName = sceneName;
    m_ui.ResetAccumulation = true;
    SetAsynchronousLoadingEnabled( false );
    BeginLoadingScene( std::make_shared<vfs::NativeFileSystem>(), GetLocalPath("media") / sceneName);
    if( m_Scene == nullptr )
    {
        log::error( "Unable to load scene '%s'", sceneName.c_str() );
        return;
    }
}

void Sample::SceneUnloading( ) 
{
    m_ui.TogglableNodes = nullptr;
    ApplicationBase::SceneUnloading();
    m_BindingSet = nullptr;
    m_TopLevelAS = nullptr;
    m_SubInstanceBuffer = nullptr;
    m_BindingCache->Clear( );
    m_Lights.clear();
    m_ui.SelectedMaterial = nullptr;
    m_ui.EnvironmentMapParams = EnvironmentMapRuntimeParameters();
    m_EnvMapBaker = nullptr;
    m_UncompressedTextures.clear();
	m_RtxdiPass->Reset();
    if (m_OmmBuildQueue)
        m_OmmBuildQueue->CancelPendingBuilds();
}

bool Sample::LoadScene(std::shared_ptr<vfs::IFileSystem> fs, const std::filesystem::path& sceneFileName)
{
    m_Scene = std::shared_ptr<engine::ExtendedScene>( new engine::ExtendedScene(GetDevice(), *m_ShaderFactory, fs, m_TextureCache, m_DescriptorTable, std::make_shared<ExtendedSceneTypeFactory>() ) );
    if (m_Scene->Load(sceneFileName))
        return true;
    m_Scene = nullptr;
    return false;
}

void Sample::UpdateCameraFromScene( const std::shared_ptr<donut::engine::PerspectiveCamera> & sceneCamera )
{
    dm::affine3 viewToWorld = sceneCamera->GetViewToWorldMatrix();
    dm::float3 cameraPos = viewToWorld.m_translation;
    m_Camera.LookAt(cameraPos, cameraPos + viewToWorld.m_linear.row2, viewToWorld.m_linear.row1);
    m_CameraVerticalFOV = sceneCamera->verticalFov;
    m_CameraZNear = sceneCamera->zNear;

    std::shared_ptr<donut::engine::PerspectiveCameraEx> sceneCameraEx = std::dynamic_pointer_cast<PerspectiveCameraEx>(sceneCamera);
    if( sceneCameraEx != nullptr )
    {
        ToneMappingParameters defaults;

        m_ui.ToneMappingParams.autoExposure = sceneCameraEx->enableAutoExposure.value_or(defaults.autoExposure);
        m_ui.ToneMappingParams.exposureCompensation = sceneCameraEx->exposureCompensation.value_or(defaults.exposureCompensation);
        m_ui.ToneMappingParams.exposureValue = sceneCameraEx->exposureValue.value_or(defaults.exposureValue);
        m_ui.ToneMappingParams.exposureValueMin = sceneCameraEx->exposureValueMin.value_or(defaults.exposureValueMin);
        m_ui.ToneMappingParams.exposureValueMax = sceneCameraEx->exposureValueMax.value_or(defaults.exposureValueMax);
    }
}

void Sample::UpdateViews( nvrhi::IFramebuffer* framebuffer )
{
    // we currently use TAA for jitter even when it's not used itself
    if (m_TemporalAntiAliasingPass)
        m_TemporalAntiAliasingPass->SetJitter(m_ui.TemporalAntiAliasingJitter);

    nvrhi::Viewport windowViewport(float(m_RenderSize.x), float(m_RenderSize.y));
    m_View->SetViewport(windowViewport);

    m_View->SetMatrices(m_Camera.GetWorldToViewMatrix(), perspProjD3DStyleReverse(m_CameraVerticalFOV, windowViewport.width() / windowViewport.height(), m_CameraZNear));
    m_View->SetPixelOffset(ComputeCameraJitter(m_sampleIndex));
    m_View->UpdateCache();
    if (GetFrameIndex() == 0)
    {
        m_ViewPrevious->SetMatrices(m_View->GetViewMatrix(), m_View->GetProjectionMatrix());
        m_ViewPrevious->SetPixelOffset(m_View->GetPixelOffset());
        m_ViewPrevious->UpdateCache();
    }
}

void Sample::SceneLoaded( )
{
    ApplicationBase::SceneLoaded( );

    m_SceneTime = 0.f;
    m_Scene->FinishedLoading( GetFrameIndex( ) );

	// Find lights; do this before special cases to avoid duplicates 
	for (auto light : m_Scene->GetSceneGraph()->GetLights())
	{
		m_Lights.push_back(light);
	}

    // Make a list of uncompressed textures
    auto listUncompressedTextureIfNeeded = [ & ](std::shared_ptr<LoadedTexture> texture, bool normalMap)//, TextureCompressionType compressionType)
    {
        if( texture == nullptr || texture->texture == nullptr )
            return;
        nvrhi::TextureDesc desc = texture->texture->getDesc();
        if (nvrhi::getFormatInfo(desc.format).blockSize != 1) // it's compressed, everything is fine!
            return;
        TextureCompressionType compressionType = normalMap?(TextureCompressionType::Normalmap):(
            (nvrhi::getFormatInfo(desc.format).isSRGB) ? (TextureCompressionType::GenericSRGB) : (TextureCompressionType::GenericLinear));

        auto it = m_UncompressedTextures.insert(std::make_pair(texture, compressionType));
        if (!it.second)
        {
            assert(it.first->second == compressionType); // not the same compression type? that's bad!
            return;
        }
    };
    for (auto material : m_Scene->GetSceneGraph()->GetMaterials())
    {
        listUncompressedTextureIfNeeded( material->baseOrDiffuseTexture, false );
        listUncompressedTextureIfNeeded( material->metalRoughOrSpecularTexture, false );
        listUncompressedTextureIfNeeded( material->normalTexture, true );
        listUncompressedTextureIfNeeded( material->emissiveTexture, false );
        listUncompressedTextureIfNeeded( material->occlusionTexture, false );
        listUncompressedTextureIfNeeded( material->transmissionTexture, false );
    };

    // seem like sensible defaults
    m_ui.ToneMappingParams.exposureCompensation = 2.0f;
    m_ui.ToneMappingParams.exposureValue = 0.0f;

    std::shared_ptr<EnvironmentLight> envLight = FindEnvironmentLight(m_Lights);
    m_EnvMapLocalPath = (envLight==nullptr)?(""):(envLight->path);
    m_ui.EnvironmentMapParams = EnvironmentMapRuntimeParameters();


    if (m_ui.TogglableNodes == nullptr)
    {
        m_ui.TogglableNodes = std::make_shared<std::vector<TogglableNode>>();
        UpdateTogglableNodes(*m_ui.TogglableNodes, GetScene()->GetSceneGraph()->GetRootNode().get()); // UNSAFE - make sure not to keep m_ui.TogglableNodes longer than scenegraph!
    }

    // clean up invisible lights / markers because they slow things down
    for (int i = (int)m_Lights.size() - 1; i >= 0; i--)
    {
        LightConstants lc;
        m_Lights[i]->FillLightConstants(lc);
        if (length(lc.color * lc.intensity) <= 1e-7f)
            m_Lights.erase( m_Lights.begin() + i );
    }

    if( m_EnvMapLocalPath != "" )
    {
        // Make sure that there's an environment light object attached to the scene,
        // so that RTXDI will pick it up and sample.
        if (envLight == nullptr)
        {
            envLight = std::make_shared<EnvironmentLight>();
            m_Scene->GetSceneGraph()->AttachLeafNode(m_Scene->GetSceneGraph()->GetRootNode(), envLight);
            m_Lights.push_back(envLight);
        }
    }

    // setup camera - just load the last from the scene if available
    auto cameras = m_Scene->GetSceneGraph( )->GetCameras( );
    auto camScene = (cameras.empty( ))?(nullptr):(std::dynamic_pointer_cast<PerspectiveCamera>(cameras.back()));

    if( camScene == nullptr )
    {
        m_Camera.LookAt(float3(0.f, 1.8f, 0.f), float3(1.f, 1.55f, 0.f), float3(0, 1, 0));
        m_CameraVerticalFOV = dm::radians(60.0f);
        m_CameraZNear = 0.001f;
    }
    else
    {
        UpdateCameraFromScene( camScene );
    }

    // raytracing acceleration structures
    m_CommandList->open( );
    CreateAccelStructs( m_CommandList );
    m_CommandList->close( );
    GetDevice( )->executeCommandList( m_CommandList );
    GetDevice( )->waitForIdle( );

    // if we don't re-set these, BLAS-es for animated stuff don't get updated
    for( const auto& anim : m_Scene->GetSceneGraph( )->GetAnimations( ) )
        (void)anim->Apply( 0.0f );

    // PrintSceneGraph( m_Scene->GetSceneGraph( )->GetRootNode( ) );

    m_ui.ShaderReloadRequested = true;  // we have to re-create shader hit table
    m_ui.EnableAnimations = false;
    m_ui.RealtimeMode = false;
    m_ui.UseReSTIRDI = false;
    m_ui.UseReSTIRGI = false;

    std::shared_ptr<SampleSettings> settings = m_Scene->GetSampleSettingsNode();
    if (settings != nullptr)
    {
        m_ui.RealtimeMode = settings->realtimeMode.value_or(m_ui.RealtimeMode);
        m_ui.EnableAnimations = settings->enableAnimations.value_or(m_ui.EnableAnimations);
        if (settings->enableRTXDI.value_or(false))
        {
            m_ui.UseReSTIRDI = m_ui.UseReSTIRGI = true;
        }
        if (settings->startingCamera.has_value())
            m_SelectedCameraIndex = settings->startingCamera.value()+1; // slot 0 reserved for free flight camera
        if (settings->realtimeFireflyFilter.has_value())
        {
            m_ui.RealtimeFireflyFilterThreshold = settings->realtimeFireflyFilter.value();
            m_ui.RealtimeFireflyFilterEnabled = true;
        }
        m_ui.BounceCount = settings->maxBounces.value_or(m_ui.BounceCount);
        m_ui.RealtimeDiffuseBounceCount = settings->realtimeMaxDiffuseBounces.value_or(m_ui.RealtimeDiffuseBounceCount);
        m_ui.ReferenceDiffuseBounceCount = settings->referenceMaxDiffuseBounces.value_or(m_ui.ReferenceDiffuseBounceCount);
        m_ui.TexLODBias = settings->textureMIPBias.value_or(m_ui.TexLODBias);
    }

    LocalConfig::PostSceneLoad( *this, m_ui );

    if (m_EnvMapBaker!=nullptr) m_EnvMapBaker->SceneReloaded();
}

bool Sample::KeyboardUpdate(int key, int scancode, int action, int mods) 
{
    m_Camera.KeyboardUpdate(key, scancode, action, mods);

    if (key == GLFW_KEY_SPACE && action == GLFW_PRESS)
    {
        m_ui.EnableAnimations = !m_ui.EnableAnimations;
        return true;
    }
    if( key == GLFW_KEY_F2 && action == GLFW_PRESS )
        m_ui.ShowUI = !m_ui.ShowUI;
    if( key == GLFW_KEY_R && action == GLFW_PRESS && mods == GLFW_MOD_CONTROL )
        m_ui.ShaderReloadRequested = true;

#ifdef STREAMLINE_INTEGRATION
    if (key == GLFW_KEY_F13 && action == GLFW_PRESS) {
        // As GLFW abstracts away from Windows messages
        // We instead set the F13 as the PC_Ping key in the constants and compare against that.
         SLWrapper::Get().ReflexTriggerPcPing(GetFrameIndex());
    }
#endif
        
    return true;
}

bool Sample::MousePosUpdate(double xpos, double ypos)
{
    float scaleX, scaleY;
    GetDeviceManager()->GetDPIScaleInfo(scaleX, scaleY);
    xpos *= scaleX;
    ypos *= scaleY;

    m_Camera.MousePosUpdate(xpos, ypos);

    float2 upscalingScale = float2(1,1);
    if (m_RenderTargets != nullptr)
    {
        float2 nativeRes = float2((float)m_RenderTargets->OutputColor->getDesc().width, (float)m_RenderTargets->OutputColor->getDesc().height);
        float2 finalRes = float2((float)m_RenderTargets->LdrColor->getDesc().width, (float)m_RenderTargets->LdrColor->getDesc().height);
        upscalingScale = nativeRes / finalRes;
    }
    
    m_PickPosition = uint2( static_cast<uint>( xpos * upscalingScale.x ), static_cast<uint>( ypos * upscalingScale.y ) );
    m_ui.MousePos = uint2( static_cast<uint>( xpos * upscalingScale.x ), static_cast<uint>( ypos * upscalingScale.y ) );

    return true;
}

bool Sample::MouseButtonUpdate(int button, int action, int mods)
{
    m_Camera.MouseButtonUpdate(button, action, mods);

    if( action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_2 )
    {
        m_Pick = true;
        m_ui.DebugPixel = m_PickPosition;
    }

#ifdef STREAMLINE_INTEGRATION
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
         SLWrapper::Get().ReflexTriggerFlash(GetFrameIndex());
    }
#endif

    return true;
}

bool Sample::MouseScrollUpdate(double xoffset, double yoffset)
{
    m_Camera.MouseScrollUpdate(xoffset, yoffset);
    return true;
}

void Sample::Animate(float fElapsedTimeSeconds)
{
    if (m_ui.FPSLimiter>0)    // essential for stable video recording
        fElapsedTimeSeconds = 1.0f / (float)m_ui.FPSLimiter;

    m_Camera.SetMoveSpeed(m_ui.CameraMoveSpeed);

    if( m_ui.ShaderReloadDelayedRequest > 0 )
    {
        m_ui.ShaderReloadDelayedRequest -= fElapsedTimeSeconds;
        if (m_ui.ShaderReloadDelayedRequest <= 0 )
        {
            m_ui.ShaderReloadDelayedRequest = 0;
            m_ui.ShaderReloadRequested = true;
        }
    }

    if (m_ToneMappingPass)
        m_ToneMappingPass->AdvanceFrame(fElapsedTimeSeconds);

    const bool enableAnimations = m_ui.EnableAnimations && m_ui.RealtimeMode;
    const bool enableAnimationUpdate = enableAnimations || m_ui.ResetAccumulation;

    if (IsSceneLoaded() && enableAnimationUpdate)
    {
        if (enableAnimations)
            m_SceneTime += fElapsedTimeSeconds * 0.5f;
        float offset = 0;

        if (m_ui.LoopLongestAnimation)
        {
            float longestAnim = 0.0f;
            for (const auto& anim : m_Scene->GetSceneGraph()->GetAnimations())
                longestAnim = std::max( longestAnim, anim->GetDuration() );
            if (longestAnim > 0)
            {
                if( longestAnim > 0.0f && m_SceneTime > longestAnim )
                    m_SceneTime -= int(m_SceneTime/longestAnim)*longestAnim;
                for (const auto& anim : m_Scene->GetSceneGraph()->GetAnimations())
                    anim->Apply((float)m_SceneTime);
            }
        }
        else // loop each animation individually
        {
            for (const auto& anim : m_Scene->GetSceneGraph()->GetAnimations())
                anim->Apply((float)fmod(m_SceneTime, anim->GetDuration()));
        }
    }
    else
    {
        m_SceneTime = 0.0f;
    }

    m_SelectedCameraIndex = std::min( m_SelectedCameraIndex, GetSceneCameraCount()-1 );
    if (m_SelectedCameraIndex > 0)
    {
        std::shared_ptr<donut::engine::PerspectiveCamera> sceneCamera = std::dynamic_pointer_cast<PerspectiveCamera>(m_Scene->GetSceneGraph()->GetCameras()[m_SelectedCameraIndex-1]);
        if (sceneCamera != nullptr)
            UpdateCameraFromScene( sceneCamera );
    }

    m_Camera.Animate(fElapsedTimeSeconds);

    dm::float3 camPos = m_Camera.GetPosition();
    dm::float3 camDir = m_Camera.GetDir();
    dm::float3 camUp = m_Camera.GetUp();

    // if camera moves, reset accumulation
    if (m_LastCamDir.x != camDir.x || m_LastCamDir.y != camDir.y || m_LastCamDir.z != camDir.z || m_LastCamPos.x != camPos.x || m_LastCamPos.y != camPos.y || m_LastCamPos.z != camPos.z
        || m_LastCamUp.x != camUp.x || m_LastCamUp.y != camUp.y || m_LastCamUp.z != camUp.z )
    {
        m_LastCamPos = camPos;
        m_LastCamDir = camDir;
        m_LastCamUp = camUp;
        m_ui.ResetAccumulation = true;
    }
    
    double frameTime = GetDeviceManager()->GetAverageFrameTimeSeconds();
    if (frameTime > 0.0)
    {
#ifdef STREAMLINE_INTEGRATION
        if (m_ui.DLSSG_multiplier != 1)
            m_FPSInfo = string_format("%.3f ms/%d-frames* (%.1f FPS*) *DLSS-G", frameTime * 1e3, m_ui.DLSSG_multiplier, m_ui.DLSSG_multiplier / frameTime);
        else
#endif
            m_FPSInfo = string_format("%.3f ms/frame (%.1f FPS)", frameTime * 1e3, 1.0 / frameTime);
    }

    // Window title
    std::string extraInfo = ", " + m_FPSInfo + ", " + m_CurrentSceneName + ", " + GetResolutionInfo() + ", (L: " + std::to_string(m_Scene->GetSceneGraph()->GetLights().size()) + ", MAT: " + std::to_string(m_Scene->GetSceneGraph()->GetMaterials().size()) 
        + ", MESH: " + std::to_string(m_Scene->GetSceneGraph()->GetMeshes().size()) + ", I: " + std::to_string(m_Scene->GetSceneGraph()->GetMeshInstances().size()) + ", SI: " + std::to_string(m_Scene->GetSceneGraph()->GetSkinnedMeshInstances().size()) 
        //+ ", AvgLum: " + std::to_string((m_RenderTargets!=nullptr)?(m_RenderTargets->AvgLuminanceLastCaptured):(0.0f)) 
#if ENABLE_DEBUG_VIZUALISATION
        + ", ENABLE_DEBUG_VIZUALISATION: 1"
#endif
        + ")";

   
    GetDeviceManager()->SetInformativeWindowTitle(g_WindowTitle, false, extraInfo.c_str());
}

std::string Sample::GetResolutionInfo() const
{
    if (m_RenderTargets == nullptr || m_RenderTargets->OutputColor == nullptr)
        return "uninitialized";

    uint2 nativeRes = uint2(m_RenderTargets->OutputColor->getDesc().width, m_RenderTargets->OutputColor->getDesc().height);
    uint2 finalRes = uint2(m_RenderTargets->LdrColor->getDesc().width, m_RenderTargets->LdrColor->getDesc().height);
    if (dm::all(nativeRes == finalRes))
        return std::to_string(nativeRes.x) + "x" + std::to_string(nativeRes.y);
    else
        return std::to_string(nativeRes.x) + "x" + std::to_string(nativeRes.y) + "->" + std::to_string(finalRes.x) + "x" + std::to_string(finalRes.y);
}

float Sample::GetAvgTimePerFrame() const 
{ 
    if (m_BenchFrames == 0) 
        return 0.0f; 
    std::chrono::duration<double> elapsed = (m_BenchLast - m_BenchStart); 
    return float(elapsed.count() / m_BenchFrames); 
}

void Sample::SaveCurrentCamera()
{
    float3 worldPos = m_Camera.GetPosition();
    float3 worldDir = m_Camera.GetDir();
    float3 worldUp  = m_Camera.GetUp();
    dm::dquat rotation;
    dm::affine3 sceneWorldToView = dm::scaling(dm::float3(1.f, 1.f, -1.f)) * dm::inverse(m_Camera.GetWorldToViewMatrix()); // see SceneCamera::GetViewToWorldMatrix
    dm::decomposeAffine<double>( daffine3(sceneWorldToView), nullptr, &rotation, nullptr );

    float4x4 projMatrix = m_View->GetProjectionMatrix();
    bool rowMajor = true;
    float tanHalfFOVY = 1.0f / (projMatrix.m_data[1 * 4 + 1]);
    float fovY = atanf(tanHalfFOVY) * 2.0f;

    bool autoExposure = m_ui.ToneMappingParams.autoExposure;
    float exposureCompensation = m_ui.ToneMappingParams.exposureCompensation;
    float exposureValue = m_ui.ToneMappingParams.exposureValue;

    std::ofstream file;
    file.open(app::GetDirectoryWithExecutable( ) / "campos.txt", std::ios_base::out | std::ios_base::trunc );
    if( file.is_open() )
    {
        file << worldPos.x << " " << worldPos.y << " " << worldPos.z << " " << std::endl;
        file << worldDir.x << " " << worldDir.y << " " << worldDir.z << " " << std::endl;
        file << worldUp.x  << " " << worldUp.y  << " " << worldUp.z  << " " << std::endl;

        file << std::endl;
        file << "below is the camera node that can be included into the *.scene.json;" << std::endl;
        file << "'Cameras' node goes into 'Graph' array" << std::endl;
        file << std::endl;
        file << "{"                                                             << std::endl;
        file << "    \"name\": \"Cameras\","                                    << std::endl;
        file << "        \"children\" : ["                                      << std::endl;
        file << "    {"                                                         << std::endl;
        file << "        \"name\": \"Default\","                                   << std::endl;
        file << "        \"type\" : \"PerspectiveCameraEx\","                     << std::endl;
        file << "        \"translation\" : [" << std::to_string(worldPos.x) << ", " << std::to_string(worldPos.y) << ", " << std::to_string(worldPos.z) << "]," << std::endl;
        file << "        \"rotation\" : [" << std::to_string(rotation.x) << ", " << std::to_string(rotation.y) << ", " << std::to_string(rotation.z) << ", " << std::to_string(rotation.w) << "]," << std::endl;
        file << "        \"verticalFov\" : " << std::to_string(fovY)            << "," << std::endl;
        file << "        \"zNear\" : " << std::to_string(m_CameraZNear)         << "," << std::endl;
        file << "        \"enableAutoExposure\" : " << (autoExposure?"true":"false") << "," << std::endl;
        file << "        \"exposureCompensation\" : " << std::to_string(exposureCompensation) << "," << std::endl;
        file << "        \"exposureValue\" : " << std::to_string(exposureValue) << std::endl;
        file << "    }"                                                         << std::endl;
        file << "        ]"                                                     << std::endl;
        file << "},"                                                            << std::endl;

        file.close();
    }
}

void Sample::LoadCurrentCamera()
{
    float3 worldPos;
    float3 worldDir;
    float3 worldUp;

    std::ifstream file;
    file.open(app::GetDirectoryWithExecutable( ) / "campos.txt", std::ios_base::in);
    if (file.is_open())
    {
        file >> worldPos.x >> std::ws >> worldPos.y >> std::ws >> worldPos.z; file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        file >> worldDir.x >> std::ws >> worldDir.y >> std::ws >> worldDir.z; file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        file >> worldUp.x  >> std::ws >> worldUp.y  >> std::ws >> worldUp.z; file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        file.close();
        m_Camera.LookAt( worldPos, worldPos + worldDir, worldUp );
    }
}

struct HitGroupInfo
{
    std::string ExportName;
    std::string ClosestHitShader;
    std::string AnyHitShader;
};

MaterialShadingProperties MaterialShadingProperties::Compute(const donut::engine::Material& material)
{
    MaterialShadingProperties props;
    props.AlphaTest = (material.domain == MaterialDomain::AlphaTested) || (material.domain == engine::MaterialDomain::TransmissiveAlphaTested);
    props.HasTransmission = (material.domain == MaterialDomain::Transmissive) || (material.domain == MaterialDomain::TransmissiveAlphaBlended) || (material.domain == MaterialDomain::TransmissiveAlphaTested);
    props.NoTransmission = !props.HasTransmission;
    props.FullyTransmissive = props.HasTransmission && ((material.transmissionFactor + material.diffuseTransmissionFactor) >= 1.0);
    //bool hasEmissive = (material.emissiveIntensity > 0) && ((donut::math::luminance(material.emissiveColor) > 0) || (material.enableEmissiveTexture && material.emissiveTexture != nullptr));
    props.NoTextures = (!material.enableBaseOrDiffuseTexture || material.baseOrDiffuseTexture == nullptr)
        && (!material.enableEmissiveTexture || material.emissiveTexture == nullptr)
        && (!material.enableNormalTexture || material.normalTexture == nullptr)
        && (!material.enableMetalRoughOrSpecularTexture || material.metalRoughOrSpecularTexture == nullptr)
        && (!material.enableTransmissionTexture || material.transmissionTexture == nullptr);
    static const float kMinGGXRoughness = 0.08f; // see BxDF.hlsli, kMinGGXAlpha constant: kMinGGXRoughness must match sqrt(kMinGGXAlpha)!
    props.OnlyDeltaLobes = ((props.HasTransmission && material.transmissionFactor == 1.0) || (material.metalness == 1)) && (material.roughness < kMinGGXRoughness) && !(material.enableMetalRoughOrSpecularTexture && material.metalRoughOrSpecularTexture != nullptr);
    //bool hasOnlyTransmission = (!material.enableTransmissionTexture || material.transmissionTexture == nullptr) && ((material.transmissionFactor + material.diffuseTransmissionFactor) >= 1.0);
    return props;
}

// see OptimizationHints
HitGroupInfo ComputeSubInstanceHitGroupInfo(const donut::engine::Material& material)
{
    MaterialShadingProperties matProps = MaterialShadingProperties::Compute(material);

    HitGroupInfo info;

    info.ClosestHitShader = "ClosestHit";
    info.ClosestHitShader += std::to_string(matProps.NoTextures);
    info.ClosestHitShader += std::to_string(matProps.NoTransmission);
    info.ClosestHitShader += std::to_string(matProps.OnlyDeltaLobes);

    info.AnyHitShader = matProps.AlphaTest?"AnyHit":"";

    info.ExportName = "HitGroup";
    if (matProps.NoTextures)
        info.ExportName += "_NoTextures";
    if (matProps.NoTransmission)
        info.ExportName += "_NoTransmission";
    if (matProps.OnlyDeltaLobes)
        info.ExportName += "_OnlyDeltaLobes";
    if (matProps.AlphaTest)
        info.ExportName += "_HasAlphaTest";

    return info;
}

bool Sample::CreatePTPipeline(engine::ShaderFactory& shaderFactory)
{
    bool SERSupported = GetDevice()->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12 && GetDevice()->queryFeatureSupport(nvrhi::Feature::ShaderExecutionReordering);

    assert( m_SubInstanceCount > 0 );
    std::vector<HitGroupInfo> perSubInstanceHitGroup;
    perSubInstanceHitGroup.reserve(m_SubInstanceCount);
    for (const auto& instance : m_Scene->GetSceneGraph()->GetMeshInstances())
    {
        uint instanceID = (uint)perSubInstanceHitGroup.size();
        for (int gi = 0; gi < instance->GetMesh()->geometries.size(); gi++)
            perSubInstanceHitGroup.push_back(ComputeSubInstanceHitGroupInfo(*instance->GetMesh()->geometries[gi]->material));
    }

    // Prime the instances to make sure we only include the nessesary CHS variants in the PSO.
    std::unordered_map<std::string, HitGroupInfo> uniqueHitGroups;
    for (int i = 0; i < perSubInstanceHitGroup.size(); i++)
        uniqueHitGroups[perSubInstanceHitGroup[i].ExportName] = perSubInstanceHitGroup[i];

    // We use separate variants for
    //  - PATH_TRACER_MODE : because it modifies path payload and has different code coverage; switching dynamically significantly reduces shader compiler's ability to optimize
    //  - USE_HIT_OBJECT_EXTENSION : because it requires use of extended API
    for( int variant = 0; variant < c_PathTracerVariants; variant++ )
    {
        std::vector<engine::ShaderMacro> defines;
        // must match shaders.cfg - USE_HIT_OBJECT_EXTENSION path will possibly go away once part of API (it can be dynamic)
        if (variant == 0) { defines.push_back({ "PATH_TRACER_MODE", "PATH_TRACER_MODE_REFERENCE" });      defines.push_back({ "USE_HIT_OBJECT_EXTENSION", "0" }); }
        if (variant == 1) { defines.push_back({ "PATH_TRACER_MODE", "PATH_TRACER_MODE_BUILD_STABLE_PLANES" });    defines.push_back({ "USE_HIT_OBJECT_EXTENSION", "0" }); }
        if (variant == 2) { defines.push_back({ "PATH_TRACER_MODE", "PATH_TRACER_MODE_FILL_STABLE_PLANES" });    defines.push_back({ "USE_HIT_OBJECT_EXTENSION", "0" }); }
        if (variant == 3) { defines.push_back({ "PATH_TRACER_MODE", "PATH_TRACER_MODE_REFERENCE" });      defines.push_back({ "USE_HIT_OBJECT_EXTENSION", "1" }); }
        if (variant == 4) { defines.push_back({ "PATH_TRACER_MODE", "PATH_TRACER_MODE_BUILD_STABLE_PLANES" });    defines.push_back({ "USE_HIT_OBJECT_EXTENSION", "1" }); }
        if (variant == 5) { defines.push_back({ "PATH_TRACER_MODE", "PATH_TRACER_MODE_FILL_STABLE_PLANES" });    defines.push_back({ "USE_HIT_OBJECT_EXTENSION", "1" }); }
        m_PTShaderLibrary[variant] = shaderFactory.CreateShaderLibrary("app/Sample.hlsl", &defines);

        if (!m_PTShaderLibrary)
            return false;

        const bool exportAnyHit = variant < 3; // non-USE_HIT_OBJECT_EXTENSION codepaths require miss and hit; USE_HIT_OBJECT_EXTENSION codepaths can handle miss & anyhit inline!

        nvrhi::rt::PipelineDesc pipelineDesc;
        pipelineDesc.globalBindingLayouts = { m_BindingLayout, m_BindlessLayout };
        pipelineDesc.shaders.push_back({ "", m_PTShaderLibrary[variant]->getShader("RayGen", nvrhi::ShaderType::RayGeneration), nullptr });
        pipelineDesc.shaders.push_back({ "", m_PTShaderLibrary[variant]->getShader("Miss", nvrhi::ShaderType::Miss), nullptr });

        for (auto& [_, hitGroupInfo]: uniqueHitGroups)
        {
            pipelineDesc.hitGroups.push_back(
                {
                    .exportName = hitGroupInfo.ExportName,
                    .closestHitShader = m_PTShaderLibrary[variant]->getShader(hitGroupInfo.ClosestHitShader.c_str(), nvrhi::ShaderType::ClosestHit),
                    .anyHitShader = (exportAnyHit && hitGroupInfo.AnyHitShader!="")?(m_PTShaderLibrary[variant]->getShader(hitGroupInfo.AnyHitShader.c_str(), nvrhi::ShaderType::AnyHit)):(nullptr),
                    .intersectionShader = nullptr,
                    .bindingLayout = nullptr,
                    .isProceduralPrimitive = false
                }
            );
        }
       
        pipelineDesc.maxPayloadSize = PATH_TRACER_MAX_PAYLOAD_SIZE;
        pipelineDesc.maxRecursionDepth = 1; // 1 is enough if using inline visibility rays

        if (SERSupported)
            pipelineDesc.hlslExtensionsUAV = NV_SHADER_EXTN_SLOT_NUM;

        m_PTPipeline[variant] = GetDevice()->createRayTracingPipeline(pipelineDesc);

        if (!m_PTPipeline)
            return false;

        m_PTShaderTable[variant] = m_PTPipeline[variant]->createShaderTable();

        if (!m_PTShaderTable)
            return false;

        m_PTShaderTable[variant]->setRayGenerationShader("RayGen");
        for (int i = 0; i < perSubInstanceHitGroup.size(); i++)
            m_PTShaderTable[variant]->addHitGroup(perSubInstanceHitGroup[i].ExportName.c_str());

        m_PTShaderTable[variant]->addMissShader("Miss");
    }

    {
        std::vector<donut::engine::ShaderMacro> shaderMacros;
		// shaderMacros.push_back(donut::engine::ShaderMacro({ "USE_RTXDI", "0" }));
        m_ExportVBufferCS = m_ShaderFactory->CreateShader("app/ExportVisibilityBuffer.hlsl", "main", &shaderMacros, nvrhi::ShaderType::Compute);
        nvrhi::ComputePipelineDesc pipelineDesc;
		pipelineDesc.bindingLayouts = { m_BindingLayout, m_BindlessLayout };
		pipelineDesc.CS = m_ExportVBufferCS;
        m_ExportVBufferPSO = GetDevice()->createComputePipeline(pipelineDesc);
    }

    return true;
}

// sub-instance is a geometry within an instance
SubInstanceData ComputeSubInstanceData(const donut::engine::MeshInstance& meshInstance, uint meshInstanceIndex, const donut::engine::MeshGeometry& geometry, uint meshGeometryIndex, const donut::engine::Material& material)
{
    SubInstanceData ret;

    bool alphaTest = (material.domain == MaterialDomain::AlphaTested) || (material.domain == engine::MaterialDomain::TransmissiveAlphaTested);
    bool hasTransmission = (material.domain == MaterialDomain::Transmissive) || (material.domain == MaterialDomain::TransmissiveAlphaBlended) || (material.domain == MaterialDomain::TransmissiveAlphaTested);
    bool notMiss = true; // because miss defaults to 0 :)

    // bool path.interiorList.isEmpty() - could additionally sort on this at runtime

    bool hasEmissive = (material.emissiveIntensity > 0) && ((donut::math::luminance(material.emissiveColor) > 0) || (material.enableEmissiveTexture && material.emissiveTexture != nullptr));
    bool noTextures = (!material.enableBaseOrDiffuseTexture || material.baseOrDiffuseTexture == nullptr)
        && (!material.enableEmissiveTexture || material.emissiveTexture == nullptr)
        && (!material.enableNormalTexture || material.normalTexture == nullptr)
        && (!material.enableMetalRoughOrSpecularTexture || material.metalRoughOrSpecularTexture == nullptr)
        && (!material.enableTransmissionTexture || material.transmissionTexture == nullptr);
    bool hasNonDeltaLobes = (material.roughness > 0) || (material.enableMetalRoughOrSpecularTexture && material.metalRoughOrSpecularTexture != nullptr) || material.diffuseTransmissionFactor > 0;
    //bool hasOnlyTransmission = (!material.enableTransmissionTexture || material.transmissionTexture == nullptr) && ((material.transmissionFactor + material.diffuseTransmissionFactor) >= 1.0);

    ret.FlagsAndSERSortKey = 0;
    ret.FlagsAndSERSortKey |= alphaTest ? 1 : 0;
    ret.FlagsAndSERSortKey <<= 1;
    ret.FlagsAndSERSortKey |= hasTransmission ? 1 : 0;
    ret.FlagsAndSERSortKey <<= 1;
    ret.FlagsAndSERSortKey |= hasEmissive ? 1 : 0;
    ret.FlagsAndSERSortKey <<= 1;
    ret.FlagsAndSERSortKey |= noTextures ? 1 : 0;
    ret.FlagsAndSERSortKey <<= 1;
    ret.FlagsAndSERSortKey |= hasNonDeltaLobes ? 1 : 0;
    //ret.FlagsAndSERSortKey <<= 1;
    //ret.FlagsAndSERSortKey |= hasOnlyTransmission ? 1 : 0;
//
    ret.FlagsAndSERSortKey <<= 10;
    ret.FlagsAndSERSortKey |= meshInstanceIndex;

    ret.FlagsAndSERSortKey <<= 1;
    ret.FlagsAndSERSortKey |= notMiss ? 1 : 0;

#if 0
    ret.FlagsAndSERSortKey = 1 + material.materialID;
#elif 0
    const uint bitsForGeometryIndex = 6;
    ret.FlagsAndSERSortKey = 1 + (meshInstanceIndex << bitsForGeometryIndex) + meshGeometryIndex; // & ((1u << bitsForGeometryIndex) - 1u) );
#endif

    ret.FlagsAndSERSortKey &= 0xFFFF; // 16 bits for sort key above, clean anything else, the rest is used for flags

    if (alphaTest)
    {
        ret.FlagsAndSERSortKey |= SubInstanceData::Flags_AlphaTested;

        const std::shared_ptr<MeshInfo>& mesh = meshInstance.GetMesh();
        assert(mesh->buffers->hasAttribute(VertexAttribute::TexCoord1));
        assert(material.enableBaseOrDiffuseTexture && material.baseOrDiffuseTexture != nullptr); // disable alpha testing if this happens to be possible
        ret.AlphaTextureIndex = material.baseOrDiffuseTexture->bindlessDescriptor.Get();
        ret.GlobalGeometryIndex = mesh->geometries[0]->globalGeometryIndex + meshGeometryIndex;
        ret.AlphaCutoff = material.alphaCutoff;
    }

    if (material.excludeFromNEE)
    {
        ret.FlagsAndSERSortKey |= SubInstanceData::Flags_ExcludeFromNEE;
    }

    return ret;
}

void Sample::DestroyOpacityMicromaps(nvrhi::ICommandList* commandList)
{
    commandList->close();
    GetDevice()->executeCommandList(commandList);
    GetDevice()->waitForIdle();
    commandList->open();

    for (const std::shared_ptr<MeshInfo>& mesh : m_Scene->GetSceneGraph()->GetMeshes())
    {
        mesh->accelStructOMM = nullptr;
		mesh->opacityMicroMaps.clear();
        mesh->debugData = nullptr;
        mesh->debugDataDirty = true;
    }
}

void Sample::CreateOpacityMicromaps()
{
    if (!m_OmmBuildQueue)
        return;

    m_OmmBuildQueue->CancelPendingBuilds();

    m_ui.OpacityMicroMaps.ActiveState = m_ui.OpacityMicroMaps.DesiredState;
    m_ui.OpacityMicroMaps.BuildsLeftInQueue = 0;
    m_ui.OpacityMicroMaps.BuildsQueued = 0;

    for (const std::shared_ptr<MeshInfo>& mesh : m_Scene->GetSceneGraph()->GetMeshes())
    {
        if (mesh->buffers->hasAttribute(engine::VertexAttribute::JointWeights))
            continue; // skip the skinning prototypes
        if (mesh->skinPrototype)
            continue;

        OmmBuildQueue::BuildInput input;
        input.mesh = mesh;

        for (uint32_t i = 0; i < mesh->geometries.size(); ++i)
        {
            const std::shared_ptr<donut::engine::MeshGeometry>& geometry = mesh->geometries[i];
            if (!geometry->material)
                continue;
            if (!geometry->material->baseOrDiffuseTexture)
                continue;
            if (!geometry->material->baseOrDiffuseTexture->texture)
                continue;
            if (!geometry->material->enableBaseOrDiffuseTexture)
                continue;
            bool alphaTest = (geometry->material->domain == MaterialDomain::AlphaTested || geometry->material->domain == MaterialDomain::TransmissiveAlphaTested);
            if (!alphaTest)
                continue;

            std::shared_ptr<TextureData> alphaTexture = m_TextureCache->GetLoadedTexture(geometry->material->baseOrDiffuseTexture->path);
            
            OmmBuildQueue::BuildInput::Geometry geom;
            geom.geometryIndexInMesh = i;
            geom.alphaTexture = alphaTexture;
            geom.maxSubdivisionLevel = m_ui.OpacityMicroMaps.ActiveState->MaxSubdivisionLevel;
            geom.dynamicSubdivisionScale = m_ui.OpacityMicroMaps.ActiveState->EnableDynamicSubdivision ? m_ui.OpacityMicroMaps.ActiveState->DynamicSubdivisionScale : 0.f;
            geom.format = m_ui.OpacityMicroMaps.ActiveState->Format;
            geom.flags = m_ui.OpacityMicroMaps.ActiveState->Flag;
            geom.maxOmmArrayDataSizeInMB = m_ui.OpacityMicroMaps.ActiveState->MaxOmmArrayDataSizeInMB;
            geom.computeOnly = m_ui.OpacityMicroMaps.ActiveState->ComputeOnly;
            geom.enableLevelLineIntersection = m_ui.OpacityMicroMaps.ActiveState->LevelLineIntersection;
            geom.enableTexCoordDeduplication = m_ui.OpacityMicroMaps.ActiveState->EnableTexCoordDeduplication;
            geom.force32BitIndices = m_ui.OpacityMicroMaps.ActiveState->Force32BitIndices;
            geom.enableSpecialIndices = m_ui.OpacityMicroMaps.ActiveState->EnableSpecialIndices;
            geom.enableNsightDebugMode = m_ui.OpacityMicroMaps.ActiveState->EnableNsightDebugMode;

            input.geometries.push_back(geom);
        }

        if (input.geometries.size() != 0ull)
        {
            m_ui.OpacityMicroMaps.BuildsQueued += (uint32_t)input.geometries.size();
            m_OmmBuildQueue->QueueBuild(input);
        }
    }
}

void Sample::CreateBlases(nvrhi::ICommandList* commandList)
{
    for (const std::shared_ptr<MeshInfo>& mesh : m_Scene->GetSceneGraph()->GetMeshes())
    {
        if (mesh->buffers->hasAttribute(engine::VertexAttribute::JointWeights))
            continue; // skip the skinning prototypes

        bvh::Config cfg = { .excludeTransmissive = m_ui.AS.ExcludeTransmissive };

        nvrhi::rt::AccelStructDesc blasDesc = bvh::GetMeshBlasDesc(cfg , *mesh, nullptr);
        assert((int)blasDesc.bottomLevelGeometries.size() < (1 << 12)); // we can only hold 13 bits for the geometry index in the HitInfo - see GeometryInstanceID in SceneTypes.hlsli

        nvrhi::rt::AccelStructHandle as = GetDevice()->createAccelStruct(blasDesc);

        nvrhi::utils::BuildBottomLevelAccelStruct(commandList, as, blasDesc);

        mesh->accelStruct = as;
    }
}

void Sample::CreateTlas(nvrhi::ICommandList* commandList)
{

    nvrhi::rt::AccelStructDesc tlasDesc;
    tlasDesc.isTopLevel = true;
    tlasDesc.topLevelMaxInstances = m_Scene->GetSceneGraph()->GetMeshInstances().size();
    tlasDesc.buildFlags = nvrhi::rt::AccelStructBuildFlags::PreferFastTrace;
    assert( tlasDesc.topLevelMaxInstances < (1 << 15) ); // we can only hold 16 bits for the identifier in the HitInfo - see GeometryInstanceID in SceneTypes.hlsli
    m_TopLevelAS = GetDevice()->createAccelStruct(tlasDesc);


    // setup subInstances (entry is per geometry per instance) - some of it might require rebuild at runtime in more realistic scenarios
    {
        // figure out the required number
        m_SubInstanceCount = 0;
        for (const auto& instance : m_Scene->GetSceneGraph()->GetMeshInstances())
            m_SubInstanceCount += (uint)instance->GetMesh()->geometries.size();
        // create GPU buffer
        nvrhi::BufferDesc bufferDesc;
        bufferDesc.byteSize = sizeof(SubInstanceData) * m_SubInstanceCount;
        bufferDesc.debugName = "Instances";
        bufferDesc.structStride = sizeof(SubInstanceData);
        bufferDesc.canHaveRawViews = false;
        bufferDesc.canHaveUAVs = true;
        bufferDesc.isVertexBuffer = false;
        bufferDesc.initialState = nvrhi::ResourceStates::Common;
        bufferDesc.keepInitialState = true;
        m_SubInstanceBuffer = GetDevice()->createBuffer(bufferDesc);
        // figure out the data
        std::vector<SubInstanceData> subInstanceData;
        subInstanceData.reserve(m_SubInstanceCount);
        for (const auto& instance : m_Scene->GetSceneGraph()->GetMeshInstances())
        {
            uint instanceID = (uint)subInstanceData.size();
            for (int gi = 0; gi < instance->GetMesh()->geometries.size(); gi++)
                subInstanceData.push_back(ComputeSubInstanceData(*instance, instanceID, *instance->GetMesh()->geometries[gi], gi, *instance->GetMesh()->geometries[gi]->material));
        }
        assert(m_SubInstanceCount == subInstanceData.size());
        // upload data to GPU buffer
        commandList->writeBuffer(m_SubInstanceBuffer, subInstanceData.data(), subInstanceData.size() * sizeof(SubInstanceData));
    }
}

void Sample::CreateAccelStructs(nvrhi::ICommandList* commandList)
{
    CreateOpacityMicromaps();
    CreateBlases(commandList);
    CreateTlas(commandList);
}

void Sample::UpdateAccelStructs(nvrhi::ICommandList* commandList)
{
    // If the subInstanceData or BLAS build input data changes we trigger a full update here
    // could be made more efficient by only rebuilding the geometry in question,
    // or split the BLAS and subInstanceData updates
    if (m_ui.AS.IsDirty)
    {
        m_ui.AS.IsDirty = false;
        m_ui.ResetAccumulation = true;

        GetDevice()->waitForIdle();

        m_BindingSet = nullptr;
        m_TopLevelAS = nullptr;

        for (const std::shared_ptr<MeshInfo>& mesh : m_Scene->GetSceneGraph()->GetMeshes())
        {
            mesh->accelStruct = nullptr;
            mesh->accelStructOMM = nullptr;
            mesh->opacityMicroMaps.clear();
            mesh->debugData = nullptr;
            mesh->debugDataDirty = true;
        }

        // raytracing acceleration structures
        commandList->open();
        CreateAccelStructs(commandList);
        commandList->close();
        GetDevice()->executeCommandList(commandList);
        GetDevice()->waitForIdle();
    }
}

void Sample::BuildOpacityMicromaps(nvrhi::ICommandList* commandList, uint32_t frameIndex)
{
    if (!m_OmmBuildQueue)
        return;

    commandList->beginMarker("OMM Updates");

    if (m_ui.OpacityMicroMaps.TriggerRebuild)
    {
        {
            DestroyOpacityMicromaps(commandList);

            m_OmmBuildQueue->CancelPendingBuilds();

            CreateOpacityMicromaps();
        }

        m_ui.OpacityMicroMaps.TriggerRebuild = false;
    }

    m_OmmBuildQueue->Update(commandList);

    m_ui.OpacityMicroMaps.BuildsLeftInQueue = m_OmmBuildQueue->NumPendingBuilds();

    commandList->endMarker();
}

void Sample::TransitionMeshBuffersToReadOnly(nvrhi::ICommandList* commandList)
{
    // Transition all the buffers to their necessary states before building the BLAS'es to allow BLAS batching
    for (const auto& skinnedInstance : m_Scene->GetSceneGraph()->GetSkinnedMeshInstances())
        commandList->setBufferState(skinnedInstance->GetMesh()->buffers->vertexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->commitBarriers();
}

void Sample::BuildTLAS(nvrhi::ICommandList* commandList, uint32_t frameIndex) const
{
    commandList->beginMarker("Skinned BLAS Updates");

    // Transition all the buffers to their necessary states before building the BLAS'es to allow BLAS batching
    for (const auto& skinnedInstance : m_Scene->GetSceneGraph()->GetSkinnedMeshInstances())
    {
        if (skinnedInstance->GetLastUpdateFrameIndex() < frameIndex)
            continue;

        commandList->setAccelStructState(skinnedInstance->GetMesh()->accelStruct, nvrhi::ResourceStates::AccelStructWrite);
        commandList->setBufferState(skinnedInstance->GetMesh()->buffers->vertexBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
    }
    commandList->commitBarriers();

    // Now build the BLAS'es
    for (const auto& skinnedInstance : m_Scene->GetSceneGraph()->GetSkinnedMeshInstances())
    {
        if (skinnedInstance->GetLastUpdateFrameIndex() < frameIndex)
            continue;

        bvh::Config cfg = { .excludeTransmissive = m_ui.AS.ExcludeTransmissive };

        nvrhi::rt::AccelStructDesc blasDesc = bvh::GetMeshBlasDesc(cfg , *skinnedInstance->GetMesh(), nullptr);
            
        nvrhi::utils::BuildBottomLevelAccelStruct(commandList, skinnedInstance->GetMesh()->accelStruct, blasDesc);
    }
    commandList->endMarker();

    std::vector<nvrhi::rt::InstanceDesc> instances; // TODO: make this a member, avoid allocs :)
    
    uint subInstanceCount = 0;
    for (const auto& instance : m_Scene->GetSceneGraph()->GetMeshInstances())
    {
        const bool ommDebugViewEnabled = m_ui.DebugView == DebugViewType::FirstHitOpacityMicroMapInWorld || m_ui.DebugView == DebugViewType::FirstHitOpacityMicroMapOverlay;
        // ommDebugViewEnabled must do two things: use a BLAS without OMMs and disable all alpha testing.
        // This may sound a bit counter intuitive, the goal is to intersect micro-triangles marked as transparent without them actually being treated as such.

        const bool forceOpaque          = ommDebugViewEnabled || m_ui.AS.ForceOpaque;
        const bool hasAttachementOMM    = instance->GetMesh()->accelStructOMM.Get() != nullptr;
        const bool useOmmBLAS           = m_ui.OpacityMicroMaps.Enable && hasAttachementOMM && !forceOpaque;

        nvrhi::rt::InstanceDesc instanceDesc;
        instanceDesc.bottomLevelAS = useOmmBLAS ? instance->GetMesh()->accelStructOMM.Get() : instance->GetMesh()->accelStruct.Get();
        instanceDesc.instanceMask = m_ui.OpacityMicroMaps.OnlyOMMs && !hasAttachementOMM ? 0 : 1;
        instanceDesc.instanceID = instance->GetGeometryInstanceIndex();
        instanceDesc.instanceContributionToHitGroupIndex = subInstanceCount;
        instanceDesc.flags = m_ui.OpacityMicroMaps.Force2State ? nvrhi::rt::InstanceFlags::ForceOMM2State : nvrhi::rt::InstanceFlags::None;
        if (forceOpaque)
            instanceDesc.flags = (nvrhi::rt::InstanceFlags)((uint32_t)instanceDesc.flags | (uint32_t)nvrhi::rt::InstanceFlags::ForceOpaque);

        assert( subInstanceCount == instance->GetGeometryInstanceIndex() );
        subInstanceCount += (uint)instance->GetMesh()->geometries.size();

        auto node = instance->GetNode();
        assert(node);
        dm::affineToColumnMajor(node->GetLocalToWorldTransformFloat(), instanceDesc.transform);

        instances.push_back(instanceDesc);
    }
    assert (m_SubInstanceCount == subInstanceCount);

    // Compact acceleration structures that are tagged for compaction and have finished executing the original build
    commandList->compactBottomLevelAccelStructs();

    commandList->beginMarker("TLAS Update");
    commandList->buildTopLevelAccelStruct(m_TopLevelAS, instances.data(), instances.size(), nvrhi::rt::AccelStructBuildFlags::AllowEmptyInstances);
    commandList->endMarker();
}


void Sample::BackBufferResizing()
{
    ApplicationBase::BackBufferResizing();
    //Todo: Needed for vulkan realtime path, remove
    if(GetDevice()->getGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN)
    {
      m_RenderTargets = nullptr;
    }
    m_BindingCache->Clear();
    m_LinesPipeline = nullptr; // the pipeline is based on the framebuffer so needs a reset
    for (int i=0; i < std::size(m_NRD); i++ )
        m_NRD[i] = nullptr;
    if (m_RtxdiPass)
        m_RtxdiPass->Reset();
}

void Sample::CreateRenderPasses( bool& exposureResetRequired )
{
    m_RtxdiPass = std::make_unique<RtxdiPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_BindlessLayout);

    m_AccumulationPass = std::make_unique<AccumulationPass>(GetDevice(), m_ShaderFactory);
    m_AccumulationPass->CreatePipeline();
    m_AccumulationPass->CreateBindingSet(m_RenderTargets->OutputColor, m_RenderTargets->AccumulatedRadiance);

    if (!CreatePTPipeline(*m_ShaderFactory))
    {
        assert( false );
    }

    // these get re-created every time intentionally, to pick up changes after at-runtime shader recompile
    m_ToneMappingPass = std::make_unique<ToneMappingPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->LdrFramebuffer, *m_View, m_RenderTargets->OutputColor);
    m_PostProcess = std::make_shared<PostProcess>(GetDevice(), m_ShaderFactory, m_CommonPasses);

    for (int i = 0; i < std::size(m_NRD); i++)
    {
        if (m_NRD[i] == nullptr)
        {
            nrd::Denoiser denoiserMethod = m_ui.NRDMethod == NrdConfig::DenoiserMethod::REBLUR ?
                nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR : nrd::Denoiser::RELAX_DIFFUSE_SPECULAR;

            m_NRD[i] = std::make_unique<NrdIntegration>(GetDevice(), denoiserMethod);
            m_NRD[i]->Initialize(m_RenderSize.x, m_RenderSize.y, *m_ShaderFactory);
        }
    }

    {
        TemporalAntiAliasingPass::CreateParameters taaParams;
        taaParams.sourceDepth = m_RenderTargets->Depth;
        taaParams.motionVectors = m_RenderTargets->ScreenMotionVectors;
        taaParams.unresolvedColor = m_RenderTargets->OutputColor;
        taaParams.resolvedColor = m_RenderTargets->ProcessedOutputColor;
        taaParams.feedback1 = m_RenderTargets->TemporalFeedback1;
        taaParams.feedback2 = m_RenderTargets->TemporalFeedback2;
        taaParams.historyClampRelax = m_RenderTargets->CombinedHistoryClampRelax;
        taaParams.motionVectorStencilMask = 0; ///*uint32_t motionVectorStencilMask =*/ 0x01;
        taaParams.useCatmullRomFilter = true;

        m_TemporalAntiAliasingPass = std::make_unique<TemporalAntiAliasingPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, *m_View, taaParams);
    }

    if (m_EnvMapBaker == nullptr )
        m_EnvMapBaker = std::make_shared<EnvMapBaker>(GetDevice(), m_TextureCache, m_ShaderFactory, m_CommonPasses);
    m_EnvMapBaker->CreateRenderPasses( );
}

void Sample::UpdateLighting(nvrhi::CommandListHandle commandList, bool & needNewBindings)
{
    m_CommandList->beginMarker("UpdateLights");

    m_EnvMapBaker->PreUpdate(m_EnvMapLocalPath);

    EMB_DirectionalLight dirLights[EnvMapBaker::c_MaxDirLights];
    uint dirLightCount = 0;
    {   // Find and pre-process directional analytic lights, and convert them to environment map local frame so they remain pointing in correct world direction!
        float3 rotationInRadians = radians(m_ui.EnvironmentMapParams.RotationXYZ);
        affine3 rotationTransform = donut::math::rotation(rotationInRadians);
        affine3 inverseTransform = inverse(rotationTransform);
        for (int i = 0; i < (int)m_Lights.size(); i++)
        {
            std::shared_ptr<DirectionalLight> dirLight = std::dynamic_pointer_cast<DirectionalLight>(m_Lights[i]);
            if( dirLight != nullptr )
            {
                LightConstants light;
                dirLight->FillLightConstants(light);

                const float minAngularSize = PI_f / (m_EnvMapBaker->GetTargetCubeResolution()/2.0f);
                assert( light.angularSizeOrInvRange >= minAngularSize );    // point lights smaller than this cannot be reliably baked into cubemap
                dirLights[dirLightCount].AngularSize = std::max( light.angularSizeOrInvRange, minAngularSize );
                dirLights[dirLightCount].ColorIntensity = float4(light.color, light.intensity);
                dirLights[dirLightCount].Direction = rotationTransform.transformVector(light.direction);
                dirLightCount++;
            }
        }
    }

    auto preUpdateCube = m_EnvMapBaker->GetEnvMapCube();

    if (m_EnvMapBaker->Update(commandList, m_EnvMapLocalPath, EnvMapBaker::BakeSettings(c_EnvMapRadianceScale), m_SceneTime, dirLights, dirLightCount) )
        m_ui.ResetAccumulation = true;

    if (preUpdateCube != m_EnvMapBaker->GetEnvMapCube())
        needNewBindings = true;

    m_CommandList->endMarker();
}

void Sample::PreUpdatePathTracing( bool resetAccum, nvrhi::CommandListHandle commandList )
{
    m_frameIndex++;

    resetAccum |= m_ui.ResetAccumulation;
    resetAccum |= m_ui.RealtimeMode;
    m_ui.ResetAccumulation = false;

    if( m_ui.AccumulationTarget != m_AccumulationSampleTarget )
    {
        resetAccum = true;
        m_AccumulationSampleTarget = m_ui.AccumulationTarget;
    }

    if (resetAccum)
    {
        m_AccumulationSampleIndex = 0;
    }
#if ENABLE_DEBUG_VIZUALISATION
    if (resetAccum)
        commandList->clearTextureFloat(m_RenderTargets->DebugVizOutput, nvrhi::AllSubresources, nvrhi::Color(0, 0, 0, 0));
#endif


    m_ui.AccumulationIndex = m_AccumulationSampleIndex;

    // profile perf - only makes sense with high accumulation sample counts; only start counting after n-th after it stabilizes
    if( m_AccumulationSampleIndex < 16 )
    {
        m_BenchStart = std::chrono::high_resolution_clock::now( );
        m_BenchLast = m_BenchStart;
        m_BenchFrames = 0;
    } else if( m_AccumulationSampleIndex < m_AccumulationSampleTarget )
    {
        m_BenchFrames++;
        m_BenchLast = std::chrono::high_resolution_clock::now( );
    }

    // 'min' in non-realtime path here is to keep looping the last sample for debugging purposes!
    if( !m_ui.RealtimeMode )
        m_sampleIndex = min(m_AccumulationSampleIndex, m_AccumulationSampleTarget - 1);
    else
        m_sampleIndex = (m_ui.RealtimeNoise)?( m_frameIndex % 1024 ):0;     // actual sample index
}

void Sample::PostUpdatePathTracing( )
{
    m_AccumulationSampleIndex = std::min( m_AccumulationSampleIndex+1, m_AccumulationSampleTarget );
    
    //if (m_ui.ActualUseRTXDIPasses())
    m_RtxdiPass->EndFrame();
}

void Sample::UpdatePathTracerConstants( PathTracerConstants & constants, const PathTracerCameraData & cameraData )
{
    constants.camera = cameraData;

    constants.bounceCount = m_ui.BounceCount;
    constants.diffuseBounceCount = (m_ui.RealtimeMode)?(m_ui.RealtimeDiffuseBounceCount):(m_ui.ReferenceDiffuseBounceCount);
    constants.enablePerPixelJitterAA = m_ui.RealtimeMode == false && m_ui.AccumulationAA;
    constants.texLODBias = m_ui.TexLODBias;
    constants.sampleBaseIndex = m_sampleIndex * m_ui.ActualSamplesPerPixel();

    constants.subSampleCount = m_ui.ActualSamplesPerPixel();
    constants.invSubSampleCount = 1.0f / (float)m_ui.ActualSamplesPerPixel();

    constants.imageWidth = m_RenderTargets->OutputColor->getDesc().width;
    constants.imageHeight = m_RenderTargets->OutputColor->getDesc().height;

    constants.hasEnvMap = (m_ui.EnvironmentMapParams.Enabled)?(1):(0);

    // this is the dynamic luminance that when passed through current tonemapper with current exposure settings, produces the same 50% gray
    constants.preExposedGrayLuminance = m_ui.EnableToneMapping?(donut::math::luminance(m_ToneMappingPass->GetPreExposedGray(0))):(1.0f);

    if (m_ui.RealtimeMode)
        constants.fireflyFilterThreshold = (m_ui.RealtimeFireflyFilterEnabled)?(m_ui.RealtimeFireflyFilterThreshold*std::sqrtf(constants.preExposedGrayLuminance)*1e3f):(0.0f); // it does make sense to make the realtime variant dependent on avg luminance - just didn't have time to try it out yet
    else
        constants.fireflyFilterThreshold = (m_ui.ReferenceFireflyFilterEnabled)?(m_ui.ReferenceFireflyFilterThreshold*std::sqrtf(constants.preExposedGrayLuminance)*1e3f):(0.0f); // making it exposure-adaptive breaks determinism with accumulation (because there's a feedback loop), so that's disabled
    constants.useReSTIRDI = m_ui.ActualUseReSTIRDI();
    constants.useReSTIRGI = m_ui.ActualUseReSTIRGI();
    constants.denoiserRadianceClampK = m_ui.DenoiserRadianceClampK;

    // no stable planes by default
    constants.denoisingEnabled = m_ui.RealtimeMode && m_ui.RealtimeDenoiser;
    constants.suppressPrimaryNEE = m_ui.SuppressPrimaryNEE;

    constants.activeStablePlaneCount            = m_ui.StablePlanesActiveCount;
    constants.maxStablePlaneVertexDepth         = std::min( std::min( (uint)m_ui.StablePlanesMaxVertexDepth, cStablePlaneMaxVertexIndex ), (uint)m_ui.BounceCount );
    constants.allowPrimarySurfaceReplacement    = m_ui.AllowPrimarySurfaceReplacement;
    //constants.stablePlanesSkipIndirectNoiseP0   = m_ui.ActualSkipIndirectNoisePlane0();
    constants.stablePlanesSplitStopThreshold    = m_ui.StablePlanesSplitStopThreshold;
    constants.stablePlanesMinRoughness          = m_ui.StablePlanesMinRoughness;
    constants.enableShaderExecutionReordering   = m_ui.ShaderExecutionReordering?1:0;
    constants.stablePlanesSuppressPrimaryIndirectSpecularK  = m_ui.StablePlanesSuppressPrimaryIndirectSpecular?m_ui.StablePlanesSuppressPrimaryIndirectSpecularK:0.0f;
    constants.stablePlanesAntiAliasingFallthrough = m_ui.StablePlanesAntiAliasingFallthrough;
    constants.enableRussianRoulette             = (m_ui.EnableRussianRoulette)?(1):(0);
    constants.frameIndex                        = GetFrameIndex();
    constants.genericTSLineStride               = GenericTSComputeLineStride(constants.imageWidth, constants.imageHeight);
    constants.genericTSPlaneStride              = GenericTSComputePlaneStride(constants.imageWidth, constants.imageHeight);

    constants.NEEEnabled                        = m_ui.UseNEE;
    constants.NEEDistantType                    = m_ui.NEEDistantType;
    constants.NEEDistantCandidateSamples        = m_ui.NEEDistantCandidateSamples;        
    constants.NEEDistantFullSamples             = m_ui.NEEDistantFullSamples;    
    constants.NEEMinRadianceThreshold           = m_ui.NEEMinRadianceThresholdMul * constants.preExposedGrayLuminance;
    constants.NEELocalType                      = m_ui.NEELocalType;
    constants.NEELocalCandidateSamples          = m_ui.NEELocalCandidateSamples;    
    constants.NEELocalFullSamples               = m_ui.NEELocalFullSamples;
    constants.NEEBoostSamplingOnDominantPlane   = m_ui.NEEBoostSamplingOnDominantPlane;
}


void Sample::RtxdiSetupFrame(nvrhi::IFramebuffer* framebuffer, PathTracerCameraData cameraData, uint2 renderDims)
{	
    const bool envMapPresent = m_ui.EnvironmentMapParams.Enabled;

    RtxdiBridgeParameters bridgeParameters;
	bridgeParameters.frameIndex = GetFrameIndex();
	bridgeParameters.frameDims = renderDims;
	bridgeParameters.cameraPosition = m_Camera.GetPosition();
	bridgeParameters.userSettings = m_ui.RTXDI;
    bridgeParameters.usingLightSampling = m_ui.ActualUseReSTIRDI() || (m_ui.NEELocalFullSamples > 0 && m_ui.UseNEE);
    bridgeParameters.usingReGIR = m_ui.ActualUseReSTIRDI() || (m_ui.NEELocalType == 2 && (m_ui.NEELocalFullSamples > 0 && m_ui.UseNEE));

    bridgeParameters.userSettings.restirDI.initialSamplingParams.environmentMapImportanceSampling = m_ui.EnvironmentMapParams.Enabled;

	m_RtxdiPass->PrepareResources(m_CommandList, *m_RenderTargets, envMapPresent ? m_EnvMapBaker : nullptr, m_EnvMapSceneParams, 
        m_Scene, bridgeParameters, m_BindingLayout );
 }

void Sample::Render(nvrhi::IFramebuffer* framebuffer)
{
    const auto& fbinfo = framebuffer->getFramebufferInfo();
    m_DisplaySize = m_RenderSize = int2(fbinfo.width, fbinfo.height);
    float lodBias = 0.f;

    if( m_ui.FPSLimiter > 0 )
        g_FPSLimiter.FramerateLimit( m_ui.FPSLimiter );

    if (m_Scene == nullptr)
    {
        assert( false ); // TODO: handle separately, just display pink color
        return; 
    }

    bool needNewPasses = false;
    bool needNewBindings = false;

#ifdef STREAMLINE_INTEGRATION
    // DLSS-G Setup

    // If DLSS-G has been turned off, then we tell tell SL to clean it up expressly 
    if (SLWrapper::Get().GetDLSSGLastEnable() && m_ui.DLSSG_mode == sl::DLSSGMode::eOff) {
        SLWrapper::Get().CleanupDLSSG();
    }

    // This is where DLSS-G is toggled On and Off (using dlssgConst.mode) and where we set DLSS-G parameters.  
    auto dlssgConst = sl::DLSSGOptions{};
    dlssgConst.mode = m_ui.DLSSG_mode;

    // This is where we query DLSS-G minimum swapchain size
    if (SLWrapper::Get().GetDLSSGAvailable())
    {
        uint64_t estimatedVramUsage;
        sl::DLSSGStatus status;
        int fps_multiplier;
        int minSize = 0;
        if (SLWrapper::Get().QueryDLSSGState(estimatedVramUsage, fps_multiplier, status, minSize))
            m_ui.DLSSG_multiplier = fps_multiplier;

        SLWrapper::Get().SetDLSSGOptions(dlssgConst);
    }

       // Setup Reflex
    auto reflexConst = sl::ReflexOptions{};
    reflexConst.mode = (sl::ReflexMode)m_ui.REFLEX_Mode;
    reflexConst.useMarkersToOptimize = true;
    reflexConst.virtualKey = VK_F13;
    reflexConst.frameLimitUs = m_ui.REFLEX_CapedFPS == 0 ? 0 : int(1000000. / m_ui.REFLEX_CapedFPS);
    SLWrapper::Get().SetReflexConsts(reflexConst);

    bool flashIndicatorDriverAvailable;
    SLWrapper::Get().QueryReflexStats(m_ui.REFLEX_LowLatencyAvailable, flashIndicatorDriverAvailable, m_ui.REFLEX_Stats);
    SLWrapper::Get().SetReflexFlashIndicator(flashIndicatorDriverAvailable);

    //Make sure DLSS is available
    if ((m_ui.RealtimeAA == 2 || m_ui.RealtimeAA == 3) && !SLWrapper::Get().GetDLSSAvailable())
    {
        log::warning("DLSS antialiasing is not available. Switching to TAA. ");
        m_ui.RealtimeAA = 1;
    }

    // Reset DLSS vars if we stop using it
    bool changeToDLSSMode = (m_ui.RealtimeAA == 2 || m_ui.RealtimeAA == 3) && m_ui.DLSS_Last_RealtimeAA != m_ui.RealtimeAA;
    if (changeToDLSSMode || m_ui.DLSS_Mode == sl::DLSSMode::eOff) {
        m_ui.DLSS_Last_Mode = SampleUIData::DLSS_ModeDefault;
        m_ui.DLSS_Mode = SampleUIData::DLSS_ModeDefault;
        m_ui.DLSS_Last_DisplaySize = { 0,0 };
    }

    m_ui.DLSS_Last_RealtimeAA = m_ui.RealtimeAA;

    // If we are using DLSS set its constants
    if (((m_ui.RealtimeAA == 2 || m_ui.RealtimeAA == 3) && m_ui.DLSS_Mode != sl::DLSSMode::eOff && m_ui.RealtimeMode))
    {
        if (SLWrapper::Get().GetDLSSAvailable())
        {
            sl::DLSSOptions dlssConstants = {};
            dlssConstants.mode = m_ui.DLSS_Mode;
            dlssConstants.outputWidth = m_DisplaySize.x;
            dlssConstants.outputHeight = m_DisplaySize.y;
            dlssConstants.colorBuffersHDR = sl::Boolean::eTrue;
            dlssConstants.sharpness = m_RecommendedDLSSSettings.sharpness;
            SLWrapper::Get().SetDLSSOptions(dlssConstants);
        }

        if(m_ui.RealtimeAA == 2) {
            // Check if we need to update the rendertarget size.
            bool DLSS_resizeRequired = (m_ui.DLSS_Mode != m_ui.DLSS_Last_Mode) || (m_DisplaySize.x != m_ui.DLSS_Last_DisplaySize.x) || (m_DisplaySize.y != m_ui.DLSS_Last_DisplaySize.y);
            if (DLSS_resizeRequired) {
                // Only quality, target width and height matter here
                SLWrapper::Get().QueryDLSSOptimalSettings(m_RecommendedDLSSSettings);

                if (m_RecommendedDLSSSettings.optimalRenderSize.x <= 0 || m_RecommendedDLSSSettings.optimalRenderSize.y <= 0) {
                    m_ui.RealtimeAA = 0;
                    m_ui.DLSS_Mode = SampleUIData::DLSS_ModeDefault;
                    m_RenderSize = m_DisplaySize;
                }
                else {
                    m_ui.DLSS_Last_Mode = m_ui.DLSS_Mode;
                    m_ui.DLSS_Last_DisplaySize = m_DisplaySize;
                }
            }

            m_RenderSize = m_RecommendedDLSSSettings.optimalRenderSize;
        }
        if (m_ui.RealtimeAA == 3) {
            m_ui.DLSS_Mode = sl::DLSSMode::eDLAA;
            m_RenderSize = m_DisplaySize;
        }
    }
    else 
    {
        if (SLWrapper::Get().GetDLSSAvailable())
        {
            sl::DLSSOptions dlssConstants = {};
            dlssConstants.mode = sl::DLSSMode::eOff;
            SLWrapper::Get().SetDLSSOptions(dlssConstants);
        }

        m_RenderSize = m_DisplaySize;
    }
#else
    const bool changeToDLSSMode = false;
#endif // #ifdef STREAMLINE_INTEGRATION

    if (m_View == nullptr)
    {
        m_View = std::make_shared<PlanarView>();
        m_ViewPrevious = std::make_shared<PlanarView>();
        m_ViewPrevious->SetViewport(nvrhi::Viewport(float(m_RenderSize.x), float(m_RenderSize.y)));
        m_View->SetViewport(nvrhi::Viewport(float(m_RenderSize.x), float(m_RenderSize.y)));
    }

    // Changes to material properties and settings might require a BLAS/TLAS or subInstanceBuffer rebuild (alpha tested/exclusion flags etc)
    // normally this should be a no-op.
    UpdateAccelStructs(m_CommandList);

    if( m_RenderTargets == nullptr || m_RenderTargets->IsUpdateRequired( m_RenderSize, m_DisplaySize ) || changeToDLSSMode)
    {
        m_RenderTargets = nullptr;
        m_BindingCache->Clear( );
        m_RenderTargets = std::make_unique<RenderTargets>( );
        m_RenderTargets->Init(GetDevice( ), m_RenderSize, m_DisplaySize, true, true, c_SwapchainCount);
        for (int i = 0; i < std::size(m_NRD); i++)
            m_NRD[i] = nullptr;

        needNewPasses = true;
    }

    if (m_ui.ShaderReloadRequested)
    {
        m_ui.ShaderReloadRequested = false;
        m_ShaderFactory->ClearCache();
        needNewPasses = true;
    }

    bool exposureResetRequired = false;

    if (m_ui.NRDModeChanged)
    {
        needNewPasses = true;
        for (int i = 0; i < std::size(m_NRD); i++)
            m_NRD[i] = nullptr;
    }

    if (needNewPasses)
    {
        CreateRenderPasses(exposureResetRequired);
    }

    PathTracerCameraData cameraData;
    {
        m_CommandList->open();

        // Update input lighting, environment map, etc.
        UpdateLighting(m_CommandList, needNewBindings);

        // Update camera data used by the path tracer & other systems
        UpdateViews(framebuffer);
        {   // TODO: pull all this to BridgeCamera - sizeX and sizeY are already inputs so we just need to pass projMatrix
            nvrhi::Viewport viewport = m_View->GetViewport();
            float2 jitter = m_View->GetPixelOffset();
            float4x4 projMatrix = m_View->GetProjectionMatrix();
            float2 viewSize = { viewport.maxX - viewport.minX, viewport.maxY - viewport.minY };
            float aspectRatio = viewSize.x / viewSize.y;
            bool rowMajor = true;
            float tanHalfFOVY = 1.0f / ((rowMajor) ? (projMatrix.m_data[1 * 4 + 1]) : (projMatrix.m_data[1 + 1 * 4]));
            float fovY = atanf(tanHalfFOVY) * 2.0f;
            cameraData = BridgeCamera(uint(viewSize.x), uint(viewSize.y), m_Camera.GetPosition(), m_Camera.GetDir(), m_Camera.GetUp(), fovY, m_CameraZNear, 1e7f, m_ui.CameraFocalDistance, m_ui.CameraAperture, jitter);
        }

        // Early init for RTXDI
        if (needNewPasses || needNewBindings || m_BindingSet == nullptr)
            m_RtxdiPass->Reset();
        RtxdiSetupFrame(framebuffer, cameraData, uint2(m_RenderTargets->OutputColor->getDesc().width, m_RenderTargets->OutputColor->getDesc().height));

        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);
    }

	if( needNewPasses || needNewBindings || m_BindingSet == nullptr )
    {
        // WARNING: this must match the layout of the m_BindingLayout (or switch to CreateBindingSetAndLayout)
        // Fixed resources that do not change between binding sets
        nvrhi::BindingSetDesc bindingSetDescBase;
        bindingSetDescBase.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
            nvrhi::BindingSetItem::PushConstants(1, sizeof(SampleMiniConstants)),
            nvrhi::BindingSetItem::RayTracingAccelStruct(0, m_TopLevelAS),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(1, m_SubInstanceBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(2, m_Scene->GetInstanceBuffer()),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(3, m_Scene->GetGeometryBuffer()),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(4, m_Scene->GetGeometryDebugBuffer()),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(5, m_Scene->GetMaterialBuffer()),
            nvrhi::BindingSetItem::Texture_SRV(6, m_EnvMapBaker->GetEnvMapCube()), //m_EnvironmentMap->IsEnvMapLoaded() ? m_EnvironmentMap->GetEnvironmentMap() : m_CommonPasses->m_BlackTexture),
            nvrhi::BindingSetItem::Texture_SRV(7, m_EnvMapBaker->GetImportanceSampling()->GetImportanceMap()), //m_EnvironmentMap->IsImportanceMapLoaded() ? m_EnvironmentMap->GetImportanceMap() : m_CommonPasses->m_BlackTexture),
            nvrhi::BindingSetItem::TypedBuffer_SRV(8, m_EnvMapBaker->GetImportanceSampling()->GetPresampledBuffer()),
            //nvrhi::BindingSetItem::Texture_SRV(10, m_EnvMapBaker->GetEnvMapCube() ? m_EnvMapBaker->GetEnvMapCube() : m_CommonPasses->m_BlackTexture),
#if USE_PRECOMPUTED_SOBOL_BUFFER
            nvrhi::BindingSetItem::TypedBuffer_SRV(42, m_PrecomputedSobolBuffer),
#endif
            nvrhi::BindingSetItem::Sampler(0, m_CommonPasses->m_AnisotropicWrapSampler),
            nvrhi::BindingSetItem::Sampler(1, m_EnvMapBaker->GetEnvMapCubeSampler()),
            nvrhi::BindingSetItem::Sampler(2, m_EnvMapBaker->GetImportanceSampling()->GetImportanceMapSampler()),
            nvrhi::BindingSetItem::Texture_UAV(0, m_RenderTargets->OutputColor),
            nvrhi::BindingSetItem::Texture_UAV(4, m_RenderTargets->Throughput),
            nvrhi::BindingSetItem::Texture_UAV(5, m_RenderTargets->ScreenMotionVectors),
            nvrhi::BindingSetItem::Texture_UAV(6, m_RenderTargets->Depth),
            nvrhi::BindingSetItem::Texture_UAV(31, m_RenderTargets->DenoiserViewspaceZ),              
            nvrhi::BindingSetItem::Texture_UAV(32, m_RenderTargets->DenoiserMotionVectors),      
            nvrhi::BindingSetItem::Texture_UAV(33, m_RenderTargets->DenoiserNormalRoughness),         
            nvrhi::BindingSetItem::Texture_UAV(34, m_RenderTargets->DenoiserDiffRadianceHitDist),     
            nvrhi::BindingSetItem::Texture_UAV(35, m_RenderTargets->DenoiserSpecRadianceHitDist),     
            nvrhi::BindingSetItem::Texture_UAV(36, m_RenderTargets->DenoiserDisocclusionThresholdMix),
            nvrhi::BindingSetItem::Texture_UAV(37, m_RenderTargets->CombinedHistoryClampRelax),
            nvrhi::BindingSetItem::Texture_UAV(50, m_RenderTargets->DebugVizOutput),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(51, m_Feedback_Buffer_Gpu),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(52, m_DebugLineBufferCapture),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(53, m_DebugDeltaPathTree_Gpu),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(54, m_DebugDeltaPathTreeSearchStack),
            nvrhi::BindingSetItem::Texture_UAV(60, m_RenderTargets->SecondarySurfacePositionNormal),
            nvrhi::BindingSetItem::Texture_UAV(61, m_RenderTargets->SecondarySurfaceRadiance)
            
            // RTXDI for Local light sampling
            , nvrhi::BindingSetItem::TypedBuffer_UAV(62, m_RtxdiPass->GetRTXDIResources()->GetRisLightDataBuffer() )        // u_LL_RisLightDataBuffer
            , nvrhi::BindingSetItem::StructuredBuffer_SRV(62, m_RtxdiPass->GetRTXDIResources()->GetLightDataBuffer() )      // t_LL_LightDataBuffer
            , nvrhi::BindingSetItem::TypedBuffer_UAV(63, m_RtxdiPass->GetRTXDIResources()->GetRisBuffer() )                 // u_LL_RisBuffer
            , nvrhi::BindingSetItem::ConstantBuffer(2, m_RtxdiPass->GetRTXDIConstants()),                                   // g_LL_RtxdiBridgeConst
        };

        // NVAPI shader extension UAV is only applicable on DX12
        if (GetDevice()->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
        {
            bindingSetDescBase.bindings.push_back(
                nvrhi::BindingSetItem::TypedBuffer_UAV(NV_SHADER_EXTN_SLOT_NUM, nullptr));
        }

        // Main raytracing & etc binding set
		{
            nvrhi::BindingSetDesc bindingSetDesc;
            
            bindingSetDesc.bindings = bindingSetDescBase.bindings;

            bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::Texture_UAV(40, m_RenderTargets->StablePlanesHeader));
            bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::StructuredBuffer_UAV(42, m_RenderTargets->StablePlanesBuffer));
            bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::Texture_UAV(44, m_RenderTargets->StableRadiance));
            bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::StructuredBuffer_UAV(45, m_RenderTargets->SurfaceDataBuffer));
                    
            m_BindingSet = GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);
        }

        {
            nvrhi::BindingSetDesc lineBindingSetDesc;
            lineBindingSetDesc.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
                nvrhi::BindingSetItem::Texture_SRV(0, m_RenderTargets->Depth)
            };
            m_LinesBindingSet = GetDevice()->createBindingSet(lineBindingSetDesc, m_LinesBindingLayout);
    
            nvrhi::GraphicsPipelineDesc psoDesc;
            psoDesc.VS = m_LinesVertexShader;
            psoDesc.PS = m_LinesPixelShader;
            psoDesc.inputLayout = m_LinesInputLayout;
            psoDesc.bindingLayouts = { m_LinesBindingLayout };
            psoDesc.primType = nvrhi::PrimitiveType::LineList;
            psoDesc.renderState.depthStencilState.depthTestEnable = false;
            psoDesc.renderState.blendState.targets[0].enableBlend().setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
                .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha).setSrcBlendAlpha(nvrhi::BlendFactor::Zero).setDestBlendAlpha(nvrhi::BlendFactor::One);

            m_LinesPipeline = GetDevice()->createGraphicsPipeline(psoDesc, framebuffer);

            // nvrhi::ComputePipelineDesc psoCSDesc;
            // psoCSDesc.bindingLayouts = { m_BindingLayout };
            // psoCSDesc.CS = m_LinesAddExtraComputeShader;
            // m_LinesAddExtraPipeline = GetDevice()->createComputePipeline(psoCSDesc);
        }
    }
        
    if (m_ui.EnableToneMapping)
    {
        m_ToneMappingPass->PreRender(m_ui.ToneMappingParams);
    }

    m_CommandList->open();

    PreUpdatePathTracing(needNewPasses, m_CommandList);

    // I suppose we need to clear depth for right-click picking at least
    m_RenderTargets->Clear( m_CommandList );

    SampleConstants & constants = m_currentConstants; memset(&constants, 0, sizeof(constants));
    SampleMiniConstants miniConstants = { uint4(0, 0, 0, 0) }; // accessible but unused in path tracing at the moment
    if( m_Scene == nullptr )
    {
        m_CommandList->clearTextureFloat( m_RenderTargets->OutputColor, nvrhi::AllSubresources, nvrhi::Color( 1, 1, 0, 0 ) );
        m_CommandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));
    }
    else
    {
        m_Scene->Refresh(m_CommandList, GetFrameIndex());
        BuildOpacityMicromaps(m_CommandList, GetFrameIndex());
        BuildTLAS(m_CommandList, GetFrameIndex());
        TransitionMeshBuffersToReadOnly(m_CommandList);

        UpdatePathTracerConstants(constants.ptConsts, cameraData);
        constants.ambientColor = float4(0.0f);
        constants.materialCount = (uint)m_Scene->GetSceneGraph()->GetMaterials().size();
        constants._padding1 = 0;
        constants._padding2 = 0;

        if (m_ui.EnvironmentMapParams.Enabled)
        {
            float intensity = m_ui.EnvironmentMapParams.Intensity / c_EnvMapRadianceScale;
            m_EnvMapSceneParams.ColorMultiplier = m_ui.EnvironmentMapParams.TintColor * intensity;

            float3 rotationInRadians = donut::math::radians(m_ui.EnvironmentMapParams.RotationXYZ);
            affine3 rotationTransform = donut::math::rotation(rotationInRadians);
            affine3 inverseTransform = inverse(rotationTransform);
            affineToColumnMajor(rotationTransform, m_EnvMapSceneParams.Transform);
            affineToColumnMajor(inverseTransform, m_EnvMapSceneParams.InvTransform);
            m_EnvMapSceneParams.padding0 = 0;

            constants.envMapSceneParams = m_EnvMapSceneParams;
            constants.envMapImportanceSamplingParams = m_EnvMapBaker->GetImportanceSampling()->GetShaderParams();
        }

        m_View->FillPlanarViewConstants(constants.view);
        m_ViewPrevious->FillPlanarViewConstants(constants.previousView);

        // add lights
        int lightCount = m_Lights.size() < PTDEMO_LIGHT_CONSTANTS_COUNT ? 
            (int)m_Lights.size() : PTDEMO_LIGHT_CONSTANTS_COUNT;

        constants.lightConstantsCount = lightCount;

        for(int i = 0; i < lightCount; i++)
		{
            m_Lights[i]->FillLightConstants(constants.lights[i]);
		}

        constants.debug = {};
        constants.debug.pick = m_Pick || m_ui.ContinuousDebugFeedback;
        constants.debug.pickX = (constants.debug.pick)?(m_ui.DebugPixel.x):(-1);
        constants.debug.pickY = (constants.debug.pick)?(m_ui.DebugPixel.y):(-1);
        constants.debug.debugLineScale = (m_ui.ShowDebugLines)?(m_ui.DebugLineScale):(0.0f);
        constants.debug.showWireframe = m_ui.ShowWireframe;
        constants.debug.debugViewType = (int)m_ui.DebugView;
        constants.debug.debugViewStablePlaneIndex = (m_ui.StablePlanesActiveCount==1)?(0):(m_ui.DebugViewStablePlaneIndex);
#if ENABLE_DEBUG_DELTA_TREE_VIZUALISATION
        constants.debug.exploreDeltaTree = (m_ui.ShowDeltaTree && constants.debug.pick)?(1):(0);
#else
        constants.debug.exploreDeltaTree = false;
#endif
        constants.debug.imageWidth = constants.ptConsts.imageWidth;
        constants.debug.imageHeight = constants.ptConsts.imageHeight;
        constants.debug.mouseX = m_ui.MousePos.x;
        constants.debug.mouseY = m_ui.MousePos.y;

        constants.denoisingHitParamConsts = { m_ui.ReblurSettings.hitDistanceParameters.A, m_ui.ReblurSettings.hitDistanceParameters.B,
                                              m_ui.ReblurSettings.hitDistanceParameters.C, m_ui.ReblurSettings.hitDistanceParameters.D };

		m_CommandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));

		//if (m_ui.ActualUseRTXDIPasses())
        m_RtxdiPass->BeginFrame(m_CommandList, *m_RenderTargets, m_BindingLayout, m_BindingSet);
        
        PathTrace(framebuffer, constants);

        Denoise(framebuffer);

        // SET STREAMLINE CONSTANTS
#ifdef STREAMLINE_INTEGRATION
        {
            // This section of code updates the streamline constants every frame. Regardless of whether we are utilising the streamline plugins, as long as streamline is in use, we must set its constants.

            constexpr float zNear = 0.1f;
            constexpr float zFar = 200.f;

            affine3 viewReprojection = m_View->GetChildView(ViewType::PLANAR, 0)->GetInverseViewMatrix() * m_ViewPrevious->GetViewMatrix();
            float4x4 reprojectionMatrix = inverse(m_View->GetProjectionMatrix(false)) * affineToHomogeneous(viewReprojection) * m_ViewPrevious->GetProjectionMatrix(false);
            float aspectRatio = float(m_RenderSize.x) / float(m_RenderSize.y);
            float4x4 projection = perspProjD3DStyleReverse(dm::radians(m_CameraVerticalFOV), aspectRatio, zNear);

            //float2 jitterOffset = std::dynamic_pointer_cast<PlanarView, IView>(m_View)->GetPixelOffset();
            float2 jitterOffset = ComputeCameraJitter(m_sampleIndex);

            sl::Constants slConstants = {};
            slConstants.cameraAspectRatio = aspectRatio;
            slConstants.cameraFOV = dm::radians(m_CameraVerticalFOV);
            slConstants.cameraFar = zFar;
            slConstants.cameraMotionIncluded = sl::Boolean::eTrue;
            slConstants.cameraNear = zNear;
            slConstants.cameraPinholeOffset = { 0.f, 0.f };
            slConstants.cameraPos = make_sl_float3(m_Camera.GetPosition());
            slConstants.cameraFwd = make_sl_float3(m_Camera.GetDir());
            slConstants.cameraUp = make_sl_float3(m_Camera.GetUp());
            slConstants.cameraRight = make_sl_float3(normalize(cross(m_Camera.GetDir(), m_Camera.GetUp())));
            slConstants.cameraViewToClip = make_sl_float4x4(projection);
            slConstants.clipToCameraView = make_sl_float4x4(inverse(projection));
            slConstants.clipToPrevClip = make_sl_float4x4(reprojectionMatrix);
            slConstants.depthInverted = m_View->IsReverseDepth() ? sl::Boolean::eTrue : sl::Boolean::eFalse;
            slConstants.jitterOffset = make_sl_float2(jitterOffset);
            slConstants.mvecScale = { 1.0f / m_RenderSize.x , 1.0f / m_RenderSize.y }; // This are scale factors used to normalize mvec (to -1,1) and donut has mvec in pixel space
            slConstants.prevClipToClip = make_sl_float4x4(inverse(reprojectionMatrix));
            slConstants.reset = needNewPasses ? sl::Boolean::eTrue : sl::Boolean::eFalse;
            slConstants.motionVectors3D = sl::Boolean::eFalse;
            slConstants.motionVectorsInvalidValue = FLT_MIN;

            SLWrapper::Get().SetSLConsts(slConstants);
        }

        // TAG STREAMLINE RESOURCES
        SLWrapper::Get().TagResources_General(m_CommandList,
            m_View->GetChildView(ViewType::PLANAR, 0),
            m_RenderTargets->ScreenMotionVectors,
            m_RenderTargets->Depth,
            m_RenderTargets->PreUIColor);

        // TAG STREAMLINE RESOURCES
        SLWrapper::Get().TagResources_DLSS_NIS(m_CommandList,
            m_View->GetChildView(ViewType::PLANAR, 0),
            m_RenderTargets->ProcessedOutputColor,
            m_RenderTargets->OutputColor);

#endif // #ifdef STREAMLINE_INTEGRATION
      
        PostProcessAA(framebuffer);
    }


    nvrhi::ITexture* finalColor = m_ui.RealtimeMode ? m_RenderTargets->ProcessedOutputColor : m_RenderTargets->AccumulatedRadiance;

    //Tone Mapping
    if (m_ui.EnableToneMapping)
    {
        donut::engine::PlanarView fullscreenView = *m_View;
        nvrhi::Viewport windowViewport(float(m_DisplaySize.x), float(m_DisplaySize.y));
        fullscreenView.SetViewport(windowViewport);
        fullscreenView.UpdateCache();

        if (m_ToneMappingPass->Render(m_CommandList, fullscreenView, finalColor))
        {
            // first run tonemapper can close command list - we have to re-upload volatile constants then
            m_CommandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));
        }

        finalColor = m_RenderTargets->LdrColor;
    }

    //m_PostProcess->Render(m_CommandList, finalColor);

    m_CommandList->beginMarker("Blit");
    m_CommonPasses->BlitTexture(m_CommandList, framebuffer, finalColor, m_BindingCache.get());
    m_CommandList->endMarker();

    // this allows path tracer to easily output debug viz or error metrics into a separate buffer that gets applied after tone-mapping
    m_PostProcess->Apply(m_CommandList, PostProcess::RenderPassType::Debug_BlendDebugViz, m_ConstantBuffer, miniConstants, framebuffer, *m_RenderTargets, finalColor);

    if (m_ui.ShowDebugLines == true)
    {
        m_CommandList->beginMarker("Debug Lines");

        // // this copies over additional (CPU written) lines!
        // {
        //     nvrhi::ComputeState state;
        //     state.bindings = { m_BindingSet };
        //     state.pipeline = m_LinesAddExtraPipeline;
        //     m_CommandList->setComputeState(state);
        //     const dm::uint  threads = 256;
        //     const dm::uint2 dispatchSize = dm::uint2(1, 1);
        //     m_CommandList->dispatch(dispatchSize.x, dispatchSize.y);
        // }

        // this draws the debug lines - should be the only actual rasterization around :)
        {
            nvrhi::GraphicsState state;
            state.bindings = { m_LinesBindingSet };
            state.vertexBuffers = { {m_DebugLineBufferDisplay, 0, 0} };
            state.pipeline = m_LinesPipeline;
            state.framebuffer = framebuffer;
            state.viewport.addViewportAndScissorRect(fbinfo.getViewport());

            m_CommandList->setGraphicsState(state);

            nvrhi::DrawArguments args;
            args.vertexCount = m_FeedbackData.lineVertexCount;
            m_CommandList->draw(args);
        }

        if (m_CPUSideDebugLines.size() > 0)
        {
            // using m_DebugLineBufferCapture for direct drawing here
            m_CommandList->writeBuffer( m_DebugLineBufferCapture, m_CPUSideDebugLines.data(), sizeof(DebugLineStruct) * m_CPUSideDebugLines.size() );

            nvrhi::GraphicsState state;
            state.bindings = { m_LinesBindingSet };
            state.vertexBuffers = { {m_DebugLineBufferCapture, 0, 0} };
            state.pipeline = m_LinesPipeline;
            state.framebuffer = framebuffer;
            state.viewport.addViewportAndScissorRect(fbinfo.getViewport());

            m_CommandList->setGraphicsState(state);

            nvrhi::DrawArguments args;
            args.vertexCount = (uint32_t)m_CPUSideDebugLines.size();
            m_CommandList->draw(args);
        }

        m_CommandList->endMarker(); 
    }
    m_CPUSideDebugLines.clear();

    if( m_ui.ContinuousDebugFeedback || m_Pick )
    {
        m_CommandList->copyBuffer(m_Feedback_Buffer_Cpu, 0, m_Feedback_Buffer_Gpu, 0, sizeof(DebugFeedbackStruct) * 1);
        m_CommandList->copyBuffer(m_DebugLineBufferDisplay, 0, m_DebugLineBufferCapture, 0, sizeof(DebugLineStruct) * MAX_DEBUG_LINES );
        m_CommandList->copyBuffer(m_DebugDeltaPathTree_Cpu, 0, m_DebugDeltaPathTree_Gpu, 0, sizeof(DeltaTreeVizPathVertex) * cDeltaTreeVizMaxVertices);
	}

    nvrhi::ITexture* framebufferTexture = framebuffer->getDesc().colorAttachments[0].texture;
    //m_CommandList->copyTexture(m_RenderTargets->PreUIColor, nvrhi::TextureSlice(), framebufferTexture, nvrhi::TextureSlice());

	m_CommandList->close();
	GetDevice()->executeCommandList(m_CommandList);

    // resolve right click picking and debug info
    if (m_ui.ContinuousDebugFeedback || m_Pick)
    {
        GetDevice()->waitForIdle();
        void* pData = GetDevice()->mapBuffer(m_Feedback_Buffer_Cpu, nvrhi::CpuAccessMode::Read);
        assert(pData);
        memcpy(&m_FeedbackData, pData, sizeof(DebugFeedbackStruct)* 1);
        GetDevice()->unmapBuffer(m_Feedback_Buffer_Cpu);

        pData = GetDevice()->mapBuffer(m_DebugDeltaPathTree_Cpu, nvrhi::CpuAccessMode::Read);
        assert(pData);
        memcpy(&m_DebugDeltaPathTree, pData, sizeof(DeltaTreeVizPathVertex) * cDeltaTreeVizMaxVertices);
        GetDevice()->unmapBuffer(m_DebugDeltaPathTree_Cpu);

        if (m_Pick)
            m_ui.SelectedMaterial = FindMaterial(int(m_FeedbackData.pickedMaterialID));


        m_Pick = false;
    }

    auto DumpScreenshot = [this](nvrhi::ITexture* framebufferTexture, const char* fileName, bool exitOnCompletion)
    {
        const bool success = SaveTextureToFile(GetDevice(), m_CommonPasses.get(), framebufferTexture, nvrhi::ResourceStates::Common, fileName);

        if (exitOnCompletion)
        {
            if (success)
            {
                donut::log::info("Image saved successfully %s. Exiting.", fileName);
                std::exit(0);
            }
            else
            {
                donut::log::fatal("Unable to save image %s. Exiting.", fileName);
                std::exit(1);
            }
        }
    };

    if (!m_ui.ScreenshotFileName.empty())
    {
        DumpScreenshot(framebufferTexture, m_ui.ScreenshotFileName.c_str(), false /*exitOnCompletion*/);
        m_ui.ScreenshotFileName = "";
    }

	if (!m_cmdLine.screenshotFileName.empty() && (m_cmdLine.screenshotFrameIndex == GetFrameIndex()))
	{
        DumpScreenshot(framebufferTexture, m_cmdLine.screenshotFileName.c_str(), true /*exitOnCompletion*/);
	}

    if (m_ui.ExperimentalPhotoModeScreenshot)
    {
        DenoisedScreenshot( framebufferTexture );
        m_ui.ExperimentalPhotoModeScreenshot = false;
    }

    if (m_TemporalAntiAliasingPass != nullptr)
        m_TemporalAntiAliasingPass->AdvanceFrame();

	std::swap(m_View, m_ViewPrevious);
	GetDeviceManager()->SetVsyncEnabled(m_ui.EnableVsync);

    PostUpdatePathTracing();
}

std::shared_ptr<donut::engine::Material> Sample::FindMaterial(int materialID) const
{
    // if slow switch to map
    for (const auto& material : m_Scene->GetSceneGraph()->GetMaterials())
        if (material->materialID == materialID)
            return material;
    return nullptr;
}

void Sample::PathTrace(nvrhi::IFramebuffer* framebuffer, const SampleConstants & constants)
{
    //m_CommandList->beginMarker("MainRendering"); <- removed (for now) since added hierarchy reduces readability

    bool useStablePlanes = m_ui.ActualUseStablePlanes();

    nvrhi::rt::State state;

    nvrhi::rt::DispatchRaysArguments args;
    nvrhi::Viewport viewport = m_View->GetViewport();
    uint32_t width = (uint32_t)(viewport.maxX - viewport.minX);
    uint32_t height = (uint32_t)(viewport.maxY - viewport.minY);
    args.width = width;
    args.height = height;
    
    uint version;
    uint versionBase = (m_ui.DXRHitObjectExtension)?(3):(0);    // HitObjectExtension-enabled permutations are offset by 3 - see CreatePTPipeline; this will possibly go away once part of API (it can be dynamic)

    // default miniConstants
    SampleMiniConstants miniConstants = { uint4(0, 0, 0, 0) };

    if (useStablePlanes)
    {
        m_CommandList->beginMarker("PathTracePrePass");
        int version = versionBase+PATH_TRACER_MODE_BUILD_STABLE_PLANES;
        state.shaderTable = m_PTShaderTable[version];
        state.bindings = { m_BindingSet, m_DescriptorTable->GetDescriptorTable() };
        m_CommandList->setRayTracingState(state);
        m_CommandList->setPushConstants(&miniConstants, sizeof(miniConstants));
        m_CommandList->dispatchRays(args);
        m_CommandList->endMarker();

        m_CommandList->setBufferState(m_RenderTargets->StablePlanesBuffer, nvrhi::ResourceStates::UnorderedAccess);
        m_CommandList->commitBarriers();

        m_CommandList->beginMarker("VBufferExport");
		nvrhi::ComputeState state;
		state.bindings = { m_BindingSet, m_DescriptorTable->GetDescriptorTable() };
        state.pipeline = m_ExportVBufferPSO;
        m_CommandList->setComputeState(state);
        
		const dm::uint2 dispatchSize = { (width + NUM_COMPUTE_THREADS_PER_DIM - 1) / NUM_COMPUTE_THREADS_PER_DIM, (height + NUM_COMPUTE_THREADS_PER_DIM - 1) / NUM_COMPUTE_THREADS_PER_DIM };
        m_CommandList->setPushConstants(&miniConstants, sizeof(miniConstants));
		m_CommandList->dispatch(dispatchSize.x, dispatchSize.y);
		m_CommandList->endMarker();
    }

    version = (useStablePlanes ? PATH_TRACER_MODE_FILL_STABLE_PLANES : PATH_TRACER_MODE_REFERENCE) + versionBase;

    {
        m_CommandList->beginMarker("PathTrace");

#if EXPERIMENTAL_SUPERSAMPLE_LOOP_IN_SHADER==0
        for (uint subSampleIndex = 0; subSampleIndex < m_ui.ActualSamplesPerPixel(); subSampleIndex++)
#else
        uint subSampleIndex = 0;
#endif
        {
            // moved to before path tracer now to allow for each multipass supersampling pass to have separate presampling set as otherwise there are temporal issues
            m_CommandList->beginMarker("EnvMapPresample");
            m_EnvMapBaker->GetImportanceSampling()->ExecutePresampling(m_CommandList, m_EnvMapBaker->GetEnvMapCube(),m_currentConstants.ptConsts.sampleBaseIndex+subSampleIndex);
            m_CommandList->endMarker();

            //m_CommandList->beginMarker("PTSubPass");
            state.shaderTable = m_PTShaderTable[version];
            state.bindings = { m_BindingSet, m_DescriptorTable->GetDescriptorTable() };
            m_CommandList->setRayTracingState(state);

            // required to avoid race conditions in back to back dispatchRays 
            m_CommandList->setBufferState(m_RenderTargets->StablePlanesBuffer, nvrhi::ResourceStates::UnorderedAccess);
            m_CommandList->commitBarriers();

            // tell path tracer which subSampleIndex we're processing
            SampleMiniConstants miniConstants = { uint4(subSampleIndex, 0, 0, 0) }; 
            m_CommandList->setPushConstants(&miniConstants, sizeof(miniConstants));
            m_CommandList->dispatchRays(args);
            //m_CommandList->endMarker();
        }

        m_CommandList->endMarker();

        m_CommandList->setBufferState(m_RenderTargets->StablePlanesBuffer, nvrhi::ResourceStates::UnorderedAccess);
        m_CommandList->commitBarriers();
    }

    // this is a performance optimization where final 2 passes from ReSTIR DI and ReSTIR GI are combined to avoid loading GBuffer twice
    static bool enableFusedDIGIFinal = true;
    bool useFusedDIGIFinal = m_ui.ActualUseReSTIRDI() && m_ui.ActualUseReSTIRGI() && enableFusedDIGIFinal;

    if (m_ui.ActualUseReSTIRDI() || m_ui.ActualUseReSTIRGI())
        m_CommandList->beginMarker("RTXDI");

    if (m_ui.ActualUseReSTIRDI())
        // this does all ReSTIR DI magic including applying the final sample into correct radiance buffer (depending on denoiser state)
        m_RtxdiPass->Execute(m_CommandList, m_BindingSet, useFusedDIGIFinal);

    if (m_ui.ActualUseReSTIRGI())
        m_RtxdiPass->ExecuteGI(m_CommandList, m_BindingSet, useFusedDIGIFinal);

    if (useFusedDIGIFinal)
        m_RtxdiPass->ExecuteFusedDIGIFinal(m_CommandList, m_BindingSet);

    if (m_ui.ActualUseReSTIRDI() || m_ui.ActualUseReSTIRGI())
        m_CommandList->endMarker();
        
    if (useStablePlanes && (m_ui.DebugView >= DebugViewType::ImagePlaneRayLength && m_ui.DebugView <= DebugViewType::StablePlaneSpecHitDist || m_ui.DebugView == DebugViewType::StableRadiance) )
    {
        m_CommandList->beginMarker("StablePlanesDebugViz");
        nvrhi::TextureDesc tdesc = m_RenderTargets->OutputColor->getDesc();
        m_PostProcess->Apply(m_CommandList, PostProcess::ComputePassType::StablePlanesDebugViz, m_ConstantBuffer, miniConstants, m_BindingSet, m_BindingLayout, tdesc.width, tdesc.height);
        m_CommandList->endMarker();

    }

    //m_CommandList->endMarker(); // "MainRendering"
}

void Sample::Denoise(nvrhi::IFramebuffer* framebuffer)
{
    if( !m_ui.RealtimeMode || !m_ui.RealtimeDenoiser )
        return;

    //const auto& fbinfo = framebuffer->getFramebufferInfo();
    const char* passNames[] = { "Denoising plane 0", "Denoising plane 1", "Denoising plane 2", "Denoising plane 3" }; assert( std::size(m_NRD) <= std::size(passNames) );
    
    bool nrdUseRelax = m_ui.NRDMethod == NrdConfig::DenoiserMethod::RELAX;
    PostProcess::ComputePassType preparePassType = nrdUseRelax ? PostProcess::ComputePassType::RELAXDenoiserPrepareInputs : PostProcess::ComputePassType::REBLURDenoiserPrepareInputs;
    PostProcess::ComputePassType mergePassType = nrdUseRelax ? PostProcess::ComputePassType::RELAXDenoiserFinalMerge : PostProcess::ComputePassType::REBLURDenoiserFinalMerge;

    int maxPassCount = std::min(m_ui.StablePlanesActiveCount, (int)std::size(m_NRD));
    for (int pass = maxPassCount-1; pass >= 0; pass--)
    {
        m_CommandList->beginMarker(passNames[pass]);

        SampleMiniConstants miniConstants = { uint4((uint)pass, 0, 0, 0) };

        // Direct inputs to denoiser are reused between passes; there's redundant copies but it makes interfacing simpler
        nvrhi::TextureDesc tdesc = m_RenderTargets->OutputColor->getDesc();
        m_CommandList->beginMarker("PrepareInputs");
        m_PostProcess->Apply(m_CommandList, preparePassType, m_ConstantBuffer, miniConstants, m_BindingSet, m_BindingLayout, tdesc.width, tdesc.height);
        m_CommandList->endMarker();

        const float timeDeltaBetweenFrames = m_cmdLine.noWindow ? 1.f/60.f : -1.f; // if we're rendering without a window we set a fix timeDeltaBetweenFrames to ensure that output is deterministic
        bool enableValidation = m_ui.DebugView == DebugViewType::StablePlaneDenoiserValidation;
        if (nrdUseRelax)
        {
            m_NRD[pass]->RunDenoiserPasses(m_CommandList, *m_RenderTargets, pass, *m_View, *m_ViewPrevious, GetFrameIndex(), m_ui.NRDDisocclusionThreshold, m_ui.NRDDisocclusionThresholdAlternate, m_ui.NRDUseAlternateDisocclusionThresholdMix, timeDeltaBetweenFrames, enableValidation, &m_ui.RelaxSettings);
        }
        else
        {
            m_NRD[pass]->RunDenoiserPasses(m_CommandList, *m_RenderTargets, pass, *m_View, *m_ViewPrevious, GetFrameIndex(), m_ui.NRDDisocclusionThreshold, m_ui.NRDDisocclusionThresholdAlternate, m_ui.NRDUseAlternateDisocclusionThresholdMix, timeDeltaBetweenFrames, enableValidation, &m_ui.ReblurSettings);
        }

        m_CommandList->beginMarker("MergeOutputs");
        m_PostProcess->Apply(m_CommandList, mergePassType, pass, m_ConstantBuffer, miniConstants, m_RenderTargets->OutputColor, *m_RenderTargets, nullptr);
        m_CommandList->endMarker();

        m_CommandList->endMarker();
    }
}

void Sample::PostProcessAA(nvrhi::IFramebuffer* framebuffer)
{
    if (m_ui.RealtimeMode)
    {
        if (m_ui.RealtimeAA == 0)
        {
            // TODO: Remove Redundant copy for non AA case
            m_CommandList->copyTexture(m_RenderTargets->ProcessedOutputColor, nvrhi::TextureSlice(), m_RenderTargets->OutputColor, nvrhi::TextureSlice());
        }
        else if (m_ui.RealtimeAA == 1 && m_TemporalAntiAliasingPass != nullptr )
        {
            bool previousViewValid = (GetFrameIndex() != 0);

            m_CommandList->beginMarker("TAA");
            
            m_TemporalAntiAliasingPass->TemporalResolve(m_CommandList, m_ui.TemporalAntiAliasingParams, previousViewValid, *m_View, *m_View);

            // just copy back to output - redundant copy 
            //m_CommandList->copyTexture( m_RenderTargets->OutputColor, nvrhi::TextureSlice(), m_RenderTargets->ProcessedOutputColor, nvrhi::TextureSlice() );

            m_CommandList->endMarker();
        }
    }
    // Reference mode - run the accumulation pass.
    // Don't run it when the sample count has reached the target, just keep the previous output.
    // Otherwise, the frames that are rendered past the target all have the same RNG sequence,
    // and the output starts to converge to that single sample.
    else if (m_AccumulationSampleIndex < m_AccumulationSampleTarget)
    {
        const float accumulationWeight = 1.f / float(m_AccumulationSampleIndex + 1);

        m_AccumulationPass->Render(m_CommandList, *m_View, *m_View, accumulationWeight);
    }
#ifdef STREAMLINE_INTEGRATION
    if(m_ui.RealtimeMode && (m_ui.RealtimeAA == 2 || m_ui.RealtimeAA == 3) && m_ui.DLSS_Mode != sl::DLSSMode::eOff)
    {
        m_CommandList->setTextureState(m_RenderTargets->ProcessedOutputColor, nvrhi::TextureSubresourceSet(), nvrhi::ResourceStates::RenderTarget);
        m_CommandList->setTextureState(m_RenderTargets->OutputColor, nvrhi::TextureSubresourceSet(), nvrhi::ResourceStates::RenderTarget);
        m_CommandList->setTextureState(m_RenderTargets->ScreenMotionVectors, nvrhi::TextureSubresourceSet(), nvrhi::ResourceStates::RenderTarget);
        m_CommandList->setTextureState(m_RenderTargets->Depth, nvrhi::TextureSubresourceSet(), nvrhi::ResourceStates::RenderTarget);
        m_CommandList->setTextureState(m_RenderTargets->PreUIColor, nvrhi::TextureSubresourceSet(), nvrhi::ResourceStates::RenderTarget);
        m_CommandList->commitBarriers();

        SLWrapper::Get().EvaluateDLSS(m_CommandList);

        m_CommandList->clearState();
    }
#endif
}

bool Sample::CompressTextures()
{
    // if async needed, do something like std::thread([sytemCommand](){ system( sytemCommand.c_str() ); }).detach();

    std::string batchFileName = std::string(getenv("localappdata")) + "\\temp\\donut_compressor.bat";
    std::ofstream batchFile(batchFileName, std::ios_base::trunc);
    if (!batchFile.is_open())
    {
        log::message(log::Severity::Error, "Unable to write %s", batchFileName.c_str());
        return false;
    }

    std::string cmdLine;

    // prefix part
    //cmdLine += "echo off \n";
    cmdLine += "ECHO: \n";
    cmdLine += "WHERE nvtt_export \n";
    //cmdLine += "ECHO WHERE nvtt_export returns %ERRORLEVEL% \n";
    cmdLine += "IF %ERRORLEVEL% NEQ 0 (goto :error_tool)\n";
    cmdLine += "ECHO: \n";
    cmdLine += "ECHO nvtt_export exists in the Path, proceeding with compression (this might take a while!) \n";
    cmdLine += "ECHO: \n";

    uint i = 0; uint totalCount = (uint)m_UncompressedTextures.size();
    for (auto it : m_UncompressedTextures)
    {
        auto texture = it.first;
        std::string inPath = texture->path;
        std::string outPath = std::filesystem::path(inPath).replace_extension(".dds").string();
        
        cmdLine += "ECHO converting texture " + std::to_string(++i) + " " + " out of " + std::to_string( totalCount ) + "\n";

        cmdLine += "nvtt_export";
        cmdLine += " -f 23"; // this sets format BC7
        cmdLine += " ";

        if( it.second == TextureCompressionType::Normalmap )
        {
            // cmdLine += " --normal-filter 1";
            // cmdLine += " --normalize";
            cmdLine += " --no-mip-gamma-correct";
        }
        else if (it.second == TextureCompressionType::GenericLinear)
        {
            cmdLine += " --no-mip-gamma-correct";
        }
        else if (it.second == TextureCompressionType::GenericSRGB)
        {
            cmdLine += " --mip-gamma-correct";
        }
        // cmdLine += " -q 2";  // 2 is production quality, 1 is "normal" (default)
       
        cmdLine += " -o \"" + outPath;
        cmdLine += "\" \"" + inPath + "\"\n";
    }
    cmdLine += "ECHO:\n";
    cmdLine += "pause\n";
    cmdLine += "ECHO on\n";
    cmdLine += "exit /b 0\n";

    cmdLine += ":error_tool\n";
    cmdLine += "ECHO !! nvtt_export.exe not found !!\n";
    cmdLine += "ECHO nvtt_export.exe is part of the https://developer.nvidia.com/nvidia-texture-tools-exporter package - please install\n";
    cmdLine += "ECHO and add 'C:/Program Files/NVIDIA Corporation/NVIDIA Texture Tools' or equivalent to your PATH and retry!\n";
    cmdLine += "pause\n";
    cmdLine += "ECHO on\n";
    cmdLine += "exit /b 1\n";

    batchFile << cmdLine;
    batchFile.close();

    std::string startCmd = " \"\" " + batchFileName;
    std::system(startCmd.c_str());

    //remove(batchFileName.c_str());

    return true; // TODO: check error code
}

void Sample::DenoisedScreenshot(nvrhi::ITexture * framebufferTexture) const
{
    std::string noisyImagePath = (app::GetDirectoryWithExecutable( ) / "photo.bmp").string();

    auto execute = [&](const std::string & dn = "OptiX")
    {
	    const auto p1 = std::chrono::system_clock::now();
		const std::string timestamp = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(p1.time_since_epoch()).count());

		const std::string fileName = "photo-denoised_" + dn + "_" + timestamp + ".bmp";

        std::string denoisedImagePath = (app::GetDirectoryWithExecutable() / fileName).string();
        std::string denoiserPath = GetLocalPath("tools/denoiser_"+dn).string();
        if (denoiserPath == "")
        { assert(false); return; }        
        denoiserPath += "/denoiser.exe";

        if (!SaveTextureToFile(GetDevice(), m_CommonPasses.get(), framebufferTexture, nvrhi::ResourceStates::Common, noisyImagePath.c_str()))
        { assert(false); return; }

        std::string startCmd = "\"\"" + denoiserPath + "\"" + " -hdr 0 -i \"" + noisyImagePath + "\"" " -o \"" + denoisedImagePath + "\"\"";
        std::system(startCmd.c_str());
    
        std::string viewCmd = "\"\"" + denoisedImagePath + "\"\"";
        std::system(viewCmd.c_str());
    };
    execute("OptiX");
    execute("OIDN");
}

donut::math::float2 Sample::ComputeCameraJitter(uint frameIndex)
{
    if (!m_ui.RealtimeMode || m_ui.RealtimeAA == 0 || m_TemporalAntiAliasingPass == nullptr)
        return dm::float2(0,0);

    // we currently use TAA for jitter even when it's not used itself
    return m_TemporalAntiAliasingPass->GetCurrentPixelOffset();
}

#ifdef WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#else
int main(int __argc, const char** __argv)
#endif
{
    nvrhi::GraphicsAPI api = app::GetGraphicsAPIFromCommandLine(__argc, __argv);
    app::DeviceManager* deviceManager = app::DeviceManager::Create(api);

    app::DeviceCreationParameters deviceParams;
    // deviceParams.adapter = VrSystem::GetRequiredAdapter();
    deviceParams.backBufferWidth = 1920;
    deviceParams.backBufferHeight = 1080;
    deviceParams.swapChainSampleCount = 1;
    deviceParams.swapChainBufferCount = c_SwapchainCount;
    deviceParams.startFullscreen = false;
    deviceParams.vsyncEnabled = true;
    deviceParams.enableRayTracingExtensions = true;
    deviceParams.requireAdapterRaytracingSupport = true;
#if USE_DX11 || USE_DX12
    deviceParams.featureLevel = D3D_FEATURE_LEVEL_12_1;
#endif
#ifdef _DEBUG
    deviceParams.enableDebugRuntime = true;
    deviceParams.enableNvrhiValidationLayer = true;
    deviceParams.enableGPUValidation = false; // <- this severely impact performance but is good to enable from time to time
#endif
#if USE_VK
    deviceParams.requiredVulkanDeviceExtensions.push_back("VK_KHR_buffer_device_address");
    deviceParams.requiredVulkanDeviceExtensions.push_back("VK_KHR_format_feature_flags2");

    // Attachment 0 not written by fragment shader; undefined values will be written to attachment (OMM baker)
    deviceParams.ignoredVulkanValidationMessageLocations.push_back(0x0000000023e43bb7);

    // vertex shader writes to output location 0.0 which is not consumed by fragment shader (OMM baker)
    deviceParams.ignoredVulkanValidationMessageLocations.push_back(0x000000000609a13b);

    // vkCmdPipelineBarrier2(): pDependencyInfo.pBufferMemoryBarriers[0].dstAccessMask bit VK_ACCESS_SHADER_READ_BIT
    // is not supported by stage mask (Unhandled VkPipelineStageFlagBits)
    // Vulkan validation layer not supporting OMM?
    deviceParams.ignoredVulkanValidationMessageLocations.push_back(0x00000000591f70f2);

    // vkCmdPipelineBarrier2(): pDependencyInfo->pBufferMemoryBarriers[0].dstAccessMask(VK_ACCESS_SHADER_READ_BIT) is not supported by stage mask(VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT)
    // Vulkan Validaiotn layer not supporting OMM bug
    deviceParams.ignoredVulkanValidationMessageLocations.push_back(0x000000005e6e827d);
#endif

    deviceParams.enablePerMonitorDPI = true;
    
    std::string preferredScene = "kitchen.scene.json"; //"programmer-art.scene.json";
    LocalConfig::PreferredSceneOverride(preferredScene);

    CommandLineOptions cmdLine;
    if (!cmdLine.InitFromCommandLine(__argc, __argv))
    {
        return 1;
    }

    if (!cmdLine.scene.empty())
    {
        preferredScene = cmdLine.scene;
    }

    if (cmdLine.nonInteractive)
    {
        donut::log::DisablePopups();
    }

    if (cmdLine.debug)
    {
        deviceParams.enableDebugRuntime = true;
        deviceParams.enableNvrhiValidationLayer = true;
    }

    deviceParams.backBufferWidth = cmdLine.width;
    deviceParams.backBufferHeight = cmdLine.height;
    deviceParams.startFullscreen = cmdLine.fullscreen;
    if (!cmdLine.adapter.empty())
        deviceParams.adapterNameSubstring = std::wstring(cmdLine.adapter.begin(), cmdLine.adapter.end());

#ifdef STREAMLINE_INTEGRATION
    if (!cmdLine.noStreamline)
    {
        bool checkSig = true;
        bool SLlog = false;

    #ifdef _DEBUG
        checkSig = false;
    #endif

        auto success = SLWrapper::Get().Initialize_preDevice(api, checkSig, SLlog);
        if (!success)
            return 2;

        if (deviceParams.adapterNameSubstring.empty() && (api == nvrhi::GraphicsAPI::D3D11 || api == nvrhi::GraphicsAPI::D3D12))
        {
            SLWrapper::Get().FindAdapter((void*&)(deviceParams.adapter));
        }
    }
#endif

    if (cmdLine.noWindow)
    {
        if (!deviceManager->CreateDeviceAndSwapChain(deviceParams))
        {
            log::fatal("CreateDeviceAndSwapChain failed: Cannot initialize a graphics device with the requested parameters");
            return 3;
        }
    }
    else
    {
        if (!deviceManager->CreateWindowDeviceAndSwapChain(deviceParams, g_WindowTitle))
        {
            log::fatal("Cannot initialize a graphics device with the requested parameters");
            return 3;
        }
    }

#ifdef STREAMLINE_INTEGRATION
    if (!cmdLine.noStreamline)
    {
        SLWrapper::Get().SetDevice_nvrhi(deviceManager->GetDevice());
        SLWrapper::Get().Initialize_postDevice();
        SLWrapper::Get().UpdateFeatureAvailable(deviceManager);
    }
#endif

    if (!deviceManager->GetDevice()->queryFeatureSupport(nvrhi::Feature::RayTracingPipeline))
    {
        log::fatal("The graphics device does not support Ray Tracing Pipelines");
        return 4;
    }

    if (!deviceManager->GetDevice()->queryFeatureSupport(nvrhi::Feature::RayQuery))
    {
        log::fatal("The graphics device does not support Ray Queries");
        return 4;
    }

    bool SERSupported = deviceManager->GetDevice()->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12 && deviceManager->GetDevice()->queryFeatureSupport(nvrhi::Feature::ShaderExecutionReordering);
    
    bool ommSupported = deviceManager->GetDevice()->queryFeatureSupport(nvrhi::Feature::RayTracingOpacityMicromap);

    {
        SampleUIData uiData;
        Sample example(deviceManager, cmdLine, uiData);
        SampleUI gui(deviceManager, example, uiData, SERSupported, ommSupported);

        if (example.Init(preferredScene))
        {
            if (!cmdLine.noWindow)
                gui.Init( example.GetShaderFactory( ) );

            LocalConfig::PostAppInit(example, uiData);

            deviceManager->AddRenderPassToBack(&example);
            if (!cmdLine.noWindow)
                deviceManager->AddRenderPassToBack(&gui);
            deviceManager->RunMessageLoop();
            if (!cmdLine.noWindow)
                deviceManager->AddRenderPassToBack(&gui);
            deviceManager->RemoveRenderPass(&example);
        }
    }

#ifdef STREAMLINE_INTEGRATION
    if (!cmdLine.noStreamline)
    {
        SLWrapper::Get().Shutdown();
    }
#endif

    deviceManager->Shutdown();

    delete deviceManager;

    return 0;
}
