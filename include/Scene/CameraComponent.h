#pragma once

#include "Scene/Component.h"

namespace Kiwi
{

    // ============================================================
    // Camera projection type
    // ============================================================
    enum class ECameraProjection
    {
        Perspective,
        Orthographic,
    };

    // ============================================================
    // CameraComponent — camera at the component's transform
    // ============================================================
    class CameraComponent : public Component
    {
    public:
        CameraComponent() = default;
        ~CameraComponent() override = default;

        CameraComponent(CameraComponent&&) = default;
        CameraComponent& operator=(CameraComponent&&) = default;

        EComponentType GetType() const override { return EComponentType::Camera; }
        const char* GetTypeName() const override { return "CameraComponent"; }

        // Camera parameters
        ECameraProjection Projection = ECameraProjection::Perspective;
        float FieldOfView  = 45.0f;   // In degrees (perspective only)
        float NearPlane    = 0.1f;
        float FarPlane     = 100.0f;
        float OrthoWidth   = 10.0f;   // Orthographic width
        float OrthoHeight  = 10.0f;   // Orthographic height

        // Main camera flag — only one camera should be the main camera at a time.
        // The main camera's transform drives the engine's rendering viewpoint.
        bool IsMainCamera  = false;

        // Computed matrices (updated by the engine each frame)
        Mat4 ViewMatrix;
        Mat4 ProjectionMatrix;

        // Build view matrix from transform
        void UpdateViewMatrix()
        {
            // Camera looks along +Z (forward) in its local space
            Vec3 target = Position + GetForward();
            Vec3 up = GetUp();
            ViewMatrix = Mat4::LookAt(Position, target, up);
        }

        // Build projection matrix
        void UpdateProjectionMatrix(float aspectRatio)
        {
            if (Projection == ECameraProjection::Perspective)
            {
                ProjectionMatrix = Mat4::Perspective(
                    DegToRad(FieldOfView), aspectRatio, NearPlane, FarPlane);
            }
            else
            {
                ProjectionMatrix = Mat4::Orthographic(
                    OrthoWidth, OrthoHeight, NearPlane, FarPlane);
            }
        }

        // Update both matrices
        void UpdateMatrices(float aspectRatio)
        {
            UpdateViewMatrix();
            UpdateProjectionMatrix(aspectRatio);
        }
    };

} // namespace Kiwi
