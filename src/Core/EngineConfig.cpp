#include "Core/EngineConfig.h"

#include <Windows.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <filesystem>

namespace Kiwi
{

    EngineConfig& EngineConfig::Get()
    {
        static EngineConfig instance;
        return instance;
    }

    bool EngineConfig::Load(const std::string& filePath)
    {
        std::ifstream file(filePath);
        if (!file.is_open())
        {
            std::cerr << "[Config] Failed to open config file: " << filePath << std::endl;
            return false;
        }

        std::cout << "[Config] Loading: " << filePath << std::endl;

        std::string line;
        std::string currentSection;

        while (std::getline(file, line))
        {
            line = Trim(line);

            // Skip empty lines and comments
            if (line.empty() || line[0] == ';' || line[0] == '#')
                continue;

            // Section header: [SectionName]
            if (line.front() == '[' && line.back() == ']')
            {
                currentSection = Trim(line.substr(1, line.size() - 2));
                continue;
            }

            // Key=Value pair
            size_t eqPos = line.find('=');
            if (eqPos != std::string::npos && !currentSection.empty())
            {
                std::string key = Trim(line.substr(0, eqPos));
                std::string value = Trim(line.substr(eqPos + 1));

                // Remove inline comments (but not if inside quotes)
                if (!value.empty() && value.front() != '"')
                {
                    size_t commentPos = value.find(';');
                    if (commentPos == std::string::npos)
                        commentPos = value.find('#');
                    if (commentPos != std::string::npos)
                        value = Trim(value.substr(0, commentPos));
                }

                // Remove surrounding quotes if present
                if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                {
                    value = value.substr(1, value.size() - 2);
                }

                if (!key.empty())
                {
                    SetString(currentSection, key, value);
                }
            }
        }

        m_Loaded = true;
        std::cout << "[Config] Loaded " << m_Sections.size() << " section(s) from " << filePath << std::endl;
        return true;
    }

    bool EngineConfig::LoadDefaultConfig()
    {
        if (m_Loaded)
            return true;

        // Get exe directory
        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string exeDir(exePath);
        size_t lastSlash = exeDir.find_last_of("\\/");
        if (lastSlash != std::string::npos)
            exeDir = exeDir.substr(0, lastSlash);

        namespace fs = std::filesystem;

        // Search order:
        // 1. Config/DefaultEngine.ini relative to exe
        // 2. ../../Config/DefaultEngine.ini (exe in build/bin/, source is ../../)
        // 3. DefaultEngine.ini relative to exe (flat layout)
        std::string searchPaths[] = {
            exeDir + "\\Config\\DefaultEngine.ini",
            exeDir + "\\..\\..\\Config\\DefaultEngine.ini",
            exeDir + "\\DefaultEngine.ini",
        };

        for (const auto& path : searchPaths)
        {
            if (fs::exists(path))
            {
                return Load(path);
            }
        }

        std::cout << "[Config] No DefaultEngine.ini found. Using defaults." << std::endl;
        return false;
    }

    // ---- Getters ----

    std::string EngineConfig::GetString(const std::string& section, const std::string& key,
                                        const std::string& defaultValue) const
    {
        const Section* sec = FindSection(section);
        if (!sec) return defaultValue;

        auto it = sec->KeyIndex.find(key);
        if (it == sec->KeyIndex.end()) return defaultValue;

        return sec->KeyValues[it->second].second;
    }

    int EngineConfig::GetInt(const std::string& section, const std::string& key,
                             int defaultValue) const
    {
        std::string val = GetString(section, key, "");
        if (val.empty()) return defaultValue;

        try
        {
            return std::stoi(val);
        }
        catch (...)
        {
            std::cerr << "[Config] Warning: Invalid int value for [" << section << "]."
                      << key << " = \"" << val << "\"" << std::endl;
            return defaultValue;
        }
    }

    float EngineConfig::GetFloat(const std::string& section, const std::string& key,
                                 float defaultValue) const
    {
        std::string val = GetString(section, key, "");
        if (val.empty()) return defaultValue;

        try
        {
            return std::stof(val);
        }
        catch (...)
        {
            std::cerr << "[Config] Warning: Invalid float value for [" << section << "]."
                      << key << " = \"" << val << "\"" << std::endl;
            return defaultValue;
        }
    }

    bool EngineConfig::GetBool(const std::string& section, const std::string& key,
                               bool defaultValue) const
    {
        std::string val = GetString(section, key, "");
        if (val.empty()) return defaultValue;

        // Convert to lowercase for comparison
        std::string lower = val;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (lower == "true" || lower == "1" || lower == "yes" || lower == "on")
            return true;
        if (lower == "false" || lower == "0" || lower == "no" || lower == "off")
            return false;

        std::cerr << "[Config] Warning: Invalid bool value for [" << section << "]."
                  << key << " = \"" << val << "\"" << std::endl;
        return defaultValue;
    }

    // ---- Setters ----

    void EngineConfig::SetString(const std::string& section, const std::string& key,
                                 const std::string& value)
    {
        Section& sec = FindOrCreateSection(section);

        auto it = sec.KeyIndex.find(key);
        if (it != sec.KeyIndex.end())
        {
            // Update existing key
            sec.KeyValues[it->second].second = value;
        }
        else
        {
            // Add new key
            sec.KeyIndex[key] = sec.KeyValues.size();
            sec.KeyValues.push_back({ key, value });
        }
    }

    void EngineConfig::SetInt(const std::string& section, const std::string& key, int value)
    {
        SetString(section, key, std::to_string(value));
    }

    void EngineConfig::SetFloat(const std::string& section, const std::string& key, float value)
    {
        SetString(section, key, std::to_string(value));
    }

    void EngineConfig::SetBool(const std::string& section, const std::string& key, bool value)
    {
        SetString(section, key, value ? "true" : "false");
    }

    // ---- Query ----

    bool EngineConfig::HasSection(const std::string& section) const
    {
        return FindSection(section) != nullptr;
    }

    bool EngineConfig::HasKey(const std::string& section, const std::string& key) const
    {
        const Section* sec = FindSection(section);
        if (!sec) return false;
        return sec->KeyIndex.find(key) != sec->KeyIndex.end();
    }

    std::vector<std::string> EngineConfig::GetKeys(const std::string& section) const
    {
        std::vector<std::string> result;
        const Section* sec = FindSection(section);
        if (sec)
        {
            for (const auto& kv : sec->KeyValues)
                result.push_back(kv.first);
        }
        return result;
    }

    std::vector<std::string> EngineConfig::GetSections() const
    {
        std::vector<std::string> result;
        for (const auto& sec : m_Sections)
            result.push_back(sec.Name);
        return result;
    }

    // ---- Save ----

    bool EngineConfig::Save(const std::string& filePath) const
    {
        std::ofstream file(filePath);
        if (!file.is_open())
        {
            std::cerr << "[Config] Failed to save config to: " << filePath << std::endl;
            return false;
        }

        for (size_t i = 0; i < m_Sections.size(); i++)
        {
            const auto& sec = m_Sections[i];
            if (i > 0) file << "\n";
            file << "[" << sec.Name << "]\n";
            for (const auto& kv : sec.KeyValues)
            {
                file << kv.first << "=" << kv.second << "\n";
            }
        }

        std::cout << "[Config] Saved config to: " << filePath << std::endl;
        return true;
    }

    // ---- Debug ----

    void EngineConfig::DumpToConsole() const
    {
        std::cout << "[Config] === Configuration Dump ===" << std::endl;
        for (const auto& sec : m_Sections)
        {
            std::cout << "[" << sec.Name << "]" << std::endl;
            for (const auto& kv : sec.KeyValues)
            {
                std::cout << "  " << kv.first << " = " << kv.second << std::endl;
            }
        }
        std::cout << "[Config] === End Dump ===" << std::endl;
    }

    // ---- Internal helpers ----

    std::string EngineConfig::MakeKey(const std::string& section, const std::string& key)
    {
        return section + "." + key;
    }

    std::string EngineConfig::Trim(const std::string& str)
    {
        size_t start = str.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = str.find_last_not_of(" \t\r\n");
        return str.substr(start, end - start + 1);
    }

    EngineConfig::Section& EngineConfig::FindOrCreateSection(const std::string& name)
    {
        auto it = m_SectionIndex.find(name);
        if (it != m_SectionIndex.end())
            return m_Sections[it->second];

        // Create new section
        m_SectionIndex[name] = m_Sections.size();
        m_Sections.push_back({ name, {}, {} });
        return m_Sections.back();
    }

    const EngineConfig::Section* EngineConfig::FindSection(const std::string& name) const
    {
        auto it = m_SectionIndex.find(name);
        if (it == m_SectionIndex.end()) return nullptr;
        return &m_Sections[it->second];
    }

} // namespace Kiwi
