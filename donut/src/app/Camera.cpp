/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include <cassert>
#include <donut/app/Camera.h>

#include "donut/engine/View.h"

using namespace donut::math;
using namespace donut::app;

void BaseCamera::UpdateWorldToView()
{
    m_MatTranslatedWorldToView = affine3::from_cols(m_CameraRight, m_CameraUp, m_CameraDir, 0.f);
    m_MatWorldToView = translation(-m_CameraPos) * m_MatTranslatedWorldToView;
}

void BaseCamera::BaseLookAt(float3 cameraPos, float3 cameraTarget, float3 cameraUp)
{
    this->m_CameraPos = cameraPos;
    this->m_CameraDir = normalize(cameraTarget - cameraPos);
    this->m_CameraUp = normalize(cameraUp);
    this->m_CameraRight = normalize(cross(this->m_CameraDir, this->m_CameraUp));
    this->m_CameraUp = normalize(cross(this->m_CameraRight, this->m_CameraDir));

    UpdateWorldToView();
}

void FirstPersonCamera::KeyboardUpdate(int key, int scancode, int action, int mods)
{
    if (keyboardMap.find(key) == keyboardMap.end())
    {
        return;
    }

    auto cameraKey = keyboardMap.at(key);
    if (action == GLFW_PRESS || action == GLFW_REPEAT)
    {
        keyboardState[cameraKey] = true;
    }
    else {
        keyboardState[cameraKey] = false;
    }
}

void FirstPersonCamera::MousePosUpdate(double xpos, double ypos)
{
    mousePos = { float(xpos), float(ypos) };
}

void FirstPersonCamera::MouseButtonUpdate(int button, int action, int mods)
{
    if (mouseButtonMap.find(button) == mouseButtonMap.end())
    {
        return;
    }

    auto cameraButton = mouseButtonMap.at(button);
    if (action == GLFW_PRESS)
    {
        mouseButtonState[cameraButton] = true;
    }
    else {
        mouseButtonState[cameraButton] = false;
    }
}

void FirstPersonCamera::LookAt(float3 cameraPos, float3 cameraTarget, float3 cameraUp)
{
    // Make the base method public.
    BaseLookAt(cameraPos, cameraTarget, cameraUp);
}

std::pair<bool, float3> FirstPersonCamera::AnimateTranslation(float deltaT)
{
    bool cameraDirty = false;
    float moveStep = deltaT * m_MoveSpeed;
    float3 cameraMoveVec = 0.f;

    if (keyboardState[KeyboardControls::SpeedUp])
        moveStep *= 5.f;

    if (keyboardState[KeyboardControls::SlowDown])
        moveStep *= 0.1f;

    if (keyboardState[KeyboardControls::MoveForward])
    {
        cameraDirty = true;
        cameraMoveVec += m_CameraDir * moveStep;
    }

    if (keyboardState[KeyboardControls::MoveBackward])
    {
        cameraDirty = true;
        cameraMoveVec += -m_CameraDir * moveStep;
    }

    if (keyboardState[KeyboardControls::MoveLeft])
    {
        cameraDirty = true;
        cameraMoveVec += -m_CameraRight * moveStep;
    }

    if (keyboardState[KeyboardControls::MoveRight])
    {
        cameraDirty = true;
        cameraMoveVec += m_CameraRight * moveStep;
    }

    if (keyboardState[KeyboardControls::MoveUp])
    {
        cameraDirty = true;
        cameraMoveVec += m_CameraUp * moveStep;
    }

    if (keyboardState[KeyboardControls::MoveDown])
    {
        cameraDirty = true;
        cameraMoveVec += -m_CameraUp * moveStep;
    }
    return std::make_pair(cameraDirty, cameraMoveVec);
}

void FirstPersonCamera::UpdateCamera(dm::float3 cameraMoveVec, dm::affine3 cameraRotation)
{
    m_CameraPos += cameraMoveVec;
    m_CameraDir = normalize(cameraRotation.transformVector(m_CameraDir));
    m_CameraUp = normalize(cameraRotation.transformVector(m_CameraUp));
    m_CameraRight = normalize(cross(m_CameraDir, m_CameraUp));

    UpdateWorldToView();
}

std::pair<bool, affine3> FirstPersonCamera::AnimateRoll(affine3 initialRotation)
{
    bool cameraDirty = false;
    affine3 cameraRotation = initialRotation;
    if (keyboardState[KeyboardControls::RollLeft] ||
        keyboardState[KeyboardControls::RollRight])
    {
        float roll = float(keyboardState[KeyboardControls::RollLeft]) * -m_RotateSpeed * 2.0f +
            float(keyboardState[KeyboardControls::RollRight]) * m_RotateSpeed * 2.0f;

        cameraRotation = rotation(m_CameraDir, roll) * cameraRotation;
        cameraDirty = true;
    }
    return std::make_pair(cameraDirty, cameraRotation);
}

void FirstPersonCamera::Animate(float deltaT)
{
    // track mouse delta
    float2 mouseMove = mousePos - mousePosPrev;
    mousePosPrev = mousePos;

    bool cameraDirty = false;
    affine3 cameraRotation = affine3::identity();

    // handle mouse rotation first
    // this will affect the movement vectors in the world matrix, which we use below
    if (mouseButtonState[MouseButtons::Left] && (mouseMove.x != 0 || mouseMove.y != 0))
    {
        float yaw = m_RotateSpeed * mouseMove.x;
        float pitch = m_RotateSpeed * mouseMove.y;

        cameraRotation = rotation(float3(0.f, 1.f, 0.f), -yaw);
        cameraRotation = rotation(m_CameraRight, -pitch) * cameraRotation;

        cameraDirty = true;
    }

    // handle keyboard roll next
    auto rollResult = AnimateRoll(cameraRotation);
    cameraDirty |= rollResult.first;
    cameraRotation = rollResult.second;

    // handle translation
    auto translateResult = AnimateTranslation(deltaT);
    cameraDirty |= translateResult.first;
    const float3& cameraMoveVec = translateResult.second;

    if (cameraDirty)
    {
        UpdateCamera(cameraMoveVec, cameraRotation);
    }
}

void FirstPersonCamera::AnimateSmooth(float deltaT)
{
    const float c_DampeningRate = 7.5f;
    float dampenWeight = exp(-c_DampeningRate * deltaT);

    float2 mouseMove{ 0, 0 };
    if (mouseButtonState[MouseButtons::Left])
    {
        if (!isMoving)
        {
            isMoving = true;
            mousePosPrev = mousePos;
        }

        mousePosDamp.x = lerp(mousePos.x, mousePosPrev.x, dampenWeight);
        mousePosDamp.y = lerp(mousePos.y, mousePosPrev.y, dampenWeight);

        // track mouse delta
        mouseMove = mousePosDamp - mousePosPrev;
        mousePosPrev = mousePosDamp;
    }
    else
    {
        isMoving = false;
    }

    bool cameraDirty = false;
    affine3 cameraRotation = affine3::identity();

    // handle mouse rotation first
    // this will affect the movement vectors in the world matrix, which we use below
    if (mouseMove.x || mouseMove.y)
    {
        float yaw = m_RotateSpeed * mouseMove.x;
        float pitch = m_RotateSpeed * mouseMove.y;

        cameraRotation = rotation(float3(0.f, 1.f, 0.f), -yaw);
        cameraRotation = rotation(m_CameraRight, -pitch) * cameraRotation;

        cameraDirty = true;
    }

    // handle keyboard roll next
    auto rollResult = AnimateRoll(cameraRotation);
    cameraDirty |= rollResult.first;
    cameraRotation = rollResult.second;

    // handle translation
    auto translateResult = AnimateTranslation(deltaT);
    cameraDirty |= translateResult.first;
    const float3& cameraMoveVec = translateResult.second;

    if (cameraDirty)
    {
        UpdateCamera(cameraMoveVec, cameraRotation);
    }
}

void ThirdPersonCamera::KeyboardUpdate(int key, int scancode, int action, int mods)
{
    if (keyboardMap.find(key) == keyboardMap.end())
    {
        return;
    }

    auto cameraKey = keyboardMap.at(key);
    if (action == GLFW_PRESS || action == GLFW_REPEAT)
    {
        keyboardState[cameraKey] = true;
    }
    else {
        keyboardState[cameraKey] = false;
    }
}

void ThirdPersonCamera::MousePosUpdate(double xpos, double ypos)
{
    m_MousePosPrev = m_MousePos;
    m_MousePos = float2(float(xpos), float(ypos));
}

void ThirdPersonCamera::MouseButtonUpdate(int button, int action, int mods)
{
    const bool pressed = (action == GLFW_PRESS);

    switch(button)
    {
    case GLFW_MOUSE_BUTTON_LEFT: mouseButtonState[MouseButtons::Left] = pressed; break;
    case GLFW_MOUSE_BUTTON_MIDDLE : mouseButtonState[MouseButtons::Middle] = pressed; break;
    case GLFW_MOUSE_BUTTON_RIGHT: mouseButtonState[MouseButtons::Right] = pressed; break;
    default: break;
    }
}

void ThirdPersonCamera::MouseScrollUpdate(double xoffset, double yoffset)
{
    const float scrollFactor = 1.15f;
    m_Distance = clamp(m_Distance * (yoffset < 0 ? scrollFactor : 1.0f / scrollFactor), m_MinDistance,  m_MaxDistance);
}

void ThirdPersonCamera::JoystickUpdate(int axis, float value)
{
    switch (axis)
    {
    case GLFW_GAMEPAD_AXIS_RIGHT_X: m_DeltaYaw = value; break;
    case GLFW_GAMEPAD_AXIS_RIGHT_Y: m_DeltaPitch = value; break;
    default: break;
    }
}

void ThirdPersonCamera::JoystickButtonUpdate(int button, bool pressed)
{
    switch (button)
    {
    case GLFW_GAMEPAD_BUTTON_B: if (pressed) m_DeltaDistance -= 1; break;
    case GLFW_GAMEPAD_BUTTON_A: if (pressed) m_DeltaDistance += 1; break;
    default: break;
    }
}

void ThirdPersonCamera::SetRotation(float yaw, float pitch)
{
    m_Yaw = yaw;
    m_Pitch = pitch;
}

void ThirdPersonCamera::SetView(const engine::PlanarView& view)
{
    m_ProjectionMatrix = view.GetProjectionMatrix(false);
    m_InverseProjectionMatrix = view.GetInverseProjectionMatrix(false);
    auto viewport = view.GetViewport();
    m_ViewportSize = float2(viewport.width(), viewport.height());
}

void ThirdPersonCamera::AnimateOrbit(float deltaT)
{
    if (mouseButtonState[MouseButtons::Left])
    {
        float2 mouseMove = m_MousePos - m_MousePosPrev;
        float rotateSpeed = .005f;  // mouse sensitivity in radians/pixel

        m_Yaw -= rotateSpeed * mouseMove.x;
        m_Pitch += rotateSpeed * mouseMove.y;
    }

    const float ORBIT_SENSITIVITY = 1.5f;
    const float ZOOM_SENSITIVITY = 40.f;
    m_Distance += ZOOM_SENSITIVITY * deltaT * m_DeltaDistance;
    m_Yaw += ORBIT_SENSITIVITY * deltaT * m_DeltaYaw;
    m_Pitch += ORBIT_SENSITIVITY * deltaT * m_DeltaPitch;

    m_Distance = clamp(m_Distance, m_MinDistance, m_MaxDistance);
    
    m_Pitch = clamp(m_Pitch, PI_f * -0.5f, PI_f * 0.5f);
    
    m_DeltaDistance = 0;
    m_DeltaYaw = 0;
    m_DeltaPitch = 0;
}

void ThirdPersonCamera::AnimateTranslation(const dm::float3x3& viewMatrix)
{
    // If the view parameters have never been set, we can't translate
    if (m_ViewportSize.x <= 0.f || m_ViewportSize.y <= 0.f)
        return;

    if (all(m_MousePos == m_MousePosPrev))
        return;

    if (mouseButtonState[MouseButtons::Middle])
    {
        float4 oldClipPos = float4(0.f, 0.f, m_Distance, 1.f) * m_ProjectionMatrix;
        oldClipPos /= oldClipPos.w;
        oldClipPos.x = 2.f * (m_MousePosPrev.x) / m_ViewportSize.x - 1.f;
        oldClipPos.y = 1.f - 2.f * (m_MousePosPrev.y) / m_ViewportSize.y;
        float4 newClipPos = oldClipPos;
        newClipPos.x = 2.f * (m_MousePos.x) / m_ViewportSize.x - 1.f;
        newClipPos.y = 1.f - 2.f * (m_MousePos.y) / m_ViewportSize.y;

        float4 oldViewPos = oldClipPos * m_InverseProjectionMatrix;
        oldViewPos /= oldViewPos.w;
        float4 newViewPos = newClipPos * m_InverseProjectionMatrix;
        newViewPos /= newViewPos.w;

        float2 viewMotion = oldViewPos.xy() - newViewPos.xy();

        m_TargetPos -= viewMotion.x * viewMatrix.row0;

        if (keyboardState[KeyboardControls::HorizontalPan])
        {
            float3 horizontalForward = float3(viewMatrix.row2.x, 0.f, viewMatrix.row2.z);
            float horizontalLength = length(horizontalForward);
            if (horizontalLength == 0.f)
                horizontalForward = float3(viewMatrix.row1.x, 0.f, viewMatrix.row1.z);
            horizontalForward = normalize(horizontalForward);
            m_TargetPos += viewMotion.y * horizontalForward * 1.5f;
        }
        else
            m_TargetPos += viewMotion.y * viewMatrix.row1;
    }
}

void ThirdPersonCamera::Animate(float deltaT)
{
    AnimateOrbit(deltaT);

    quat orbit = rotationQuat(float3(m_Pitch, m_Yaw, 0));

    const auto targetRotation = orbit.toMatrix();
    AnimateTranslation(targetRotation);

    const float3 vectorToCamera = -m_Distance * targetRotation.row2;

    const float3 camPos = m_TargetPos + vectorToCamera;

    m_CameraPos = camPos;
    m_CameraRight = -targetRotation.row0;
    m_CameraUp = targetRotation.row1;
    m_CameraDir = targetRotation.row2;
    UpdateWorldToView();
    
    m_MousePosPrev = m_MousePos;
}
