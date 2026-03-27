#pragma once

#include "RHI/RHI.h"
#include "Math/Math.h"
#include <vector>

namespace Kiwi
{

    // ============================================================
    // 顶点结构
    // ============================================================

    struct Vertex
    {
        Vec3 Position;
        Vec3 Normal;
        Vec3 Tangent;     // Tangent vector for TBN normal mapping
        Vec4 Color;
        Vec2 TexCoord;    // UV coordinates for texture sampling
    };

    // ============================================================
    // Mesh - 存储顶点/索引数据
    // ============================================================

    class Mesh
    {
    public:
        Mesh() = default;

        const std::vector<Vertex>& GetVertices() const { return m_Vertices; }
        const std::vector<uint32_t>& GetIndices() const { return m_Indices; }

        uint32_t GetVertexCount() const { return (uint32_t)m_Vertices.size(); }
        uint32_t GetIndexCount() const { return (uint32_t)m_Indices.size(); }
        uint32_t GetIndexFormatSize() const { return 4; } // 32-bit indices

        // 创建标准图元
        static Mesh CreateCube(float size = 1.0f);
        static Mesh CreateSphere(float radius = 0.5f, uint32_t segments = 32);
        static Mesh CreateCylinder(float radius = 0.5f, float height = 1.0f, uint32_t segments = 24);
        static Mesh CreatePlane(float width = 1.0f, float height = 1.0f);

    private:
        std::vector<Vertex> m_Vertices;
        std::vector<uint32_t> m_Indices;
    };

} // namespace Kiwi
