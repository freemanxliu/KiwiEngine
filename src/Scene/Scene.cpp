#include "Scene/Scene.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace Kiwi
{

    Scene::Scene()
    {
    }

    SceneObject* Scene::AddObject(EPrimitiveType type, const std::string& name)
    {
        SceneObject obj;
        obj.ID = GenerateID();
        obj.PrimitiveType = type;
        obj.MeshData = CreateMeshForType(type);

        if (name.empty())
        {
            obj.Name = std::string(PrimitiveTypeToString(type)) + "_" + std::to_string(obj.ID);
        }
        else
        {
            obj.Name = name;
        }

        // 默认颜色根据类型
        switch (type)
        {
        case EPrimitiveType::Cube:     obj.Color = { 0.9f, 0.3f, 0.3f, 1.0f }; break;
        case EPrimitiveType::Sphere:   obj.Color = { 0.3f, 0.7f, 0.9f, 1.0f }; break;
        case EPrimitiveType::Cylinder: obj.Color = { 0.3f, 0.9f, 0.4f, 1.0f }; break;
        case EPrimitiveType::Floor:
            obj.Color = { 0.6f, 0.6f, 0.6f, 1.0f };
            obj.TransformData.Scale = { 10.0f, 1.0f, 10.0f };
            obj.TransformData.Position.y = -0.5f;
            break;
        }

        m_Objects.push_back(std::move(obj));
        return &m_Objects.back();
    }

    void Scene::RemoveObject(uint32_t id)
    {
        auto it = std::remove_if(m_Objects.begin(), m_Objects.end(),
            [id](const SceneObject& obj) { return obj.ID == id; });
        m_Objects.erase(it, m_Objects.end());

        if (m_SelectedID == (int32_t)id)
            m_SelectedID = -1;
    }

    SceneObject* Scene::GetObject(uint32_t id)
    {
        for (auto& obj : m_Objects)
        {
            if (obj.ID == id) return &obj;
        }
        return nullptr;
    }

    void Scene::SelectObject(uint32_t id)
    {
        DeselectAll();
        for (auto& obj : m_Objects)
        {
            if (obj.ID == id)
            {
                obj.Selected = true;
                m_SelectedID = (int32_t)id;
                return;
            }
        }
    }

    void Scene::DeselectAll()
    {
        for (auto& obj : m_Objects)
            obj.Selected = false;
        m_SelectedID = -1;
    }

    SceneObject* Scene::GetSelectedObject()
    {
        if (m_SelectedID < 0) return nullptr;
        return GetObject((uint32_t)m_SelectedID);
    }

    void Scene::Clear()
    {
        m_Objects.clear();
        m_SelectedID = -1;
        m_NextID = 1;
    }

    Mesh Scene::CreateMeshForType(EPrimitiveType type)
    {
        switch (type)
        {
        case EPrimitiveType::Cube:     return Mesh::CreateCube(1.0f);
        case EPrimitiveType::Sphere:   return Mesh::CreateSphere(0.5f, 24);
        case EPrimitiveType::Cylinder: return Mesh::CreateCylinder(0.5f, 1.0f, 24);
        case EPrimitiveType::Floor:    return Mesh::CreatePlane(1.0f, 1.0f);
        default:                       return Mesh::CreateCube(1.0f);
        }
    }

    uint32_t Scene::GenerateID()
    {
        return m_NextID++;
    }

    // ============================================================
    // Simple JSON-like serialization (no external dependency)
    // ============================================================

    static std::string EscapeString(const std::string& s)
    {
        std::string result;
        for (char c : s)
        {
            if (c == '"') result += "\\\"";
            else if (c == '\\') result += "\\\\";
            else result += c;
        }
        return result;
    }

    bool Scene::SaveToFile(const std::string& filepath) const
    {
        std::ofstream file(filepath);
        if (!file.is_open())
        {
            std::cerr << "[Kiwi] Failed to save scene to: " << filepath << std::endl;
            return false;
        }

        file << "{\n";
        file << "  \"name\": \"" << EscapeString(m_Name) << "\",\n";
        file << "  \"objects\": [\n";

        for (size_t i = 0; i < m_Objects.size(); i++)
        {
            const auto& obj = m_Objects[i];
            file << "    {\n";
            file << "      \"name\": \"" << EscapeString(obj.Name) << "\",\n";
            file << "      \"type\": \"" << PrimitiveTypeToString(obj.PrimitiveType) << "\",\n";
            file << "      \"position\": [" << obj.TransformData.Position.x << ", " << obj.TransformData.Position.y << ", " << obj.TransformData.Position.z << "],\n";
            file << "      \"rotation\": [" << obj.TransformData.Rotation.x << ", " << obj.TransformData.Rotation.y << ", " << obj.TransformData.Rotation.z << "],\n";
            file << "      \"scale\": [" << obj.TransformData.Scale.x << ", " << obj.TransformData.Scale.y << ", " << obj.TransformData.Scale.z << "],\n";
            file << "      \"color\": [" << obj.Color.x << ", " << obj.Color.y << ", " << obj.Color.z << ", " << obj.Color.w << "],\n";
            file << "      \"shader\": \"" << EscapeString(obj.ShaderName) << "\"\n";
            file << "    }";
            if (i + 1 < m_Objects.size()) file << ",";
            file << "\n";
        }

        file << "  ]\n";
        file << "}\n";

        file.close();
        std::cout << "[Kiwi] Scene saved to: " << filepath << std::endl;
        return true;
    }

    // Simple JSON parser (handles our specific format)
    static std::string ReadQuotedString(const std::string& line, const std::string& key)
    {
        size_t pos = line.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        pos = line.find(':', pos);
        if (pos == std::string::npos) return "";
        size_t start = line.find('"', pos + 1);
        if (start == std::string::npos) return "";
        start++;
        size_t end = line.find('"', start);
        if (end == std::string::npos) return "";
        return line.substr(start, end - start);
    }

    static bool ReadVec3(const std::string& line, const std::string& key, Vec3& out)
    {
        size_t pos = line.find("\"" + key + "\"");
        if (pos == std::string::npos) return false;
        size_t bracket = line.find('[', pos);
        if (bracket == std::string::npos) return false;
        if (sscanf(line.c_str() + bracket, "[%f, %f, %f]", &out.x, &out.y, &out.z) == 3)
            return true;
        return false;
    }

    static bool ReadVec4(const std::string& line, const std::string& key, Vec4& out)
    {
        size_t pos = line.find("\"" + key + "\"");
        if (pos == std::string::npos) return false;
        size_t bracket = line.find('[', pos);
        if (bracket == std::string::npos) return false;
        if (sscanf(line.c_str() + bracket, "[%f, %f, %f, %f]", &out.x, &out.y, &out.z, &out.w) == 4)
            return true;
        return false;
    }

    bool Scene::LoadFromFile(const std::string& filepath)
    {
        std::ifstream file(filepath);
        if (!file.is_open())
        {
            std::cerr << "[Kiwi] Failed to load scene from: " << filepath << std::endl;
            return false;
        }

        Clear();

        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        file.close();

        // Read scene name
        {
            std::string name = ReadQuotedString(content, "name");
            if (!name.empty()) m_Name = name;
        }

        // Parse objects
        size_t objStart = 0;
        while ((objStart = content.find('{', objStart + 1)) != std::string::npos)
        {
            size_t objEnd = content.find('}', objStart);
            if (objEnd == std::string::npos) break;

            std::string objStr = content.substr(objStart, objEnd - objStart + 1);

            std::string name = ReadQuotedString(objStr, "name");
            std::string typeStr = ReadQuotedString(objStr, "type");

            if (typeStr.empty()) continue;

            EPrimitiveType type = StringToPrimitiveType(typeStr);
            SceneObject* obj = AddObject(type, name);

            Vec3 pos, rot, scale;
            Vec4 color;
            if (ReadVec3(objStr, "position", pos)) obj->TransformData.Position = pos;
            if (ReadVec3(objStr, "rotation", rot)) obj->TransformData.Rotation = rot;
            if (ReadVec3(objStr, "scale", scale))  obj->TransformData.Scale = scale;
            if (ReadVec4(objStr, "color", color))  obj->Color = color;

            std::string shaderName = ReadQuotedString(objStr, "shader");
            if (!shaderName.empty()) obj->ShaderName = shaderName;
        }

        std::cout << "[Kiwi] Scene loaded from: " << filepath
                  << " (" << m_Objects.size() << " objects)" << std::endl;
        return true;
    }

} // namespace Kiwi
