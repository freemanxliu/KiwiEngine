#pragma once

#include "Scene/Mesh.h"
#include "Math/Math.h"

#include <cstdint>
#include <vector>

namespace Kiwi
{

// CPU geometry for gizmo parts. Vertices are white; the tint comes from the object uniform buffer.
struct GizmoMeshData
{
    std::vector<Vertex> Vertices;
    std::vector<uint32_t> Indices;
};

// Translate handle: shaft cylinder + cone head along axisDir.
GizmoMeshData CreateGizmoArrow(const Vec3& axisDir, float length = 1.2f, float shaftRadius = 0.02f,
                               float headRadius = 0.06f, float headLength = 0.2f);

// Rotate handle: torus around axisNormal.
GizmoMeshData CreateGizmoRing(const Vec3& axisNormal, float ringRadius = 1.0f, float tubeRadius = 0.03f,
                              uint32_t ringSegs = 40, uint32_t tubeSegs = 8);

// Scale handle: shaft cylinder + cube end cap along axisDir.
GizmoMeshData CreateGizmoScaleAxis(const Vec3& axisDir, float length = 1.2f, float shaftRadius = 0.02f,
                                   float cubeHalf = 0.07f);

} // namespace Kiwi
