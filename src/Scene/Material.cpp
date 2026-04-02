#include "Scene/Material.h"
#include <filesystem>
#include <algorithm>
#include <regex>

namespace Kiwi
{

    // ============================================================
    // Material Serialization (.mat JSON)
    // ============================================================

    bool Material::SaveToFile(const std::string& filepath) const
    {
        std::ofstream file(filepath);
        if (!file.is_open())
        {
            std::cerr << "[Kiwi] Material: Failed to save: " << filepath << std::endl;
            return false;
        }

        file << "{\n";
        file << "  \"name\": \"" << Name << "\",\n";
        file << "  \"shadingModel\": \"" << ShadingModelToString(ShadingModel) << "\",\n";
        file << "  \"properties\": {\n";

        size_t count = 0;
        for (const auto& [key, value] : Properties)
        {
            file << "    \"" << key << "\": ";

            if (std::holds_alternative<float>(value))
            {
                file << std::get<float>(value);
            }
            else if (std::holds_alternative<Vec4>(value))
            {
                const auto& v = std::get<Vec4>(value);
                file << "[" << v.x << ", " << v.y << ", " << v.z << ", " << v.w << "]";
            }
            else if (std::holds_alternative<std::string>(value))
            {
                file << "\"" << std::get<std::string>(value) << "\"";
            }

            count++;
            if (count < Properties.size()) file << ",";
            file << "\n";
        }

        file << "  }\n";
        file << "}\n";

        file.close();
        return true;
    }

    // Simple JSON value parser helpers
    static std::string ReadQuotedStr(const std::string& json, const std::string& key)
    {
        std::string search = "\"" + key + "\"";
        auto pos = json.find(search);
        if (pos == std::string::npos) return "";
        pos = json.find("\"", pos + search.size() + 1); // skip colon, find opening quote
        if (pos == std::string::npos) return "";
        pos++; // skip opening quote
        auto end = json.find("\"", pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    }

    static bool ReadFloatVal(const std::string& json, const std::string& key, float& out)
    {
        std::string search = "\"" + key + "\"";
        auto pos = json.find(search);
        if (pos == std::string::npos) return false;
        pos = json.find(":", pos);
        if (pos == std::string::npos) return false;
        pos++;
        // Skip whitespace
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        // Check if it's a string (quoted) or a number
        if (pos < json.size() && json[pos] == '"') return false; // it's a string, not float
        if (pos < json.size() && json[pos] == '[') return false; // it's an array
        try { out = std::stof(json.substr(pos)); return true; }
        catch (...) { return false; }
    }

    static bool ReadVec4Val(const std::string& json, const std::string& key, Vec4& out)
    {
        std::string search = "\"" + key + "\"";
        auto pos = json.find(search);
        if (pos == std::string::npos) return false;
        pos = json.find("[", pos);
        if (pos == std::string::npos) return false;
        pos++; // skip [
        auto end = json.find("]", pos);
        if (end == std::string::npos) return false;
        std::string arr = json.substr(pos, end - pos);
        float vals[4] = { 0, 0, 0, 1 };
        int idx = 0;
        std::stringstream ss(arr);
        std::string token;
        while (std::getline(ss, token, ',') && idx < 4)
        {
            try { vals[idx++] = std::stof(token); } catch (...) {}
        }
        out = { vals[0], vals[1], vals[2], vals[3] };
        return true;
    }

    bool Material::LoadFromFile(const std::string& filepath)
    {
        std::ifstream file(filepath);
        if (!file.is_open()) return false;

        std::stringstream ss;
        ss << file.rdbuf();
        std::string json = ss.str();
        file.close();

        Name = ReadQuotedStr(json, "name");

        // Support both new "shadingModel" and legacy "shader" field
        std::string smStr = ReadQuotedStr(json, "shadingModel");
        if (smStr.empty())
            smStr = ReadQuotedStr(json, "shader"); // Legacy .mat file compat
        ShadingModel = smStr.empty() ? EShadingModel::DefaultLit : StringToShadingModel(smStr);

        // Parse properties block
        auto propsPos = json.find("\"properties\"");
        if (propsPos != std::string::npos)
        {
            auto braceStart = json.find("{", propsPos + 12);
            if (braceStart != std::string::npos)
            {
                // Find matching closing brace
                int depth = 1;
                size_t braceEnd = braceStart + 1;
                while (braceEnd < json.size() && depth > 0)
                {
                    if (json[braceEnd] == '{') depth++;
                    else if (json[braceEnd] == '}') depth--;
                    braceEnd++;
                }
                std::string propsJson = json.substr(braceStart, braceEnd - braceStart);

                // Parse each property — look for "key": value patterns
                // Find all quoted keys
                size_t searchPos = 1; // skip opening brace
                while (searchPos < propsJson.size())
                {
                    auto keyStart = propsJson.find("\"", searchPos);
                    if (keyStart == std::string::npos) break;
                    keyStart++;
                    auto keyEnd = propsJson.find("\"", keyStart);
                    if (keyEnd == std::string::npos) break;
                    std::string key = propsJson.substr(keyStart, keyEnd - keyStart);

                    auto colonPos = propsJson.find(":", keyEnd);
                    if (colonPos == std::string::npos) break;

                    // Determine value type
                    size_t valStart = colonPos + 1;
                    while (valStart < propsJson.size() && (propsJson[valStart] == ' ' || propsJson[valStart] == '\t' || propsJson[valStart] == '\n' || propsJson[valStart] == '\r'))
                        valStart++;

                    if (valStart >= propsJson.size()) break;

                    if (propsJson[valStart] == '"')
                    {
                        // String value (texture path)
                        auto strEnd = propsJson.find("\"", valStart + 1);
                        if (strEnd != std::string::npos)
                        {
                            Properties[key] = propsJson.substr(valStart + 1, strEnd - valStart - 1);
                            searchPos = strEnd + 1;
                        }
                        else break;
                    }
                    else if (propsJson[valStart] == '[')
                    {
                        // Vec4 array
                        Vec4 v;
                        if (ReadVec4Val(propsJson, key, v))
                            Properties[key] = v;
                        searchPos = propsJson.find("]", valStart);
                        if (searchPos != std::string::npos) searchPos++;
                        else break;
                    }
                    else
                    {
                        // Float
                        float f = 0;
                        if (ReadFloatVal(propsJson, key, f))
                            Properties[key] = f;
                        // Skip to next comma or brace
                        searchPos = propsJson.find_first_of(",}", valStart);
                        if (searchPos != std::string::npos) searchPos++;
                        else break;
                    }
                }
            }
        }

        return true;
    }

    // ============================================================
    // Default Material
    // ============================================================

    Material Material::CreateDefault()
    {
        Material mat;
        mat.Name = "Default-Material";
        mat.ShadingModel = EShadingModel::DefaultLit;
        mat.SetColor("_Color", { 0.8f, 0.8f, 0.8f, 1.0f });
        mat.SetFloat("_Roughness", 0.5f);
        mat.SetFloat("_Metallic", 0.0f);
        mat.SetTexture("_BaseColorTex", "");
        mat.SetTexture("_NormalTex", "");
        mat.SetTexture("_MetallicRoughnessTex", "");
        return mat;
    }

    // ============================================================
    // MaterialLibrary
    // ============================================================

    void MaterialLibrary::Initialize(const std::string& materialsDir)
    {
        m_Materials.clear();
        m_MaterialsDir = materialsDir;

        // Create built-in default material
        auto defaultMat = std::make_unique<Material>(Material::CreateDefault());
        m_Materials["Default-Material"] = std::move(defaultMat);

        // Ensure directory exists
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(materialsDir, ec))
            fs::create_directories(materialsDir, ec);

        // Save default material if it doesn't exist on disk
        std::string defaultPath = materialsDir + "/Default-Material.mat";
        if (!fs::exists(defaultPath, ec))
        {
            m_Materials["Default-Material"]->SaveToFile(defaultPath);
            std::cout << "[Kiwi] MaterialLibrary: Created Default-Material.mat" << std::endl;
        }

        // Scan for existing .mat files
        ScanAndLoad(materialsDir);

        std::cout << "[Kiwi] MaterialLibrary: " << m_Materials.size() << " material(s) loaded." << std::endl;
    }

    void MaterialLibrary::ScanAndLoad(const std::string& dir)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(dir, ec)) return;

        for (const auto& entry : fs::directory_iterator(dir, ec))
        {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".mat") continue;

            std::string name = entry.path().stem().string();
            if (m_Materials.count(name)) continue; // already loaded (e.g. default)

            auto mat = std::make_unique<Material>();
            if (mat->LoadFromFile(entry.path().string()))
            {
                if (mat->Name.empty()) mat->Name = name;
                std::cout << "[Kiwi] MaterialLibrary: Loaded '" << name << "'" << std::endl;
                m_Materials[name] = std::move(mat);
            }
        }
    }

    Material* MaterialLibrary::GetMaterial(const std::string& name)
    {
        auto it = m_Materials.find(name);
        return (it != m_Materials.end()) ? it->second.get() : nullptr;
    }

    Material* MaterialLibrary::GetOrCreateMaterial(const std::string& name)
    {
        auto* existing = GetMaterial(name);
        if (existing) return existing;

        // Create new material based on default
        auto mat = std::make_unique<Material>(Material::CreateDefault());
        mat->Name = name;
        Material* ptr = mat.get();
        m_Materials[name] = std::move(mat);
        return ptr;
    }

    Material* MaterialLibrary::AddMaterial(std::unique_ptr<Material> mat)
    {
        std::string name = mat->Name;
        Material* ptr = mat.get();
        m_Materials[name] = std::move(mat);
        return ptr;
    }

    std::vector<std::string> MaterialLibrary::GetMaterialNames() const
    {
        std::vector<std::string> names;
        for (const auto& [key, val] : m_Materials)
            names.push_back(key);
        std::sort(names.begin(), names.end());
        return names;
    }

    bool MaterialLibrary::SaveMaterial(const std::string& name)
    {
        auto* mat = GetMaterial(name);
        if (!mat) return false;
        std::string path = m_MaterialsDir + "/" + name + ".mat";
        return mat->SaveToFile(path);
    }

    void MaterialLibrary::SaveAll()
    {
        for (const auto& [name, mat] : m_Materials)
            SaveMaterial(name);
    }

    // ============================================================
    // Shader Properties Parser
    // ============================================================

    std::vector<ShaderPropertyDef> ParseShaderProperties(const std::string& shaderSource)
    {
        std::vector<ShaderPropertyDef> props;

        // Find // @Properties { ... } block
        auto startPos = shaderSource.find("// @Properties {");
        if (startPos == std::string::npos)
            startPos = shaderSource.find("// @Properties{");
        if (startPos == std::string::npos) return props;

        auto endPos = shaderSource.find("// }", startPos);
        if (endPos == std::string::npos) return props;

        std::string block = shaderSource.substr(startPos, endPos - startPos);

        // Parse each line: //   _Name ("Display Name", Type) = default
        std::istringstream stream(block);
        std::string line;
        while (std::getline(stream, line))
        {
            // Skip header line
            if (line.find("@Properties") != std::string::npos) continue;

            // Remove leading "// " 
            auto contentPos = line.find("//");
            if (contentPos == std::string::npos) continue;
            std::string content = line.substr(contentPos + 2);

            // Trim whitespace
            auto first = content.find_first_not_of(" \t");
            if (first == std::string::npos) continue;
            content = content.substr(first);

            // Must start with _ (property name)
            if (content.empty() || content[0] != '_') continue;

            ShaderPropertyDef prop;

            // Parse: _Name ("Display", Type(args)) = default
            auto parenOpen = content.find('(');
            if (parenOpen == std::string::npos) continue;
            prop.Name = content.substr(0, content.find(' '));

            // Extract display name
            auto quoteStart = content.find('"', parenOpen);
            auto quoteEnd = content.find('"', quoteStart + 1);
            if (quoteStart != std::string::npos && quoteEnd != std::string::npos)
                prop.DisplayName = content.substr(quoteStart + 1, quoteEnd - quoteStart - 1);

            // Extract type
            auto commaAfterName = content.find(',', quoteEnd);
            if (commaAfterName == std::string::npos) continue;
            auto typeStart = commaAfterName + 1;
            while (typeStart < content.size() && content[typeStart] == ' ') typeStart++;
            auto parenClose = content.find(')', typeStart);
            if (parenClose == std::string::npos) continue;
            std::string typeStr = content.substr(typeStart, parenClose - typeStart);

            // Remove trailing whitespace from typeStr
            while (!typeStr.empty() && (typeStr.back() == ' ' || typeStr.back() == '\t'))
                typeStr.pop_back();

            if (typeStr == "Float")
            {
                prop.Type = EShaderPropertyType::Float;
            }
            else if (typeStr.substr(0, 5) == "Range")
            {
                prop.Type = EShaderPropertyType::Range;
                // Parse Range(min, max)
                auto rp = typeStr.find('(');
                if (rp != std::string::npos)
                {
                    auto rc = typeStr.find(',', rp);
                    auto re = typeStr.find(')', rc);
                    if (rc != std::string::npos && re != std::string::npos)
                    {
                        try {
                            prop.RangeMin = std::stof(typeStr.substr(rp + 1, rc - rp - 1));
                            prop.RangeMax = std::stof(typeStr.substr(rc + 1, re - rc - 1));
                        } catch (...) {}
                    }
                }
            }
            else if (typeStr == "Color")
            {
                prop.Type = EShaderPropertyType::Color;
            }
            else if (typeStr == "Texture2D")
            {
                prop.Type = EShaderPropertyType::Texture2D;
            }
            else
            {
                continue; // unknown type
            }

            // Parse default value after "="
            auto eqPos = content.find('=', parenClose);
            if (eqPos != std::string::npos)
            {
                std::string defStr = content.substr(eqPos + 1);
                // Trim
                auto df = defStr.find_first_not_of(" \t");
                if (df != std::string::npos) defStr = defStr.substr(df);

                if (prop.Type == EShaderPropertyType::Float || prop.Type == EShaderPropertyType::Range)
                {
                    try { prop.DefaultFloat = std::stof(defStr); } catch (...) {}
                }
                else if (prop.Type == EShaderPropertyType::Color)
                {
                    // (r, g, b, a)
                    auto cp = defStr.find('(');
                    auto ce = defStr.find(')');
                    if (cp != std::string::npos && ce != std::string::npos)
                    {
                        std::string vals = defStr.substr(cp + 1, ce - cp - 1);
                        float v[4] = { 1, 1, 1, 1 };
                        int idx = 0;
                        std::stringstream vss(vals);
                        std::string tok;
                        while (std::getline(vss, tok, ',') && idx < 4)
                        {
                            try { v[idx++] = std::stof(tok); } catch (...) {}
                        }
                        prop.DefaultColor = { v[0], v[1], v[2], v[3] };
                    }
                }
                else if (prop.Type == EShaderPropertyType::Texture2D)
                {
                    // "white" or "path"
                    auto qs = defStr.find('"');
                    auto qe = defStr.find('"', qs + 1);
                    if (qs != std::string::npos && qe != std::string::npos)
                        prop.DefaultTexture = defStr.substr(qs + 1, qe - qs - 1);
                }
            }

            props.push_back(prop);
        }

        return props;
    }

} // namespace Kiwi
