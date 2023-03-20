/*
* Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
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

#include <donut/engine/IesProfile.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/DescriptorTableManager.h>
#include <donut/core/vfs/VFS.h>
#include <donut/core/math/math.h>
#include <donut/core/log.h>
#include <nvrhi/utils.h>
#include <sstream>

using namespace donut;
using namespace donut::engine;

IesProfileLoader::IesProfileLoader(
    nvrhi::IDevice* device, 
    std::shared_ptr<ShaderFactory> shaderFactory, 
    std::shared_ptr<DescriptorTableManager> descriptorTableManager)
    : m_Device(device)
    , m_ShaderFactory(shaderFactory)
    , m_DescriptorTableManager(descriptorTableManager)
{
    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::Compute;
    layoutDesc.bindings = {
        nvrhi::BindingLayoutItem::TypedBuffer_SRV(0),
        nvrhi::BindingLayoutItem::Texture_UAV(0),
    };
    m_BindingLayout = device->createBindingLayout(layoutDesc);

    m_ComputeShader = m_ShaderFactory->CreateShader("donut/ies_profile_cs.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);

    nvrhi::ComputePipelineDesc pipelineDesc;
    pipelineDesc.bindingLayouts = { m_BindingLayout };
    pipelineDesc.CS = m_ComputeShader;
    m_ComputePipeline = device->createComputePipeline(pipelineDesc);
}

static const char* c_SupportedProfiles[] = {
    "IESNA:LM-63-1986",
    "IESNA:LM-63-1991",
    "IESNA91",
    "IESNA:LM-63-1995",
    "IESNA:LM-63-2002",
    "ERCO Leuchten GmbH  BY: ERCO/LUM650/8701",
    "ERCO Leuchten GmbH"
};

// See https://docs.agi32.com/PhotometricToolbox/Content/Open_Tool/iesna_lm-63_format.htm for the format reference. Pasted below.

/*
Each line marked with an asterisk must begin a new line.
Descriptions enclosed by the brackets"<" and ">" refer to the actual data stored on that line.
Lines marked with an "at sign" @ appear only if TILT=INCLUDE.

All data is in standard ASCII format.

*IESNA:LM-63-2002
*<keyword [TEST]>
*<keyword [TESTLAB]>
*<keyword [ISSUEDATE]>
*<keyword [MANUFAC]>
*<keyword 5>
 "
*<keyword n>
*TILT=<filespec> or INCLUDE or NONE
@ *<lamp to luminaire geometry>
@ *<# of pairs of angles and multiplying factors>
@ *<angles>
@ *<multiplying factors>
* <# lamps> <lumens/lamp> <multiplier> <# vertical angles> <# horizontal angles>
  <photometric type> <units type> <width> <length> <height>
* <ballast factor> <ballast lamp factor> <input watts>
* <vertical angles>
* <horizontal angles>
* <candela values for all vertical angles at first horizontal angle>
* <candela values for all vertical angles at second horizontal angle>
* "
* "
<candela values for all vertical angles at last horizontal angle>
*/

enum class IesStatus
{
    Success,
    UnsupportedProfile,
    UnsuppoeredTilt,
    WrongDataSize,
    InvalidData
};

static IesStatus ParseIesFile(char* fileData,
    std::vector<float>& numericData,
    float& maxCandelas)
{
    // count whitespace to get a rough estimate of the number of floats stored
    int numWhitespace = 0;
    for (char* p = fileData; *p; p++)
    {
        if (*p == ' ')
            ++numWhitespace;
    }

    // parse the header line by line
    const char* lineDelimiters = "\r\n";
    const char* dataDelimiters = "\r\n\t ";
    char* line = strtok(fileData, lineDelimiters);
    int lineNumber = 1;

    while (line)
    {
        if (lineNumber == 1)
        {
            bool profileFound = false;
            for (const char* profile : c_SupportedProfiles)
            {
                if (strstr(line, profile))
                {
                    profileFound = true;
                    break;
                }
            }

            if (!profileFound)
            {
                return IesStatus::UnsupportedProfile;
            }
        }
        else
        {
            if (strstr(line, "TILT=NONE") == line)
            {
                break;
            }
            else if (strstr(line, "TILT=") == line)
            {
                return IesStatus::UnsuppoeredTilt;
            }
        }

        line = strtok(NULL, lineDelimiters);
        ++lineNumber;
    }

    numericData.reserve(numWhitespace);
    while ((line = strtok(NULL, dataDelimiters)))
    {
        float value = 0.f;
        if (sscanf(line, "%f", &value) == 1)
            numericData.push_back(value);
    }

    if (numericData.size() < 16)
    {
        return IesStatus::WrongDataSize;
    }

    int numLamps = int(numericData[0]);
    int numVerticalAngles = int(numericData[3]);
    int numHorizontalAngles = int(numericData[4]);
    int headerSize = 13;

    int expectedDataSize = headerSize + numHorizontalAngles + numVerticalAngles + numHorizontalAngles * numVerticalAngles;
    if (numericData.size() != expectedDataSize)
    {
        return IesStatus::WrongDataSize;
    }

    maxCandelas = 0.f;
    for (int index = headerSize + numHorizontalAngles + numVerticalAngles; index < expectedDataSize; index++)
        maxCandelas = std::max(maxCandelas, numericData[index]);

    return IesStatus::Success;
}

std::shared_ptr<IesProfile> IesProfileLoader::LoadIesProfile(donut::vfs::IFileSystem& fs, const std::filesystem::path& path)
{
    auto fileBlob = fs.readFile(path);

    if (!fileBlob)
        return nullptr;

    if (fileBlob->size() == 0)
        return nullptr;

    // make a copy of the data because we need to modify it, and blobs are immutable
    char* fileData = (char*)malloc(fileBlob->size() + 1);
    if (!fileData)
        return nullptr;

    memcpy(fileData, fileBlob->data(), fileBlob->size());
    fileData[fileBlob->size()] = 0;

    fileBlob = nullptr;

    std::vector<float> numericData;
    float maxCandelas;

    IesStatus status = ParseIesFile(fileData, numericData, maxCandelas);

    free(fileData);

    if (status != IesStatus::Success)
        return nullptr;

    // Stash the normalization factor in data[0], we don't use that anyway
    numericData[0] = 1.f / maxCandelas;

    std::shared_ptr<IesProfile> profile = std::make_shared<IesProfile>();
    profile->name = path.filename().generic_string();
    profile->textureIndex = -1;
    profile->rawData = std::move(numericData);

    return profile;
}

void IesProfileLoader::BakeIesProfile(IesProfile& profile, nvrhi::ICommandList* commandList)
{
    if (profile.texture)
        return;

    nvrhi::BufferDesc bufferDesc;
    bufferDesc.byteSize = sizeof(float) * profile.rawData.size();
    bufferDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    bufferDesc.format = nvrhi::Format::R32_FLOAT;
    bufferDesc.keepInitialState = true;
    bufferDesc.debugName = "IesProfileData";
    bufferDesc.canHaveTypedViews = true;
    nvrhi::BufferHandle buffer = m_Device->createBuffer(bufferDesc);

    nvrhi::TextureDesc textureDesc;
    textureDesc.dimension = nvrhi::TextureDimension::Texture2D;
    textureDesc.width = 128;
    textureDesc.height = 128;
    textureDesc.debugName = profile.name;
    textureDesc.format = nvrhi::Format::R16_FLOAT;
    textureDesc.isUAV = true;
    profile.texture = m_Device->createTexture(textureDesc);

    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::TypedBuffer_SRV(0, buffer),
        nvrhi::BindingSetItem::Texture_UAV(0, profile.texture)
    };
    nvrhi::BindingSetHandle bindingSet = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);

    commandList->writeBuffer(buffer, profile.rawData.data(), profile.rawData.size() * sizeof(float));

    commandList->beginTrackingTextureState(profile.texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);

    nvrhi::ComputeState state;
    state.bindings = { bindingSet };
    state.pipeline = m_ComputePipeline;
    commandList->setComputeState(state);
    commandList->dispatch(8, 8, 1);

    commandList->setPermanentTextureState(profile.texture, nvrhi::ResourceStates::ShaderResource);
    commandList->commitBarriers();

    if (m_DescriptorTableManager)
    {
        profile.textureIndex = m_DescriptorTableManager->CreateDescriptor(nvrhi::BindingSetItem::Texture_SRV(0, profile.texture));
    }
}
