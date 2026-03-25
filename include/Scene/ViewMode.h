#pragma once

namespace Kiwi
{

    // ============================================================
    // View Mode — Controls how the scene is rendered
    // ============================================================

    enum class EViewMode
    {
        Lit,           // Default: Full deferred rendering with lighting
        Unlit,         // No lighting — pure albedo color
        BaseColor,     // Buffer Visualization: G-Buffer Albedo
        Roughness,     // Buffer Visualization: G-Buffer Roughness
        Metallic,      // Buffer Visualization: G-Buffer Metallic
    };

    inline const char* GetViewModeName(EViewMode mode)
    {
        switch (mode)
        {
        case EViewMode::Lit:       return "Lit";
        case EViewMode::Unlit:     return "Unlit";
        case EViewMode::BaseColor: return "BaseColor";
        case EViewMode::Roughness: return "Roughness";
        case EViewMode::Metallic:  return "Metallic";
        default:                   return "Unknown";
        }
    }

} // namespace Kiwi
