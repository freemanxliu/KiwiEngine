#pragma once

#include "Math/Math.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <variant>
#include <iostream>
#include <fstream>
#include <sstream>

namespace Kiwi
{

    // ============================================================
    // Shader Property Types (parsed from shader @Properties block)
    // ============================================================

    enum class EShaderPropertyType
    {
        Float,      // float value
        Range,      // float with min/max
        Color,      // Vec4 RGBA
        Texture2D,  // texture path string
    };

    struct ShaderPropertyDef
    {
        std::string Name;           // e.g. "_Roughness"
        std::string DisplayName;    // e.g. "Roughness"
        EShaderPropertyType Type;
        float RangeMin = 0.0f;      // for Range type
        float RangeMax = 1.0f;
        // Default value
        float DefaultFloat = 0.0f;
        Vec4  DefaultColor = { 1, 1, 1, 1 };
        std::string DefaultTexture; // "white", "black", "normal", or path
    };

    // ============================================================
    // Material Property Value (runtime)
    // ============================================================

    using MaterialPropertyValue = std::variant<float, Vec4, std::string>;

    // ============================================================
    // Material — independent asset referencing a Shader
    // ============================================================

    class Material
    {
    public:
        std::string Name;                    // Material name (also filename stem)
        std::string ShaderName = "DefaultLit"; // Which shader this material uses

        // Property values (keyed by property name like "_Roughness")
        std::unordered_map<std::string, MaterialPropertyValue> Properties;

        // ---- Convenience getters with defaults ----

        float GetFloat(const std::string& name, float fallback = 0.0f) const
        {
            auto it = Properties.find(name);
            if (it != Properties.end() && std::holds_alternative<float>(it->second))
                return std::get<float>(it->second);
            return fallback;
        }

        Vec4 GetColor(const std::string& name, Vec4 fallback = { 1, 1, 1, 1 }) const
        {
            auto it = Properties.find(name);
            if (it != Properties.end() && std::holds_alternative<Vec4>(it->second))
                return std::get<Vec4>(it->second);
            return fallback;
        }

        std::string GetTexture(const std::string& name, const std::string& fallback = "") const
        {
            auto it = Properties.find(name);
            if (it != Properties.end() && std::holds_alternative<std::string>(it->second))
                return std::get<std::string>(it->second);
            return fallback;
        }

        void SetFloat(const std::string& name, float value)
        {
            Properties[name] = value;
        }

        void SetColor(const std::string& name, const Vec4& value)
        {
            Properties[name] = value;
        }

        void SetTexture(const std::string& name, const std::string& path)
        {
            Properties[name] = path;
        }

        // ---- Serialization (JSON .mat file) ----

        bool SaveToFile(const std::string& filepath) const;
        bool LoadFromFile(const std::string& filepath);

        // ---- Create default material ----
        static Material CreateDefault();
    };

    // ============================================================
    // MaterialLibrary — manages all loaded materials
    // ============================================================

    class MaterialLibrary
    {
    public:
        MaterialLibrary() = default;

        // Initialize: create built-in default material, scan Materials/ folder
        void Initialize(const std::string& materialsDir);

        // Get a material by name. Returns nullptr if not found.
        Material* GetMaterial(const std::string& name);

        // Get or create: returns existing, or creates a new default material
        Material* GetOrCreateMaterial(const std::string& name);

        // Add a material (takes ownership)
        Material* AddMaterial(std::unique_ptr<Material> mat);

        // Get all material names
        std::vector<std::string> GetMaterialNames() const;

        // Save a specific material to its file
        bool SaveMaterial(const std::string& name);

        // Save all materials
        void SaveAll();

        // Get the materials directory
        const std::string& GetMaterialsDir() const { return m_MaterialsDir; }

    private:
        void ScanAndLoad(const std::string& dir);

        std::string m_MaterialsDir;
        std::unordered_map<std::string, std::unique_ptr<Material>> m_Materials;
    };

    // ============================================================
    // Shader Properties Parser
    // ============================================================

    // Parse @Properties block from shader source (comment-based metadata)
    std::vector<ShaderPropertyDef> ParseShaderProperties(const std::string& shaderSource);

} // namespace Kiwi
