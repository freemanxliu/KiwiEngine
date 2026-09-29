#include "KiwiEngineApp.h"

#include "Debug/RenderDocIntegration.h"

#include <imgui.h>

#include <cmath>
#include <string>

namespace
{

constexpr float kToolbarButtonSize = 32.0f;
constexpr float kToolbarButtonGap = 6.0f;

// Top-right toolbar slots, laid out right to left. The gizmo mode group sits left of the last slot.
enum EToolbarSlot
{
    ToolbarSlot_RenderDoc = 0,
    ToolbarSlot_Stats,
    ToolbarSlot_ViewMode,
    ToolbarSlot_Camera,
    ToolbarSlot_ShaderReload,
};

float ToolbarSlotX(float overlayWidth, int slot)
{
    return overlayWidth - kToolbarButtonSize - 12.0f - slot * (kToolbarButtonSize + kToolbarButtonGap);
}

const ImGuiWindowFlags kOverlayWindowFlags =
    ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoCollapse |
    ImGuiWindowFlags_NoTitleBar |
    ImGuiWindowFlags_NoScrollbar |
    ImGuiWindowFlags_AlwaysAutoResize |
    ImGuiWindowFlags_NoFocusOnAppearing |
    ImGuiWindowFlags_NoNav;

// Transparent auto-sized window just below the menu bar, x relative to the main viewport.
// Always pair with EndToolbarWindow(), whatever this returns.
bool BeginToolbarWindow(const char* id, float x)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + x, vp->Pos.y + ImGui::GetFrameHeight() + 6.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    return ImGui::Begin(id, nullptr, kOverlayWindowFlags);
}

void EndToolbarWindow()
{
    ImGui::End();
    ImGui::PopStyleVar(2);
}

struct ButtonColors
{
    ImVec4 Normal;
    ImVec4 Hovered;
    ImVec4 Active;
};

const ButtonColors kNeutralButton = {
    ImVec4(0.25f, 0.32f, 0.38f, 0.95f),
    ImVec4(0.32f, 0.42f, 0.50f, 1.0f),
    ImVec4(0.18f, 0.25f, 0.30f, 1.0f),
};

// Square rounded button. Callers draw their icon over GetItemRectMin/Max afterwards.
bool ToolbarButton(const char* label, const ButtonColors& colors)
{
    ImGui::PushStyleColor(ImGuiCol_Button, colors.Normal);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors.Hovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors.Active);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    bool pressed = ImGui::Button(label, ImVec2(kToolbarButtonSize, kToolbarButtonSize));
    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(4);
    return pressed;
}

void ToolbarTooltip(const char* title, const char* hint)
{
    if (!ImGui::IsItemHovered())
        return;
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(title);
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", hint);
    ImGui::EndTooltip();
}

ImVec2 LastItemCenter()
{
    ImVec2 btnMin = ImGui::GetItemRectMin();
    ImVec2 btnMax = ImGui::GetItemRectMax();
    return ImVec2((btnMin.x + btnMax.x) * 0.5f, (btnMin.y + btnMax.y) * 0.5f);
}

} // namespace

// ImGui positions are in points. GetWidth() is framebuffer pixels, which
// is 2x on a Retina display and would place these overlays off-screen.
float KiwiEngineApp::OverlayWidth() const
{
    const float displayWidth = ImGui::GetIO().DisplaySize.x;
    return displayWidth > 0.0f ? displayWidth : (float)GetWindow()->GetWidth();
}

void KiwiEngineApp::DrawRenderDocOverlay()
{
    auto& rdoc = RenderDocIntegration::Get();
    if (!rdoc.IsAvailable()) return;
    // RenderDoc capture not supported in Vulkan mode (RenderDoc + NVIDIA Vulkan ICD conflict)
    if (GetCurrentRHIType() == RHI_API_TYPE::VULKAN) return;

    if (BeginToolbarWindow("##RenderDocBtn", ToolbarSlotX(OverlayWidth(), ToolbarSlot_RenderDoc)))
    {
        bool capturing = rdoc.IsFrameCapturing();

        // RenderDoc brand colors: dark blue background, white icon
        ButtonColors colors = capturing
            ? ButtonColors{ ImVec4(0.7f, 0.3f, 0.1f, 0.95f), ImVec4(0.8f, 0.4f, 0.2f, 1.0f), ImVec4(0.6f, 0.2f, 0.1f, 1.0f) }
            : ButtonColors{ ImVec4(0.22f, 0.30f, 0.42f, 0.95f), ImVec4(0.29f, 0.42f, 0.60f, 1.0f), ImVec4(0.16f, 0.23f, 0.32f, 1.0f) };

        if (ToolbarButton(capturing ? "..." : "RD", colors) && !capturing)
        {
            rdoc.TriggerCapture();
            CaptureTriggered = true;
            AutoOpenRenderDoc = true;
        }

        uint32_t numCaptures = rdoc.GetNumCaptures();
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("RenderDoc Capture");
            if (numCaptures > 0)
                ImGui::Text("Captures: %u", numCaptures);
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Click to capture & open");
            ImGui::EndTooltip();
        }

        // Auto-open RenderDoc after capture completes
        if (CaptureTriggered && numCaptures > LastCaptureCount)
        {
            LastCaptureCount = numCaptures;
            CaptureTriggered = false;

            if (AutoOpenRenderDoc)
            {
                AutoOpenRenderDoc = false;
                rdoc.LaunchReplayUI();
            }
        }

        // Lens icon
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 center = LastItemCenter();
        ImU32 white = IM_COL32(255, 255, 255, 220);
        drawList->AddCircle(center, kToolbarButtonSize * 0.35f, white, 24, 2.0f);
        drawList->AddCircleFilled(center, kToolbarButtonSize * 0.18f, white);
    }
    EndToolbarWindow();
}

void KiwiEngineApp::DrawStatsOverlay()
{
    if (BeginToolbarWindow("##StatsBtn", ToolbarSlotX(OverlayWidth(), ToolbarSlot_Stats)))
    {
        // Teal while the stats panel is open
        ButtonColors colors = ShowStats
            ? ButtonColors{ ImVec4(0.15f, 0.50f, 0.45f, 0.95f), ImVec4(0.20f, 0.60f, 0.55f, 1.0f), ImVec4(0.10f, 0.40f, 0.35f, 1.0f) }
            : ButtonColors{ ImVec4(0.25f, 0.32f, 0.38f, 0.95f), ImVec4(0.32f, 0.42f, 0.50f, 1.0f), ImVec4(0.18f, 0.25f, 0.30f, 1.0f) };

        if (ToolbarButton("##StatsIcon", colors))
            ShowStats = !ShowStats;
        ToolbarTooltip("Render Stats", ShowStats ? "Click to hide stats" : "Click to show stats");

        // Bar chart icon: three bars of different heights
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 center = LastItemCenter();
        ImU32 white = IM_COL32(255, 255, 255, 220);
        float barW = 3.0f;
        float gap = 2.0f;
        float baseY = center.y + 6.0f;
        float heights[] = { 8.0f, 14.0f, 11.0f };
        float startX = center.x - (barW * 3 + gap * 2) * 0.5f;
        for (int i = 0; i < 3; i++)
        {
            float x = startX + i * (barW + gap);
            drawList->AddRectFilled(ImVec2(x, baseY - heights[i]), ImVec2(x + barW, baseY), white, 1.0f);
        }
    }
    EndToolbarWindow();

    if (ShowStats)
        DrawStatsPanel();
}

void KiwiEngineApp::DrawStatsPanel()
{
    float menuBarHeight = ImGui::GetFrameHeight();
    float windowWidth = OverlayWidth();
    float panelWidth = 240.0f;

    ImGuiViewport* mvp = ImGui::GetMainViewport();
    float vpX = mvp->Pos.x, vpY = mvp->Pos.y;

    ImGui::SetNextWindowPos(
        ImVec2(vpX + windowWidth - panelWidth - 8.0f, vpY + menuBarHeight + 44.0f),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelWidth, 0), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.14f, 0.18f, 0.92f));

    if (ImGui::Begin("##StatsPanel", nullptr, flags))
    {
        // Header
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.75f, 1.0f), "Render Stats");
        ImGui::Separator();

        // FPS
        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("FPS: %.1f (%.2f ms)", io.Framerate, 1000.0f / io.Framerate);

        // RHI backend
        auto rhiType = GetCurrentRHIType();
        const char* rhiName = (rhiType == RHI_API_TYPE::DX11) ? "DX11" :
                              (rhiType == RHI_API_TYPE::DX12) ? "DX12" :
                              (rhiType == RHI_API_TYPE::OPENGL) ? "OpenGL" :
                              (rhiType == RHI_API_TYPE::VULKAN) ? "Vulkan" :
                              (rhiType == RHI_API_TYPE::METAL) ? "Metal" : "Unknown";
        ImGui::Text("RHI: %s", rhiName);

        ImGui::Separator();

        // Pass timings
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Pass Timings (CPU)");

        // Published by the render thread at the end of the frame it last finished.
        const RenderStats stats = GetRenderStats();
        const auto& entries = stats.PassTimings;
        double totalMs = stats.FrameTotalMs;

        // Bar width for visual proportions
        float maxBarWidth = panelWidth - 100.0f;

        for (const auto& entry : entries)
        {
            // Color-code by pass name
            ImVec4 barColor = ImVec4(0.4f, 0.6f, 0.8f, 0.8f); // default blue
            if (entry.Name.find("Geometry") != std::string::npos)
                barColor = ImVec4(0.3f, 0.75f, 0.4f, 0.8f);   // green
            else if (entry.Name.find("Gizmo") != std::string::npos)
                barColor = ImVec4(0.9f, 0.7f, 0.2f, 0.8f);    // yellow
            else if (entry.Name.find("Post-Process") != std::string::npos)
                barColor = ImVec4(0.7f, 0.4f, 0.8f, 0.8f);    // purple
            else if (entry.Name.find("ImGui") != std::string::npos)
                barColor = ImVec4(0.8f, 0.45f, 0.3f, 0.8f);   // orange

            // Pass name and time
            ImGui::Text("%s", entry.Name.c_str());
            ImGui::SameLine(140.0f);
            ImGui::Text("%.3f ms", entry.TimeMs);

            // Proportion bar
            float fraction = (totalMs > 0.001) ? (float)(entry.TimeMs / totalMs) : 0.0f;
            float barWidth = fraction * maxBarWidth;
            if (barWidth < 2.0f) barWidth = 2.0f;

            ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(
                cursor,
                ImVec2(cursor.x + barWidth, cursor.y + 4.0f),
                ImGui::GetColorU32(barColor),
                2.0f);
            // Also draw background bar
            drawList->AddRectFilled(
                ImVec2(cursor.x + barWidth, cursor.y),
                ImVec2(cursor.x + maxBarWidth, cursor.y + 4.0f),
                IM_COL32(60, 60, 60, 100),
                2.0f);
            ImGui::Dummy(ImVec2(maxBarWidth, 6.0f));
        }

        if (!entries.empty())
        {
            ImGui::Separator();
            ImGui::Text("Total");
            ImGui::SameLine(140.0f);
            ImGui::Text("%.3f ms", totalMs);
        }

        // Render list info
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Scene");
        ImGui::Text("Objects: %d", (int)Scene.GetObjects().size());
        ImGui::Text("Visible: %u", stats.VisibleItems);
        ImGui::Text("Threading: %s", GetThreadingModeName(GetRenderingThread().GetMode()));
    }
    ImGui::End();
    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar(2);
}

void KiwiEngineApp::DrawViewModeButton()
{
    if (BeginToolbarWindow("##ViewModeBtn", ToolbarSlotX(OverlayWidth(), ToolbarSlot_ViewMode)))
    {
        bool canDeferred = IsDeferredRHI(GetCurrentRHIType());
        ERenderPath defaultPath = canDeferred ? ERenderPath::Deferred : ERenderPath::Forward;
        bool isLit = (ViewMode == EViewMode::Lit && RenderPath == defaultPath);

        // Non-Lit modes get a yellow/orange tint to indicate active override
        ButtonColors colors = isLit
            ? kNeutralButton
            : ButtonColors{ ImVec4(0.55f, 0.45f, 0.10f, 0.95f), ImVec4(0.65f, 0.55f, 0.15f, 1.0f), ImVec4(0.45f, 0.35f, 0.08f, 1.0f) };

        if (ToolbarButton("##ViewModeIcon", colors))
            ImGui::OpenPopup("ViewModePopup");

        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Pipeline: %s", GetRenderPathName(RenderPath));
            ImGui::Text("View Mode: %s", GetViewModeName(ViewMode));
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Click to change view mode");
            ImGui::EndTooltip();
        }

        // Eye icon
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 center = LastItemCenter();
        ImU32 iconColor = isLit ? IM_COL32(200, 200, 200, 255) : IM_COL32(255, 220, 80, 255);
        float r = 7.0f;
        drawList->AddEllipse(center, ImVec2(r, r * 0.55f), iconColor, 0.0f, 0, 1.5f);
        drawList->AddCircleFilled(center, 2.5f, iconColor);

        if (ImGui::BeginPopup("ViewModePopup"))
        {
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Pipeline");
            ImGui::Separator();

            if (ImGui::MenuItem("Deferred", nullptr, RenderPath == ERenderPath::Deferred, canDeferred))
                RenderPath = ERenderPath::Deferred;
            if (ImGui::MenuItem("Forward", nullptr, RenderPath == ERenderPath::Forward))
            {
                RenderPath = ERenderPath::Forward;
                if (IsBufferVisualization(ViewMode))
                    ViewMode = EViewMode::Lit;
            }
            if (ImGui::MenuItem("Ray Tracing", nullptr, RenderPath == ERenderPath::RayTracing))
            {
                RenderPath = ERenderPath::RayTracing;
                if (IsBufferVisualization(ViewMode))
                    ViewMode = EViewMode::Lit;
            }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "View Mode");
            ImGui::Separator();

            if (ImGui::MenuItem("Lit", nullptr, ViewMode == EViewMode::Lit))
                ViewMode = EViewMode::Lit;

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Buffer Visualization");

            bool bufferVis = canDeferred && RenderPath == ERenderPath::Deferred;
            if (ImGui::MenuItem("BaseColor", nullptr, ViewMode == EViewMode::BaseColor, bufferVis))
                ViewMode = EViewMode::BaseColor;
            if (ImGui::MenuItem("Roughness", nullptr, ViewMode == EViewMode::Roughness, bufferVis))
                ViewMode = EViewMode::Roughness;
            if (ImGui::MenuItem("Metallic", nullptr, ViewMode == EViewMode::Metallic, bufferVis))
                ViewMode = EViewMode::Metallic;

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Debug");

            if (ImGui::MenuItem("Unlit", nullptr, ViewMode == EViewMode::Unlit))
                ViewMode = EViewMode::Unlit;

            ImGui::EndPopup();
        }
    }
    EndToolbarWindow();
}

void KiwiEngineApp::DrawShaderReloadButton()
{
    if (BeginToolbarWindow("##ShaderReloadBtn", ToolbarSlotX(OverlayWidth(), ToolbarSlot_ShaderReload)))
    {
        if (ToolbarButton("##ShaderReloadIcon", kNeutralButton))
            PendingShaderReload = true;
        ToolbarTooltip("Hot-Reload Shaders", "Recompile modified shaders (F5)");

        // Circular arrow: 3/4 arc with an arrowhead, gap at the top-right
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 center = LastItemCenter();
        ImU32 iconColor = IM_COL32(200, 200, 200, 255);
        float radius = 7.0f;
        float thickness = 1.5f;

        const int segments = 20;
        float startAngle = -PI * 0.25f;  // -45°
        float endAngle = PI * 1.25f;     // 225°
        for (int i = 0; i < segments; i++)
        {
            float t0 = startAngle + (endAngle - startAngle) * ((float)i / segments);
            float t1 = startAngle + (endAngle - startAngle) * ((float)(i + 1) / segments);
            ImVec2 p0 = ImVec2(center.x + cosf(t0) * radius, center.y + sinf(t0) * radius);
            ImVec2 p1 = ImVec2(center.x + cosf(t1) * radius, center.y + sinf(t1) * radius);
            drawList->AddLine(p0, p1, iconColor, thickness);
        }

        ImVec2 tipPos = ImVec2(center.x + cosf(endAngle) * radius,
                               center.y + sinf(endAngle) * radius);
        float arrowSize = 3.5f;
        float arrowAngle1 = endAngle + 2.2f;
        float arrowAngle2 = endAngle + 0.8f;
        ImVec2 a1 = ImVec2(tipPos.x + cosf(arrowAngle1) * arrowSize,
                           tipPos.y + sinf(arrowAngle1) * arrowSize);
        ImVec2 a2 = ImVec2(tipPos.x + cosf(arrowAngle2) * arrowSize,
                           tipPos.y + sinf(arrowAngle2) * arrowSize);
        drawList->AddTriangleFilled(tipPos, a1, a2, iconColor);
    }
    EndToolbarWindow();
}

void KiwiEngineApp::DrawCameraButton()
{
    if (BeginToolbarWindow("##CameraBtn", ToolbarSlotX(OverlayWidth(), ToolbarSlot_Camera)))
    {
        if (ToolbarButton("##CameraIcon", kNeutralButton))
            ImGui::OpenPopup("CameraSettingsPopup");
        ToolbarTooltip("Camera Settings", "Move speed & FOV");

        // Film camera side profile
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 center = LastItemCenter();
        ImU32 iconColor = IM_COL32(200, 200, 200, 255);
        ImU32 reelHole = IM_COL32(40, 50, 60, 255);
        // Camera body
        drawList->AddRectFilled(
            ImVec2(center.x - 6.0f, center.y - 3.0f),
            ImVec2(center.x + 6.0f, center.y + 5.0f),
            iconColor, 2.0f);
        // Lens barrel (front)
        drawList->AddRectFilled(
            ImVec2(center.x + 6.0f, center.y - 1.0f),
            ImVec2(center.x + 10.0f, center.y + 3.0f),
            iconColor, 1.0f);
        // Film reels
        drawList->AddCircleFilled(ImVec2(center.x - 3.0f, center.y - 6.0f), 4.0f, iconColor);
        drawList->AddCircleFilled(ImVec2(center.x - 3.0f, center.y - 6.0f), 1.5f, reelHole);
        drawList->AddCircleFilled(ImVec2(center.x + 4.0f, center.y - 5.0f), 3.0f, iconColor);
        drawList->AddCircleFilled(ImVec2(center.x + 4.0f, center.y - 5.0f), 1.2f, reelHole);

        if (ImGui::BeginPopup("CameraSettingsPopup"))
        {
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Camera Settings");
            ImGui::Separator();

            float moveSpeed = EditorInput.GetCameraMoveSpeed();
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::SliderFloat("Move Speed", &moveSpeed, 0.5f, 50.0f, "%.1f"))
                EditorInput.SetCameraMoveSpeed(moveSpeed);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Camera movement speed when holding Right Mouse + WASD");

            // FOV slider (only for perspective cameras)
            auto* cam = Scene.GetActiveCamera();
            if (cam && cam->Projection == ECameraProjection::Perspective)
            {
                ImGui::SetNextItemWidth(180.0f);
                ImGui::SliderFloat("FOV", &cam->FieldOfView, 10.0f, 120.0f, "%.0f deg");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Camera field of view (degrees)");
            }

            ImGui::EndPopup();
        }
    }
    EndToolbarWindow();
}

void KiwiEngineApp::DrawGizmoModeBar()
{
    float menuBarHeight = ImGui::GetFrameHeight();
    ImGuiViewport* mvp = ImGui::GetMainViewport();
    float vpX = mvp->Pos.x, vpY = mvp->Pos.y;

    // W / E / R shortcut keys (only when ImGui doesn't want keyboard)
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureKeyboard)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false) && !Gizmo.IsDragging())
            Gizmo.SetMode(EGizmoMode::Translate);
        if (ImGui::IsKeyPressed(ImGuiKey_E, false) && !Gizmo.IsDragging())
            Gizmo.SetMode(EGizmoMode::Rotate);
        if (ImGui::IsKeyPressed(ImGuiKey_R, false) && !Gizmo.IsDragging())
            Gizmo.SetMode(EGizmoMode::Scale);
    }

    // ---- Left toolbar (W/E/R text buttons) ----
    {
        float btnW = 36.0f, btnH = 26.0f, gap = 2.0f, padLeft = 8.0f;
        ImGui::SetNextWindowPos(
            ImVec2(vpX + padLeft, vpY + menuBarHeight + 6.0f),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.75f);

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(gap, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        if (ImGui::Begin("##GizmoModeBar", nullptr, flags))
        {
            struct ModeBtn { const char* label; EGizmoMode mode; const char* tip; };
            ModeBtn btns[] = {
                { "W", EGizmoMode::Translate, "Translate (W)" },
                { "E", EGizmoMode::Rotate,    "Rotate (E)"    },
                { "R", EGizmoMode::Scale,     "Scale (R)"     },
            };

            for (int i = 0; i < 3; i++)
            {
                bool active = (Gizmo.GetMode() == btns[i].mode);
                if (active)
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.85f, 1.0f));
                else
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.28f, 0.34f, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.60f, 0.90f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.15f, 0.45f, 0.75f, 1.0f));

                if (ImGui::Button(btns[i].label, ImVec2(btnW, btnH)))
                    Gizmo.SetMode(btns[i].mode);

                ImGui::PopStyleColor(3);

                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(btns[i].tip);
                    ImGui::EndTooltip();
                }

                if (i < 2) ImGui::SameLine();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(4);
    }

    // ---- Right toolbar: 3 icon buttons, 26px, left of the ShaderReload button ----
    {
        float gizmoBtnSize = 26.0f;
        float gizmoGap     = 2.0f;
        float gizmoGroupW  = gizmoBtnSize * 3 + gizmoGap * 2;
        float gizmoBtnX    = ToolbarSlotX(OverlayWidth(), ToolbarSlot_ShaderReload) - gizmoGroupW - kToolbarButtonGap;

        ImGui::SetNextWindowPos(
            ImVec2(vpX + gizmoBtnX, vpY + menuBarHeight + 6.0f),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,      ImVec2(gizmoGap, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,    6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        if (ImGui::Begin("##GizmoModeTopRight", nullptr, flags))
        {
            struct GizmoBtn { EGizmoMode mode; const char* tip; };
            GizmoBtn btns[3] = {
                { EGizmoMode::Translate, "Translate (W)" },
                { EGizmoMode::Rotate,    "Rotate (E)"    },
                { EGizmoMode::Scale,     "Scale (R)"     },
            };

            for (int i = 0; i < 3; i++)
            {
                bool active = (Gizmo.GetMode() == btns[i].mode);

                ImVec4 btnCol    = active
                    ? ImVec4(0.18f, 0.50f, 0.82f, 1.0f)
                    : ImVec4(0.20f, 0.26f, 0.32f, 0.92f);
                ImVec4 hoverCol  = ImVec4(0.28f, 0.58f, 0.90f, 1.0f);
                ImVec4 activeCol = ImVec4(0.14f, 0.42f, 0.72f, 1.0f);

                ImGui::PushStyleColor(ImGuiCol_Button,        btnCol);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverCol);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  activeCol);

                if (ImGui::Button(("##gtr" + std::to_string(i)).c_str(),
                                  ImVec2(gizmoBtnSize, gizmoBtnSize)))
                    Gizmo.SetMode(btns[i].mode);

                ImGui::PopStyleColor(3);

                // Custom icon drawn over the button area
                ImVec2 bMin = ImGui::GetItemRectMin();
                ImVec2 bMax = ImGui::GetItemRectMax();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 ctr = ImVec2((bMin.x + bMax.x) * 0.5f, (bMin.y + bMax.y) * 0.5f);
                ImU32 iconCol = active
                    ? IM_COL32(255, 255, 255, 255)
                    : IM_COL32(160, 180, 200, 220);

                if (btns[i].mode == EGizmoMode::Translate)
                {
                    // Move: cross arrows icon
                    float arm = 7.0f, head = 3.0f;
                    // horizontal arrow
                    dl->AddLine(ImVec2(ctr.x - arm, ctr.y), ImVec2(ctr.x + arm, ctr.y), iconCol, 1.5f);
                    dl->AddTriangleFilled(
                        ImVec2(ctr.x + arm,       ctr.y),
                        ImVec2(ctr.x + arm - head, ctr.y - head * 0.6f),
                        ImVec2(ctr.x + arm - head, ctr.y + head * 0.6f), iconCol);
                    dl->AddTriangleFilled(
                        ImVec2(ctr.x - arm,       ctr.y),
                        ImVec2(ctr.x - arm + head, ctr.y - head * 0.6f),
                        ImVec2(ctr.x - arm + head, ctr.y + head * 0.6f), iconCol);
                    // vertical arrow
                    dl->AddLine(ImVec2(ctr.x, ctr.y - arm), ImVec2(ctr.x, ctr.y + arm), iconCol, 1.5f);
                    dl->AddTriangleFilled(
                        ImVec2(ctr.x,             ctr.y - arm),
                        ImVec2(ctr.x - head*0.6f, ctr.y - arm + head),
                        ImVec2(ctr.x + head*0.6f, ctr.y - arm + head), iconCol);
                    dl->AddTriangleFilled(
                        ImVec2(ctr.x,             ctr.y + arm),
                        ImVec2(ctr.x - head*0.6f, ctr.y + arm - head),
                        ImVec2(ctr.x + head*0.6f, ctr.y + arm - head), iconCol);
                }
                else if (btns[i].mode == EGizmoMode::Rotate)
                {
                    // Rotate: circular arc with arrowhead
                    float r = 7.0f;
                    const int arcSegs = 20;
                    float startAngle = 0.3f;
                    float endAngle   = 2.0f * 3.14159f - 0.3f;
                    for (int s = 0; s < arcSegs; s++)
                    {
                        float a0 = startAngle + (endAngle - startAngle) * s / arcSegs;
                        float a1 = startAngle + (endAngle - startAngle) * (s + 1) / arcSegs;
                        dl->AddLine(
                            ImVec2(ctr.x + r * cosf(a0), ctr.y + r * sinf(a0)),
                            ImVec2(ctr.x + r * cosf(a1), ctr.y + r * sinf(a1)),
                            iconCol, 1.8f);
                    }
                    // Arrowhead at end
                    float ae = endAngle;
                    float tang = ae + 3.14159f * 0.5f;
                    ImVec2 tip(ctr.x + r * cosf(ae), ctr.y + r * sinf(ae));
                    float hs = 3.5f;
                    dl->AddTriangleFilled(
                        tip,
                        ImVec2(tip.x + hs * cosf(tang - 2.4f), tip.y + hs * sinf(tang - 2.4f)),
                        ImVec2(tip.x + hs * cosf(tang + 2.4f), tip.y + hs * sinf(tang + 2.4f)),
                        iconCol);
                }
                else // Scale
                {
                    // Scale: 3 axis stubs with square end caps
                    float arm = 6.5f, sq = 2.2f;
                    // X axis (right, red-ish but use iconCol)
                    dl->AddLine(ImVec2(ctr.x, ctr.y), ImVec2(ctr.x + arm, ctr.y), iconCol, 1.5f);
                    dl->AddRectFilled(
                        ImVec2(ctr.x + arm - sq, ctr.y - sq),
                        ImVec2(ctr.x + arm + sq, ctr.y + sq), iconCol);
                    // Y axis (up)
                    dl->AddLine(ImVec2(ctr.x, ctr.y), ImVec2(ctr.x, ctr.y - arm), iconCol, 1.5f);
                    dl->AddRectFilled(
                        ImVec2(ctr.x - sq, ctr.y - arm - sq),
                        ImVec2(ctr.x + sq, ctr.y - arm + sq), iconCol);
                    // Z axis (diagonal hint)
                    float d = arm * 0.70f;
                    dl->AddLine(ImVec2(ctr.x, ctr.y), ImVec2(ctr.x - d, ctr.y + d), iconCol, 1.5f);
                    dl->AddRectFilled(
                        ImVec2(ctr.x - d - sq, ctr.y + d - sq),
                        ImVec2(ctr.x - d + sq, ctr.y + d + sq), iconCol);
                }

                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(btns[i].tip);
                    ImGui::EndTooltip();
                }

                if (i < 2) ImGui::SameLine();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(4);
    }
}
