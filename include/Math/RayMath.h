#pragma once

#include "Math/Math.h"

#include <cstdint>

namespace Kiwi
{

struct Ray
{
    Vec3 Origin;
    Vec3 Direction;
};

// Mouse position in pixels -> world-space ray from the camera eye.
Ray ScreenToRay(int mouseX, int mouseY, uint32_t screenW, uint32_t screenH,
                const Mat4& view, const Mat4& proj);

// Returns { -1, -1 } when the point projects to w ~= 0.
Vec2 WorldToScreen(const Vec3& worldPos, uint32_t screenW, uint32_t screenH,
                   const Mat4& view, const Mat4& proj);

bool RayIntersectsAABB(const Ray& ray, const Vec3& aabbMin, const Vec3& aabbMax, float& tOut);

// Parameter t such that (axisOrigin + axisDir * t) is the point on the axis closest to the ray.
float RayAxisClosestParam(const Ray& ray, const Vec3& axisOrigin, const Vec3& axisDir);

// Falls back to planePoint when the ray is parallel to the plane.
Vec3 RayPlaneIntersect(const Ray& ray, const Vec3& planeNormal, const Vec3& planePoint);

float PointToSegmentDist2D(const Vec2& p, const Vec2& a, const Vec2& b);

} // namespace Kiwi
