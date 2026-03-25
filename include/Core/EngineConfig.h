#pragma once

// EngineConfig - Singleton configuration system for KiwiEngine
// Reads settings from INI-style config files (similar to UE's DefaultEngine.ini)
//
// INI Format:
//   [SectionName]
//   Key=Value
//   ; This is a comment
//   # This is also a comment
//
// Usage:
//   auto& config = EngineConfig::Get();
//   config.Load("Config/DefaultEngine.ini");
//   std::string val = config.GetString("RenderDoc", "DllPath", "");
//   int num = config.GetInt("Rendering", "MaxFPS", 60);
//   bool flag = config.GetBool("Debug", "EnableValidation", true);
//   float f = config.GetFloat("Camera", "FOV", 45.0f);

#include <string>
#include <unordered_map>
#include <vector>

namespace Kiwi
{

    class EngineConfig
    {
    public:
        // Singleton access
        static EngineConfig& Get();

        // Load a config file. Can be called multiple times to layer configs.
        // Later loads override earlier values for the same section/key.
        // Returns true if the file was found and parsed successfully.
        bool Load(const std::string& filePath);

        // Load the default config file (searches relative to exe, then source dir)
        // Automatically called on first access if not loaded manually.
        bool LoadDefaultConfig();

        // Check if any config has been loaded
        bool IsLoaded() const { return m_Loaded; }

        // ---- Getters ----
        // All getters return the defaultValue if the key is not found.

        std::string GetString(const std::string& section, const std::string& key,
                              const std::string& defaultValue = "") const;

        int GetInt(const std::string& section, const std::string& key,
                   int defaultValue = 0) const;

        float GetFloat(const std::string& section, const std::string& key,
                       float defaultValue = 0.0f) const;

        bool GetBool(const std::string& section, const std::string& key,
                     bool defaultValue = false) const;

        // ---- Setters (runtime only, does not write to disk) ----

        void SetString(const std::string& section, const std::string& key,
                       const std::string& value);

        void SetInt(const std::string& section, const std::string& key, int value);

        void SetFloat(const std::string& section, const std::string& key, float value);

        void SetBool(const std::string& section, const std::string& key, bool value);

        // ---- Query ----

        // Check if a section exists
        bool HasSection(const std::string& section) const;

        // Check if a key exists in a section
        bool HasKey(const std::string& section, const std::string& key) const;

        // Get all keys in a section
        std::vector<std::string> GetKeys(const std::string& section) const;

        // Get all section names
        std::vector<std::string> GetSections() const;

        // ---- Save ----

        // Save current config to a file (INI format)
        bool Save(const std::string& filePath) const;

        // ---- Debug ----

        // Dump all config to stdout
        void DumpToConsole() const;

    private:
        EngineConfig() = default;
        ~EngineConfig() = default;
        EngineConfig(const EngineConfig&) = delete;
        EngineConfig& operator=(const EngineConfig&) = delete;

        // Internal: make a lookup key from section + key
        static std::string MakeKey(const std::string& section, const std::string& key);

        // Internal: trim whitespace from both ends of a string
        static std::string Trim(const std::string& str);

        // Section -> { Key -> Value }
        // We use an ordered structure to preserve section order for saving
        struct Section
        {
            std::string Name;
            std::vector<std::pair<std::string, std::string>> KeyValues; // ordered
            std::unordered_map<std::string, size_t> KeyIndex;           // fast lookup
        };

        std::vector<Section> m_Sections;
        std::unordered_map<std::string, size_t> m_SectionIndex; // name -> index in m_Sections

        bool m_Loaded = false;

        // Helper to find or create a section
        Section& FindOrCreateSection(const std::string& name);
        const Section* FindSection(const std::string& name) const;
    };

} // namespace Kiwi
