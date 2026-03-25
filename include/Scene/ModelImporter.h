#pragma once

#include "Scene/Mesh.h"
#include <string>
#include <vector>

namespace Kiwi
{

    // ============================================================
    // ModelImporter — loads external 3D model files into Mesh data
    //
    // Supported formats:
    //   .obj  — Wavefront OBJ  (via tinyobjloader)
    //   .fbx  — Autodesk FBX   (via ufbx)
    //
    // Each sub-mesh in the file becomes a separate Mesh in the
    // returned vector. If the file has no sub-meshes, a single
    // Mesh is returned.
    // ============================================================

    struct ImportedModel
    {
        struct SubMesh
        {
            Mesh        MeshData;
            std::string Name;           // Material or group name
            Vec4        Color = { 0.8f, 0.8f, 0.8f, 1.0f };
        };

        std::vector<SubMesh> SubMeshes;
        std::string          SourcePath;

        bool IsValid() const { return !SubMeshes.empty(); }
    };

    class ModelImporter
    {
    public:
        // Import a model file. Returns ImportedModel with sub-meshes.
        // Supported extensions: .obj, .fbx (case-insensitive)
        static ImportedModel Import(const std::string& filepath);

        // Check if a file extension is supported
        static bool IsSupported(const std::string& filepath);

        // Get supported file filter string for Win32 Open File Dialog
        // e.g. "3D Models (*.obj;*.fbx)\0*.obj;*.fbx\0All Files (*.*)\0*.*\0"
        static const char* GetFileFilter();

    private:
        static ImportedModel ImportOBJ(const std::string& filepath);
        static ImportedModel ImportFBX(const std::string& filepath);

        static std::string GetExtension(const std::string& filepath);
    };

} // namespace Kiwi
