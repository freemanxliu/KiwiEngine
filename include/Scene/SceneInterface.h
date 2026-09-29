#pragma once

namespace Kiwi
{
    class MeshComponent;
    class LightComponent;

    // Renderer-side scene as seen from the game scene (UE5 FSceneInterface).
    // Calls are queued and take effect in the renderer's next update.
    class SceneInterface
    {
    public:
        virtual ~SceneInterface() = default;

        virtual void AddPrimitive(MeshComponent* Primitive) = 0;
        virtual void RemovePrimitive(MeshComponent* Primitive) = 0;
        virtual void UpdatePrimitiveTransform(MeshComponent* Primitive) = 0;
        virtual void UpdatePrimitiveSelectedState(MeshComponent* Primitive) = 0;
        virtual void UpdatePrimitiveMaterial(MeshComponent* Primitive) = 0;

        virtual void AddLight(LightComponent* Light) = 0;
        virtual void RemoveLight(LightComponent* Light) = 0;
        virtual void UpdateLightTransform(LightComponent* Light) = 0;
        // Color, intensity, radius, enable flags and shadow casting.
        virtual void UpdateLightColorAndBrightness(LightComponent* Light) = 0;
    };
}
