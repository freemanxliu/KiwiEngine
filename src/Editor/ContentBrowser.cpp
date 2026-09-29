#include "KiwiEngineApp.h"

#include "Core/Platform.h"
#include "Editor/AssetTypes.h"

#include <imgui.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace
{

const char* GetContentIcon(const std::string& ext)
{
    if (ext == ".json") return "[Scene]";
    if (ext == ".hlsl" || ext == ".HLSL") return "[HLSL]";
    if (ext == ".glsl" || ext == ".GLSL") return "[GLSL]";
    if (ext == ".mat") return "[Mat]";
    if (IsTextureExtension(ext)) return "[Tex]";
    return "[?]";
}

ImVec4 GetContentIconColor(const std::string& ext)
{
    if (ext == ".json") return ImVec4(0.3f, 0.9f, 0.5f, 1.0f); // green
    if (ext == ".hlsl" || ext == ".HLSL") return ImVec4(0.4f, 0.6f, 1.0f, 1.0f); // blue
    if (ext == ".glsl" || ext == ".GLSL") return ImVec4(0.6f, 0.4f, 1.0f, 1.0f); // purple
    if (ext == ".mat") return ImVec4(0.9f, 0.5f, 0.7f, 1.0f); // pink
    if (IsTextureExtension(ext)) return ImVec4(1.0f, 0.7f, 0.3f, 1.0f); // orange
    return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
}

const char* GetContentTypeName(const std::string& ext)
{
    if (ext == ".json") return "Scene";
    if (ext == ".hlsl" || ext == ".HLSL") return "Shader";
    if (ext == ".glsl" || ext == ".GLSL") return "GL Shader";
    if (ext == ".mat") return "Material";
    if (ext == ".png") return "PNG";
    if (ext == ".jpg" || ext == ".jpeg") return "JPEG";
    if (ext == ".bmp") return "BMP";
    if (ext == ".tga") return "TGA";
    return "File";
}

} // namespace

void KiwiEngineApp::DrawContentBrowser()
{
    if (!ShowContentBrowser) return;

    namespace fs = std::filesystem;

    ImGui::SetNextWindowSize(ImVec2(720, 450), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Content Browser", &ShowContentBrowser))
    {
        ImGui::End();
        return;
    }

    // ---- Resource folders ----
    struct ContentFolder
    {
        const char* Name;
        const char* Icon;
        std::string Path;
    };

    ContentFolder folders[] = {
        { "Scenes",             "[S] ",  ScenesDir },
        { "Shaders",            "[SH] ", ShaderDir },
        { "GLShaders",          "[GL] ", GLShaderDir },
        { "PostProcessShaders", "[PP] ", PostProcessShaderDir },
        { "Textures",           "[TX] ", TexturesDir },
        { "Materials",          "[MT] ", MaterialsDir },
    };
    int folderCount = sizeof(folders) / sizeof(folders[0]);

    // If no folder selected, default to first
    if (ContentBrowserSelectedDir.empty())
        ContentBrowserSelectedDir = folders[0].Path;

    // ---- Left panel: folder tree ----
    ImGui::BeginChild("CB_FolderTree", ImVec2(180, 0), true);

    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Folders");
    ImGui::Separator();

    for (int i = 0; i < folderCount; i++)
    {
        auto& f = folders[i];
        if (f.Path.empty()) continue;

        bool selected = (ContentBrowserSelectedDir == f.Path);
        std::string label = std::string(f.Icon) + f.Name;
        if (ImGui::Selectable(label.c_str(), selected))
        {
            ContentBrowserSelectedDir = f.Path;
        }
    }

    ImGui::EndChild();

    ImGui::SameLine();

    // ---- Right panel: file list ----
    ImGui::BeginChild("CB_FileList", ImVec2(0, 0), true);

    // Find current folder name for display
    std::string currentFolderName = "Content";
    for (int i = 0; i < folderCount; i++)
    {
        if (folders[i].Path == ContentBrowserSelectedDir)
        {
            currentFolderName = folders[i].Name;
            break;
        }
    }

    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", currentFolderName.c_str());
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.0f);
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Path:");
    ImGui::Separator();

    // Small path display
    ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "%s", ContentBrowserSelectedDir.c_str());
    ImGui::Separator();

    // Enumerate files
    std::error_code ec;
    if (fs::exists(ContentBrowserSelectedDir, ec) && fs::is_directory(ContentBrowserSelectedDir, ec))
    {
        // Collect files first for sorting
        struct FileEntry
        {
            std::string Name;
            std::string Extension;
            std::string FullPath;
            uintmax_t Size;
        };
        std::vector<FileEntry> files;

        for (const auto& entry : fs::directory_iterator(ContentBrowserSelectedDir, ec))
        {
            if (!entry.is_regular_file()) continue;
            std::string name = entry.path().filename().string();
            std::string ext = entry.path().extension().string();
            // Skip README files
            if (name == "README.txt" || name == "README.md") continue;

            uintmax_t size = 0;
            std::error_code szEc;
            size = fs::file_size(entry.path(), szEc);

            files.push_back({ name, ext, entry.path().string(), size });
        }

        // Sort alphabetically
        std::sort(files.begin(), files.end(), [](const FileEntry& a, const FileEntry& b) {
            return a.Name < b.Name;
        });

        // Table display
        if (ImGui::BeginTable("ContentFiles", 3,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY))
        {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();

            for (const auto& file : files)
            {
                ImGui::TableNextRow();

                // Name column (with icon)
                ImGui::TableSetColumnIndex(0);
                const char* icon = GetContentIcon(file.Extension);
                ImVec4 iconColor = GetContentIconColor(file.Extension);
                ImGui::TextColored(iconColor, "%s", icon);
                ImGui::SameLine();

                // Selectable filename
                std::string stem = fs::path(file.Name).stem().string();
                if (ImGui::Selectable(stem.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
                {
                    if (ImGui::IsMouseDoubleClicked(0))
                    {
                        OnContentDoubleClick(file.FullPath, file.Extension);
                    }
                }

                // Drag-drop source: texture files only
                if (IsTextureExtension(file.Extension) && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                {
                    // payload = just the filename (relative to Textures/)
                    const std::string& fname = file.Name;
                    ImGui::SetDragDropPayload("KIWI_TEXTURE", fname.c_str(), fname.size() + 1);
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "[Tex] %s", stem.c_str());
                    ImGui::EndDragDropSource();
                }

                // Right-click context menu
                if (ImGui::BeginPopupContextItem(("##ctx_" + file.Name).c_str()))
                {
                    if (ImGui::MenuItem("Show In Explorer"))
                    {
                        // Open Explorer with file selected
#if defined(__APPLE__)
                        std::string cmd = "open -R \"" + file.FullPath + "\"";
#else
                        std::string cmd = "explorer /select,\"" + file.FullPath + "\"";
#endif
                        system(cmd.c_str());
                    }
                    if (ImGui::MenuItem("Open File"))
                    {
                        // Open with default associated application
                        OpenFileWithDefaultApp(file.FullPath.c_str());
                    }
                    ImGui::EndPopup();
                }

                // Tooltip with full path
                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s", file.FullPath.c_str());
                    ImGui::EndTooltip();
                }

                // Type column
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", GetContentTypeName(file.Extension));

                // Size column
                ImGui::TableSetColumnIndex(2);
                if (file.Size < 1024)
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%llu B", (unsigned long long)file.Size);
                else if (file.Size < 1024 * 1024)
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%.1f KB", (float)file.Size / 1024.0f);
                else
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%.1f MB", (float)file.Size / (1024.0f * 1024.0f));
            }

            ImGui::EndTable();
        }

        if (files.empty())
        {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(empty folder)");
        }
    }
    else
    {
        ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.4f, 1.0f), "Folder not found: %s", ContentBrowserSelectedDir.c_str());
    }

    ImGui::EndChild();
    ImGui::End();
}

void KiwiEngineApp::OnContentDoubleClick(const std::string& fullPath, const std::string& ext)
{
    if (ext == ".json")
    {
        // Load scene
        if (Scene.LoadFromFile(fullPath))
        {
            std::cout << "[Kiwi] Content Browser: Loaded scene: " << fullPath << std::endl;
        }
    }
    else if (IsTextureExtension(ext))
    {
        // Pre-load texture into TextureManager
        GPUTexture* tex = TextureManager.LoadTexture(fullPath);
        if (tex)
        {
            std::cout << "[Kiwi] Content Browser: Loaded texture: " << fullPath
                      << " (" << tex->Width << "x" << tex->Height << ")" << std::endl;
        }
    }
    else if (ext == ".mat")
    {
        // Open material editor
        namespace fs = std::filesystem;
        std::string matName = fs::path(fullPath).stem().string();
        // Ensure it's loaded in MaterialLibrary
        Material* mat = MaterialLibrary.GetMaterial(matName);
        if (!mat)
        {
            auto newMat = std::make_unique<Material>();
            if (newMat->LoadFromFile(fullPath))
            {
                matName = newMat->Name.empty() ? matName : newMat->Name;
                MaterialLibrary.AddMaterial(std::move(newMat));
                RenderScene.UpdatePrimitivesUsingMaterial(matName);
            }
        }
        MaterialEditorTarget = matName;
        ShowMaterialEditor = true;
    }
}
