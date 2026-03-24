#pragma once

#include <cmath>
#include <cstring>
#include <algorithm>

namespace Kiwi
{

    // ============================================================
    // 2D Vector
    // ============================================================
    struct Vec2
    {
        float x = 0.0f, y = 0.0f;

        Vec2() = default;
        Vec2(float x, float y) : x(x), y(y) {}
        Vec2 operator+(const Vec2& v) const { return { x + v.x, y + v.y }; }
        Vec2 operator-(const Vec2& v) const { return { x - v.x, y - v.y }; }
        Vec2 operator*(float s) const { return { x * s, y * s }; }
    };

    // ============================================================
    // 3D Vector
    // ============================================================
    struct Vec3
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;

        Vec3() = default;
        Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

        Vec3 operator+(const Vec3& v) const { return { x + v.x, y + v.y, z + v.z }; }
        Vec3 operator-(const Vec3& v) const { return { x - v.x, y - v.y, z - v.z }; }
        Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
        Vec3 operator*(const Vec3& v) const { return { x * v.x, y * v.y, z * v.z }; }
        Vec3 operator/(float s) const { return { x / s, y / s, z / s }; }

        Vec3 Cross(const Vec3& v) const
        {
            return {
                y * v.z - z * v.y,
                z * v.x - x * v.z,
                x * v.y - y * v.x
            };
        }

        float Dot(const Vec3& v) const
        {
            return x * v.x + y * v.y + z * v.z;
        }

        float Length() const
        {
            return std::sqrt(x * x + y * y + z * z);
        }

        Vec3 Normalize() const
        {
            float len = Length();
            if (len > 0.0001f) return *this / len;
            return { 0, 0, 0 };
        }

        Vec3 Negate() const { return { -x, -y, -z }; }
    };

    // ============================================================
    // 4D Vector
    // ============================================================
    struct Vec4
    {
        float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;

        Vec4() = default;
        Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
        Vec4(const Vec3& v, float w) : x(v.x), y(v.y), z(v.z), w(w) {}
    };

    // ============================================================
    // 4x4 Matrix (row-major for HLSL compatibility)
    // ============================================================
    struct Mat4
    {
        float m[4][4] = {
            { 1, 0, 0, 0 },
            { 0, 1, 0, 0 },
            { 0, 0, 1, 0 },
            { 0, 0, 0, 1 }
        };

        Mat4() = default;

        static Mat4 Identity()
        {
            Mat4 result;
            memset(result.m, 0, sizeof(result.m));
            result.m[0][0] = result.m[1][1] = result.m[2][2] = result.m[3][3] = 1.0f;
            return result;
        }

        static Mat4 Translation(float tx, float ty, float tz)
        {
            Mat4 result = Identity();
            result.m[3][0] = tx;
            result.m[3][1] = ty;
            result.m[3][2] = tz;
            return result;
        }

        static Mat4 Scaling(float sx, float sy, float sz)
        {
            Mat4 result = Identity();
            result.m[0][0] = sx;
            result.m[1][1] = sy;
            result.m[2][2] = sz;
            return result;
        }

        static Mat4 RotationX(float angleRad)
        {
            Mat4 result = Identity();
            float c = cosf(angleRad);
            float s = sinf(angleRad);
            result.m[1][1] = c;  result.m[1][2] = s;
            result.m[2][1] = -s; result.m[2][2] = c;
            return result;
        }

        static Mat4 RotationY(float angleRad)
        {
            Mat4 result = Identity();
            float c = cosf(angleRad);
            float s = sinf(angleRad);
            result.m[0][0] = c;  result.m[0][2] = -s;
            result.m[2][0] = s;  result.m[2][2] = c;
            return result;
        }

        static Mat4 RotationZ(float angleRad)
        {
            Mat4 result = Identity();
            float c = cosf(angleRad);
            float s = sinf(angleRad);
            result.m[0][0] = c;  result.m[0][1] = s;
            result.m[1][0] = -s; result.m[1][1] = c;
            return result;
        }

        // 透视投影矩阵（DirectX 风格，Z 范围 [0, 1]）
        static Mat4 Perspective(float fovY, float aspect, float nearZ, float farZ)
        {
            Mat4 result;
            memset(result.m, 0, sizeof(result.m));

            float tanHalfFov = tanf(fovY * 0.5f);
            result.m[0][0] = 1.0f / (aspect * tanHalfFov);
            result.m[1][1] = 1.0f / tanHalfFov;
            result.m[2][2] = farZ / (farZ - nearZ);
            result.m[2][3] = 1.0f;
            result.m[3][2] = -(nearZ * farZ) / (farZ - nearZ);

            return result;
        }

        // 正交投影矩阵
        static Mat4 Orthographic(float width, float height, float nearZ, float farZ)
        {
            Mat4 result;
            memset(result.m, 0, sizeof(result.m));

            result.m[0][0] = 2.0f / width;
            result.m[1][1] = 2.0f / height;
            result.m[2][2] = 1.0f / (farZ - nearZ);
            result.m[3][2] = -nearZ / (farZ - nearZ);
            result.m[3][3] = 1.0f;

            return result;
        }

        // LookAt 矩阵
        static Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up)
        {
            Vec3 zAxis = (target - eye).Normalize(); // LH: +Z 朝向目标（远离相机）
            Vec3 xAxis = up.Cross(zAxis).Normalize();
            Vec3 yAxis = zAxis.Cross(xAxis);

            Mat4 result;
            memset(result.m, 0, sizeof(result.m));

            result.m[0][0] = xAxis.x;
            result.m[0][1] = yAxis.x;
            result.m[0][2] = zAxis.x;

            result.m[1][0] = xAxis.y;
            result.m[1][1] = yAxis.y;
            result.m[1][2] = zAxis.y;

            result.m[2][0] = xAxis.z;
            result.m[2][1] = yAxis.z;
            result.m[2][2] = zAxis.z;

            result.m[3][0] = -xAxis.Dot(eye);
            result.m[3][1] = -yAxis.Dot(eye);
            result.m[3][2] = -zAxis.Dot(eye);
            result.m[3][3] = 1.0f;

            return result;
        }

        // 矩阵乘法
        Mat4 operator*(const Mat4& other) const
        {
            Mat4 result;
            for (int i = 0; i < 4; i++)
            {
                for (int j = 0; j < 4; j++)
                {
                    result.m[i][j] = 0;
                    for (int k = 0; k < 4; k++)
                    {
                        result.m[i][j] += m[i][k] * other.m[k][j];
                    }
                }
            }
            return result;
        }

        // 转置
        Mat4 Transpose() const
        {
            Mat4 result;
            for (int i = 0; i < 4; i++)
                for (int j = 0; j < 4; j++)
                    result.m[i][j] = m[j][i];
            return result;
        }
    };

    // ============================================================
    // 常量
    // ============================================================
    constexpr float PI = 3.14159265358979323846f;
    constexpr float DEG2RAD = PI / 180.0f;
    constexpr float RAD2DEG = 180.0f / PI;

    inline float DegToRad(float deg) { return deg * DEG2RAD; }
    inline float RadToDeg(float rad) { return rad * RAD2DEG; }

} // namespace Kiwi
