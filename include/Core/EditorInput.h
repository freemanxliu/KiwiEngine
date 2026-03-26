#pragma once

#include "Core/Window.h"
#include "Scene/Scene.h"
#include "Scene/CameraComponent.h"
#include <imgui.h>
#include <algorithm>

namespace Kiwi
{

    // ============================================================
    // EditorInput — Centralized editor keyboard/mouse input manager
    //
    // All editor-level input actions (camera navigation, shortcuts,
    // etc.) go through this class. To add a new keyboard action:
    //   1. Add a private handler method (e.g. HandleMyAction)
    //   2. Call it from Update()
    // ============================================================
    class EditorInput
    {
    public:
        EditorInput() = default;

        void Init(Window* window, Scene* scene)
        {
            m_Window = window;
            m_Scene  = scene;
        }

        // Call once per frame from OnUpdate
        void Update(float deltaTime)
        {
            if (!m_Window || !m_Scene)
                return;

            HandleCameraLook();
            HandleCameraMovement(deltaTime);

            // Save mouse position for next frame delta
            const auto& mouse = m_Window->GetMouseState();
            m_LastMouseX = mouse.X;
            m_LastMouseY = mouse.Y;
        }

        // --------------------------------------------------------
        // Configuration — exposed for UI
        // --------------------------------------------------------
        float GetCameraMoveSpeed() const { return m_CameraMoveSpeed; }
        void  SetCameraMoveSpeed(float s) { m_CameraMoveSpeed = s; }

    private:
        // --------------------------------------------------------
        // Camera look: hold Right Mouse + move mouse to rotate
        // Yaw (left/right) rotates around world Y axis
        // Pitch (up/down) rotates around local X axis, clamped to ~89 deg
        // --------------------------------------------------------
        void HandleCameraLook()
        {
            const auto& mouse = m_Window->GetMouseState();
            if (!mouse.RightDown)
            {
                m_WasRightDown = false;
                return;
            }

            auto* cam = m_Scene->GetActiveCamera();
            if (!cam)
                return;

            // Skip if ImGui is capturing mouse
            ImGuiIO& io = ImGui::GetIO();
            if (io.WantCaptureMouse)
                return;

            // First frame of right-click: just record position, don't rotate
            if (!m_WasRightDown)
            {
                m_WasRightDown = true;
                m_LastMouseX = mouse.X;
                m_LastMouseY = mouse.Y;
                return;
            }

            int32_t dx = mouse.X - m_LastMouseX;
            int32_t dy = mouse.Y - m_LastMouseY;

            if (dx == 0 && dy == 0)
                return;

            // Yaw: mouse moving right => increase Y rotation (turn right in LH)
            // Pitch: mouse moving down => increase X rotation (look down)
            cam->Rotation.y += (float)dx * m_MouseSensitivity;
            cam->Rotation.x += (float)dy * m_MouseSensitivity;

            // Clamp pitch to avoid gimbal lock
            cam->Rotation.x = std::max(-89.0f, std::min(89.0f, cam->Rotation.x));
        }

        // --------------------------------------------------------
        // Camera fly navigation: hold Right Mouse + WASD / Arrow keys
        // Moves on the horizontal (XZ) plane at constant speed.
        // --------------------------------------------------------
        void HandleCameraMovement(float dt)
        {
            const auto& mouse = m_Window->GetMouseState();
            if (!mouse.RightDown)
                return;

            auto* cam = m_Scene->GetActiveCamera();
            if (!cam)
                return;

            // Skip if ImGui is capturing keyboard
            ImGuiIO& io = ImGui::GetIO();
            if (io.WantCaptureKeyboard)
                return;

            const float speed = m_CameraMoveSpeed * dt;

            Vec3 forward = cam->GetForward();
            Vec3 right   = cam->GetRight();

            // Project forward onto horizontal plane (XZ) to avoid flying up/down
            Vec3 forwardH = { forward.x, 0.0f, forward.z };
            if (forwardH.Dot(forwardH) > 0.001f)
                forwardH = forwardH.Normalize();

            Vec3 delta = { 0, 0, 0 };

            const auto& keys = m_Window->GetKeyState();

            // W / Up Arrow => move forward
            if (keys.IsKeyDown('W') || keys.IsKeyDown(VK_UP))
                delta = delta + forwardH * speed;
            // S / Down Arrow => move backward
            if (keys.IsKeyDown('S') || keys.IsKeyDown(VK_DOWN))
                delta = delta - forwardH * speed;
            // D / Right Arrow => strafe right
            if (keys.IsKeyDown('D') || keys.IsKeyDown(VK_RIGHT))
                delta = delta + right * speed;
            // A / Left Arrow => strafe left
            if (keys.IsKeyDown('A') || keys.IsKeyDown(VK_LEFT))
                delta = delta - right * speed;

            cam->Position = cam->Position + delta;
        }

        // --------------------------------------------------------
        // Configuration
        // --------------------------------------------------------
        float m_CameraMoveSpeed  = 5.0f;   // units/sec
        float m_MouseSensitivity = 0.15f;  // degrees per pixel

        // --------------------------------------------------------
        // State
        // --------------------------------------------------------
        int32_t m_LastMouseX = 0;
        int32_t m_LastMouseY = 0;
        bool    m_WasRightDown = false;

        // --------------------------------------------------------
        // References (non-owning)
        // --------------------------------------------------------
        Window* m_Window = nullptr;
        Scene*  m_Scene  = nullptr;
    };

} // namespace Kiwi
