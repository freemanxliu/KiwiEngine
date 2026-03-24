#include "Scene/Mesh.h"

namespace Kiwi
{

    Mesh Mesh::CreateCube(float size)
    {
        Mesh mesh;
        float half = size * 0.5f;

        // 定义8个顶点
        Vec3 positions[8] = {
            { -half, -half, -half }, // 0
            {  half, -half, -half }, // 1
            {  half,  half, -half }, // 2
            { -half,  half, -half }, // 3
            { -half, -half,  half }, // 4
            {  half, -half,  half }, // 5
            {  half,  half,  half }, // 6
            { -half,  half,  half }, // 7
        };

        // 为每个面定义不同的颜色
        Vec4 faceColors[6] = {
            { 1.0f, 0.2f, 0.2f, 1.0f }, // Front  - Red
            { 0.2f, 1.0f, 0.2f, 1.0f }, // Back   - Green
            { 0.2f, 0.2f, 1.0f, 1.0f }, // Top    - Blue
            { 1.0f, 1.0f, 0.2f, 1.0f }, // Bottom - Yellow
            { 1.0f, 0.2f, 1.0f, 1.0f }, // Right  - Magenta
            { 0.2f, 1.0f, 1.0f, 1.0f }, // Left   - Cyan
        };

        // 定义6个面（每面2个三角形，24个顶点）
        struct Face
        {
            int indices[4];
            Vec3 normal;
        };

        Face faces[6] = {
            // Front (Z+)
            { { 4, 5, 6, 7 }, {  0,  0,  1 } },
            // Back (Z-)
            { { 1, 0, 3, 2 }, {  0,  0, -1 } },
            // Top (Y+)
            { { 3, 2, 6, 7 }, {  0,  1,  0 } },
            // Bottom (Y-)
            { { 0, 1, 5, 4 }, {  0, -1,  0 } },
            // Right (X+)
            { { 1, 5, 6, 2 }, {  1,  0,  0 } },
            // Left (X-)
            { { 0, 4, 7, 3 }, { -1,  0,  0 } },
        };

        for (int f = 0; f < 6; f++)
        {
            Vec4 color = faceColors[f];
            Vec3 normal = faces[f].normal;

            // 4个顶点
            for (int i = 0; i < 4; i++)
            {
                Vertex v;
                v.Position = positions[faces[f].indices[i]];
                v.Normal = normal;
                v.Color = color;
                mesh.m_Vertices.push_back(v);
            }

            // 2个三角形（使用 4个顶点中的索引）
            uint32_t base = (uint32_t)(f * 4);
            mesh.m_Indices.push_back(base + 0);
            mesh.m_Indices.push_back(base + 1);
            mesh.m_Indices.push_back(base + 2);

            mesh.m_Indices.push_back(base + 0);
            mesh.m_Indices.push_back(base + 2);
            mesh.m_Indices.push_back(base + 3);
        }

        return mesh;
    }

    Mesh Mesh::CreateSphere(float radius, uint32_t segments)
    {
        Mesh mesh;

        // 经纬线球体生成
        for (uint32_t y = 0; y <= segments; y++)
        {
            float phi = (float)y / segments * PI;
            for (uint32_t x = 0; x <= segments; x++)
            {
                float theta = (float)x / segments * 2.0f * PI;

                Vertex v;
                v.Position.x = radius * sinf(phi) * cosf(theta);
                v.Position.y = radius * cosf(phi);
                v.Position.z = radius * sinf(phi) * sinf(theta);

                v.Normal = v.Position.Normalize();

                // 简单颜色：根据位置
                v.Color = Vec4(
                    0.5f + 0.5f * v.Normal.x,
                    0.5f + 0.5f * v.Normal.y,
                    0.5f + 0.5f * v.Normal.z,
                    1.0f);

                mesh.m_Vertices.push_back(v);
            }
        }

        for (uint32_t y = 0; y < segments; y++)
        {
            for (uint32_t x = 0; x < segments; x++)
            {
                uint32_t a = y * (segments + 1) + x;
                uint32_t b = a + 1;
                uint32_t c = a + (segments + 1);
                uint32_t d = c + 1;

                mesh.m_Indices.push_back(a);
                mesh.m_Indices.push_back(c);
                mesh.m_Indices.push_back(b);

                mesh.m_Indices.push_back(b);
                mesh.m_Indices.push_back(c);
                mesh.m_Indices.push_back(d);
            }
        }

        return mesh;
    }

    Mesh Mesh::CreatePlane(float width, float height)
    {
        Mesh mesh;

        float hw = width * 0.5f;
        float hh = height * 0.5f;

        Vertex v0 = { { -hw, 0, -hh }, { 0, 1, 0 }, { 1, 1, 1, 1 } };
        Vertex v1 = { {  hw, 0, -hh }, { 0, 1, 0 }, { 1, 1, 1, 1 } };
        Vertex v2 = { {  hw, 0,  hh }, { 0, 1, 0 }, { 1, 1, 1, 1 } };
        Vertex v3 = { { -hw, 0,  hh }, { 0, 1, 0 }, { 1, 1, 1, 1 } };

        mesh.m_Vertices.push_back(v0);
        mesh.m_Vertices.push_back(v1);
        mesh.m_Vertices.push_back(v2);
        mesh.m_Vertices.push_back(v3);

        mesh.m_Indices = { 0, 2, 1, 0, 3, 2 };

        return mesh;
    }

} // namespace Kiwi
