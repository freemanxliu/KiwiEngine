#include "Editor/TransformGizmo.h"

#include "Math/RayMath.h"
#include "Scene/CameraComponent.h"
#include "Scene/LightComponent.h"
#include "Scene/SceneObject.h"
#include "Scene/Shaders.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace Kiwi
{

namespace
{

const Vec3 kAxisDirs[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
const char* const kAxisSuffixes[3] = { "_X", "_Y", "_Z" };

// Must match the handle sizes passed to CreateGizmoArrow / CreateGizmoRing.
constexpr float kHandleLength = 1.2f;
constexpr float kRingRadius = 1.0f;
constexpr float kPickThresholdPx = 14.0f;

int AxisIndex(EGizmoAxis axis)
{
    return (int)axis - 1;
}

} // namespace

TransformGizmo::TransformGizmo()
{
    for (int i = 0; i < 3; i++)
    {
        TranslateMeshes[i] = CreateGizmoArrow(kAxisDirs[i]);
        RotateMeshes[i] = CreateGizmoRing(kAxisDirs[i]);
        ScaleMeshes[i] = CreateGizmoScaleAxis(kAxisDirs[i]);
    }
    // Direction indicator for directional lights (longer arrow)
    DirLightIndicatorMesh = CreateGizmoArrow({ 0, 0, 1 }, 2.0f, 0.03f, 0.08f, 0.3f);
}

TransformGizmo::GPUMesh TransformGizmo::UploadMesh(RHIDevice* device, const GizmoMeshData& data,
                                                   const char* namePrefix, const char* nameSuffix)
{
    GPUMesh mesh;
    mesh.VertexCount = (uint32_t)data.Vertices.size();
    mesh.IndexCount = (uint32_t)data.Indices.size();
    if (mesh.VertexCount == 0)
        return mesh;

    std::string vbName = std::string(namePrefix) + "VB" + nameSuffix;
    std::string ibName = std::string(namePrefix) + "IB" + nameSuffix;

    BufferDesc vbDesc;
    vbDesc.SizeInBytes = mesh.VertexCount * sizeof(Vertex);
    vbDesc.BindFlags = BUFFER_USAGE_VERTEX;
    vbDesc.Usage = EResourceUsage::Immutable;
    vbDesc.DebugName = vbName.c_str();
    mesh.VertexBuffer = device->CreateBuffer(vbDesc, data.Vertices.data());

    BufferDesc ibDesc;
    ibDesc.SizeInBytes = mesh.IndexCount * sizeof(uint32_t);
    ibDesc.BindFlags = BUFFER_USAGE_INDEX;
    ibDesc.Usage = EResourceUsage::Immutable;
    ibDesc.DebugName = ibName.c_str();
    mesh.IndexBuffer = device->CreateBuffer(ibDesc, data.Indices.data());
    return mesh;
}

void TransformGizmo::CreateGPUResources(RHIDevice* device)
{
    for (int i = 0; i < 3; i++)
    {
        TranslateGPU[i] = UploadMesh(device, TranslateMeshes[i], "Gizmo", kAxisSuffixes[i]);
        RotateGPU[i] = UploadMesh(device, RotateMeshes[i], "GizmoRing", kAxisSuffixes[i]);
        ScaleGPU[i] = UploadMesh(device, ScaleMeshes[i], "GizmoScale", kAxisSuffixes[i]);
    }
    DirLightIndicatorGPU = UploadMesh(device, DirLightIndicatorMesh, "Gizmo", "_DirLight");
}

void TransformGizmo::ReleaseGPUResources()
{
    for (int i = 0; i < 3; i++)
    {
        TranslateGPU[i] = {};
        RotateGPU[i] = {};
        ScaleGPU[i] = {};
    }
    DirLightIndicatorGPU = {};
}

// Keeps the gizmo roughly the same pixel size on screen: 1x at the reference distance.
float TransformGizmo::ComputeScale(const Vec3& gizmoPos, const Vec3& cameraPosition)
{
    const float referenceDistance = 8.0f;
    float dist = (gizmoPos - cameraPosition).Length();
    return std::max(0.1f, dist / referenceDistance);
}

EGizmoAxis TransformGizmo::PickAxis(const Vec3& gizmoPos, int mouseX, int mouseY, const GizmoViewInfo& view) const
{
    const uint32_t w = view.ScreenWidth;
    const uint32_t h = view.ScreenHeight;

    float gizmoScale = ComputeScale(gizmoPos, view.CameraPosition);
    float handleLength = kHandleLength * gizmoScale;
    float ringRadius = kRingRadius * gizmoScale;

    Vec2 originSS = WorldToScreen(gizmoPos, w, h, view.View, view.Projection);
    if (originSS.x < 0) return EGizmoAxis::None;

    const EGizmoAxis axisTypes[3] = { EGizmoAxis::X, EGizmoAxis::Y, EGizmoAxis::Z };
    Vec2 mousePos = { (float)mouseX, (float)mouseY };

    float closestDist = 1e30f;
    EGizmoAxis result = EGizmoAxis::None;

    if (Mode == EGizmoMode::Translate || Mode == EGizmoMode::Scale)
    {
        for (int i = 0; i < 3; i++)
        {
            Vec3 tipWorld = gizmoPos + kAxisDirs[i] * handleLength;
            Vec2 tipSS = WorldToScreen(tipWorld, w, h, view.View, view.Projection);
            if (tipSS.x < 0) continue;

            float dist = PointToSegmentDist2D(mousePos, originSS, tipSS);
            if (dist < kPickThresholdPx && dist < closestDist)
            {
                closestDist = dist;
                result = axisTypes[i];
            }
        }
    }
    else if (Mode == EGizmoMode::Rotate)
    {
        // Sample the ring circumference and take the projected point closest to the mouse
        for (int i = 0; i < 3; i++)
        {
            Vec3 N = kAxisDirs[i];
            Vec3 T;
            if (std::abs(N.y) < 0.99f) T = Vec3(0, 1, 0).Cross(N).Normalize();
            else                        T = Vec3(1, 0, 0).Cross(N).Normalize();
            Vec3 B = N.Cross(T).Normalize();

            const int kSamples = 40;
            float minDist = 1e30f;
            for (int s = 0; s < kSamples; s++)
            {
                float phi = (float)s / kSamples * 2.0f * PI;
                Vec3 pt = gizmoPos + (T * cosf(phi) + B * sinf(phi)) * ringRadius;
                Vec2 ptSS = WorldToScreen(pt, w, h, view.View, view.Projection);
                if (ptSS.x < 0) continue;
                Vec2 diff = mousePos - ptSS;
                minDist = std::min(minDist, sqrtf(diff.x * diff.x + diff.y * diff.y));
            }
            if (minDist < kPickThresholdPx && minDist < closestDist)
            {
                closestDist = minDist;
                result = axisTypes[i];
            }
        }
    }
    return result;
}

bool TransformGizmo::TryBeginDrag(SceneObject& target, int mouseX, int mouseY, const GizmoViewInfo& view)
{
    DragAxis = PickAxis(target.GetPosition(), mouseX, mouseY, view);
    if (DragAxis == EGizmoAxis::None)
        return false;

    bIsDragging = true;
    DragStartMouseX = mouseX;
    DragStartMouseY = mouseY;
    DragStartPos      = target.GetPosition();
    DragStartRotation = target.GetRotation();
    DragStartScale    = target.GetScale();
    return true;
}

void TransformGizmo::UpdateDrag(SceneObject& target, int mouseX, int mouseY, const GizmoViewInfo& view)
{
    int axisIdx = AxisIndex(DragAxis);
    if (!bIsDragging || axisIdx < 0 || axisIdx > 2)
        return;

    const Vec3 axisDir = kAxisDirs[axisIdx];
    Ray rayNow   = ScreenToRay(mouseX, mouseY, view.ScreenWidth, view.ScreenHeight, view.View, view.Projection);
    Ray rayStart = ScreenToRay(DragStartMouseX, DragStartMouseY, view.ScreenWidth, view.ScreenHeight, view.View, view.Projection);

    if (Mode == EGizmoMode::Translate)
    {
        float tNow   = RayAxisClosestParam(rayNow,   DragStartPos, axisDir);
        float tStart = RayAxisClosestParam(rayStart, DragStartPos, axisDir);
        target.GetPosition() = DragStartPos + axisDir * (tNow - tStart);
    }
    else if (Mode == EGizmoMode::Rotate)
    {
        // Signed angle between the start and current hits on the rotation plane
        Vec3 center = DragStartPos;
        Vec3 vStart = RayPlaneIntersect(rayStart, axisDir, center) - center;
        Vec3 vNow   = RayPlaneIntersect(rayNow,   axisDir, center) - center;

        float lenS = vStart.Length();
        float lenN = vNow.Length();
        if (lenS > 1e-5f && lenN > 1e-5f)
        {
            vStart = vStart * (1.0f / lenS);
            vNow   = vNow * (1.0f / lenN);

            float sinAngle = vStart.Cross(vNow).Dot(axisDir);
            float cosAngle = vStart.Dot(vNow);
            float deltaDeg = atan2f(sinAngle, cosAngle) * (180.0f / PI);

            Vec3 newRot = DragStartRotation;
            if      (DragAxis == EGizmoAxis::X) newRot.x += deltaDeg;
            else if (DragAxis == EGizmoAxis::Y) newRot.y += deltaDeg;
            else if (DragAxis == EGizmoAxis::Z) newRot.z += deltaDeg;
            target.GetRotation() = newRot;
        }
    }
    else if (Mode == EGizmoMode::Scale)
    {
        float tNow   = RayAxisClosestParam(rayNow,   DragStartPos, axisDir);
        float tStart = RayAxisClosestParam(rayStart, DragStartPos, axisDir);
        float delta  = tNow - tStart; // world units dragged

        Vec3 newScale = DragStartScale;
        if      (DragAxis == EGizmoAxis::X) newScale.x = std::max(0.001f, DragStartScale.x + delta);
        else if (DragAxis == EGizmoAxis::Y) newScale.y = std::max(0.001f, DragStartScale.y + delta);
        else if (DragAxis == EGizmoAxis::Z) newScale.z = std::max(0.001f, DragStartScale.z + delta);
        target.GetScale() = newScale;
    }

    if (Component* Primary = target.GetPrimaryComponent())
        Primary->MarkRenderTransformDirty();
}

void TransformGizmo::EndDrag()
{
    bIsDragging = false;
    DragAxis = EGizmoAxis::None;
}

GizmoDrawState TransformGizmo::MakeDrawState(const SceneObject* Target) const
{
    GizmoDrawState State;
    if (!Target)
        return State;
    const auto* CamComp = Target->GetComponent<CameraComponent>();
    if (CamComp && CamComp->IsMainCamera)
        return State;

    State.bVisible = true;
    if (const Component* Primary = Target->GetPrimaryComponent())
        State.Position = Primary->Position;
    State.Mode = Mode;
    State.HighlightAxis = bIsDragging ? DragAxis : EGizmoAxis::None;
    if (const auto* DirLight = Target->GetComponent<DirectionalLightComponent>())
    {
        State.bHasDirectionalLight = true;
        State.LightForward = DirLight->GetForward();
        State.LightUp = DirLight->GetUp();
        State.LightRight = DirLight->GetRight();
    }
    return State;
}

void TransformGizmo::Draw(RHICommandContext* ctx, const GizmoDrawState& State, const Vec3& cameraPosition, const TUniformBufferRef<PrimitiveUniformBuffer>& objectUB) const
{
    if (!State.bVisible)
        return;
    Vec3 gizmoPos = State.Position;
    float gizmoScale = ComputeScale(gizmoPos, cameraPosition);

    Vec4 colors[3] = {
        { 1.0f, 0.2f, 0.2f, 1.0f }, // X - Red
        { 0.2f, 1.0f, 0.2f, 1.0f }, // Y - Green
        { 0.2f, 0.4f, 1.0f, 1.0f }, // Z - Blue
    };

    int axisIdx = AxisIndex(State.HighlightAxis);
    if (axisIdx >= 0 && axisIdx < 3)
        colors[axisIdx] = { 1.0f, 1.0f, 0.3f, 1.0f }; // Yellow highlight

    auto submitMesh = [&](const GPUMesh& mesh, const Mat4& world, const Vec4& color)
    {
        if (!mesh.VertexBuffer || !mesh.IndexBuffer || mesh.VertexCount == 0 || mesh.IndexCount == 0) return;

        VertexBufferView vbView;
        vbView.BufferLocation = 0;
        vbView.SizeInBytes    = mesh.VertexCount * sizeof(Vertex);
        vbView.StrideInBytes  = sizeof(Vertex);
        RHIBuffer* vbPtr = mesh.VertexBuffer.get();
        ctx->SetVertexBuffers(0, &vbPtr, &vbView, 1);

        IndexBufferView ibView;
        ibView.BufferLocation = 0;
        ibView.SizeInBytes    = mesh.IndexCount * sizeof(uint32_t);
        ibView.Format         = EFormat::R32_UINT;
        ctx->SetIndexBuffer(mesh.IndexBuffer.get(), &ibView);

        PrimitiveUniformBuffer oub = {};
        memcpy(oub.WorldMatrix, world.m, sizeof(world.m));
        oub.ObjectColor[0] = color.x;
        oub.ObjectColor[1] = color.y;
        oub.ObjectColor[2] = color.z;
        oub.ObjectColor[3] = color.w;
        oub.Selected    = 2.0f; // Unlit/gizmo mode
        oub.ObjectPadding[0] = oub.ObjectPadding[1] = 0.0f;

        objectUB.UpdateUniformBufferImmediate(oub);
        ctx->SetConstantBuffer(1, objectUB.GetReference());
        ctx->DrawIndexed(mesh.IndexCount, 0, 0);
    };

    Mat4 scaleMat = Mat4::Scaling(gizmoScale, gizmoScale, gizmoScale);
    Mat4 transMat = Mat4::Translation(gizmoPos.x, gizmoPos.y, gizmoPos.z);
    Mat4 baseWorld = scaleMat * transMat; // scale then translate

    const GPUMesh* handles = State.Mode == EGizmoMode::Rotate ? RotateGPU
                           : State.Mode == EGizmoMode::Scale  ? ScaleGPU
                                                                : TranslateGPU;
    for (int i = 0; i < 3; i++)
        submitMesh(handles[i], baseWorld, colors[i]);

    // ---- Directional Light Direction Indicator ----
    if (State.bHasDirectionalLight)
    {
        Vec3 fwd = State.LightForward;
        Vec3 up = State.LightUp;
        Vec3 right = State.LightRight;

        Mat4 rotMat = Mat4::Identity();
        rotMat.m[0][0] = right.x; rotMat.m[0][1] = right.y; rotMat.m[0][2] = right.z;
        rotMat.m[1][0] = up.x;    rotMat.m[1][1] = up.y;    rotMat.m[1][2] = up.z;
        rotMat.m[2][0] = fwd.x;   rotMat.m[2][1] = fwd.y;   rotMat.m[2][2] = fwd.z;
        Mat4 worldDL = scaleMat * rotMat * transMat;

        submitMesh(DirLightIndicatorGPU, worldDL, { 1.0f, 0.9f, 0.2f, 1.0f });
    }
}

} // namespace Kiwi
