#include "Renderer/RenderScene.h"
#include "Core/RenderingThread.h"
#include "Core/RendererUtils.h"
#include "Scene/SceneObject.h"
#include "Scene/MeshComponent.h"
#include "Scene/LightComponent.h"
#include "Scene/Material.h"

#include <algorithm>
#include <cstring>
#include <iostream>

namespace Kiwi
{

void RenderScene::Initialize(RHIDevice* InDevice)
{
    Device = InDevice;
    GPUScene.Initialize(InDevice);
}

void RenderScene::Release()
{
    GamePrimitives.clear();
    GamePendingAdds.clear();
    GameDirtyPrimitives.clear();
    GameLights.clear();
    GameDirtyLights.clear();

    Primitives.clear();
    PendingAdds.clear();
    PendingRemoves.clear();
    DirtyPrimitives.clear();
    SharedMeshes.clear();
    Lights.clear();
    bLightListDirty = false;
    NumLights = 0;
    NumDirectionalLights = 0;
    ShadowCastingLight = nullptr;
    GPUScene.Release();
    Device = nullptr;

    std::lock_guard<std::mutex> Lock(PublishedIdsMutex);
    PublishedIds.clear();
}

// ============================================================
// Game thread
// ============================================================

void RenderScene::AddPrimitive(MeshComponent* Primitive)
{
    if (!GamePrimitives.insert(Primitive).second)
        return;
    GamePendingAdds.push_back(Primitive);
}

void RenderScene::RemovePrimitive(MeshComponent* Primitive)
{
    if (!GamePrimitives.erase(Primitive))
        return;
    GameDirtyPrimitives.erase(Primitive);
    auto PendingIt = std::find(GamePendingAdds.begin(), GamePendingAdds.end(), Primitive);
    if (PendingIt != GamePendingAdds.end())
    {
        GamePendingAdds.erase(PendingIt);
        return;
    }
    // The component is destroyed right after this call; the render thread only needs its address.
    const MeshComponent* Key = Primitive;
    EnqueueRenderCommand([this, Key] { RemovePrimitive_RenderThread(Key); });
}

void RenderScene::UpdatePrimitiveTransform(MeshComponent* Primitive)
{
    MarkPrimitiveDirty(Primitive, DirtyTransform);
}

void RenderScene::UpdatePrimitiveSelectedState(MeshComponent* Primitive)
{
    MarkPrimitiveDirty(Primitive, DirtyState);
}

void RenderScene::UpdatePrimitiveMaterial(MeshComponent* Primitive)
{
    MarkPrimitiveDirty(Primitive, DirtyState);
}

void RenderScene::UpdatePrimitivesUsingMaterial(const std::string& MaterialName)
{
    for (MeshComponent* Primitive : GamePrimitives)
    {
        if (Primitive->Material.Parent == MaterialName)
            MarkPrimitiveDirty(Primitive, DirtyState);
    }
}

void RenderScene::MarkPrimitiveDirty(MeshComponent* Primitive, uint8_t Flags)
{
    // Pending adds send a full proxy anyway.
    if (!GamePrimitives.count(Primitive) || std::find(GamePendingAdds.begin(), GamePendingAdds.end(), Primitive) != GamePendingAdds.end())
        return;
    GameDirtyPrimitives[Primitive] |= Flags;
}

void RenderScene::AddLight(LightComponent* Light)
{
    if (GameLights.insert(Light).second)
        GameDirtyLights.insert(Light);
}

void RenderScene::RemoveLight(LightComponent* Light)
{
    if (!GameLights.erase(Light))
        return;
    GameDirtyLights.erase(Light);
    const LightComponent* Key = Light;
    EnqueueRenderCommand([this, Key] { RemoveLight_RenderThread(Key); });
}

void RenderScene::UpdateLightTransform(LightComponent* Light)
{
    if (GameLights.count(Light))
        GameDirtyLights.insert(Light);
}

void RenderScene::UpdateLightColorAndBrightness(LightComponent* Light)
{
    if (GameLights.count(Light))
        GameDirtyLights.insert(Light);
}

PrimitiveSceneProxy RenderScene::BuildPrimitiveProxy(const MeshComponent& Component, const MaterialLibrary& Materials, bool bIncludeMesh)
{
    PrimitiveSceneProxy Proxy;
    Proxy.LocalToWorld = Component.GetWorldMatrix();
    ComputeWorldAABB(Component, Proxy.WorldBounds.Min, Proxy.WorldBounds.Max);
    Proxy.bVisible = Component.Enabled;
    Proxy.bSelected = Component.Owner && Component.Owner->Selected;
    Proxy.SortOrder = Component.SortOrder;
    Proxy.CullMode = Component.CullMode;
    Proxy.PrimitiveType = Component.PrimitiveType;

    const MaterialInstance& Instance = Component.Material;
    const Material* Parent = Materials.GetMaterial(Instance.Parent);
    Proxy.MaterialName = Instance.Parent;
    if (Parent && !Parent->SurfaceShader.empty())
        Proxy.SurfaceShader = Parent->SurfaceShader;
    Proxy.Color = Instance.GetColor(Parent, "_Color", { 0.8f, 0.8f, 0.8f, 1.0f });
    Proxy.Emissive = Instance.GetColor(Parent, "_Emissive", { 0, 0, 0, 1 });
    Proxy.Roughness = Instance.GetFloat(Parent, "_Roughness", 0.5f);
    Proxy.Metallic = Instance.GetFloat(Parent, "_Metallic", 0.0f);
    Proxy.ShadingModel = Parent ? (float)(uint8_t)Parent->ShadingModel : 1.0f;
    Proxy.BaseColorTexture = Instance.GetTexture(Parent, "_BaseColorTex");
    Proxy.NormalTexture = Instance.GetTexture(Parent, "_NormalTex");
    Proxy.MetallicRoughnessTexture = Instance.GetTexture(Parent, "_MetallicRoughnessTex");

    if (bIncludeMesh)
        Proxy.MeshData = std::make_shared<const Mesh>(Component.MeshData);
    return Proxy;
}

LightSceneProxy RenderScene::BuildLightProxy(const LightComponent& Light)
{
    LightSceneProxy Proxy;
    GPULightData& Out = Proxy.Data;
    Out.ColorIntensity[0] = Light.LightColor.x * Light.Intensity;
    Out.ColorIntensity[1] = Light.LightColor.y * Light.Intensity;
    Out.ColorIntensity[2] = Light.LightColor.z * Light.Intensity;
    Proxy.bAffectsWorld = Light.Enabled && Light.AffectWorld;
    if (Light.GetLightType() == ELightType::Directional)
    {
        const auto& Directional = static_cast<const DirectionalLightComponent&>(Light);
        const Vec3 Forward = Light.GetForward();
        Out.Type = 0;
        Out.DirectionOrPos[0] = Forward.x;
        Out.DirectionOrPos[1] = Forward.y;
        Out.DirectionOrPos[2] = Forward.z;
        Out.Radius = 0.0f;

        Proxy.bCastShadow = Directional.CastShadow;
        DirectionalShadowSettings& Shadow = Proxy.Shadow;
        Shadow.Direction = Forward;
        Shadow.NumCascades = Directional.NumCascades;
        Shadow.ShadowMapResolution = Directional.ShadowMapResolution;
        Shadow.ShadowDistance = Directional.ShadowDistance;
        Shadow.CascadeSplitLambda = Directional.CascadeSplitLambda;
        Shadow.ShadowBias = Directional.ShadowBias;
        Shadow.NormalBias = Directional.NormalBias;
        Shadow.ShadowStrength = Directional.ShadowStrength;
    }
    else
    {
        Out.Type = 1;
        Out.DirectionOrPos[0] = Light.Position.x;
        Out.DirectionOrPos[1] = Light.Position.y;
        Out.DirectionOrPos[2] = Light.Position.z;
        const auto* Point = dynamic_cast<const PointLightComponent*>(&Light);
        Out.Radius = Point ? Point->Radius : 10.0f;
    }
    return Proxy;
}

void RenderScene::SendAllEndOfFrameUpdates(const MaterialLibrary& Materials)
{
    if (GamePendingAdds.empty() && GameDirtyPrimitives.empty() && GameDirtyLights.empty())
        return;

    std::vector<PrimitiveUpdate> PrimitiveUpdates;
    PrimitiveUpdates.reserve(GamePendingAdds.size() + GameDirtyPrimitives.size());
    for (MeshComponent* Primitive : GamePendingAdds)
    {
        PrimitiveUpdate Update;
        Update.Key = Primitive;
        Update.Proxy = BuildPrimitiveProxy(*Primitive, Materials, true);
        Update.bAdd = true;
        PrimitiveUpdates.push_back(std::move(Update));
    }
    for (const auto& [Primitive, Flags] : GameDirtyPrimitives)
    {
        PrimitiveUpdate Update;
        Update.Key = Primitive;
        Update.Proxy = BuildPrimitiveProxy(*Primitive, Materials, false);
        Update.DirtyFlags = Flags;
        PrimitiveUpdates.push_back(std::move(Update));
    }
    GamePendingAdds.clear();
    GameDirtyPrimitives.clear();

    std::vector<LightUpdate> LightUpdates;
    LightUpdates.reserve(GameDirtyLights.size());
    for (LightComponent* Light : GameDirtyLights)
        LightUpdates.push_back({ Light, BuildLightProxy(*Light) });
    GameDirtyLights.clear();

    auto Primitives = std::make_shared<std::vector<PrimitiveUpdate>>(std::move(PrimitiveUpdates));
    auto LightList = std::make_shared<std::vector<LightUpdate>>(std::move(LightUpdates));
    EnqueueRenderCommand([this, Primitives, LightList] { ApplyUpdates(*Primitives, *LightList); });
}

bool RenderScene::GetPrimitiveGPUIds(const MeshComponent* Component, uint32_t& OutPrimitiveId, uint32_t& OutInstanceId) const
{
    std::lock_guard<std::mutex> Lock(PublishedIdsMutex);
    auto It = PublishedIds.find(Component);
    if (It == PublishedIds.end())
        return false;
    OutPrimitiveId = It->second.first;
    OutInstanceId = It->second.second;
    return true;
}

// ============================================================
// Render thread
// ============================================================

void RenderScene::ApplyUpdates(std::vector<PrimitiveUpdate>& PrimitiveUpdates, std::vector<LightUpdate>& LightUpdates)
{
    for (PrimitiveUpdate& Update : PrimitiveUpdates)
    {
        if (Update.bAdd)
        {
            PendingAdds.push_back(std::move(Update));
            continue;
        }

        auto It = Primitives.find(Update.Key);
        if (It == Primitives.end())
        {
            // Still waiting for GPU Scene slots: refresh the queued proxy, keeping its geometry.
            for (PrimitiveUpdate& Pending : PendingAdds)
            {
                if (Pending.Key == Update.Key)
                {
                    Update.Proxy.MeshData = std::move(Pending.Proxy.MeshData);
                    Pending.Proxy = std::move(Update.Proxy);
                }
            }
            continue;
        }

        PrimitiveSceneInfo& Info = It->second;
        Update.Proxy.MeshData = std::move(Info.Proxy.MeshData);
        Info.Proxy = std::move(Update.Proxy);
        if (!Info.bPrimitiveDataDirty && !Info.bInstanceDataDirty)
            DirtyPrimitives.push_back(Update.Key);
        Info.bPrimitiveDataDirty |= (Update.DirtyFlags & DirtyState) != 0;
        Info.bInstanceDataDirty |= (Update.DirtyFlags & DirtyTransform) != 0;
    }

    for (LightUpdate& Update : LightUpdates)
    {
        auto It = std::find_if(Lights.begin(), Lights.end(), [&](const LightSceneInfo& Info) { return Info.Key == Update.Key; });
        if (It != Lights.end())
            It->Proxy = Update.Proxy;
        else
            Lights.push_back({ Update.Key, Update.Proxy });
        bLightListDirty = true;
    }
}

void RenderScene::RemovePrimitive_RenderThread(const MeshComponent* Key)
{
    auto PendingIt = std::find_if(PendingAdds.begin(), PendingAdds.end(), [Key](const PrimitiveUpdate& Pending) { return Pending.Key == Key; });
    if (PendingIt != PendingAdds.end())
    {
        PendingAdds.erase(PendingIt);
        return;
    }

    auto It = Primitives.find(Key);
    if (It == Primitives.end())
        return;
    PendingRemoves.push_back({ It->second.PrimitiveId, It->second.InstanceId });
    Primitives.erase(It);
    bGPUIdsChanged = true;
}

void RenderScene::RemoveLight_RenderThread(const LightComponent* Key)
{
    auto It = std::find_if(Lights.begin(), Lights.end(), [Key](const LightSceneInfo& Info) { return Info.Key == Key; });
    if (It == Lights.end())
        return;
    Lights.erase(It);
    bLightListDirty = true;
}

SharedMeshEntry RenderScene::GetSharedMesh(EPrimitiveType Type) const
{
    auto It = SharedMeshes.find((uint32_t)Type);
    if (It == SharedMeshes.end())
        return {};
    SharedMeshEntry Entry;
    Entry.VertexBuffer = It->second.VertexBuffer.get();
    Entry.IndexBuffer = It->second.IndexBuffer.get();
    Entry.VertexCount = It->second.VertexCount;
    Entry.IndexCount = It->second.IndexCount;
    Entry.MeshID = (uint32_t)Type;
    return Entry;
}

void RenderScene::EnsureSharedMesh(const PrimitiveSceneProxy& Proxy)
{
    const uint32_t Type = (uint32_t)Proxy.PrimitiveType;
    if (SharedMeshes.count(Type) || !Proxy.MeshData || !Device)
        return;

    GPUMeshData& Gpu = SharedMeshes[Type];
    Gpu.VertexCount = Proxy.MeshData->GetVertexCount();
    Gpu.IndexCount = Proxy.MeshData->GetIndexCount();
    if (Gpu.VertexCount == 0 || Gpu.IndexCount == 0)
        return;

    const std::string TypeName = std::to_string(Type);
    const std::string VbName = "SharedVB_Type" + TypeName;
    const std::string IbName = "SharedIB_Type" + TypeName;

    BufferDesc VbDesc;
    VbDesc.SizeInBytes = Gpu.VertexCount * sizeof(Vertex);
    VbDesc.BindFlags = BUFFER_USAGE_VERTEX;
    VbDesc.Usage = EResourceUsage::Immutable;
    VbDesc.DebugName = VbName.c_str();
    Gpu.VertexBuffer = Device->CreateBuffer(VbDesc, Proxy.MeshData->GetVertices().data());

    BufferDesc IbDesc;
    IbDesc.SizeInBytes = Gpu.IndexCount * sizeof(uint32_t);
    IbDesc.BindFlags = BUFFER_USAGE_INDEX;
    IbDesc.Usage = EResourceUsage::Immutable;
    IbDesc.DebugName = IbName.c_str();
    Gpu.IndexBuffer = Device->CreateBuffer(IbDesc, Proxy.MeshData->GetIndices().data());

    std::cout << "[Kiwi] Shared Mesh Pool: created mesh for primitive type " << Type << std::endl;
}

void RenderScene::UpdateLights()
{
    if (!bLightListDirty)
        return;
    bLightListDirty = false;

    NumLights = 0;
    ShadowCastingLight = nullptr;
    memset(LightData, 0, sizeof(LightData));
    for (const LightSceneInfo& Info : Lights)
    {
        const LightSceneProxy& Proxy = Info.Proxy;
        if (!Proxy.bAffectsWorld)
            continue;
        if (!ShadowCastingLight && Proxy.Data.Type == 0 && Proxy.bCastShadow)
            ShadowCastingLight = &Proxy.Shadow;
        if (NumLights < MAX_LIGHTS)
            LightData[NumLights++] = Proxy.Data;
    }

    // Deferred lighting draws directional and point lights as two instanced ranges of g_Lights.
    GPULightData* DirectionalEnd = std::stable_partition(LightData, LightData + NumLights, [](const GPULightData& Light) { return Light.Type == 0; });
    NumDirectionalLights = (int32_t)(DirectionalEnd - LightData);
}

void RenderScene::Update()
{
    UpdatePrimitiveSceneInfos();
    UpdateLights();
    GPUScene.Upload();
    PublishGPUIds();
}

void RenderScene::UpdatePrimitiveSceneInfos()
{
    // Free first so this frame's adds can reuse the slots.
    for (const PendingRemove& Remove : PendingRemoves)
    {
        GPUScene.FreePrimitive(Remove.PrimitiveId);
        GPUScene.FreeInstance(Remove.InstanceId);
    }
    PendingRemoves.clear();

    // Adds that find the tables full stay pending and retry next frame.
    std::vector<PrimitiveUpdate> RetryAdds;
    for (PrimitiveUpdate& Add : PendingAdds)
    {
        PrimitiveSceneInfo Info;
        Info.Key = Add.Key;
        Info.PrimitiveId = GPUScene.AllocatePrimitive();
        Info.InstanceId = GPUScene.AllocateInstance();
        if (!Info.HasGPUSlots())
        {
            GPUScene.FreePrimitive(Info.PrimitiveId);
            GPUScene.FreeInstance(Info.InstanceId);
            RetryAdds.push_back(std::move(Add));
            continue;
        }
        EnsureSharedMesh(Add.Proxy);
        Info.Proxy = std::move(Add.Proxy);
        UploadPrimitiveData(Info);
        UploadInstanceData(Info);
        Primitives[Info.Key] = std::move(Info);
        bGPUIdsChanged = true;
    }
    PendingAdds.swap(RetryAdds);

    // A dirty entry may name a primitive that was removed since; it is skipped.
    for (const MeshComponent* Key : DirtyPrimitives)
    {
        auto It = Primitives.find(Key);
        if (It == Primitives.end())
            continue;
        PrimitiveSceneInfo& Info = It->second;
        if (Info.bPrimitiveDataDirty)
            UploadPrimitiveData(Info);
        if (Info.bInstanceDataDirty)
            UploadInstanceData(Info);
        Info.bPrimitiveDataDirty = false;
        Info.bInstanceDataDirty = false;
    }
    DirtyPrimitives.clear();
}

void RenderScene::UploadPrimitiveData(const PrimitiveSceneInfo& Info)
{
    const PrimitiveSceneProxy& Proxy = Info.Proxy;
    PrimitiveSceneData Primitive = {};
    Primitive.ObjectColor[0] = Proxy.Color.x;
    Primitive.ObjectColor[1] = Proxy.Color.y;
    Primitive.ObjectColor[2] = Proxy.Color.z;
    Primitive.ObjectColor[3] = Proxy.Color.w;
    Primitive.Material0[0] = Proxy.bSelected ? 1.0f : 0.0f;
    Primitive.Material0[1] = Proxy.Roughness;
    Primitive.Material0[2] = Proxy.Metallic;
    Primitive.Material0[3] = Proxy.BaseColorTexture.empty() ? 0.0f : 1.0f;
    Primitive.Material1[0] = Proxy.NormalTexture.empty() ? 0.0f : 1.0f;
    Primitive.Material1[1] = Proxy.ShadingModel;
    Primitive.Material1[2] = Proxy.Emissive.x;
    Primitive.Material1[3] = Proxy.Emissive.y;
    Primitive.Material2[0] = Proxy.Emissive.z;
    Primitive.InstanceSceneDataOffset = Info.InstanceId;
    Primitive.NumInstances = 1;
    GPUScene.UpdatePrimitive(Info.PrimitiveId, Primitive);
}

void RenderScene::UploadInstanceData(const PrimitiveSceneInfo& Info)
{
    InstanceSceneData Instance = {};
    memcpy(Instance.WorldMatrix, Info.Proxy.LocalToWorld.m, sizeof(Info.Proxy.LocalToWorld.m));
    Instance.PrimitiveId = Info.PrimitiveId;
    GPUScene.UpdateInstance(Info.InstanceId, Instance);
}

void RenderScene::PublishGPUIds()
{
    if (!bGPUIdsChanged)
        return;
    bGPUIdsChanged = false;
    std::lock_guard<std::mutex> Lock(PublishedIdsMutex);
    PublishedIds.clear();
    for (const auto& [Key, Info] : Primitives)
        PublishedIds[Key] = { Info.PrimitiveId, Info.InstanceId };
}

} // namespace Kiwi
