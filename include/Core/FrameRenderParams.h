#pragma once

#include "Core/PassTimer.h"
#include "Editor/TransformGizmo.h"
#include "Math/Math.h"
#include "Scene/PostProcessComponent.h"
#include "Scene/ViewMode.h"

#include <cstdint>
#include <memory>
#include <vector>

struct ImDrawData;

namespace Kiwi
{
    // Everything the render thread needs from the game thread for one frame, copied at the end of the game frame.
    // The render thread reads this instead of the scene, the camera or editor state.
    struct FrameRenderParams
    {
        ERenderPath RenderPath = ERenderPath::Deferred;
        EViewMode ViewMode = EViewMode::Lit;

        Mat4 ViewMatrix = Mat4::Identity();
        Mat4 ProjectionMatrix = Mat4::Identity();
        Vec3 CameraPosition = { 0.0f, 0.0f, 0.0f };
        float FieldOfView = 60.0f;
        float NearPlane = 0.1f;
        float FarPlane = 1000.0f;
        bool bHasCamera = false;

        uint32_t ViewWidth = 0;
        uint32_t ViewHeight = 0;
        float TotalTime = 0.0f;

        // Enabled materials of enabled post-process components, in scene order.
        std::vector<PostProcessMaterial> PostProcessEffects;

        GizmoDrawState Gizmo;

        int RayTracingSamplesPerPixel = 1;
        float RayTracingResolutionPercent = 50.0f;

        bool bReloadShaders = false;

        // This frame's ImGui draw lists; textures were already updated on the game thread.
        std::shared_ptr<ImDrawData> ImGuiDrawData;
    };

    // What the render thread reports back for the editor, published once per frame.
    struct RenderStats
    {
        std::vector<PassTimingEntry> PassTimings;
        double FrameTotalMs = 0.0;
        uint32_t VisibleItems = 0;
        uint32_t RayTraceWidth = 0;
        uint32_t RayTraceHeight = 0;
    };
}
