#pragma once

namespace Kiwi
{

    // ============================================================
    // View Mode — Controls how the scene is rendered
    // ============================================================

    enum class EViewMode
    {
        Lit,           // Full lighting
        Unlit,         // No lighting — pure albedo color
        BaseColor,     // Buffer Visualization: G-Buffer Albedo
        Roughness,     // Buffer Visualization: G-Buffer Roughness
        Metallic,      // Buffer Visualization: G-Buffer Metallic
    };

    enum class ERenderPath
    {
        Deferred,
        Forward,
        RayTracing,
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

    inline const char* GetRenderPathName(ERenderPath path)
    {
        switch (path)
        {
        case ERenderPath::Deferred:   return "Deferred";
        case ERenderPath::Forward:    return "Forward";
        case ERenderPath::RayTracing: return "Ray Tracing";
        default:                      return "Unknown";
        }
    }

    inline bool IsBufferVisualization(EViewMode mode)
    {
        return mode == EViewMode::BaseColor
            || mode == EViewMode::Roughness
            || mode == EViewMode::Metallic;
    }

} // namespace Kiwi
