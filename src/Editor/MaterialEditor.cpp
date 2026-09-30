#include "KiwiEngineApp.h"

#include "Editor/AssetTypes.h"

#include <imgui.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

// ---- Texture Picker Popup ----
// Opens a modal listing all textures in Textures/ and lets the user pick one.
void KiwiEngineApp::DrawTexturePicker()
{
    if (!ShowTexturePicker) return;

    ImGui::OpenPopup("##KiwiTexPicker");
    ShowTexturePicker = false;
}

void KiwiEngineApp::DrawTexturePickerModal()
{
    if (!ImGui::BeginPopupModal("##KiwiTexPicker", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
        return;

    ImGui::TextColored(ImVec4(0.9f, 0.7f, 1.0f, 1.0f), "Select Texture");
    ImGui::Separator();
    ImGui::Spacing();

    // List textures from Textures/ folder
    namespace fs = std::filesystem;

    bool selected = false;
    if (fs::exists(TexturesDir))
    {
        for (auto& entry : fs::directory_iterator(TexturesDir))
        {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            if (!IsTextureExtension(ext)) continue;

            std::string fname = entry.path().filename().string();
            std::string stem  = entry.path().stem().string();

            // Color swatch placeholder
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "[Tex]");
            ImGui::SameLine();
            if (ImGui::Selectable(fname.c_str(), false, 0, ImVec2(280, 0)))
            {
                if (TexturePickerMesh)
                {
                    TexturePickerMesh->Material.SetTexture(TexturePickerPropKey, fname);
                    TexturePickerMesh->MarkRenderStateDirty();
                }
                else if (Material* mat = MaterialLibrary.GetMaterial(TexturePickerMatTarget))
                {
                    mat->SetTexture(TexturePickerPropKey, fname);
                    RenderScene.UpdatePrimitivesUsingMaterial(mat->Name);
                }
                TexturePickerMesh = nullptr;
                TexturePickerMatTarget.clear();
                TexturePickerPropKey.clear();
                selected = true;
                ImGui::CloseCurrentPopup();
            }
        }
    }

    if (!selected)
    {
        ImGui::Spacing();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

// Helper: draw a single texture slot row.
void KiwiEngineApp::DrawTextureSlotRow(const std::string& slotLabel, const std::string& propKey,
                                       const std::string& uniqueId, Material* mat, PrimitiveComponent* mesh)
{
    std::string texPath = mesh ? mesh->Material.GetTexture(mat, propKey) : mat->GetTexture(propKey);

    // Slot label column (fixed width)
    ImGui::Text("%-11s", slotLabel.c_str());
    ImGui::SameLine();

    // Read-only display (grey input box)
    float pickBtnW = 52.0f;
    float clearBtnW = texPath.empty() ? 0.0f : 24.0f;
    float inputW = ImGui::GetContentRegionAvail().x - pickBtnW - clearBtnW - 8.0f;
    ImGui::SetNextItemWidth(inputW > 40 ? inputW : 40);

    // Display filename only (strip path)
    namespace fs = std::filesystem;
    std::string displayName = texPath.empty() ? "" : fs::path(texPath).filename().string();
    char buf[256] = {};
    strncpy(buf, displayName.c_str(), sizeof(buf) - 1);

    // Disabled read-only text field (acts as drop target display)
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
    ImGui::InputText(("##texslot_" + uniqueId).c_str(), buf, sizeof(buf),
                     ImGuiInputTextFlags_ReadOnly);
    ImGui::PopStyleColor();

    // Accept DragDrop from Content Browser
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("KIWI_TEXTURE"))
        {
            const char* droppedFile = static_cast<const char*>(payload->Data);
            if (mesh)
            {
                mesh->Material.SetTexture(propKey, droppedFile);
                mesh->MarkRenderStateDirty();
            }
            else if (mat)
            {
                mat->SetTexture(propKey, droppedFile);
                RenderScene.UpdatePrimitivesUsingMaterial(mat->Name);
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Pick button
    ImGui::SameLine();
    if (ImGui::Button(("Pick##pick_" + uniqueId).c_str(), ImVec2(pickBtnW, 0)))
    {
        TexturePickerMesh = mesh;
        TexturePickerMatTarget = mat ? mat->Name : "";
        TexturePickerPropKey   = propKey;
        ShowTexturePicker      = true;
    }

    // Clear (X) button
    if (!texPath.empty())
    {
        ImGui::SameLine();
        if (ImGui::SmallButton(("X##clr_" + uniqueId).c_str()))
        {
            if (mesh)
            {
                mesh->Material.SetTexture(propKey, "");
                mesh->MarkRenderStateDirty();
            }
            else if (mat)
            {
                mat->SetTexture(propKey, "");
                RenderScene.UpdatePrimitivesUsingMaterial(mat->Name);
            }
        }
    }
}

void KiwiEngineApp::DrawMaterialEditor()
{
    if (!ShowMaterialEditor) return;

    Material* mat = MaterialLibrary.GetMaterial(MaterialEditorTarget);
    if (!mat)
    {
        ShowMaterialEditor = false;
        return;
    }

    // Trigger texture picker popup if requested
    DrawTexturePicker();
    DrawTexturePickerModal();

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 vp = ImGui::GetMainViewport()->Pos;

    // First-time position: center of main window
    ImGui::SetNextWindowSize(ImVec2(460.0f, 580.0f), ImGuiCond_Once);
    ImGui::SetNextWindowPos(
        ImVec2(vp.x + io.DisplaySize.x * 0.5f - 230.0f,
               vp.y + io.DisplaySize.y * 0.5f - 290.0f),
        ImGuiCond_Once);

    std::string title = "Material Editor - " + mat->Name + "###KiwiMatEditor";
    bool open = true;
    if (ImGui::Begin(title.c_str(), &open,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking))
    {
        // ---- Header: material name ----
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 1.0f, 1.0f));
        ImGui::TextUnformatted(mat->Name.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 8);
        ImGui::TextDisabled("(.mat)");

        ImGui::Separator();

        // ---- Shading Model Selection ----
        ImGui::Text("Shading Model");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        const char* smNames[] = { "Unlit", "DefaultLit" };
        int smIdx = (int)mat->ShadingModel;
        if (ImGui::Combo("##MatEdShadingModel", &smIdx, smNames, IM_ARRAYSIZE(smNames)))
        {
            mat->ShadingModel = (EShadingModel)smIdx;
            RenderScene.UpdatePrimitivesUsingMaterial(mat->Name);
        }

        ImGui::Spacing();
        ImGui::Separator();

        // ---- Properties ----
        // Load @Properties from the shader source associated with this ShadingModel
        std::vector<ShaderPropertyDef> propDefs;
        {
            // Map ShadingModel → shader file for property parsing
            std::string shaderFile;
            switch (mat->ShadingModel)
            {
            case EShadingModel::Unlit:      shaderFile = "Unlit"; break;
            case EShadingModel::DefaultLit: shaderFile = "DefaultLit"; break;
            default:                        shaderFile = "DefaultLit"; break;
            }
            std::string shaderPath = ShaderDir + "/" + shaderFile + ".hlsl";
            std::ifstream sf(shaderPath);
            if (sf.is_open())
            {
                std::string src((std::istreambuf_iterator<char>(sf)),
                                 std::istreambuf_iterator<char>());
                propDefs = ParseShaderProperties(src);
            }
        }

        ImGui::Text("Properties");
        ImGui::Spacing();

        if (!propDefs.empty())
        {
            for (auto& def : propDefs)
            {
                switch (def.Type)
                {
                case EShaderPropertyType::Float:
                {
                    float v = mat->GetFloat(def.Name, def.DefaultFloat);
                    if (ImGui::DragFloat((def.DisplayName + "##me_f").c_str(), &v, 0.01f))
                    {
                        mat->SetFloat(def.Name, v);
                        RenderScene.UpdatePrimitivesUsingMaterial(mat->Name);
                    }
                    break;
                }
                case EShaderPropertyType::Range:
                {
                    float v = mat->GetFloat(def.Name, def.DefaultFloat);
                    if (ImGui::SliderFloat((def.DisplayName + "##me_r").c_str(), &v,
                                           def.RangeMin, def.RangeMax))
                    {
                        mat->SetFloat(def.Name, v);
                        RenderScene.UpdatePrimitivesUsingMaterial(mat->Name);
                    }
                    break;
                }
                case EShaderPropertyType::Color:
                {
                    Vec4 c = mat->GetColor(def.Name, def.DefaultColor);
                    if (ImGui::ColorEdit4((def.DisplayName + "##me_c").c_str(), &c.x))
                    {
                        mat->SetColor(def.Name, c);
                        RenderScene.UpdatePrimitivesUsingMaterial(mat->Name);
                    }
                    break;
                }
                case EShaderPropertyType::Texture2D:
                    DrawTextureSlotRow(def.DisplayName, def.Name,
                                       "prop_" + def.Name, mat);
                    break;
                }
            }
        }
        else
        {
            // Fallback: standard DefaultLit properties
            // Color
            {
                Vec4 c = mat->GetColor("_Color", { 0.8f, 0.8f, 0.8f, 1.0f });
                if (ImGui::ColorEdit4("Color##me_color", &c.x))
                {
                    mat->SetColor("_Color", c);
                    RenderScene.UpdatePrimitivesUsingMaterial(mat->Name);
                }
            }

            // Roughness
            {
                float v = mat->GetFloat("_Roughness", 0.5f);
                if (ImGui::SliderFloat("Roughness##me_r", &v, 0.0f, 1.0f))
                {
                    mat->SetFloat("_Roughness", v);
                    RenderScene.UpdatePrimitivesUsingMaterial(mat->Name);
                }
            }

            // Metallic
            {
                float v = mat->GetFloat("_Metallic", 0.0f);
                if (ImGui::SliderFloat("Metallic##me_m", &v, 0.0f, 1.0f))
                {
                    mat->SetFloat("_Metallic", v);
                    RenderScene.UpdatePrimitivesUsingMaterial(mat->Name);
                }
            }

            ImGui::Spacing();
            ImGui::Text("Textures");
            ImGui::Spacing();

            DrawTextureSlotRow("Base Color", "_BaseColorTex", "fb_bc", mat);
            DrawTextureSlotRow("Normal Map", "_NormalTex",    "fb_nm", mat);
            DrawTextureSlotRow("MR Map",     "_MetallicRoughnessTex", "fb_mr", mat);
        }

        // ---- Footer: Save + Close ----
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        bool saved = false;
        if (ImGui::Button("Save##MatEdSave", ImVec2(120, 0)))
        {
            saved = MaterialLibrary.SaveMaterial(mat->Name);
        }
        if (saved)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Saved!");
        }

        ImGui::SameLine();
        if (ImGui::Button("Close##MatEdClose", ImVec2(120, 0)))
            open = false;

        // Color preview swatch
        ImGui::SameLine(0, 16);
        Vec4 col = mat->GetColor("_Color", {0.8f, 0.8f, 0.8f, 1.0f});
        ImVec2 swatchPos = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            swatchPos,
            ImVec2(swatchPos.x + 24, swatchPos.y + 24),
            IM_COL32((int)(col.x*255), (int)(col.y*255), (int)(col.z*255), 255),
            4.0f);
        ImGui::Dummy(ImVec2(24, 24));
    }
    ImGui::End();

    if (!open)
    {
        ShowMaterialEditor = false;
        MaterialEditorTarget.clear();
    }
}
