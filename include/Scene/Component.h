#pragma once

#include "Math/Math.h"
#include <string>
#include <cstdint>

namespace Kiwi
{
    // Forward declaration
    struct SceneObject;

    // ============================================================
    // Component Type — identifies the type of a component
    // ============================================================
    enum class EComponentType
    {
        Base,        // Base component (Transform only)
        Mesh,        // Mesh renderer component
        Camera,      // Camera component
        Light,       // Light component (Directional, Point, etc.)
        PostProcess, // Post-processing component (full-screen shader effects)
    };

    // ============================================================
    // Component — base class for all components
    // All components have Transform (Position, Rotation, Scale)
    // ============================================================
    class Component
    {
    public:
        Component() = default;
        virtual ~Component() = default;

        // Non-copyable, movable
        Component(const Component&) = delete;
        Component& operator=(const Component&) = delete;
        Component(Component&&) = default;
        Component& operator=(Component&&) = default;

        // Component type identification
        virtual EComponentType GetType() const { return EComponentType::Base; }
        virtual const char* GetTypeName() const { return "Component"; }

        // Transform — every component has position, rotation, scale
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

        // Get forward direction (local +Z in world space)
        Vec3 GetForward() const
        {
            Mat4 rotX = Mat4::RotationX(DegToRad(Rotation.x));
            Mat4 rotY = Mat4::RotationY(DegToRad(Rotation.y));
            Mat4 rotZ = Mat4::RotationZ(DegToRad(Rotation.z));
            Mat4 rot = rotZ * rotX * rotY;
            // Forward is +Z in local space (left-handed)
            return Vec3(rot.m[2][0], rot.m[2][1], rot.m[2][2]).Normalize();
        }

        // Get up direction (local +Y in world space)
        Vec3 GetUp() const
        {
            Mat4 rotX = Mat4::RotationX(DegToRad(Rotation.x));
            Mat4 rotY = Mat4::RotationY(DegToRad(Rotation.y));
            Mat4 rotZ = Mat4::RotationZ(DegToRad(Rotation.z));
            Mat4 rot = rotZ * rotX * rotY;
            return Vec3(rot.m[1][0], rot.m[1][1], rot.m[1][2]).Normalize();
        }

        // Get right direction (local +X in world space)
        Vec3 GetRight() const
        {
            Mat4 rotX = Mat4::RotationX(DegToRad(Rotation.x));
            Mat4 rotY = Mat4::RotationY(DegToRad(Rotation.y));
            Mat4 rotZ = Mat4::RotationZ(DegToRad(Rotation.z));
            Mat4 rot = rotZ * rotX * rotY;
            return Vec3(rot.m[0][0], rot.m[0][1], rot.m[0][2]).Normalize();
        }

        // Whether this component is enabled
        bool Enabled = true;

        // Owner object (set by SceneObject when adding)
        SceneObject* Owner = nullptr;
    };

} // namespace Kiwi
