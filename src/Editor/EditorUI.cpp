#include "KiwiEngineApp.h"

#include "Debug/RenderDocIntegration.h"

#include <imgui.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>

void KiwiEngineApp::DrawEditorUI()
{
    DrawMenuBar();
    DrawRenderDocOverlay();
    DrawStatsOverlay();
    DrawViewModeButton();
    DrawCameraButton();
    DrawShaderReloadButton();
    DrawGizmoModeBar();
    DrawScenePanel();
    DrawContentBrowser();
    DrawMaterialEditor();
}

void KiwiEngineApp::DrawMenuBar()
{
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Create Scene"))
            {
                m_Scene.Clear();
                m_GPUMeshes.clear();
                m_Scene.SetName("New Scene");
            }

            // Open Scene — lists all .json files in Scenes/ directory
            if (ImGui::BeginMenu("Open Scene"))
            {
                namespace fs = std::filesystem;
                bool hasFiles = false;
                if (fs::exists(m_ScenesDir))
                {
                    for (auto& entry : fs::directory_iterator(m_ScenesDir))
                    {
                        if (entry.is_regular_file() && entry.path().extension() == ".json")
                        {
                            std::string filename = entry.path().stem().string();
                            if (ImGui::MenuItem(filename.c_str()))
                            {
                                if (m_Scene.LoadFromFile(entry.path().string()))
                                {
                                    RebuildAllGPUBuffers();
                                }
                            }
                            hasFiles = true;
                        }
                    }
                }
                if (!hasFiles)
                {
                    ImGui::TextDisabled("(no scene files)");
                }
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Save Scene"))
            {
                m_ShowSaveDialog = true;
                // Pre-fill with current scene name
                std::string name = m_Scene.GetName();
                strncpy(m_SaveSceneName, name.c_str(), sizeof(m_SaveSceneName) - 1);
                m_SaveSceneName[sizeof(m_SaveSceneName) - 1] = '\0';
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Rendering"))
        {
            if (ImGui::BeginMenu("RHI"))
            {
                auto currentRHI = GetCurrentRHIType();

#if defined(__APPLE__)
                if (ImGui::MenuItem("Metal", nullptr, currentRHI == RHI_API_TYPE::METAL, false))
                {
                }
#else
                if (ImGui::MenuItem("Direct3D 11", nullptr,
                    currentRHI == RHI_API_TYPE::DX11, currentRHI != RHI_API_TYPE::DX11))
                {
                    m_PendingRHISwitch = true;
                    m_PendingRHIType = RHI_API_TYPE::DX11;
                }
                if (ImGui::MenuItem("Direct3D 12", nullptr,
                    currentRHI == RHI_API_TYPE::DX12, currentRHI != RHI_API_TYPE::DX12))
                {
                    m_PendingRHISwitch = true;
                    m_PendingRHIType = RHI_API_TYPE::DX12;
                }
                if (ImGui::MenuItem("OpenGL", nullptr,
                    currentRHI == RHI_API_TYPE::OPENGL, currentRHI != RHI_API_TYPE::OPENGL))
                {
                    m_PendingRHISwitch = true;
                    m_PendingRHIType = RHI_API_TYPE::OPENGL;
                }

                // Vulkan is incompatible with RenderDoc in-process hook (NVIDIA nvoglv64.dll conflict)
                bool rdocLoaded = RenderDocIntegration::Get().IsAvailable();
                bool canSwitchVulkan = !rdocLoaded && (currentRHI != RHI_API_TYPE::VULKAN);
                if (ImGui::MenuItem("Vulkan", rdocLoaded ? "(RenderDoc active)" : nullptr,
                    currentRHI == RHI_API_TYPE::VULKAN, canSwitchVulkan))
                {
                    m_PendingRHISwitch = true;
                    m_PendingRHIType = RHI_API_TYPE::VULKAN;
                }
#endif

                ImGui::EndMenu();
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Reload All Shaders", "F5"))
            {
                m_PendingShaderReload = true;
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window"))
        {
            if (ImGui::MenuItem("Content Browser", "Ctrl+Space", m_ShowContentBrowser))
            {
                m_ShowContentBrowser = !m_ShowContentBrowser;
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    // Save Scene dialog (modal popup)
    if (m_ShowSaveDialog)
    {
        ImGui::OpenPopup("Save Scene##SaveDlg");
        m_ShowSaveDialog = false;
    }

    if (ImGui::BeginPopupModal("Save Scene##SaveDlg", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Scene name:");
        ImGui::SetNextItemWidth(300.0f);
        bool enterPressed = ImGui::InputText("##SceneName", m_SaveSceneName,
            sizeof(m_SaveSceneName), ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::Spacing();

        bool doSave = false;
        if (ImGui::Button("Save", ImVec2(120, 0)) || enterPressed)
            doSave = true;
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();

        if (doSave && m_SaveSceneName[0] != '\0')
        {
            namespace fs = std::filesystem;
            std::string name(m_SaveSceneName);
            m_Scene.SetName(name);
            std::string filepath = m_ScenesDir + "/" + name + ".json";
            fs::create_directories(m_ScenesDir);
            m_Scene.SaveToFile(filepath);
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void KiwiEngineApp::DrawScenePanel()
{
    float menuBarHeight = ImGui::GetFrameHeight();

    // Get main viewport position (for Multi-Viewport mode, this is the main window's screen position)
    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    ImVec2 vpPos = mainViewport->Pos;
    ImVec2 vpSize = mainViewport->Size;

    // Side panel below menu bar — anchored to main window, not screen
    ImGui::SetNextWindowPos(ImVec2(vpPos.x, vpPos.y + menuBarHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(320, vpSize.y - menuBarHeight), ImGuiCond_Always);

    ImGui::Begin("Scene Panel", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    // Show current RHI
    auto rhiType = GetCurrentRHIType();
    const char* rhiName = (rhiType == RHI_API_TYPE::DX11) ? "Direct3D 11" :
                          (rhiType == RHI_API_TYPE::DX12) ? "Direct3D 12" :
                          (rhiType == RHI_API_TYPE::OPENGL) ? "OpenGL" :
                          (rhiType == RHI_API_TYPE::VULKAN) ? "Vulkan" :
                          (rhiType == RHI_API_TYPE::METAL) ? "Metal" : "Unknown";
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "RHI: %s", rhiName);
    ImGui::Separator();

    // Scene object list
    ImGui::Text("Objects (%d)", (int)m_Scene.GetObjects().size());
    ImGui::BeginChild("ObjectList", ImVec2(0, 120), true);
    for (auto& objPtr : m_Scene.GetObjects())
    {
        auto& obj = *objPtr;
        bool selected = obj.Selected;

        // Icon prefix based on component type
        const char* icon = "";
        if (obj.HasComponent<CameraComponent>())
        {
            auto* cam = obj.GetComponent<CameraComponent>();
            icon = (cam && cam->IsMainCamera) ? "[C*] " : "[C] ";
        }
        else if (obj.HasComponent<LightComponent>()) icon = "[L] ";
        else if (obj.HasComponent<MeshComponent>()) icon = "[M] ";
        else if (obj.HasComponent<PostProcessComponent>()) icon = "[PP] ";

        std::string label = std::string(icon) + obj.Name;
        if (ImGui::Selectable(label.c_str(), &selected))
        {
            m_Scene.SelectObject(obj.ID);
        }
    }
    ImGui::EndChild();

    if (ImGui::Button("Delete Selected") && m_Scene.GetSelectedObject())
    {
        uint32_t selID = (uint32_t)m_Scene.GetSelectedID();
        m_Scene.RemoveObject(selID);
        RebuildAllGPUBuffers();
    }

    ImGui::Separator();

    // Tabs
    if (ImGui::BeginTabBar("MainTabs"))
    {
        if (ImGui::BeginTabItem("Detail"))
        {
            DrawDetailTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Placer"))
        {
            DrawPlacerTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Rendering"))
        {
            DrawRenderingTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void KiwiEngineApp::DrawPlacerTab()
{
    ImGui::Text("Add objects to the scene:");
    ImGui::Separator();

    struct PlacerEntry
    {
        const char* label;
        EPrimitiveType type;
    };

    static PlacerEntry entries[] = {
        { "Cube",     EPrimitiveType::Cube },
        { "Sphere",   EPrimitiveType::Sphere },
        { "Cylinder", EPrimitiveType::Cylinder },
        { "Floor",    EPrimitiveType::Floor },
    };

    for (auto& entry : entries)
    {
        if (ImGui::Button(entry.label, ImVec2(280, 35)))
        {
            auto* obj = m_Scene.AddMeshObject(entry.type);
            auto* mesh = obj->GetComponent<MeshComponent>();
            if (mesh && entry.type != EPrimitiveType::Floor)
            {
                mesh->Position.y = 0.5f;
                mesh->Position.x = (float)(rand() % 60 - 30) * 0.1f;
                mesh->Position.z = (float)(rand() % 60 - 30) * 0.1f;
            }
            RebuildAllGPUBuffers();
            m_Scene.SelectObject(obj->ID);
        }
    }

    ImGui::Separator();
    ImGui::Text("Special:");
    if (ImGui::Button("Camera", ImVec2(280, 35)))
    {
        auto* obj = m_Scene.AddCameraObject();
        auto* cam = obj->GetComponent<CameraComponent>();
        if (cam)
        {
            cam->Position = { 0.0f, 3.0f, -6.0f };
        }
        m_Scene.SelectObject(obj->ID);
    }

    ImGui::Separator();
    ImGui::Text("Lights:");
    if (ImGui::Button("Directional Light", ImVec2(280, 35)))
    {
        auto* obj = m_Scene.AddDirectionalLightObject();
        m_Scene.SelectObject(obj->ID);
    }
    if (ImGui::Button("Point Light", ImVec2(280, 35)))
    {
        auto* obj = m_Scene.AddPointLightObject();
        m_Scene.SelectObject(obj->ID);
    }

    ImGui::Separator();
    ImGui::Text("Effects:");
    if (ImGui::Button("Post Process", ImVec2(280, 35)))
    {
        auto* obj = m_Scene.AddPostProcessObject();
        // Add a default material if shaders are available
        auto* ppComp = obj->GetComponent<PostProcessComponent>();
        if (ppComp && !m_PostProcessLibrary.GetShaderNames().empty())
        {
            ppComp->AddMaterial(m_PostProcessLibrary.GetShaderNames()[0]);
        }
        m_Scene.SelectObject(obj->ID);
    }
}

void KiwiEngineApp::DrawRenderingTab()
{
    ImGui::Text("Render Path: %s", GetRenderPathName(m_RenderPath));
    ImGui::Separator();

    if (m_RenderPath == ERenderPath::RayTracing)
    {
        ImGui::SliderInt("RPP", &m_RayTracingSamplesPerPixel, 1, 16);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Rays per pixel. Samples are jittered inside the pixel and averaged.");

        ImGui::SliderFloat("Resolution", &m_RayTracingResolutionPercent, 10.0f, 100.0f, "%.0f%%");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Percentage of the framebuffer resolution used for tracing.");

        ImGui::Text("Trace size: %u x %u", m_RayTraceWidth, m_RayTraceHeight);
    }
    else
    {
        ImGui::TextDisabled("No settings for this path.");
    }
}
