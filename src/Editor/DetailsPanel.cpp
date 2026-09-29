#include "KiwiEngineApp.h"

#include <imgui.h>

#include <string>
#include <utility>

void KiwiEngineApp::DrawDetailTab()
{
    SceneObject* sel = Scene.GetSelectedObject();
    if (!sel)
    {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No object selected.");
        ImGui::TextWrapped("Click an object in the viewport or select from the list above.");
        return;
    }

    ImGui::Text("Name: %s", sel->Name.c_str());
    ImGui::Text("Components: %d", (int)sel->Components.size());

    // Draw UI for each component
    for (size_t ci = 0; ci < sel->Components.size(); ci++)
    {
        auto& comp = *sel->Components[ci];
        ImGui::Separator();

        // Component header with type name
        bool compOpen = ImGui::TreeNodeEx(
            (std::string(comp.GetTypeName()) + "##" + std::to_string(ci)).c_str(),
            ImGuiTreeNodeFlags_DefaultOpen);

        if (compOpen)
        {
            // Enable/Disable toggle
            if (ImGui::Checkbox(("Enabled##comp" + std::to_string(ci)).c_str(), &comp.Enabled))
                comp.MarkRenderStateDirty();

            // Transform — every component has this
            ImGui::Text("Transform");
            bool changed = false;
            changed |= ImGui::DragFloat3(("Position##" + std::to_string(ci)).c_str(), &comp.Position.x, 0.05f);
            changed |= ImGui::DragFloat3(("Rotation##" + std::to_string(ci)).c_str(), &comp.Rotation.x, 1.0f, -360.0f, 360.0f);
            changed |= ImGui::DragFloat3(("Scale##" + std::to_string(ci)).c_str(), &comp.Scale.x, 0.05f, 0.01f, 100.0f);
            if (changed)
                comp.MarkRenderTransformDirty();

            // Type-specific UI
            if (comp.GetType() == EComponentType::Mesh)
            {
                auto& mesh = static_cast<MeshComponent&>(comp);

                // ---- Material Selection ----
                ImGui::Separator();
                ImGui::Text("Material Instance");
                {
                    auto matNames = MaterialLibrary.GetMaterialNames();
                    if (ImGui::BeginCombo(("##MaterialCombo" + std::to_string(ci)).c_str(), mesh.Material.Parent.c_str()))
                    {
                        for (const auto& name : matNames)
                        {
                            bool isSelected = (name == mesh.Material.Parent);
                            if (ImGui::Selectable(name.c_str(), isSelected))
                            {
                                mesh.Material.SetParent(name);
                                mesh.MarkRenderStateDirty();
                            }
                            if (isSelected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }

                Material* activeMat = MaterialLibrary.GetMaterial(mesh.Material.Parent);
                if (activeMat)
                {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Shading Model: %s", ShadingModelToString(activeMat->ShadingModel));
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Surface: %s", activeMat->SurfaceShader.c_str());

                    ImGui::Separator();
                    ImGui::Text("Instance Parameters");

                    {
                        Vec4 color = mesh.Material.GetColor(activeMat, "_Color", { 0.8f, 0.8f, 0.8f, 1.0f });
                        if (ImGui::ColorEdit4(("Color##mat" + std::to_string(ci)).c_str(), &color.x))
                        {
                            mesh.Material.SetColor("_Color", color);
                            mesh.MarkRenderStateDirty();
                        }
                    }
                    {
                        float roughness = mesh.Material.GetFloat(activeMat, "_Roughness", 0.5f);
                        if (ImGui::SliderFloat(("Roughness##mat" + std::to_string(ci)).c_str(), &roughness, 0.0f, 1.0f))
                        {
                            mesh.Material.SetFloat("_Roughness", roughness);
                            mesh.MarkRenderStateDirty();
                        }
                    }
                    {
                        float metallic = mesh.Material.GetFloat(activeMat, "_Metallic", 0.0f);
                        if (ImGui::SliderFloat(("Metallic##mat" + std::to_string(ci)).c_str(), &metallic, 0.0f, 1.0f))
                        {
                            mesh.Material.SetFloat("_Metallic", metallic);
                            mesh.MarkRenderStateDirty();
                        }
                    }

                    ImGui::Spacing();
                    ImGui::Text("Textures");
                    ImGui::Spacing();

                    DrawTextureSlotRow("Base Color", "_BaseColorTex",
                                       "ins_bc_" + std::to_string(ci), activeMat, &mesh);
                    DrawTextureSlotRow("Normal Map", "_NormalTex",
                                       "ins_nm_" + std::to_string(ci), activeMat, &mesh);
                    DrawTextureSlotRow("MR Map",     "_MetallicRoughnessTex",
                                       "ins_mr_" + std::to_string(ci), activeMat, &mesh);

                    ImGui::Spacing();
                    if (ImGui::SmallButton(("Reset Overrides##" + std::to_string(ci)).c_str()))
                    {
                        mesh.Material.ClearOverrides();
                        mesh.MarkRenderStateDirty();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(("Edit Material...##" + std::to_string(ci)).c_str()))
                    {
                        MaterialEditorTarget = mesh.Material.Parent;
                        ShowMaterialEditor   = true;
                    }
                }

                ImGui::Separator();
                ImGui::Text("Rendering");
                {
                    const char* cullItems[] = { "Back", "Front", "None" };
                    const ECullMode cullValues[] = { ECullMode::Back, ECullMode::Front, ECullMode::None };
                    int cullIndex = mesh.CullMode == ECullMode::Front ? 1
                        : mesh.CullMode == ECullMode::None ? 2 : 0;
                    if (ImGui::Combo(("Culling Mode##" + std::to_string(ci)).c_str(), &cullIndex, cullItems, 3))
                    {
                        mesh.CullMode = cullValues[cullIndex];
                        mesh.MarkRenderStateDirty();
                    }
                }
                if (ImGui::DragInt(("Sort Order##" + std::to_string(ci)).c_str(), &mesh.SortOrder, 0.5f, -1000, 1000))
                    mesh.MarkRenderStateDirty();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Higher values are rendered first.\nObjects with same order are sorted back-to-front.");

                ImGui::Separator();
                ImGui::Text("GPU Scene");
                uint32_t primitiveId = GPUScene::InvalidId;
                uint32_t instanceId = GPUScene::InvalidId;
                RenderScene.GetPrimitiveGPUIds(&mesh, primitiveId, instanceId);
                if (primitiveId == GPUScene::InvalidId)
                    ImGui::Text("  PrimitiveId: -");
                else
                    ImGui::Text("  PrimitiveId: %u", primitiveId);
                if (instanceId == GPUScene::InvalidId)
                    ImGui::Text("  InstanceId: -");
                else
                    ImGui::Text("  InstanceId: %u", instanceId);

                ImGui::Separator();
                ImGui::Text("Mesh Info");
                ImGui::Text("  Vertices: %u", mesh.MeshData.GetVertexCount());
                ImGui::Text("  Indices:  %u", mesh.MeshData.GetIndexCount());
                ImGui::Text("  Triangles: %u", mesh.MeshData.GetIndexCount() / 3);
            }
            else if (comp.GetType() == EComponentType::Camera)
            {
                auto& cam = static_cast<CameraComponent&>(comp);

                ImGui::Separator();
                ImGui::Text("Camera Settings");

                // Main Camera toggle — mutually exclusive
                bool isMain = cam.IsMainCamera;
                if (ImGui::Checkbox(("Main Camera##" + std::to_string(ci)).c_str(), &isMain))
                {
                    if (isMain)
                    {
                        // Set this camera as main (clears all others)
                        Scene.SetMainCamera(&cam);
                    }
                    else
                    {
                        // Unchecking: clear the flag (no main camera)
                        cam.IsMainCamera = false;
                    }
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("The Main Camera drives the engine's rendering viewpoint.\nOnly one camera can be the Main Camera at a time.");

                if (cam.IsMainCamera)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "(Active)");
                }

                // Projection type
                const char* projNames[] = { "Perspective", "Orthographic" };
                int projIdx = (cam.Projection == ECameraProjection::Perspective) ? 0 : 1;
                if (ImGui::Combo(("Projection##" + std::to_string(ci)).c_str(), &projIdx, projNames, 2))
                {
                    cam.Projection = (projIdx == 0) ? ECameraProjection::Perspective : ECameraProjection::Orthographic;
                }

                if (cam.Projection == ECameraProjection::Perspective)
                {
                    ImGui::DragFloat(("FOV##" + std::to_string(ci)).c_str(), &cam.FieldOfView, 0.5f, 10.0f, 120.0f);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Field of View in degrees.\nSmaller = zoom in, Larger = wide angle.");
                }
                else
                {
                    ImGui::DragFloat(("Ortho Width##" + std::to_string(ci)).c_str(), &cam.OrthoWidth, 0.1f, 0.1f, 100.0f);
                    ImGui::DragFloat(("Ortho Height##" + std::to_string(ci)).c_str(), &cam.OrthoHeight, 0.1f, 0.1f, 100.0f);
                }

                ImGui::DragFloat(("Near Plane##" + std::to_string(ci)).c_str(), &cam.NearPlane, 0.01f, 0.001f, 10.0f);
                ImGui::DragFloat(("Far Plane##" + std::to_string(ci)).c_str(), &cam.FarPlane, 1.0f, 1.0f, 10000.0f);
            }
            else if (comp.GetType() == EComponentType::Light)
            {
                auto& light = static_cast<LightComponent&>(comp);

                ImGui::Separator();
                ImGui::Text("Light Type: %s", light.GetLightTypeName());

                // Light Color
                if (ImGui::ColorEdit3(("Light Color##" + std::to_string(ci)).c_str(), &light.LightColor.x))
                    light.MarkRenderStateDirty();

                // Intensity
                if (ImGui::DragFloat(("Intensity##" + std::to_string(ci)).c_str(), &light.Intensity, 0.01f, 0.0f, 20.0f))
                    light.MarkRenderStateDirty();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Light intensity multiplier.\n0 = off, 1 = normal, >1 = brighter");

                // Affect World
                if (ImGui::Checkbox(("Affect World##light" + std::to_string(ci)).c_str(), &light.AffectWorld))
                    light.MarkRenderStateDirty();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("When disabled, this light will not affect any objects.");

                // Type-specific: Point Light Radius
                if (light.GetLightType() == ELightType::Point)
                {
                    auto& pointLight = static_cast<PointLightComponent&>(light);
                    ImGui::Separator();
                    ImGui::Text("Point Light");
                    if (ImGui::DragFloat(("Radius##" + std::to_string(ci)).c_str(), &pointLight.Radius, 0.1f, 0.1f, 100.0f))
                        light.MarkRenderStateDirty();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Maximum distance this light can reach.\nFragments beyond this distance receive no light.");
                }
                else // Directional
                {
                    ImGui::Separator();
                    ImGui::Text("Directional Light");
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                        "Direction is controlled by the\nRotation above (forward vector).");

                    auto& dirLight = static_cast<DirectionalLightComponent&>(light);

                    ImGui::Separator();
                    ImGui::Text("Shadow (CSM)");

                    if (ImGui::Checkbox(("Cast Shadow##" + std::to_string(ci)).c_str(), &dirLight.CastShadow))
                        light.MarkRenderStateDirty();
                    if (dirLight.CastShadow)
                    {
                        if (ImGui::SliderInt(("Cascades##" + std::to_string(ci)).c_str(), &dirLight.NumCascades, 1, 4))
                            light.MarkRenderStateDirty();
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Number of shadow map cascades.\nMore cascades = better quality at distance,\nbut more GPU cost.");

                        // Shadow map resolution dropdown
                        const int resolutions[] = { 512, 1024, 2048, 4096 };
                        const char* resLabels[] = { "512", "1024", "2048", "4096" };
                        int resIdx = 2; // default to 2048
                        for (int ri = 0; ri < 4; ri++)
                        {
                            if (resolutions[ri] == dirLight.ShadowMapResolution)
                            {
                                resIdx = ri;
                                break;
                            }
                        }
                        if (ImGui::Combo(("Resolution##shadow" + std::to_string(ci)).c_str(), &resIdx, resLabels, 4))
                        {
                            dirLight.ShadowMapResolution = resolutions[resIdx];
                            light.MarkRenderStateDirty();
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Shadow map resolution per cascade.\nHigher = sharper shadows, more VRAM.");

                        if (ImGui::DragFloat(("Shadow Distance##" + std::to_string(ci)).c_str(), &dirLight.ShadowDistance, 0.5f, 1.0f, 500.0f))
                            light.MarkRenderStateDirty();
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Maximum distance from camera\nwhere shadows are rendered.");

                        if (ImGui::SliderFloat(("Split Lambda##" + std::to_string(ci)).c_str(), &dirLight.CascadeSplitLambda, 0.0f, 1.0f))
                            light.MarkRenderStateDirty();
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Cascade split scheme.\n0 = uniform splits\n1 = logarithmic splits\n0.75 is a good balance.");

                        if (ImGui::DragFloat(("Shadow Bias##" + std::to_string(ci)).c_str(), &dirLight.ShadowBias, 0.0001f, 0.0f, 0.05f, "%.4f"))
                            light.MarkRenderStateDirty();
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Depth bias to reduce shadow acne.\nToo high = peter panning.");

                        if (ImGui::DragFloat(("Normal Bias##" + std::to_string(ci)).c_str(), &dirLight.NormalBias, 0.001f, 0.0f, 0.1f, "%.3f"))
                            light.MarkRenderStateDirty();
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Normal offset bias.\nHelps with self-shadowing artifacts.");

                        if (ImGui::SliderFloat(("Shadow Strength##" + std::to_string(ci)).c_str(), &dirLight.ShadowStrength, 0.0f, 1.0f))
                            light.MarkRenderStateDirty();
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Shadow darkness.\n0 = no shadow, 1 = full shadow.");
                    }
                }
            }
            else if (comp.GetType() == EComponentType::PostProcess)
            {
                auto& ppComp = static_cast<PostProcessComponent&>(comp);

                ImGui::Separator();
                ImGui::Text("Post-Process Effects");
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                    "Materials are applied in order (top to bottom).");

                // Material list
                int removeIdx = -1;
                for (size_t mi = 0; mi < ppComp.Materials.size(); mi++)
                {
                    auto& mat = ppComp.Materials[mi];
                    ImGui::PushID((int)(ci * 1000 + mi));

                    ImGui::Separator();

                    // Enable toggle
                    ImGui::Checkbox("##Enabled", &mat.Enabled);
                    ImGui::SameLine();

                    // Shader dropdown
                    const auto& ppShaderNames = PostProcessLibrary.GetShaderNames();
                    if (ImGui::BeginCombo("##Shader", mat.ShaderName.c_str()))
                    {
                        for (const auto& name : ppShaderNames)
                        {
                            bool isSelected = (name == mat.ShaderName);
                            if (ImGui::Selectable(name.c_str(), isSelected))
                            {
                                mat.ShaderName = name;
                            }
                            if (isSelected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }

                    // Intensity slider
                    ImGui::DragFloat("Intensity", &mat.Intensity, 0.01f, 0.0f, 2.0f);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Effect intensity.\n0 = no effect, 1 = full effect.");

                    // Remove button
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
                    if (ImGui::Button("Remove"))
                    {
                        removeIdx = (int)mi;
                    }
                    ImGui::PopStyleColor();

                    // Move up/down buttons
                    ImGui::SameLine();
                    if (mi > 0)
                    {
                        if (ImGui::Button("Up"))
                        {
                            std::swap(ppComp.Materials[mi], ppComp.Materials[mi - 1]);
                        }
                        ImGui::SameLine();
                    }
                    if (mi < ppComp.Materials.size() - 1)
                    {
                        if (ImGui::Button("Down"))
                        {
                            std::swap(ppComp.Materials[mi], ppComp.Materials[mi + 1]);
                        }
                    }

                    ImGui::PopID();
                }

                if (removeIdx >= 0)
                    ppComp.RemoveMaterial((size_t)removeIdx);

                ImGui::Separator();

                // Add material button
                const auto& ppShaderNames = PostProcessLibrary.GetShaderNames();
                if (!ppShaderNames.empty())
                {
                    if (ImGui::Button("+ Add Material", ImVec2(-1, 30)))
                    {
                        ppComp.AddMaterial(ppShaderNames[0]);
                    }
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.2f, 1.0f),
                        "No post-process shaders found.\nAdd .hlsl files to PostProcessShaders/ folder.");
                }
            }

            (void)changed;
            ImGui::TreePop();
        }
    }
}
