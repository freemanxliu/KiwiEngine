#pragma once

#include "Scene/Component.h"

namespace Kiwi
{

    // ============================================================
    // Light Type — identifies the type of a light
    // ============================================================
    enum class ELightType
    {
        Directional,
        Point,
    };

    // ============================================================
    // LightComponent — base class for all light components
    // Provides common light properties: Color, Intensity, AffectWorld
    // ============================================================
    class LightComponent : public Component
    {
    public:
        LightComponent() = default;
        ~LightComponent() override = default;

        LightComponent(LightComponent&&) = default;
        LightComponent& operator=(LightComponent&&) = default;

        EComponentType GetType() const override { return EComponentType::Light; }
        const char* GetTypeName() const override { return "LightComponent"; }

        // Light type (overridden by subclasses)
        virtual ELightType GetLightType() const = 0;
        virtual const char* GetLightTypeName() const = 0;

        // ---- Light Properties ----

        // Light color (RGB, each channel 0-1)
        Vec3 LightColor = { 1.0f, 1.0f, 1.0f };

        // Light intensity multiplier (0 = off, 1 = normal, can go higher for HDR)
        float Intensity = 1.0f;

        // Whether this light affects the world (when false, light is ignored during rendering)
        bool AffectWorld = true;
    };

    // ============================================================
    // DirectionalLightComponent — infinite distance, parallel rays
    // Direction is derived from the component's Rotation (forward vector)
    // ============================================================
    class DirectionalLightComponent : public LightComponent
    {
    public:
        DirectionalLightComponent() = default;
        ~DirectionalLightComponent() override = default;

        DirectionalLightComponent(DirectionalLightComponent&&) = default;
        DirectionalLightComponent& operator=(DirectionalLightComponent&&) = default;

        const char* GetTypeName() const override { return "DirectionalLightComponent"; }
        ELightType GetLightType() const override { return ELightType::Directional; }
        const char* GetLightTypeName() const override { return "Directional Light"; }

        // Direction is computed from Rotation via GetForward()
        // No additional properties needed — direction comes from transform
    };

    // ============================================================
    // PointLightComponent — emits light from a point in all directions
    // Position is the component's Position (from transform)
    // ============================================================
    class PointLightComponent : public LightComponent
    {
    public:
        PointLightComponent() = default;
        ~PointLightComponent() override = default;

        PointLightComponent(PointLightComponent&&) = default;
        PointLightComponent& operator=(PointLightComponent&&) = default;

        const char* GetTypeName() const override { return "PointLightComponent"; }
        ELightType GetLightType() const override { return ELightType::Point; }
        const char* GetLightTypeName() const override { return "Point Light"; }

        // ---- Point Light specific ----

        // Radius of influence — fragments beyond this distance receive no light
        float Radius = 10.0f;
    };

} // namespace Kiwi
