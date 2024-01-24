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

#include <donut/app/UserInterfaceUtils.h>
#include <donut/engine/SceneGraph.h>

#include <filesystem>
#include <imgui.h>

#ifndef _WIN32
#include <unistd.h>
#include <cstdio>
#include <climits>
#else
#include <Windows.h>
#define PATH_MAX MAX_PATH
#endif // _WIN32

using namespace donut::engine;
using namespace donut::app;
using namespace donut::math;

bool donut::app::FileDialog(bool bOpen, const char* pFilters, std::string& fileName)
{
#ifdef _WIN32
    OPENFILENAMEA ofn;
    CHAR chars[PATH_MAX] = "";
    ZeroMemory(&ofn, sizeof(ofn));

    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = GetForegroundWindow();
    ofn.lpstrFilter = pFilters;
    ofn.lpstrFile = chars;
    ofn.nMaxFile = ARRAYSIZE(chars);
    ofn.Flags = OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (bOpen)
    {
        ofn.Flags |= OFN_FILEMUSTEXIST;
    }
    ofn.lpstrDefExt = "";

    BOOL b = bOpen ? GetOpenFileNameA(&ofn) : GetSaveFileNameA(&ofn);
    if (b)
    {
        fileName = chars;
        return true;
    }

    return false;
#else // _WIN32
    // minimal implementation avoiding a GUI library, ignores filters for now,
    // and relies on external 'zenity' program commonly available on linuxoids
    char chars[PATH_MAX] = { 0 };
    std::string app = "zenity --file-selection";
    if (!bOpen)
    {
        app += " --save --confirm-overwrite";
    }
    FILE* f = popen(app.c_str(), "r");
    bool gotname = (nullptr != fgets(chars, PATH_MAX, f));
    pclose(f);

    if (gotname && chars[0] != '\0')
    {
        fileName = chars;
        return true;
    }
    return false;
#endif // _WIN32
}

bool donut::app::MaterialEditor(engine::Material* material, bool allowMaterialDomainChanges)
{
    bool update = false;

    float itemWidth = ImGui::CalcItemWidth();

    if (allowMaterialDomainChanges)
    {
        update |= ImGui::Combo("Material Domain", (int*)&material->domain,
            "Opaque\0Alpha-tested\0Alpha-blended\0Transmissive\0"
            "Transmissive alpha-tested\0Transmissive alpha-blended\0");
    }
    else
    {
        ImGui::Text("Material Domain: %s", MaterialDomainToString(material->domain));
    }

    auto getShortTexturePath = [](const std::string& fullPath)
    {
        return std::filesystem::path(fullPath).filename().generic_string();
    };

    const ImVec4 filenameColor = ImVec4(0.474f, 0.722f, 0.176f, 1.0f);

    update |= ImGui::Checkbox("Double-Sided", &material->doubleSided);
    update |= ImGui::Checkbox("Thin surface", &material->thinSurface);
    update |= ImGui::Checkbox("Ignore by NEE shadow ray", &material->excludeFromNEE);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ignored for shadow rays during Next Event Estimation");
    
    if (material->useSpecularGlossModel)
    {
        if (material->baseOrDiffuseTexture)
        {
            update |= ImGui::Checkbox("Use Diffuse Texture", &material->enableBaseOrDiffuseTexture);
            ImGui::SameLine();
            ImGui::TextColored(filenameColor, "%s", getShortTexturePath(material->baseOrDiffuseTexture->path).c_str());
        }

        update |= ImGui::ColorEdit3(material->enableBaseOrDiffuseTexture ? "Diffuse Factor" : "Diffuse Color", material->baseOrDiffuseColor.data(), ImGuiColorEditFlags_Float);

        if (material->metalRoughOrSpecularTexture)
        {
            update |= ImGui::Checkbox("Use Specular Texture", &material->enableMetalRoughOrSpecularTexture);
            ImGui::SameLine();
            ImGui::TextColored(filenameColor, "%s", getShortTexturePath(material->metalRoughOrSpecularTexture->path).c_str());
        }

        update |= ImGui::ColorEdit3(material->enableMetalRoughOrSpecularTexture ? "Specular Factor" : "Specular Color", material->specularColor.data(), ImGuiColorEditFlags_Float);

        float glossiness = 1.0f - material->roughness;
        update |= ImGui::SliderFloat(material->enableMetalRoughOrSpecularTexture ? "Glossiness Factor" : "Glossiness", &glossiness, 0.f, 1.f);
        material->roughness = 1.0f - glossiness;
    }
    else
    {
        if (material->baseOrDiffuseTexture)
        {
            update |= ImGui::Checkbox("Use Base Color Texture", &material->enableBaseOrDiffuseTexture);
            ImGui::SameLine();
            ImGui::TextColored(filenameColor, "%s", getShortTexturePath(material->baseOrDiffuseTexture->path).c_str());
        }

        update |= ImGui::ColorEdit3(material->enableBaseOrDiffuseTexture ? "Base Color Factor" : "Base Color", material->baseOrDiffuseColor.data(), ImGuiColorEditFlags_Float);

        if (material->metalRoughOrSpecularTexture)
        {
            update |= ImGui::Checkbox("Use Metal-Rough Texture", &material->enableMetalRoughOrSpecularTexture);
            ImGui::SameLine();
            ImGui::TextColored(filenameColor, "%s", getShortTexturePath(material->metalRoughOrSpecularTexture->path).c_str());
        }

        update |= ImGui::SliderFloat(material->enableMetalRoughOrSpecularTexture ? "Metalness Factor" : "Metalness", &material->metalness, 0.f, 1.f);
        update |= ImGui::SliderFloat(material->enableMetalRoughOrSpecularTexture ? "Roughness Factor" : "Roughness", &material->roughness, 0.f, 1.f);
    }

    if (material->domain == MaterialDomain::AlphaBlended || material->domain == MaterialDomain::TransmissiveAlphaBlended)
    {
        if (material->baseOrDiffuseTexture)
            update |= ImGui::SliderFloat("Opacity Factor", &material->opacity, 0.f, 2.f);
        else
            update |= ImGui::SliderFloat("Opacity", &material->opacity, 0.f, 1.f);
    }
    else if (material->domain == MaterialDomain::AlphaTested || material->domain == MaterialDomain::TransmissiveAlphaTested)
    {
        if (material->baseOrDiffuseTexture)
            update |= ImGui::SliderFloat("Alpha Cutoff", &material->alphaCutoff, 0.f, 1.f);
    }

    if (material->normalTexture)
    {
        update |= ImGui::Checkbox("Use Normal Texture", &material->enableNormalTexture);
        ImGui::SameLine();
        ImGui::TextColored(filenameColor, "%s", getShortTexturePath(material->normalTexture->path).c_str());
    }

    if (material->enableNormalTexture)
    {
        ImGui::SetNextItemWidth(itemWidth - 31.f);
        update |= ImGui::SliderFloat("###normtexscale", &material->normalTextureScale, -2.f, 2.f);
        ImGui::SameLine(0.f, 5.f);
        ImGui::SetNextItemWidth(26.f);
        if (ImGui::Button("1.0"))
        {
            material->normalTextureScale = 1.f;
            update = true;
        }
        ImGui::SameLine();
        ImGui::Text("Normal Scale");
    }

    if (material->occlusionTexture)
    {
        update |= ImGui::Checkbox("Use Occlusion Texture", &material->enableOcclusionTexture);
        ImGui::SameLine();
        ImGui::TextColored(filenameColor, "%s", getShortTexturePath(material->occlusionTexture->path).c_str());
    }
    
    if (material->enableOcclusionTexture)
        update |= ImGui::SliderFloat("Occlusion Strength", &material->occlusionStrength, 0.f, 1.f);

    if (material->emissiveTexture)
    {
        update |= ImGui::Checkbox("Use Emissive Texture", &material->enableEmissiveTexture);
        ImGui::SameLine();
        ImGui::TextColored(filenameColor, "%s", getShortTexturePath(material->emissiveTexture->path).c_str());
    }
    
    update |= ImGui::ColorEdit3("Emissive Color", material->emissiveColor.data(), ImGuiColorEditFlags_Float);
    update |= ImGui::SliderFloat("Emissive Intensity", &material->emissiveIntensity, 0.f, 100000.f, "%.3f", ImGuiSliderFlags_Logarithmic);

    if (material->domain == MaterialDomain::Transmissive ||
        material->domain == MaterialDomain::TransmissiveAlphaTested ||
        material->domain == MaterialDomain::TransmissiveAlphaBlended)
    {
        update |= ImGui::InputFloat("Index of Refraction", &material->ior);
        if (material->ior < 1.0f) { material->ior = 1.0f; update = true; }

        if (material->transmissionTexture)
        {
            update |= ImGui::Checkbox("Use Transmission Texture", &material->enableTransmissionTexture);
            ImGui::SameLine();
            ImGui::TextColored(filenameColor, "%s", getShortTexturePath(material->transmissionTexture->path).c_str());
        }

        update |= ImGui::SliderFloat("Transmission Factor", &material->transmissionFactor, 0.f, 1.f);
        update |= ImGui::SliderFloat("Diff Transmission Factor", &material->diffuseTransmissionFactor, 0.f, 1.f);

        if (!material->thinSurface)
        {
            update |= ImGui::InputFloat("Attenuation Distance", &material->volumeAttenuationDistance);
            if (material->volumeAttenuationDistance < 0.0f) { material->volumeAttenuationDistance = 0.0f; update = true; }

            update |= ImGui::ColorEdit3("Attenuation Color", material->volumeAttenuationColor.data(), ImGuiColorEditFlags_Float);

            update |= ImGui::InputInt("Nested Priority", &material->nestedPriority);
            if (material->nestedPriority < 0 || material->nestedPriority > 14) { material->nestedPriority = dm::clamp(material->nestedPriority, 0, 14); update = true; }
        }
        else
        {
            ImGui::Text( "Thin surface transmissive materials have no volume properties");
        }
    }

    update |= ImGui::SliderFloat("Shadow NoL Fadeout", &material->shadowNoLFadeout, 0.0f, 0.2f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(
    "Low tessellation geometry often has triangle (flat) normals that differ significantly from shading normals. \n"
    "This causes shading vs shadow discrepancy that exposes triangle edges. One way to mitigate this (other than \n"
    "having more detailed mesh) is to add additional shadowing falloff to hide the seam. This setting is not \n"
    "physically correct and adds bias. Setting of 0 means no fadeout (default)." );


    if (ImGui::CollapsingHeader("Path Space Decomposition (SPs)"))
    {
        ImGui::Indent();
        update |= ImGui::Checkbox("Do not decompose delta lobes", &material->psdExclude);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Some complex materials look best when denoised on their surface only.");

        if (!material->psdExclude)
        {
            int dominantDeltaLobeP1 = dm::clamp(material->psdDominantDeltaLobe, -1, 1) + 1;
            update |= ImGui::Combo("Dominant delta lobe", &dominantDeltaLobeP1, "None\0Transparency\0Reflection\0\0");    // add 3rd for Reflection Clearcoat
            material->psdDominantDeltaLobe = dm::clamp(dominantDeltaLobeP1 - 1, -1, 1);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Allows the dominant flag through specified delta lobe.\nUseful if surface does not require high quality lighting and denoising\nand we want reflected or surfaces behind to receive more attention.");
        }
        ImGui::Unindent();
    }

    return update;
}

bool donut::app::LightEditor_Directional(engine::DirectionalLight& light)
{
    bool changed = false;
    double3 direction = light.GetDirection();
    if (AzimuthElevationSliders(direction, true))
    {
        light.SetDirection(direction);
        changed = true;
    }
    changed |= ImGui::ColorEdit3("Color", &light.color.x, ImGuiColorEditFlags_Float);
    changed |= ImGui::SliderFloat("Irradiance", &light.irradiance, 0.f, 100.f, "%.2f", ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::SliderFloat("Angular Size", &light.angularSize, 0.1f, 20.f);
    return changed;
}

bool donut::app::LightEditor_Point(engine::PointLight& light)
{
    bool changed = false;
    changed |= ImGui::SliderFloat("Radius", &light.radius, 0.01f, 1.f, "%.3f", ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::ColorEdit3("Color", &light.color.x, ImGuiColorEditFlags_Float);
    changed |= ImGui::SliderFloat("Intensity", &light.intensity, 0.f, 100.f, "%.2f", ImGuiSliderFlags_Logarithmic);
    return changed;
}

bool donut::app::LightEditor_Spot(engine::SpotLight& light)
{
    bool changed = false;
    double3 direction = light.GetDirection();
    if (AzimuthElevationSliders(direction, false))
    {
        light.SetDirection(direction);
        changed = true;
    }
    changed |= ImGui::SliderFloat("Radius", &light.radius, 0.01f, 1.f, "%.3f", ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::ColorEdit3("Color", &light.color.x, ImGuiColorEditFlags_Float);
    changed |= ImGui::SliderFloat("Intensity", &light.intensity, 0.f, 100.f, "%.2f", ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::SliderFloat("Inner Angle", &light.innerAngle, 0.f, 180.f);
    changed |= ImGui::SliderFloat("Outer Angle", &light.outerAngle, 0.f, 180.f);
    return changed;
}

bool donut::app::LightEditor(engine::Light& light)
{
    switch (light.GetLightType())
    {
    case LightType_Directional:
        return LightEditor_Directional(static_cast<DirectionalLight&>(light));
    case LightType_Point:
        return LightEditor_Point(static_cast<PointLight&>(light));
    case LightType_Spot:
        return LightEditor_Spot(static_cast<SpotLight&>(light));
    default:
        return false;
    }
}

bool donut::app::AzimuthElevationSliders(math::double3& direction, bool negative)
{
    double3 normalizedDir = normalize(direction);
    if (negative) normalizedDir = -normalizedDir;

    double azimuth = degrees(atan2(normalizedDir.z, normalizedDir.x));
    double elevation = degrees(asin(normalizedDir.y));
    const double minAzimuth = -180.0;
    const double maxAzimuth = 180.0;
    const double minElevation = -90.0;
    const double maxElevation = 90.0;

    bool changed = false;
    changed |= ImGui::SliderScalar("Azimuth", ImGuiDataType_Double, &azimuth, &minAzimuth, &maxAzimuth, "%.1f deg", ImGuiSliderFlags_NoRoundToFormat);
    changed |= ImGui::SliderScalar("Elevation", ImGuiDataType_Double, &elevation, &minElevation, &maxElevation, "%.1f deg", ImGuiSliderFlags_NoRoundToFormat);

    if (changed)
    {
        azimuth = radians(azimuth);
        elevation = radians(elevation);

        direction.y = sin(elevation);
        direction.x = cos(azimuth) * cos(elevation);
        direction.z = sin(azimuth) * cos(elevation);

        if (negative)
            direction = -direction;
    }

    return changed;
}
