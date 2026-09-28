#include "Math/RayMath.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Kiwi
{

Ray ScreenToRay(int mouseX, int mouseY, uint32_t screenW, uint32_t screenH,
                const Mat4& view, const Mat4& proj)
{
    float ndcX = (2.0f * mouseX / screenW) - 1.0f;
    float ndcY = 1.0f - (2.0f * mouseY / screenH);

    float viewX = ndcX / proj.m[0][0];
    float viewY = ndcY / proj.m[1][1];

    Vec3 rayDirView = { viewX, viewY, 1.0f };

    Vec3 right  = { view.m[0][0], view.m[1][0], view.m[2][0] };
    Vec3 up     = { view.m[0][1], view.m[1][1], view.m[2][1] };
    Vec3 fwd    = { view.m[0][2], view.m[1][2], view.m[2][2] };

    Vec3 eye;
    eye.x = -(view.m[3][0] * right.x + view.m[3][1] * up.x + view.m[3][2] * fwd.x);
    eye.y = -(view.m[3][0] * right.y + view.m[3][1] * up.y + view.m[3][2] * fwd.y);
    eye.z = -(view.m[3][0] * right.z + view.m[3][1] * up.z + view.m[3][2] * fwd.z);

    Vec3 rayDirWorld;
    rayDirWorld.x = rayDirView.x * right.x + rayDirView.y * up.x + rayDirView.z * fwd.x;
    rayDirWorld.y = rayDirView.x * right.y + rayDirView.y * up.y + rayDirView.z * fwd.y;
    rayDirWorld.z = rayDirView.x * right.z + rayDirView.y * up.z + rayDirView.z * fwd.z;

    return { eye, rayDirWorld.Normalize() };
}

Vec2 WorldToScreen(const Vec3& worldPos, uint32_t screenW, uint32_t screenH,
                   const Mat4& view, const Mat4& proj)
{
    // Transform to clip space: pos * View * Proj (row-major, left-multiply)
    Mat4 vp = view * proj;
    float x = worldPos.x * vp.m[0][0] + worldPos.y * vp.m[1][0] + worldPos.z * vp.m[2][0] + vp.m[3][0];
    float y = worldPos.x * vp.m[0][1] + worldPos.y * vp.m[1][1] + worldPos.z * vp.m[2][1] + vp.m[3][1];
    float w = worldPos.x * vp.m[0][3] + worldPos.y * vp.m[1][3] + worldPos.z * vp.m[2][3] + vp.m[3][3];
    if (std::abs(w) < 1e-6f) return { -1, -1 };
    float ndcX = x / w;
    float ndcY = y / w;
    float sx = (ndcX + 1.0f) * 0.5f * screenW;
    float sy = (1.0f - ndcY) * 0.5f * screenH;
    return { sx, sy };
}

bool RayIntersectsAABB(const Ray& ray, const Vec3& aabbMin, const Vec3& aabbMax, float& tOut)
{
    float tmin = -1e30f;
    float tmax = 1e30f;

    float invDir[3] = {
        (std::abs(ray.Direction.x) > 1e-6f) ? 1.0f / ray.Direction.x : 1e30f,
        (std::abs(ray.Direction.y) > 1e-6f) ? 1.0f / ray.Direction.y : 1e30f,
        (std::abs(ray.Direction.z) > 1e-6f) ? 1.0f / ray.Direction.z : 1e30f
    };

    float origin[3] = { ray.Origin.x, ray.Origin.y, ray.Origin.z };
    float bmin[3] = { aabbMin.x, aabbMin.y, aabbMin.z };
    float bmax[3] = { aabbMax.x, aabbMax.y, aabbMax.z };

    for (int i = 0; i < 3; i++)
    {
        float t1 = (bmin[i] - origin[i]) * invDir[i];
        float t2 = (bmax[i] - origin[i]) * invDir[i];
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }

    if (tmax < 0.0f) return false;
    tOut = (tmin >= 0.0f) ? tmin : tmax;
    return true;
}

float RayAxisClosestParam(const Ray& ray, const Vec3& axisOrigin, const Vec3& axisDir)
{
    Vec3 w = ray.Origin - axisOrigin;
    float a = axisDir.Dot(axisDir);       // == 1 if normalized
    float b = axisDir.Dot(ray.Direction);
    float c = ray.Direction.Dot(ray.Direction); // == 1 if normalized
    float d = axisDir.Dot(w);
    float e = ray.Direction.Dot(w);

    float denom = a * c - b * b;
    if (std::abs(denom) < 1e-6f) return 0.0f;
    return (c * d - b * e) / denom;
}

Vec3 RayPlaneIntersect(const Ray& ray, const Vec3& planeNormal, const Vec3& planePoint)
{
    float denom = planeNormal.Dot(ray.Direction);
    if (std::abs(denom) < 1e-6f)
        return planePoint;
    float t = planeNormal.Dot(planePoint - ray.Origin) / denom;
    return ray.Origin + ray.Direction * t;
}

float PointToSegmentDist2D(const Vec2& p, const Vec2& a, const Vec2& b)
{
    Vec2 ab = b - a;
    Vec2 ap = p - a;
    float abLenSq = ab.x * ab.x + ab.y * ab.y;
    if (abLenSq < 1e-6f) // degenerate segment
    {
        return std::sqrt(ap.x * ap.x + ap.y * ap.y);
    }
    float t = (ap.x * ab.x + ap.y * ab.y) / abLenSq;
    t = std::max(0.0f, std::min(1.0f, t));
    Vec2 closest = { a.x + ab.x * t, a.y + ab.y * t };
    Vec2 diff = { p.x - closest.x, p.y - closest.y };
    return std::sqrt(diff.x * diff.x + diff.y * diff.y);
}

} // namespace Kiwi
