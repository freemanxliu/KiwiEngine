#pragma once

#include "Editor/GizmoMesh.h"
#include "Math/Math.h"
#include "RHI/RHI.h"
#include "RHI/UniformBuffer.h"
#include "Scene/Shaders.h"

#include <cstdint>
#include <memory>

namespace Kiwi
{

struct SceneObject;

enum class EGizmoAxis { None = 0, X, Y, Z };
enum class EGizmoMode { Translate = 0, Rotate, Scale };

struct GizmoViewInfo
{
    Mat4 View;
    Mat4 Projection;
    Vec3 CameraPosition;
    uint32_t ScreenWidth = 0;
    uint32_t ScreenHeight = 0;
};

// What the render thread needs to draw the handles, copied from the target on the game thread.
struct GizmoDrawState
{
    bool bVisible = false;
    Vec3 Position;
    EGizmoMode Mode = EGizmoMode::Translate;
    EGizmoAxis HighlightAxis = EGizmoAxis::None;
    bool bHasDirectionalLight = false;
    Vec3 LightForward;
    Vec3 LightUp;
    Vec3 LightRight;
};

// Translate / rotate / scale handles for the selected object.
// Owns the handle geometry and the drag state; the caller decides which object is the target.
class TransformGizmo
{
public:
    TransformGizmo();

    void CreateGPUResources(RHIDevice* device);
    void ReleaseGPUResources();

    EGizmoMode GetMode() const { return Mode; }
    void SetMode(EGizmoMode mode) { Mode = mode; }
    bool IsDragging() const { return bIsDragging; }

    // Returns true if the click hit a handle and a drag started.
    bool TryBeginDrag(SceneObject& target, int mouseX, int mouseY, const GizmoViewInfo& view);
    void UpdateDrag(SceneObject& target, int mouseX, int mouseY, const GizmoViewInfo& view);
    void EndDrag();

    // Game thread. Target may be null; the active main camera gets no handles.
    GizmoDrawState MakeDrawState(const SceneObject* Target) const;

    // Render thread. Only reads the GPU meshes and State.
    // Expects the gizmo shader, input layout and b0 view buffer to be bound already.
    void Draw(RHICommandContext* ctx, const GizmoDrawState& State, const Vec3& cameraPosition, const TUniformBufferRef<PrimitiveUniformBuffer>& objectUB) const;

private:
    struct GPUMesh
    {
        std::unique_ptr<RHIBuffer> VertexBuffer;
        std::unique_ptr<RHIBuffer> IndexBuffer;
        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;
    };

    static GPUMesh UploadMesh(RHIDevice* device, const GizmoMeshData& data, const char* namePrefix, const char* nameSuffix);
    static float ComputeScale(const Vec3& gizmoPos, const Vec3& cameraPosition);
    EGizmoAxis PickAxis(const Vec3& gizmoPos, int mouseX, int mouseY, const GizmoViewInfo& view) const;

    EGizmoMode Mode = EGizmoMode::Translate;

    GizmoMeshData TranslateMeshes[3];
    GizmoMeshData RotateMeshes[3];
    GizmoMeshData ScaleMeshes[3];
    GizmoMeshData DirLightIndicatorMesh;

    GPUMesh TranslateGPU[3];
    GPUMesh RotateGPU[3];
    GPUMesh ScaleGPU[3];
    GPUMesh DirLightIndicatorGPU;

    bool bIsDragging = false;
    EGizmoAxis DragAxis = EGizmoAxis::None;
    int DragStartMouseX = 0;
    int DragStartMouseY = 0;
    Vec3 DragStartPos;
    Vec3 DragStartRotation;
    Vec3 DragStartScale;
};

} // namespace Kiwi
