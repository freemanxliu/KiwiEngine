#include "Scene/Mesh.h"

namespace Kiwi
{

    Mesh Mesh::CreateCube(float size)
    {
        Mesh mesh;
        float half = size * 0.5f;

        // 8 corner positions
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

        // All faces use white vertex color — actual color comes from material _Color property
        Vec4 faceColors[6] = {
            { 1.0f, 1.0f, 1.0f, 1.0f }, // Front
            { 1.0f, 1.0f, 1.0f, 1.0f }, // Back
            { 1.0f, 1.0f, 1.0f, 1.0f }, // Top
            { 1.0f, 1.0f, 1.0f, 1.0f }, // Bottom
            { 1.0f, 1.0f, 1.0f, 1.0f }, // Right
            { 1.0f, 1.0f, 1.0f, 1.0f }, // Left
        };

        struct Face
        {
            int indices[4];
            Vec3 normal;
            Vec3 tangent; // Tangent aligned with U texture direction
        };

        Face faces[6] = {
            { { 4, 5, 6, 7 }, {  0,  0,  1 }, {  1,  0,  0 } }, // Front  (Z+)
            { { 1, 0, 3, 2 }, {  0,  0, -1 }, { -1,  0,  0 } }, // Back   (Z-)
            { { 7, 6, 2, 3 }, {  0,  1,  0 }, {  1,  0,  0 } }, // Top    (Y+)
            { { 4, 0, 1, 5 }, {  0, -1,  0 }, {  1,  0,  0 } }, // Bottom (Y-)
            { { 5, 1, 2, 6 }, {  1,  0,  0 }, {  0,  0, -1 } }, // Right  (X+)
            { { 0, 4, 7, 3 }, { -1,  0,  0 }, {  0,  0,  1 } }, // Left   (X-)
        };

        // UV corners for each face quad
        Vec2 faceUVs[4] = {
            { 0.0f, 1.0f }, // bottom-left
            { 1.0f, 1.0f }, // bottom-right
            { 1.0f, 0.0f }, // top-right
            { 0.0f, 0.0f }, // top-left
        };

        for (int f = 0; f < 6; f++)
        {
            Vec4 color = faceColors[f];
            Vec3 normal = faces[f].normal;
            Vec3 tangent = faces[f].tangent;

            for (int i = 0; i < 4; i++)
            {
                Vertex v;
                v.Position = positions[faces[f].indices[i]];
                v.Normal = normal;
                v.Tangent = { tangent.x, tangent.y, tangent.z, 1.0f };
                v.Color = color;
                v.TexCoord = faceUVs[i];
                mesh.m_Vertices.push_back(v);
            }

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

                // Tangent: direction of increasing theta (U direction on sphere)
                Vec3 sphereTan = Vec3(-sinf(theta), 0.0f, cosf(theta)).Normalize();
                v.Tangent = { sphereTan.x, sphereTan.y, sphereTan.z, 1.0f };

                v.Color = Vec4(1.0f, 1.0f, 1.0f, 1.0f);

                // Spherical UV mapping
                v.TexCoord = Vec2(
                    (float)x / (float)segments,
                    (float)y / (float)segments);

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

    Mesh Mesh::CreateCylinder(float radius, float height, uint32_t segments)
    {
        Mesh mesh;
        float halfH = height * 0.5f;

        // Side vertices with UV
        for (uint32_t i = 0; i <= segments; i++)
        {
            float theta = (float)i / segments * 2.0f * PI;
            float cosT = cosf(theta);
            float sinT = sinf(theta);
            float u = (float)i / (float)segments;

            Vec3 normal = { cosT, 0.0f, sinT };
            Vec3 tangent = { -sinT, 0.0f, cosT }; // Tangent along theta direction (U)

            // Bottom vertex
            Vertex vBot;
            vBot.Position = { radius * cosT, -halfH, radius * sinT };
            vBot.Normal = normal;
            vBot.Tangent = { tangent.x, tangent.y, tangent.z, 1.0f };
            vBot.Color = { 1.0f, 1.0f, 1.0f, 1.0f };
            vBot.TexCoord = { u, 1.0f };
            mesh.m_Vertices.push_back(vBot);

            // Top vertex
            Vertex vTop;
            vTop.Position = { radius * cosT, halfH, radius * sinT };
            vTop.Normal = normal;
            vTop.Tangent = { tangent.x, tangent.y, tangent.z, 1.0f };
            vTop.Color = vBot.Color;
            vTop.TexCoord = { u, 0.0f };
            mesh.m_Vertices.push_back(vTop);
        }

        // Side indices
        for (uint32_t i = 0; i < segments; i++)
        {
            uint32_t base = i * 2;
            mesh.m_Indices.push_back(base);
            mesh.m_Indices.push_back(base + 2);
            mesh.m_Indices.push_back(base + 1);

            mesh.m_Indices.push_back(base + 1);
            mesh.m_Indices.push_back(base + 2);
            mesh.m_Indices.push_back(base + 3);
        }

        // Top cap
        uint32_t topCenter = (uint32_t)mesh.m_Vertices.size();
        {
            Vertex vc;
            vc.Position = { 0, halfH, 0 };
            vc.Normal = { 0, 1, 0 };
            vc.Tangent = { 1, 0, 0, 1 };
            vc.Color = { 1.0f, 1.0f, 1.0f, 1.0f };
            vc.TexCoord = { 0.5f, 0.5f };
            mesh.m_Vertices.push_back(vc);
        }

        for (uint32_t i = 0; i <= segments; i++)
        {
            float theta = (float)i / segments * 2.0f * PI;
            float ct = cosf(theta);
            float st = sinf(theta);
            Vertex v;
            v.Position = { radius * ct, halfH, radius * st };
            v.Normal = { 0, 1, 0 };
            v.Tangent = { 1, 0, 0, 1 };
            v.Color = { 1.0f, 1.0f, 1.0f, 1.0f };
            v.TexCoord = { 0.5f + 0.5f * ct, 0.5f + 0.5f * st };
            mesh.m_Vertices.push_back(v);
        }

        for (uint32_t i = 0; i < segments; i++)
        {
            mesh.m_Indices.push_back(topCenter);
            mesh.m_Indices.push_back(topCenter + 1 + i);
            mesh.m_Indices.push_back(topCenter + 2 + i);
        }

        // Bottom cap
        uint32_t botCenter = (uint32_t)mesh.m_Vertices.size();
        {
            Vertex vc;
            vc.Position = { 0, -halfH, 0 };
            vc.Normal = { 0, -1, 0 };
            vc.Tangent = { 1, 0, 0, 1 };
            vc.Color = { 1.0f, 1.0f, 1.0f, 1.0f };
            vc.TexCoord = { 0.5f, 0.5f };
            mesh.m_Vertices.push_back(vc);
        }

        for (uint32_t i = 0; i <= segments; i++)
        {
            float theta = (float)i / segments * 2.0f * PI;
            float ct = cosf(theta);
            float st = sinf(theta);
            Vertex v;
            v.Position = { radius * ct, -halfH, radius * st };
            v.Normal = { 0, -1, 0 };
            v.Tangent = { 1, 0, 0, 1 };
            v.Color = { 1.0f, 1.0f, 1.0f, 1.0f };
            v.TexCoord = { 0.5f + 0.5f * ct, 0.5f - 0.5f * st };
            mesh.m_Vertices.push_back(v);
        }

        for (uint32_t i = 0; i < segments; i++)
        {
            mesh.m_Indices.push_back(botCenter);
            mesh.m_Indices.push_back(botCenter + 2 + i);
            mesh.m_Indices.push_back(botCenter + 1 + i);
        }

        return mesh;
    }

    Mesh Mesh::CreatePlane(float width, float height)
    {
        Mesh mesh;

        float hw = width * 0.5f;
        float hh = height * 0.5f;

        // Plane: Normal = Y+, Tangent = X+ (aligned with U direction)
        Vertex v0; v0.Position = { -hw, 0, -hh }; v0.Normal = { 0, 1, 0 }; v0.Tangent = { 1, 0, 0, 1 }; v0.Color = { 1, 1, 1, 1 }; v0.TexCoord = { 0, 0 };
        Vertex v1; v1.Position = {  hw, 0, -hh }; v1.Normal = { 0, 1, 0 }; v1.Tangent = { 1, 0, 0, 1 }; v1.Color = { 1, 1, 1, 1 }; v1.TexCoord = { 1, 0 };
        Vertex v2; v2.Position = {  hw, 0,  hh }; v2.Normal = { 0, 1, 0 }; v2.Tangent = { 1, 0, 0, 1 }; v2.Color = { 1, 1, 1, 1 }; v2.TexCoord = { 1, 1 };
        Vertex v3; v3.Position = { -hw, 0,  hh }; v3.Normal = { 0, 1, 0 }; v3.Tangent = { 1, 0, 0, 1 }; v3.Color = { 1, 1, 1, 1 }; v3.TexCoord = { 0, 1 };

        mesh.m_Vertices.push_back(v0);
        mesh.m_Vertices.push_back(v1);
        mesh.m_Vertices.push_back(v2);
        mesh.m_Vertices.push_back(v3);

        mesh.m_Indices = { 0, 1, 2, 0, 2, 3 };

        return mesh;
    }

} // namespace Kiwi
