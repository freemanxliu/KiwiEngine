#pragma once

#include "Math/Math.h"
#include "Scene/Component.h"
#include "Scene/MeshComponent.h"
#include "Scene/CameraComponent.h"
#include "Scene/LightComponent.h"
#include "Scene/PostProcessComponent.h"
#include <string>
#include <cstdint>
#include <vector>
#include <memory>
#include <algorithm>

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

    // ============================================================
    // SceneObject — entity in the scene, holds components
    // ============================================================
    struct SceneObject
    {
        uint32_t    ID = 0;
        std::string Name;

        // ---- Component storage ----
        std::vector<std::unique_ptr<Component>> Components;

        // Runtime state (not serialized)
        bool Selected = false;

        // ---- Component management ----

        // Add a component (takes ownership). Returns raw pointer for immediate use.
        template <typename T, typename... Args>
        T* AddComponent(Args&&... args)
        {
            auto comp = std::make_unique<T>(std::forward<Args>(args)...);
            comp->Owner = this;
            T* ptr = comp.get();
            Components.push_back(std::move(comp));
            return ptr;
        }

        // Get first component of given type. Returns nullptr if not found.
        template <typename T>
        T* GetComponent() const
        {
            for (auto& comp : Components)
            {
                T* casted = dynamic_cast<T*>(comp.get());
                if (casted) return casted;
            }
            return nullptr;
        }

        // Get all components of given type
        template <typename T>
        std::vector<T*> GetComponents() const
        {
            std::vector<T*> result;
            for (auto& comp : Components)
            {
                T* casted = dynamic_cast<T*>(comp.get());
                if (casted) result.push_back(casted);
            }
            return result;
        }

        // Remove first component of given type
        template <typename T>
        bool RemoveComponent()
        {
            for (auto it = Components.begin(); it != Components.end(); ++it)
            {
                if (dynamic_cast<T*>(it->get()))
                {
                    Components.erase(it);
                    return true;
                }
            }
            return false;
        }

        // Check if object has a component of given type
        template <typename T>
        bool HasComponent() const
        {
            return GetComponent<T>() != nullptr;
        }

        // ---- Convenience accessors ----
        // These provide backward-compatible access to commonly used components

        // Get the "primary" transform: first component's transform (usually MeshComponent or CameraComponent)
        // Every SceneObject should have at least one component.
        Component* GetPrimaryComponent() const
        {
            return Components.empty() ? nullptr : Components[0].get();
        }

        // Convenience: get position/rotation/scale from primary component
        Vec3& GetPosition()
        {
            static Vec3 dummy;
            auto* comp = GetPrimaryComponent();
            return comp ? comp->Position : dummy;
        }
        Vec3& GetRotation()
        {
            static Vec3 dummy;
            auto* comp = GetPrimaryComponent();
            return comp ? comp->Rotation : dummy;
        }
        Vec3& GetScale()
        {
            static Vec3 dummy = { 1, 1, 1 };
            auto* comp = GetPrimaryComponent();
            return comp ? comp->Scale : dummy;
        }

        Mat4 GetWorldMatrix() const
        {
            auto* comp = GetPrimaryComponent();
            return comp ? comp->GetWorldMatrix() : Mat4::Identity();
        }
    };

} // namespace Kiwi
