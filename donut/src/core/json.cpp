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

/*
License for JsonCpp

JsonCpp is Public Domain

The JsonCpp library's source code, including accompanying documentation, 
tests and demonstration applications, are licensed under the following
conditions...

Baptiste Lepilleur and The JsonCpp Authors explicitly disclaim copyright in all 
jurisdictions which recognize such a disclaimer. In such jurisdictions, 
this software is released into the Public Domain.
*/

#include <donut/core/json.h>
#include <donut/core/vfs/VFS.h>
#include <donut/core/log.h>
#include <json/reader.h>

using namespace donut::math;
using namespace donut::vfs;

namespace donut::json
{
    bool LoadFromFile(IFileSystem& fs, const std::filesystem::path& jsonFileName, Json::Value& documentRoot)
    {
        std::shared_ptr<IBlob> data = fs.readFile(jsonFileName);
        if (!data)
        {
            log::error("Couldn't read file %s", jsonFileName.generic_string().c_str());
            return false;
        }

        Json::CharReaderBuilder builder;
        builder["collectComments"] = false;
        Json::CharReader* reader = builder.newCharReader();

        const char* dataPtr = static_cast<const char*>(data->data());
        std::string errors;
        bool success = reader->parse(dataPtr, dataPtr + data->size(), &documentRoot, &errors);

        if (!success)
        {
            log::error("Couldn't parse JSON file %s:\n%s", jsonFileName.generic_string().c_str(), errors.c_str());
        }

        delete reader;

        return success;
    }

    template<>
    std::string Read<std::string>(const Json::Value& node, const std::string& defaultValue)
    {
        if (node.isString())
            return node.asString();

        return defaultValue;
    }

    template<>
    int Read<int>(const Json::Value& node, const int& defaultValue)
    {
        if (node.isNumeric())
            return node.asInt();

        return defaultValue;
    }

    template<>
    int2 Read<int2>(const Json::Value& node, const int2& defaultValue)
    {
        if (node.isArray() && node.size() == 2)
            return int2(node[0].asInt(), node[1].asInt());

        if (node.isNumeric())
            return node.asInt();

        return defaultValue;
    }

    template<>
    int3 Read<int3>(const Json::Value& node, const int3& defaultValue)
    {
        if (node.isArray() && node.size() == 3)
            return int3(node[0].asInt(), node[1].asInt(), node[2].asInt());

        if (node.isNumeric())
            return node.asInt();

        return defaultValue;
    }

    template<>
    int4 Read<int4>(const Json::Value& node, const int4& defaultValue)
    {
        if (node.isArray() && node.size() == 4)
            return int4(node[0].asInt(), node[1].asInt(), node[2].asInt(), node[3].asInt());

        if (node.isNumeric())
            return node.asInt();

        return defaultValue;
    }

    template<>
    uint Read<uint>(const Json::Value& node, const uint& defaultValue)
    {
        if (node.isNumeric())
            return node.asUInt();

        return defaultValue;
    }

    template<>
    uint2 Read<uint2>(const Json::Value& node, const uint2& defaultValue)
    {
        if (node.isArray() && node.size() == 2)
            return uint2(node[0].asUInt(), node[1].asUInt());

        if (node.isNumeric())
            return node.asUInt();

        return defaultValue;
    }

    template<>
    uint3 Read<uint3>(const Json::Value& node, const uint3& defaultValue)
    {
        if (node.isArray() && node.size() == 3)
            return uint3(node[0].asUInt(), node[1].asUInt(), node[2].asUInt());

        if (node.isNumeric())
            return node.asUInt();

        return defaultValue;
    }

    template<>
    uint4 Read<uint4>(const Json::Value& node, const uint4& defaultValue)
    {
        if (node.isArray() && node.size() == 4)
            return uint4(node[0].asUInt(), node[1].asUInt(), node[2].asUInt(), node[3].asUInt());

        if (node.isNumeric())
            return node.asUInt();

        return defaultValue;
    }

    template<>
    bool Read<bool>(const Json::Value& node, const bool& defaultValue)
    {
        if (node.isBool())
            return node.asBool();

        if (node.isNumeric())
            return node.asFloat() != 0.f;

        return defaultValue;
    }

    template<>
    float Read<float>(const Json::Value& node, const float& defaultValue)
    {
        if (node.isNumeric())
            return node.asFloat();

        return defaultValue;
    }

    template<>
    float2 Read<float2>(const Json::Value& node, const float2& defaultValue)
    {
        if (node.isArray() && node.size() == 2)
            return float2(node[0].asFloat(), node[1].asFloat());

        if (node.isNumeric())
            return node.asFloat();

        return defaultValue;
    }

    template<>
    float3 Read<float3>(const Json::Value& node, const float3& defaultValue)
    {
        if (node.isArray() && node.size() == 3)
            return float3(node[0].asFloat(), node[1].asFloat(), node[2].asFloat());

        if (node.isNumeric())
            return node.asFloat();

        return defaultValue;
    }

    template<>
    float4 Read<float4>(const Json::Value& node, const float4& defaultValue)
    {
        if (node.isArray() && node.size() == 4)
            return float4(node[0].asFloat(), node[1].asFloat(), node[2].asFloat(), node[3].asFloat());

        if (node.isNumeric())
            return node.asFloat();

        return defaultValue;
    }

    template<>
    double Read<double>(const Json::Value& node, const double& defaultValue)
    {
        if (node.isNumeric())
            return node.asDouble();

        return defaultValue;
    }

    template<>
    double2 Read<double2>(const Json::Value& node, const double2& defaultValue)
    {
        if (node.isArray() && node.size() == 2)
            return double2(node[0].asDouble(), node[1].asDouble());

        if (node.isNumeric())
            return node.asDouble();

        return defaultValue;
    }

    template<>
    double3 Read<double3>(const Json::Value& node, const double3& defaultValue)
    {
        if (node.isArray() && node.size() == 3)
            return double3(node[0].asDouble(), node[1].asDouble(), node[2].asDouble());

        if (node.isNumeric())
            return node.asDouble();

        return defaultValue;
    }

    template<>
    double4 Read<double4>(const Json::Value& node, const double4& defaultValue)
    {
        if (node.isArray() && node.size() == 4)
            return double4(node[0].asDouble(), node[1].asDouble(), node[2].asDouble(), node[3].asDouble());

        if (node.isNumeric())
            return node.asDouble();

        return defaultValue;
    }

    template <>
    void Write<std::string>(Json::Value& node, const std::string& value)
    {
        node = value;
    }

    template <>
    void Write<int>(Json::Value& node, const int& value)
    {
        node = value;
    }

    template <>
    void Write<int2>(Json::Value& node, const dm::int2& value)
    {
        node.append(value.x);
        node.append(value.y);
    }

    template <>
    void Write<int3>(Json::Value& node, const dm::int3& value)
    {
        node.append(value.x);
        node.append(value.y);
        node.append(value.z);
    }

    template <>
    void Write<int4>(Json::Value& node, const dm::int4& value)
    {
        node.append(value.x);
        node.append(value.y);
        node.append(value.z);
        node.append(value.w);
    }

    template <>
    void Write<uint>(Json::Value& node, const uint& value)
    {
        node = value;
    }

    template <>
    void Write<uint2>(Json::Value& node, const dm::uint2& value)
    {
        node.append(value.x);
        node.append(value.y);
    }

    template <>
    void Write<uint3>(Json::Value& node, const dm::uint3& value)
    {
        node.append(value.x);
        node.append(value.y);
        node.append(value.z);
    }

    template <>
    void Write<uint4>(Json::Value& node, const dm::uint4& value)
    {
        node.append(value.x);
        node.append(value.y);
        node.append(value.z);
        node.append(value.w);
    }

    template <>
    void Write<bool>(Json::Value& node, const bool& value)
    {
        node = value;
    }

    template <>
    void Write<float>(Json::Value& node, const float& value)
    {
        node = double(value);
    }

    template <>
    void Write<float2>(Json::Value& node, const dm::float2& value)
    {
        node.append(double(value.x));
        node.append(double(value.y));
    }

    template <>
    void Write<float3>(Json::Value& node, const dm::float3& value)
    {
        node.append(double(value.x));
        node.append(double(value.y));
        node.append(double(value.z));
    }

    template <>
    void Write<float4>(Json::Value& node, const dm::float4& value)
    {
        node.append(double(value.x));
        node.append(double(value.y));
        node.append(double(value.z));
        node.append(double(value.w));
    }

    template <>
    void Write<double>(Json::Value& node, const double& value)
    {
        node = double(value);
    }

    template <>
    void Write<double2>(Json::Value& node, const dm::double2& value)
    {
        node.append(double(value.x));
        node.append(double(value.y));
    }

    template <>
    void Write<double3>(Json::Value& node, const dm::double3& value)
    {
        node.append(double(value.x));
        node.append(double(value.y));
        node.append(double(value.z));
    }

    template <>
    void Write<double4>(Json::Value& node, const dm::double4& value)
    {
        node.append(double(value.x));
        node.append(double(value.y));
        node.append(double(value.z));
        node.append(double(value.w));
    }
}

void operator<<(Json::Value& node, const char* src)
{
    node = src;
}
