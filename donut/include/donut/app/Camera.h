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

#include <unordered_map>
#include <array>

#include <donut/core/math/math.h>

#define GLFW_INCLUDE_NONE // Do not include any OpenGL headers
#include <GLFW/glfw3.h>

namespace donut::engine
{
    class PlanarView;
}

namespace donut::app
{

    // A camera with position and orientation. Methods for moving it come from derived classes.
    class BaseCamera
    {
    public:
        virtual void KeyboardUpdate(int key, int scancode, int action, int mods) { }
        virtual void MousePosUpdate(double xpos, double ypos) { }
        virtual void MouseButtonUpdate(int button, int action, int mods) { }
        virtual void MouseScrollUpdate(double xoffset, double yoffset) { }
        virtual void JoystickButtonUpdate(int button, bool pressed) { }
        virtual void JoystickUpdate(int axis, float value) { }
        virtual void Animate(float deltaT) { }
        virtual ~BaseCamera() = default;

        void SetMoveSpeed(float value) { m_MoveSpeed = value; }
        void SetRotateSpeed(float value) { m_RotateSpeed = value; }

        [[nodiscard]] const dm::affine3& GetWorldToViewMatrix() const { return m_MatWorldToView; }
        [[nodiscard]] const dm::affine3& GetTranslatedWorldToViewMatrix() const { return m_MatTranslatedWorldToView; }
        [[nodiscard]] const dm::float3& GetPosition() const { return m_CameraPos; }
        [[nodiscard]] const dm::float3& GetDir() const { return m_CameraDir; }
        [[nodiscard]] const dm::float3& GetUp() const { return m_CameraUp; }

    protected:
        // This can be useful for derived classes while not necessarily public, i.e., in a third person
        // camera class, public clients cannot direct the gaze point.
        void BaseLookAt(dm::float3 cameraPos, dm::float3 cameraTarget, dm::float3 cameraUp = dm::float3{ 0.f, 1.f, 0.f });
        void UpdateWorldToView();

        dm::affine3 m_MatWorldToView = dm::affine3::identity();
        dm::affine3 m_MatTranslatedWorldToView = dm::affine3::identity();

        dm::float3 m_CameraPos   = 0.f;   // in worldspace
        dm::float3 m_CameraDir   = dm::float3(1.f, 0.f, 0.f); // normalized
        dm::float3 m_CameraUp    = dm::float3(0.f, 1.f, 0.f); // normalized
        dm::float3 m_CameraRight = dm::float3(0.f, 0.f, 1.f); // normalized

        float m_MoveSpeed = 1.f;      // movement speed in units/second
        float m_RotateSpeed = .005f;  // mouse sensitivity in radians/pixel
    };

    class FirstPersonCamera : public BaseCamera
    {
    public:
        void KeyboardUpdate(int key, int scancode, int action, int mods) override;
        void MousePosUpdate(double xpos, double ypos) override;
        void MouseButtonUpdate(int button, int action, int mods) override;
        void Animate(float deltaT) override;
        void AnimateSmooth(float deltaT);

        void LookAt(dm::float3 cameraPos, dm::float3 cameraTarget, dm::float3 cameraUp = dm::float3{ 0.f, 1.f, 0.f });

    private:
        std::pair<bool, dm::affine3> AnimateRoll(dm::affine3 initialRotation);
        std::pair<bool, dm::float3> AnimateTranslation(float deltaT);
        void UpdateCamera(dm::float3 cameraMoveVec, dm::affine3 cameraRotation);

        dm::float2 mousePos;
        dm::float2 mousePosPrev;
        // fields used only for AnimateSmooth()
        dm::float2 mousePosDamp;
        bool isMoving = false;

        typedef enum
        {
            MoveUp,
            MoveDown,
            MoveLeft,
            MoveRight,
            MoveForward,
            MoveBackward,

            YawRight,
            YawLeft,
            PitchUp,
            PitchDown,
            RollLeft,
            RollRight,

            SpeedUp,
            SlowDown,

            KeyboardControlCount,
        } KeyboardControls;

        typedef enum
        {
            Left,
            Middle,
            Right,

            MouseButtonCount,
            MouseButtonFirst = Left,
        } MouseButtons;

        const std::unordered_map<int, int> keyboardMap = {
            { GLFW_KEY_Q, KeyboardControls::MoveDown },
            { GLFW_KEY_E, KeyboardControls::MoveUp },
            { GLFW_KEY_A, KeyboardControls::MoveLeft },
            { GLFW_KEY_D, KeyboardControls::MoveRight },
            { GLFW_KEY_W, KeyboardControls::MoveForward },
            { GLFW_KEY_S, KeyboardControls::MoveBackward },
            { GLFW_KEY_LEFT, KeyboardControls::YawLeft },
            { GLFW_KEY_RIGHT, KeyboardControls::YawRight },
            { GLFW_KEY_UP, KeyboardControls::PitchUp },
            { GLFW_KEY_DOWN, KeyboardControls::PitchDown },
            { GLFW_KEY_Z, KeyboardControls::RollLeft },
            { GLFW_KEY_C, KeyboardControls::RollRight },
            { GLFW_KEY_LEFT_SHIFT, KeyboardControls::SpeedUp },
            { GLFW_KEY_RIGHT_SHIFT, KeyboardControls::SpeedUp },
            { GLFW_KEY_LEFT_CONTROL, KeyboardControls::SlowDown },
            { GLFW_KEY_RIGHT_CONTROL, KeyboardControls::SlowDown },
        };

        const std::unordered_map<int, int> mouseButtonMap = {
            { GLFW_MOUSE_BUTTON_LEFT, MouseButtons::Left },
            { GLFW_MOUSE_BUTTON_MIDDLE, MouseButtons::Middle },
            { GLFW_MOUSE_BUTTON_RIGHT, MouseButtons::Right },
        };

        std::array<bool, KeyboardControls::KeyboardControlCount> keyboardState = { false };
        std::array<bool, MouseButtons::MouseButtonCount> mouseButtonState = { false };
    };

    class ThirdPersonCamera : public BaseCamera
    {
    public:
        void KeyboardUpdate(int key, int scancode, int action, int mods) override;
        void MousePosUpdate(double xpos, double ypos) override;
        void MouseButtonUpdate(int button, int action, int mods) override;
        void MouseScrollUpdate(double xoffset, double yoffset) override;
        void JoystickButtonUpdate(int button, bool pressed) override;
        void JoystickUpdate(int axis, float value) override;
        void Animate(float deltaT) override;

        void SetTargetPosition(dm::float3 position) { m_TargetPos = position; }
        void SetDistance(float distance) { m_Distance = distance; }
        void SetRotation(float yaw, float pitch);
        void SetMinDistance(float value) { m_MinDistance = value; }
        void SetMaxDistance(float value) { m_MaxDistance = value; }

        void SetView(const engine::PlanarView& view);
        
    private:
        void AnimateOrbit(float deltaT);
        void AnimateTranslation(const dm::float3x3& viewMatrix);

        // View parameters to derive translation amounts
        dm::float4x4 m_ProjectionMatrix = dm::float4x4::identity();
        dm::float4x4 m_InverseProjectionMatrix = dm::float4x4::identity();
        dm::float2 m_ViewportSize = dm::float2::zero();

        dm::float2 m_MousePos = 0.f;
        dm::float2 m_MousePosPrev = 0.f;

        dm::float3 m_TargetPos = 0.f;
        float m_Distance = 30.f;
        
        float m_MinDistance = 0.f;
        float m_MaxDistance = std::numeric_limits<float>::max();
        
        float m_Yaw = 0.f;
        float m_Pitch = 0.f;
        
        float m_DeltaYaw = 0.f;
        float m_DeltaPitch = 0.f;
        float m_DeltaDistance = 0.f;

        typedef enum
        {
            HorizontalPan,

            KeyboardControlCount,
        } KeyboardControls;

        const std::unordered_map<int, int> keyboardMap = {
            { GLFW_KEY_LEFT_ALT, KeyboardControls::HorizontalPan },
        };

        typedef enum
        {
            Left,
            Middle,
            Right,

            MouseButtonCount
        } MouseButtons;

        std::array<bool, KeyboardControls::KeyboardControlCount> keyboardState = { false };
        std::array<bool, MouseButtons::MouseButtonCount> mouseButtonState = { false };
    };

}
