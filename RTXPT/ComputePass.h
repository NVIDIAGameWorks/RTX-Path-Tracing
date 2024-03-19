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

#include <nvrhi/nvrhi.h>

namespace donut::engine
{
    class ShaderFactory;
    struct ShaderMacro;
}


struct ComputePass
{
    nvrhi::ShaderHandle ComputeShader;
    nvrhi::ComputePipelineHandle ComputePipeline;
	nvrhi::ShaderLibraryHandle ShaderLibrary;

    bool Init(
        nvrhi::IDevice* device,
        donut::engine::ShaderFactory& shaderFactory,
        const char* shaderName,
        const std::vector<donut::engine::ShaderMacro>& macros,
        nvrhi::IBindingLayout* bindingLayout,
        nvrhi::IBindingLayout* extraBindingLayout = nullptr,
        nvrhi::IBindingLayout* bindlessLayout = nullptr);

    void Execute(
        nvrhi::ICommandList* commandList,
        int width,
        int height,
        int depth,
        nvrhi::IBindingSet* bindingSet,
        nvrhi::IBindingSet* extraBindingSet = nullptr,
        nvrhi::IDescriptorTable* descriptorTable = nullptr,
        const void* pushConstants = nullptr,
        size_t pushConstantSize = 0);
};