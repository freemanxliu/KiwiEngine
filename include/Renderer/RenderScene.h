#pragma once

#include "Math/Math.h"
#include "RHI/RHITypes.h"
#include "Scene/GPUScene.h"
#include "Scene/Mesh.h"
#include "Scene/PrimitiveType.h"
#include "Scene/SceneInterface.h"
#include "Scene/Shaders.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Kiwi
{
    class PrimitiveComponent;
    class LightComponent;
    class MaterialLibrary;

    // Owning VB/IB of one primitive type in the shared mesh pool.
    struct GPUMeshData
    {
        std::unique_ptr<RHIBuffer> VertexBuffer;
        std::unique_ptr<RHIBuffer> IndexBuffer;
        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;
    };

    // Non-owning view of a shared mesh. Every primitive of the same EPrimitiveType uses the same one.
    struct SharedMeshEntry
    {
        RHIBuffer* VertexBuffer = nullptr;
        RHIBuffer* IndexBuffer = nullptr;
        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;
        uint32_t MeshID = 0; // EPrimitiveType, used for sorting and batching
    };

    // Everything the renderer needs from a mesh component, copied on the game thread (UE5 FPrimitiveSceneProxy).
    // The render thread never reads the component itself.
    struct PrimitiveSceneProxy
    {
        Mat4 LocalToWorld = Mat4::Identity();
        AABB WorldBounds;
        bool bVisible = true;
        bool bSelected = false;
        int32_t SortOrder = 0;
        ECullMode CullMode = ECullMode::Back;
        EPrimitiveType PrimitiveType = EPrimitiveType::Cube;

        // Material instance resolved against its parent asset.
        std::string MaterialName;
        std::string SurfaceShader = "DefaultSurface";
        Vec4 Color = { 0.8f, 0.8f, 0.8f, 1.0f };
        Vec4 Emissive = { 0.0f, 0.0f, 0.0f, 1.0f };
        float Roughness = 0.5f;
        float Metallic = 0.0f;
        float ShadingModel = 1.0f;
        std::string BaseColorTexture;
        std::string NormalTexture;
        std::string MetallicRoughnessTexture;

        // Geometry for the shared mesh pool and the CPU ray tracer. Only sent when the primitive is added.
        std::shared_ptr<const Mesh> MeshData;
    };

    // Shadow settings of a directional light, copied on the game thread.
    struct DirectionalShadowSettings
    {
        Vec3 Direction = { 0.0f, -1.0f, 0.0f };
        int NumCascades = 4;
        int ShadowMapResolution = 2048;
        float ShadowDistance = 50.0f;
        float CascadeSplitLambda = 0.75f;
        float ShadowBias = 0.005f;
        float NormalBias = 0.02f;
        float ShadowStrength = 1.0f;
    };

    // Everything the renderer needs from a light component (UE5 FLightSceneProxy).
    struct LightSceneProxy
    {
        GPULightData Data = {};
        bool bAffectsWorld = true; // enabled and AffectWorld
        bool bCastShadow = false;  // directional lights only
        DirectionalShadowSettings Shadow;
    };

    // Renderer-side record of one mesh component (UE5 FPrimitiveSceneInfo).
    struct PrimitiveSceneInfo
    {
        const PrimitiveComponent* Key = nullptr; // identity only, never dereferenced on the render thread
        PrimitiveSceneProxy Proxy;
        uint32_t PrimitiveId = GPUScene::InvalidId;
        uint32_t InstanceId = GPUScene::InvalidId;
        bool bPrimitiveDataDirty = false;
        bool bInstanceDataDirty = false;

        bool HasGPUSlots() const { return PrimitiveId != GPUScene::InvalidId && InstanceId != GPUScene::InvalidId; }
    };

    // Renderer-side record of one light component (UE5 FLightSceneInfo).
    struct LightSceneInfo
    {
        const LightComponent* Key = nullptr;
        LightSceneProxy Proxy;
    };

    // Renderer's copy of the game scene (UE5 FScene): primitives, lights, the shared mesh pool and the GPU Scene tables.
    //
    // Game thread: the SceneInterface calls only record which components changed. SendAllEndOfFrameUpdates()
    // snapshots those components into proxies and enqueues one render command with all of them.
    // Render thread: the proxies are queued, then Update() applies them once per frame and uploads GPU Scene.
    class RenderScene : public SceneInterface
    {
    public:
        using PrimitiveMap = std::unordered_map<const PrimitiveComponent*, PrimitiveSceneInfo>;

        // Game thread, with the rendering threads flushed.
        void Initialize(RHIDevice* InDevice);
        void Release();

        // ---- Game thread ----
        void AddPrimitive(PrimitiveComponent* Primitive) override;
        void RemovePrimitive(PrimitiveComponent* Primitive) override;
        void UpdatePrimitiveTransform(PrimitiveComponent* Primitive) override;
        void UpdatePrimitiveSelectedState(PrimitiveComponent* Primitive) override;
        void UpdatePrimitiveMaterial(PrimitiveComponent* Primitive) override;

        void AddLight(LightComponent* Light) override;
        void RemoveLight(LightComponent* Light) override;
        void UpdateLightTransform(LightComponent* Light) override;
        void UpdateLightColorAndBrightness(LightComponent* Light) override;

        // A material asset changed; every primitive using it resends its material.
        void UpdatePrimitivesUsingMaterial(const std::string& MaterialName);

        // End of the game frame (UE5 SendAllEndOfFrameUpdates): builds proxies for every added or dirty component.
        void SendAllEndOfFrameUpdates(const MaterialLibrary& Materials);

        // GPU Scene slots last published by the render thread, for the details panel.
        bool GetPrimitiveGPUIds(const PrimitiveComponent* Component, uint32_t& OutPrimitiveId, uint32_t& OutInstanceId) const;

        // ---- Render thread ----
        // Once per frame (UE5 FScene::Update): applies primitive removes and adds, refreshes dirty
        // primitive data, rebuilds the light list if it changed, then uploads GPU Scene.
        void Update();

        const PrimitiveMap& GetPrimitives() const { return Primitives; }
        uint32_t GetNumPrimitives() const { return (uint32_t)Primitives.size(); }
        SharedMeshEntry GetSharedMesh(EPrimitiveType Type) const;

        // Lights that affect the world, directional lights first.
        const GPULightData* GetLightData() const { return LightData; }
        int32_t GetNumLights() const { return NumLights; }
        int32_t GetNumDirectionalLights() const { return NumDirectionalLights; }

        // First directional light with CastShadow, or null.
        const DirectionalShadowSettings* GetShadowCastingLight() const { return ShadowCastingLight; }

        Kiwi::GPUScene& GetGPUScene() { return GPUScene; }
        const Kiwi::GPUScene& GetGPUScene() const { return GPUScene; }

    private:
        enum EDirtyFlags : uint8_t
        {
            DirtyTransform = 1 << 0,
            DirtyState = 1 << 1,
        };

        struct PrimitiveUpdate
        {
            const PrimitiveComponent* Key = nullptr;
            PrimitiveSceneProxy Proxy;
            bool bAdd = false;
            uint8_t DirtyFlags = 0;
        };

        struct LightUpdate
        {
            const LightComponent* Key = nullptr;
            LightSceneProxy Proxy;
        };

        struct PendingRemove
        {
            uint32_t PrimitiveId;
            uint32_t InstanceId;
        };

        // Game thread
        void MarkPrimitiveDirty(PrimitiveComponent* Primitive, uint8_t Flags);
        static PrimitiveSceneProxy BuildPrimitiveProxy(const PrimitiveComponent& Component, const MaterialLibrary& Materials, bool bIncludeMesh);
        static LightSceneProxy BuildLightProxy(const LightComponent& Light);

        // Render thread
        void ApplyUpdates(std::vector<PrimitiveUpdate>& PrimitiveUpdates, std::vector<LightUpdate>& LightUpdates);
        void RemovePrimitive_RenderThread(const PrimitiveComponent* Key);
        void RemoveLight_RenderThread(const LightComponent* Key);
        void UpdatePrimitiveSceneInfos();
        void EnsureSharedMesh(const PrimitiveSceneProxy& Proxy);
        void UploadPrimitiveData(const PrimitiveSceneInfo& Info);
        void UploadInstanceData(const PrimitiveSceneInfo& Info);
        void UpdateLights();
        void PublishGPUIds();

        // ---- Game thread state ----
        std::unordered_set<PrimitiveComponent*> GamePrimitives;
        std::vector<PrimitiveComponent*> GamePendingAdds;
        std::unordered_map<PrimitiveComponent*, uint8_t> GameDirtyPrimitives;
        std::unordered_set<LightComponent*> GameLights;
        std::unordered_set<LightComponent*> GameDirtyLights;

        // ---- Render thread state ----
        RHIDevice* Device = nullptr;
        Kiwi::GPUScene GPUScene;
        PrimitiveMap Primitives;
        std::vector<PrimitiveUpdate> PendingAdds;
        std::vector<PendingRemove> PendingRemoves;
        std::vector<const PrimitiveComponent*> DirtyPrimitives;
        std::unordered_map<uint32_t, GPUMeshData> SharedMeshes;
        bool bGPUIdsChanged = false;

        std::vector<LightSceneInfo> Lights;
        bool bLightListDirty = false;
        GPULightData LightData[MAX_LIGHTS] = {};
        int32_t NumLights = 0;
        int32_t NumDirectionalLights = 0;
        const DirectionalShadowSettings* ShadowCastingLight = nullptr;

        // ---- Shared ----
        mutable std::mutex PublishedIdsMutex;
        std::unordered_map<const PrimitiveComponent*, std::pair<uint32_t, uint32_t>> PublishedIds;
    };
}
