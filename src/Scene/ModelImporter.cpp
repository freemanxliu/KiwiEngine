// ============================================================
// ModelImporter — OBJ (tinyobjloader) + FBX (ufbx) importer
// ============================================================

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include "ufbx.h"

#include "Scene/ModelImporter.h"
#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <cmath>

namespace Kiwi
{

    // ---- Utility ----

    std::string ModelImporter::GetExtension(const std::string& filepath)
    {
        size_t dot = filepath.rfind('.');
        if (dot == std::string::npos) return "";
        std::string ext = filepath.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext;
    }

    bool ModelImporter::IsSupported(const std::string& filepath)
    {
        std::string ext = GetExtension(filepath);
        return ext == ".obj" || ext == ".fbx";
    }

    const char* ModelImporter::GetFileFilter()
    {
        return "3D Models (*.obj;*.fbx)\0*.obj;*.fbx\0"
               "Wavefront OBJ (*.obj)\0*.obj\0"
               "Autodesk FBX (*.fbx)\0*.fbx\0"
               "All Files (*.*)\0*.*\0";
    }

    ImportedModel ModelImporter::Import(const std::string& filepath)
    {
        std::string ext = GetExtension(filepath);
        if (ext == ".obj") return ImportOBJ(filepath);
        if (ext == ".fbx") return ImportFBX(filepath);

        std::cerr << "[Kiwi] ModelImporter: unsupported format '" << ext << "'" << std::endl;
        return {};
    }

    // ============================================================
    // OBJ Import (tinyobjloader)
    // ============================================================

    ImportedModel ModelImporter::ImportOBJ(const std::string& filepath)
    {
        ImportedModel result;
        result.SourcePath = filepath;

        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;

        // Extract directory for MTL file search
        std::string dir;
        {
            size_t lastSlash = filepath.find_last_of("/\\");
            if (lastSlash != std::string::npos)
                dir = filepath.substr(0, lastSlash + 1);
        }

        bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
                                   filepath.c_str(), dir.c_str());

        if (!warn.empty())
            std::cout << "[Kiwi] OBJ warning: " << warn << std::endl;
        if (!err.empty())
            std::cerr << "[Kiwi] OBJ error: " << err << std::endl;
        if (!ok)
        {
            std::cerr << "[Kiwi] Failed to load OBJ: " << filepath << std::endl;
            return result;
        }

        std::cout << "[Kiwi] OBJ loaded: " << filepath
                  << " (" << shapes.size() << " shapes, "
                  << materials.size() << " materials, "
                  << attrib.vertices.size() / 3 << " vertices)" << std::endl;

        // Process each shape into a SubMesh
        for (const auto& shape : shapes)
        {
            ImportedModel::SubMesh subMesh;
            subMesh.Name = shape.name;

            // Vertex deduplication: OBJ uses separate index arrays for pos/normal/uv
            // We need to create unique vertices from the combination
            struct IndexHash
            {
                size_t operator()(const tinyobj::index_t& idx) const
                {
                    size_t h = std::hash<int>()(idx.vertex_index);
                    h ^= std::hash<int>()(idx.normal_index) + 0x9e3779b9 + (h << 6) + (h >> 2);
                    h ^= std::hash<int>()(idx.texcoord_index) + 0x9e3779b9 + (h << 6) + (h >> 2);
                    return h;
                }
            };
            struct IndexEqual
            {
                bool operator()(const tinyobj::index_t& a, const tinyobj::index_t& b) const
                {
                    return a.vertex_index == b.vertex_index
                        && a.normal_index == b.normal_index
                        && a.texcoord_index == b.texcoord_index;
                }
            };

            std::unordered_map<tinyobj::index_t, uint32_t, IndexHash, IndexEqual> vertexMap;
            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;

            // Get material color for this shape (first face's material)
            Vec4 matColor = { 0.8f, 0.8f, 0.8f, 1.0f };
            if (!shape.mesh.material_ids.empty() && shape.mesh.material_ids[0] >= 0)
            {
                int matIdx = shape.mesh.material_ids[0];
                if (matIdx < (int)materials.size())
                {
                    matColor.x = materials[matIdx].diffuse[0];
                    matColor.y = materials[matIdx].diffuse[1];
                    matColor.z = materials[matIdx].diffuse[2];
                }
            }
            subMesh.Color = matColor;

            size_t indexOffset = 0;
            for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++)
            {
                int faceVerts = shape.mesh.num_face_vertices[f];

                // Triangulate: fan from first vertex for polygons with >3 verts
                for (int v = 0; v < faceVerts - 2; v++)
                {
                    tinyobj::index_t triIndices[3] = {
                        shape.mesh.indices[indexOffset + 0],
                        shape.mesh.indices[indexOffset + v + 1],
                        shape.mesh.indices[indexOffset + v + 2],
                    };

                    for (int t = 0; t < 3; t++)
                    {
                        const auto& idx = triIndices[t];

                        auto it = vertexMap.find(idx);
                        if (it != vertexMap.end())
                        {
                            indices.push_back(it->second);
                        }
                        else
                        {
                            Vertex vert = {};

                            // Position (required)
                            if (idx.vertex_index >= 0)
                            {
                                vert.Position.x = attrib.vertices[3 * idx.vertex_index + 0];
                                vert.Position.y = attrib.vertices[3 * idx.vertex_index + 1];
                                vert.Position.z = attrib.vertices[3 * idx.vertex_index + 2];
                            }

                            // Normal (optional)
                            if (idx.normal_index >= 0)
                            {
                                vert.Normal.x = attrib.normals[3 * idx.normal_index + 0];
                                vert.Normal.y = attrib.normals[3 * idx.normal_index + 1];
                                vert.Normal.z = attrib.normals[3 * idx.normal_index + 2];
                            }

                            // TexCoord (optional)
                            if (idx.texcoord_index >= 0)
                            {
                                vert.TexCoord.x = attrib.texcoords[2 * idx.texcoord_index + 0];
                                vert.TexCoord.y = 1.0f - attrib.texcoords[2 * idx.texcoord_index + 1]; // Flip V
                            }

                            // Vertex color (use material diffuse)
                            vert.Color = matColor;

                            uint32_t newIdx = (uint32_t)vertices.size();
                            vertexMap[idx] = newIdx;
                            vertices.push_back(vert);
                            indices.push_back(newIdx);
                        }
                    }
                }
                indexOffset += faceVerts;
            }

            // If no normals provided, compute flat normals
            bool hasNormals = !attrib.normals.empty();
            if (!hasNormals && indices.size() >= 3)
            {
                // Zero out normals first
                for (auto& v : vertices) v.Normal = { 0, 0, 0 };

                for (size_t i = 0; i + 2 < indices.size(); i += 3)
                {
                    Vec3& p0 = vertices[indices[i + 0]].Position;
                    Vec3& p1 = vertices[indices[i + 1]].Position;
                    Vec3& p2 = vertices[indices[i + 2]].Position;

                    Vec3 e1 = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
                    Vec3 e2 = { p2.x - p0.x, p2.y - p0.y, p2.z - p0.z };
                    Vec3 n = {
                        e1.y * e2.z - e1.z * e2.y,
                        e1.z * e2.x - e1.x * e2.z,
                        e1.x * e2.y - e1.y * e2.x
                    };

                    // Accumulate face normal to vertices (smooth shading)
                    for (int k = 0; k < 3; k++)
                    {
                        vertices[indices[i + k]].Normal.x += n.x;
                        vertices[indices[i + k]].Normal.y += n.y;
                        vertices[indices[i + k]].Normal.z += n.z;
                    }
                }

                // Normalize
                for (auto& v : vertices)
                {
                    v.Normal = v.Normal.Normalize();
                }
            }

            // Build the Mesh using the friend access pattern
            // Mesh::m_Vertices and m_Indices are private, so we use SetData
            subMesh.MeshData.SetData(std::move(vertices), std::move(indices));

            result.SubMeshes.push_back(std::move(subMesh));
        }

        std::cout << "[Kiwi] OBJ import complete: " << result.SubMeshes.size() << " sub-meshes" << std::endl;
        return result;
    }

    // ============================================================
    // FBX Import (ufbx)
    // ============================================================

    ImportedModel ModelImporter::ImportFBX(const std::string& filepath)
    {
        ImportedModel result;
        result.SourcePath = filepath;

        ufbx_load_opts opts = {};
        opts.target_axes = ufbx_axes_right_handed_y_up;
        opts.target_unit_meters = 0.01f; // FBX default is cm, convert to meters

        ufbx_error error;
        ufbx_scene* scene = ufbx_load_file(filepath.c_str(), &opts, &error);
        if (!scene)
        {
            std::cerr << "[Kiwi] Failed to load FBX: " << filepath << std::endl;
            if (error.description.data)
                std::cerr << "  ufbx error: " << error.description.data << std::endl;
            return result;
        }

        std::cout << "[Kiwi] FBX loaded: " << filepath
                  << " (" << scene->meshes.count << " meshes, "
                  << scene->nodes.count << " nodes)" << std::endl;

        // Process each mesh in the scene
        for (size_t mi = 0; mi < scene->meshes.count; mi++)
        {
            ufbx_mesh* mesh = scene->meshes.data[mi];

            // Process each material partition as a sub-mesh
            // If no materials, process the entire mesh as one sub-mesh
            size_t numParts = mesh->material_parts.count;
            if (numParts == 0) numParts = 1;

            for (size_t pi = 0; pi < numParts; pi++)
            {
                ImportedModel::SubMesh subMesh;

                // Name from mesh element
                if (mesh->element.name.length > 0)
                    subMesh.Name = std::string(mesh->element.name.data, mesh->element.name.length);
                else
                    subMesh.Name = "Mesh_" + std::to_string(mi);

                if (numParts > 1)
                    subMesh.Name += "_Part" + std::to_string(pi);

                // Get material color
                Vec4 matColor = { 0.8f, 0.8f, 0.8f, 1.0f };
                if (mesh->material_parts.count > 0)
                {
                    ufbx_mesh_part* part = &mesh->material_parts.data[pi];
                    if (part->index < mesh->materials.count && mesh->materials.data[part->index])
                    {
                        ufbx_material* mat = mesh->materials.data[part->index];
                        if (mat->fbx.diffuse_color.has_value)
                        {
                            matColor.x = (float)mat->fbx.diffuse_color.value_vec3.x;
                            matColor.y = (float)mat->fbx.diffuse_color.value_vec3.y;
                            matColor.z = (float)mat->fbx.diffuse_color.value_vec3.z;
                        }
                    }
                }
                subMesh.Color = matColor;

                std::vector<Vertex> vertices;
                std::vector<uint32_t> indices;

                if (mesh->material_parts.count > 0)
                {
                    ufbx_mesh_part* part = &mesh->material_parts.data[pi];

                    // Iterate over faces in this material partition
                    // Use ufbx triangulation
                    size_t maxTriIndices = mesh->max_face_triangles * 3;
                    std::vector<uint32_t> triIndices(maxTriIndices);

                    std::unordered_map<uint64_t, uint32_t> vertexMap;

                    for (size_t fi = 0; fi < part->face_indices.count; fi++)
                    {
                        uint32_t faceIdx = part->face_indices.data[fi];
                        ufbx_face face = mesh->faces.data[faceIdx];

                        uint32_t numTris = ufbx_triangulate_face(triIndices.data(), maxTriIndices, mesh, face);

                        for (uint32_t ti = 0; ti < numTris * 3; ti++)
                        {
                            uint32_t vertIdx = triIndices[ti];

                            // Create a unique key from position index + normal index + uv index
                            uint32_t posIdx = mesh->vertex_indices.data[vertIdx];
                            uint64_t key = (uint64_t)posIdx;

                            Vertex vert = {};

                            // Position
                            ufbx_vec3 pos = mesh->vertices.data[posIdx];
                            vert.Position = { (float)pos.x, (float)pos.y, (float)pos.z };

                            // Normal
                            if (mesh->vertex_normal.exists)
                            {
                                ufbx_vec3 n = ufbx_get_vertex_vec3(&mesh->vertex_normal, vertIdx);
                                vert.Normal = { (float)n.x, (float)n.y, (float)n.z };
                                key ^= ((uint64_t)(*(uint32_t*)&vert.Normal.x)) << 32;
                            }

                            // UV
                            if (mesh->vertex_uv.exists)
                            {
                                ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, vertIdx);
                                vert.TexCoord = { (float)uv.x, 1.0f - (float)uv.y }; // Flip V
                                key ^= ((uint64_t)(*(uint32_t*)&vert.TexCoord.x)) << 16;
                            }

                            // Color
                            vert.Color = matColor;

                            auto it = vertexMap.find(key);
                            if (it != vertexMap.end())
                            {
                                indices.push_back(it->second);
                            }
                            else
                            {
                                uint32_t newIdx = (uint32_t)vertices.size();
                                vertexMap[key] = newIdx;
                                vertices.push_back(vert);
                                indices.push_back(newIdx);
                            }
                        }
                    }
                }
                else
                {
                    // No material partitions — process entire mesh
                    size_t maxTriIndices = mesh->max_face_triangles * 3;
                    std::vector<uint32_t> triIndices(maxTriIndices);

                    for (size_t fi = 0; fi < mesh->faces.count; fi++)
                    {
                        ufbx_face face = mesh->faces.data[fi];
                        uint32_t numTris = ufbx_triangulate_face(triIndices.data(), maxTriIndices, mesh, face);

                        for (uint32_t ti = 0; ti < numTris * 3; ti++)
                        {
                            uint32_t vertIdx = triIndices[ti];
                            uint32_t posIdx = mesh->vertex_indices.data[vertIdx];

                            Vertex vert = {};
                            ufbx_vec3 pos = mesh->vertices.data[posIdx];
                            vert.Position = { (float)pos.x, (float)pos.y, (float)pos.z };

                            if (mesh->vertex_normal.exists)
                            {
                                ufbx_vec3 n = ufbx_get_vertex_vec3(&mesh->vertex_normal, vertIdx);
                                vert.Normal = { (float)n.x, (float)n.y, (float)n.z };
                            }

                            if (mesh->vertex_uv.exists)
                            {
                                ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, vertIdx);
                                vert.TexCoord = { (float)uv.x, 1.0f - (float)uv.y };
                            }

                            vert.Color = matColor;

                            // No dedup for simplicity in this path
                            indices.push_back((uint32_t)vertices.size());
                            vertices.push_back(vert);
                        }
                    }
                }

                if (vertices.empty()) continue;

                // Compute normals if missing
                bool hasNormals = mesh->vertex_normal.exists;
                if (!hasNormals && indices.size() >= 3)
                {
                    for (auto& v : vertices) v.Normal = { 0, 0, 0 };

                    for (size_t i = 0; i + 2 < indices.size(); i += 3)
                    {
                        Vec3& p0 = vertices[indices[i + 0]].Position;
                        Vec3& p1 = vertices[indices[i + 1]].Position;
                        Vec3& p2 = vertices[indices[i + 2]].Position;

                        Vec3 e1 = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
                        Vec3 e2 = { p2.x - p0.x, p2.y - p0.y, p2.z - p0.z };
                        Vec3 n = {
                            e1.y * e2.z - e1.z * e2.y,
                            e1.z * e2.x - e1.x * e2.z,
                            e1.x * e2.y - e1.y * e2.x
                        };

                        for (int k = 0; k < 3; k++)
                        {
                            vertices[indices[i + k]].Normal.x += n.x;
                            vertices[indices[i + k]].Normal.y += n.y;
                            vertices[indices[i + k]].Normal.z += n.z;
                        }
                    }

                    for (auto& v : vertices)
                    {
                        v.Normal = v.Normal.Normalize();
                    }
                }

                subMesh.MeshData.SetData(std::move(vertices), std::move(indices));
                result.SubMeshes.push_back(std::move(subMesh));
            }
        }

        ufbx_free_scene(scene);

        std::cout << "[Kiwi] FBX import complete: " << result.SubMeshes.size() << " sub-meshes" << std::endl;
        return result;
    }

} // namespace Kiwi
