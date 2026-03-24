#pragma once

#include "Math/Math.h"
#include "Scene/Mesh.h"
#include <string>
#include <cstdint>

namespace Kiwi
{

    // 支持的图元类型
    enum class EPrimitiveType
    {
        Cube,
        Sphere,
        Cylinder,
        Floor,
    };

    inline const char* PrimitiveTypeToString(EPrimitiveType type)
    {
        switch (type)
        {
        case EPrimitiveType::Cube:     return "Cube";
        case EPrimitiveType::Sphere:   return "Sphere";
        case EPrimitiveType::Cylinder: return "Cylinder";
        case EPrimitiveType::Floor:    return "Floor";
        default:                       return "Unknown";
        }
    }

    inline EPrimitiveType StringToPrimitiveType(const std::string& str)
    {
        if (str == "Cube")     return EPrimitiveType::Cube;
        if (str == "Sphere")   return EPrimitiveType::Sphere;
        if (str == "Cylinder") return EPrimitiveType::Cylinder;
        if (str == "Floor")    return EPrimitiveType::Floor;
        return EPrimitiveType::Cube;
    }

    // Transform 组件
    struct Transform
    {
        Vec3 Position = { 0.0f, 0.0f, 0.0f };
        Vec3 Rotation = { 0.0f, 0.0f, 0.0f }; // Euler angles in degrees
        Vec3 Scale    = { 1.0f, 1.0f, 1.0f };

        Mat4 GetWorldMatrix() const
        {
            Mat4 scale = Mat4::Scaling(Scale.x, Scale.y, Scale.z);
            Mat4 rotX = Mat4::RotationX(DegToRad(Rotation.x));
            Mat4 rotY = Mat4::RotationY(DegToRad(Rotation.y));
            Mat4 rotZ = Mat4::RotationZ(DegToRad(Rotation.z));
            Mat4 translation = Mat4::Translation(Position.x, Position.y, Position.z);

            // Scale -> RotZ -> RotX -> RotY -> Translate (row-major, left-multiply)
            return scale * rotZ * rotX * rotY * translation;
        }
    };

    // 场景物体
    struct SceneObject
    {
        uint32_t       ID = 0;
        std::string    Name;
        EPrimitiveType PrimitiveType = EPrimitiveType::Cube;
        Transform      TransformData;
        Vec4           Color = { 0.8f, 0.8f, 0.8f, 1.0f }; // 物体颜色

        // 运行时数据（不序列化）
        Mesh           MeshData;
        bool           Selected = false;
    };

} // namespace Kiwi
