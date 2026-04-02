#pragma once

#include <cstdint>
#include <string>

namespace Kiwi
{

    // ============================================================
    // Shading Model (UE5-style)
    //
    // Determines how a surface is shaded in the deferred pipeline.
    // The ShadingModel ID is written to GBufferB.a during the
    // G-Buffer pass, then read by lighting passes to select
    // the appropriate BRDF / response.
    //
    // Material no longer references a shader by name — it only
    // declares its ShadingModel. The rendering pipeline picks
    // the correct shader for each pass (GBuffer, Shadow, Lighting).
    // ============================================================

    enum class EShadingModel : uint8_t
    {
        Unlit      = 0,   // No lighting — emissive color only
        DefaultLit = 1,   // Standard PBR (Metallic/Roughness workflow)
        // Future:
        // Subsurface   = 2,
        // ClearCoat    = 3,
        // TwoSidedFoliage = 4,

        Count
    };

    inline const char* ShadingModelToString(EShadingModel model)
    {
        switch (model)
        {
            case EShadingModel::Unlit:      return "Unlit";
            case EShadingModel::DefaultLit: return "DefaultLit";
            default:                        return "DefaultLit";
        }
    }

    inline EShadingModel StringToShadingModel(const std::string& str)
    {
        if (str == "Unlit")      return EShadingModel::Unlit;
        if (str == "DefaultLit") return EShadingModel::DefaultLit;
        // Legacy mapping: old ShaderName → ShadingModel
        if (str == "Default")    return EShadingModel::DefaultLit;
        if (str == "Wireframe")  return EShadingModel::DefaultLit;
        return EShadingModel::DefaultLit;
    }

} // namespace Kiwi
