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

#pragma once

#include <donut/core/math/math.h>
#include <string>
#include <memory>
#include <unordered_map>
#include <optional>
#include <vector>

namespace Json
{
    class Value;
}

namespace donut::engine::animation
{
    struct Keyframe
    {
        float time = 0.f;
        dm::float4 value = 0.f;
        dm::float4 inTangent = 0.f;
        dm::float4 outTangent = 0.f;
    };

    enum class InterpolationMode
    {
        Step,
        Linear,
        Slerp,
        CatmullRomSpline,
        HermiteSpline
    };

    dm::float4 Interpolate(InterpolationMode mode, 
        const Keyframe& a, const Keyframe& b,
        const Keyframe& c, const Keyframe& d, float t, float dt);

    class Sampler
    {
    protected:
        std::vector<Keyframe> m_Keyframes;
        InterpolationMode m_Mode = InterpolationMode::Step;

    public:
        Sampler() = default;
        virtual ~Sampler() = default;

        std::optional<dm::float4> Evaluate(float time, bool extrapolateLastValues = false) const;

        [[nodiscard]] std::vector<Keyframe>& GetKeyframes() { return m_Keyframes; }
        void AddKeyframe(const Keyframe keyframe);

        [[nodiscard]] InterpolationMode GetMode() const { return m_Mode; }
        void SetInterpolationMode(InterpolationMode mode) { m_Mode = mode; }

        [[nodiscard]] float GetStartTime() const;
        [[nodiscard]] float GetEndTime() const;

        void Load(Json::Value& node);
    };

    class Sequence
    {
    protected:
        std::unordered_map<std::string, std::shared_ptr<Sampler>> m_Tracks;
        float m_Duration = 0.f;

    public:
        Sequence() = default;
        virtual ~Sequence() = default;

        std::shared_ptr<Sampler> GetTrack(const std::string& name)
        {
            return m_Tracks[name];
        }

        std::optional<dm::float4> Evaluate(const std::string& name, float time, bool extrapolateLastValues = false);

        void AddTrack(const std::string& name, const std::shared_ptr<Sampler>& track);

        [[nodiscard]] float GetDuration() const { return m_Duration; }

        void Load(Json::Value& node);
    };
}
