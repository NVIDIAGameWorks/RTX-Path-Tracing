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
#include <string.h>

namespace donut::engine
{
    constexpr int LightType_Environment = 1000;

    class EnvironmentLight : public donut::engine::Light
    {
    public:
        dm::float3 radianceScale = 1.f;
        int textureIndex = -1;
        float rotation = 0.f;
        std::string path;

        void Load(const Json::Value& node) override;
        [[nodiscard]] int GetLightType() const override { return LightType_Environment; }
        [[nodiscard]] std::shared_ptr<SceneGraphLeaf> Clone() override;
        void FillLightConstants(LightConstants& lightConstants) const override;
        bool SetProperty(const std::string& name, const dm::float4& value) override { assert( false ); return false; }    // not yet implemented, never needed
    };

    class PerspectiveCameraEx : public PerspectiveCamera
    {
    public:
        std::optional<bool>     enableAutoExposure;
        std::optional<float>    exposureCompensation;
        std::optional<float>    exposureValue;
        std::optional<float>    exposureValueMin;
        std::optional<float>    exposureValueMax;

        [[nodiscard]] std::shared_ptr<SceneGraphLeaf> Clone() override;
        void Load(const Json::Value& node) override;
        bool SetProperty(const std::string& name, const dm::float4& value) override;
    };

    // used to override and/or extend any material properties (for supporting what's not supported by standard .gltf loader, or to modify without modifying .gltf itself!)
    class MaterialPatch : public SceneGraphLeaf
    {
    public:
        // Feel free to add more!
        std::optional<std::string>  domain;
        std::optional<float>        volumeThicknessFactor;
        std::optional<float>        volumeAttenuationDistance;
        std::optional<dm::float3>   volumeAttenuationColor;
        std::optional<float>        ior;
        std::optional<float>        transmissionFactor;
        std::optional<float>        diffuseTransmissionFactor;
        std::optional<int>          nestedPriority;
        std::optional<bool>         doubleSided;
        std::optional<bool>         thinSurface;
        std::optional<bool>         excludeFromNEE;
        std::optional<float>        roughness;
        std::optional<float>        metalness;
        std::optional<float>        normalTextureScale;
        std::optional<bool>         psdExclude;
        std::optional<int>          psdDominantDeltaLobe;
        std::optional<float>        emissiveIntensity;

        [[nodiscard]] std::shared_ptr<SceneGraphLeaf> Clone() override;
        void Load(const Json::Value& node) override;
        bool SetProperty(const std::string& name, const dm::float4& value) override;

        void Patch( Material & mat );
    private:
        static MaterialDomain GetDomainFromString(const std::string& domain);
    };

    // used to setup initial sample scene settings
    class SampleSettings : public SceneGraphLeaf
    {
    public:
        std::optional<bool>         realtimeMode;
        std::optional<bool>         enableAnimations;
        std::optional<bool>         enableRTXDI;
        std::optional<int>          startingCamera;
        std::optional<float>        realtimeFireflyFilter;

        [[nodiscard]] std::shared_ptr<SceneGraphLeaf> Clone() override;
        void Load(const Json::Value& node) override;
    };

    class ExtendedSceneTypeFactory : public donut::engine::SceneTypeFactory
    {
    public:
        std::shared_ptr<donut::engine::SceneGraphLeaf> CreateLeaf(const std::string& type) override;
    };

    class ExtendedScene : public donut::engine::Scene
    {
    private:
        std::shared_ptr<SampleSettings> m_loadedSettings = nullptr;

    public:
        using Scene::Scene;

        bool LoadWithExecutor(const std::filesystem::path& jsonFileName, tf::Executor* executor) override;
        std::shared_ptr<SampleSettings> GetSampleSettingsNode() const { return m_loadedSettings; }

    private:
        // maybe switch to SceneGraphWalker?
        void ProcessNodesRecursive(donut::engine::SceneGraphNode* node);
    };

    std::shared_ptr<EnvironmentLight> FindEnvironmentLight(std::vector <std::shared_ptr<Light>> lights);
}
