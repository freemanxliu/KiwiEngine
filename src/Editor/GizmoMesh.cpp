#include "Editor/GizmoMesh.h"

#include <cmath>

namespace Kiwi
{

namespace
{

// Orthonormal (right, forward) pair perpendicular to axis.
void BuildBasis(const Vec3& axis, Vec3& outRight, Vec3& outForward)
{
    if (std::abs(axis.y) < 0.99f)
        outRight = Vec3(0, 1, 0).Cross(axis).Normalize();
    else
        outRight = Vec3(1, 0, 0).Cross(axis).Normalize();
    outForward = axis.Cross(outRight).Normalize();
}

void AddVertex(GizmoMeshData& mesh, const Vec3& pos, const Vec3& normal)
{
    mesh.Vertices.push_back({ pos, normal, { 1.0f, 1.0f, 1.0f, 1.0f } });
}

// Open cylinder from the origin to up * shaftLength.
void AddShaft(GizmoMeshData& mesh, const Vec3& up, const Vec3& right, const Vec3& forward,
              float shaftRadius, float shaftLength, uint32_t segments)
{
    uint32_t base = (uint32_t)mesh.Vertices.size();
    for (uint32_t i = 0; i <= segments; i++)
    {
        float theta = (float)i / segments * 2.0f * PI;
        Vec3 circleDir = right * cosf(theta) + forward * sinf(theta);
        AddVertex(mesh, circleDir * shaftRadius, circleDir);
        AddVertex(mesh, circleDir * shaftRadius + up * shaftLength, circleDir);
    }
    for (uint32_t i = 0; i < segments; i++)
    {
        uint32_t b = base + i * 2;
        mesh.Indices.insert(mesh.Indices.end(), { b, b + 2, b + 1, b + 1, b + 2, b + 3 });
    }
}

} // namespace

GizmoMeshData CreateGizmoArrow(const Vec3& axisDir, float length, float shaftRadius,
                               float headRadius, float headLength)
{
    GizmoMeshData gizmo;
    const uint32_t segments = 12;
    float shaftLength = length - headLength;

    Vec3 up = axisDir;
    Vec3 right, forward;
    BuildBasis(up, right, forward);

    AddShaft(gizmo, up, right, forward, shaftRadius, shaftLength, segments);

    // --- Cone (arrow head) ---
    uint32_t coneBase = (uint32_t)gizmo.Vertices.size();
    AddVertex(gizmo, up * length, up);
    for (uint32_t i = 0; i <= segments; i++)
    {
        float theta = (float)i / segments * 2.0f * PI;
        Vec3 circleDir = right * cosf(theta) + forward * sinf(theta);
        Vec3 pos = circleDir * headRadius + up * shaftLength;
        Vec3 norm = (circleDir + up * (headRadius / headLength)).Normalize();
        AddVertex(gizmo, pos, norm);
    }
    for (uint32_t i = 0; i < segments; i++)
        gizmo.Indices.insert(gizmo.Indices.end(), { coneBase, coneBase + 1 + i, coneBase + 2 + i });

    // --- Bottom cap of cone ---
    uint32_t capCenter = (uint32_t)gizmo.Vertices.size();
    AddVertex(gizmo, up * shaftLength, up.Negate());
    for (uint32_t i = 0; i <= segments; i++)
    {
        float theta = (float)i / segments * 2.0f * PI;
        Vec3 circleDir = right * cosf(theta) + forward * sinf(theta);
        AddVertex(gizmo, circleDir * headRadius + up * shaftLength, up.Negate());
    }
    for (uint32_t i = 0; i < segments; i++)
        gizmo.Indices.insert(gizmo.Indices.end(), { capCenter, capCenter + 2 + i, capCenter + 1 + i });

    return gizmo;
}

GizmoMeshData CreateGizmoRing(const Vec3& axisNormal, float ringRadius, float tubeRadius,
                              uint32_t ringSegs, uint32_t tubeSegs)
{
    GizmoMeshData gizmo;

    // axisNormal is the ring's "up"; T and B span the ring plane.
    Vec3 N = axisNormal;
    Vec3 T, B;
    BuildBasis(N, T, B);

    for (uint32_t i = 0; i <= ringSegs; i++)
    {
        float phi = (float)i / ringSegs * 2.0f * PI;
        float cp = cosf(phi), sp = sinf(phi);
        Vec3 ringCenter = T * (ringRadius * cp) + B * (ringRadius * sp);
        Vec3 radial = (T * cp + B * sp).Normalize();

        for (uint32_t j = 0; j <= tubeSegs; j++)
        {
            float theta = (float)j / tubeSegs * 2.0f * PI;
            Vec3 tubeDir = radial * cosf(theta) + N * sinf(theta);
            AddVertex(gizmo, ringCenter + tubeDir * tubeRadius, tubeDir);
        }
    }

    uint32_t stride = tubeSegs + 1;
    for (uint32_t i = 0; i < ringSegs; i++)
    {
        for (uint32_t j = 0; j < tubeSegs; j++)
        {
            uint32_t a = i * stride + j;
            uint32_t b = (i + 1) * stride + j;
            gizmo.Indices.insert(gizmo.Indices.end(), { a, b, a + 1, a + 1, b, b + 1 });
        }
    }
    return gizmo;
}

GizmoMeshData CreateGizmoScaleAxis(const Vec3& axisDir, float length, float shaftRadius, float cubeHalf)
{
    GizmoMeshData gizmo;
    const uint32_t segments = 12;
    float shaftLength = length - cubeHalf * 2.0f;

    Vec3 up = axisDir;
    Vec3 right, forward;
    BuildBasis(up, right, forward);

    AddShaft(gizmo, up, right, forward, shaftRadius, shaftLength, segments);

    // Cube end cap (6 faces of a box at the end)
    Vec3 c = up * length;
    Vec3 axes[3] = { right, up, forward };
    for (int face = 0; face < 6; face++)
    {
        int dim   = face / 2;
        float sign = (face % 2 == 0) ? 1.0f : -1.0f;
        Vec3 norm = axes[dim] * sign;
        Vec3 t1   = axes[(dim + 1) % 3];
        Vec3 t2   = axes[(dim + 2) % 3];
        uint32_t base = (uint32_t)gizmo.Vertices.size();
        AddVertex(gizmo, c + norm * cubeHalf - t1 * cubeHalf - t2 * cubeHalf, norm);
        AddVertex(gizmo, c + norm * cubeHalf + t1 * cubeHalf - t2 * cubeHalf, norm);
        AddVertex(gizmo, c + norm * cubeHalf + t1 * cubeHalf + t2 * cubeHalf, norm);
        AddVertex(gizmo, c + norm * cubeHalf - t1 * cubeHalf + t2 * cubeHalf, norm);
        gizmo.Indices.insert(gizmo.Indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    }
    return gizmo;
}

} // namespace Kiwi
