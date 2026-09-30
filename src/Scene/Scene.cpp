#include "Scene/Scene.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace Kiwi
{

    static void WriteMaterialOverrides(std::ofstream& file, const MaterialInstance& inst);

    Scene::Scene()
    {
    }

    SceneObject* Scene::AddMeshObject(EPrimitiveType type, const std::string& name)
    {
        auto obj = std::make_unique<SceneObject>();
        obj->ID = GenerateID();

        if (name.empty())
        {
            obj->Name = std::string(PrimitiveTypeToString(type)) + "_" + std::to_string(obj->ID);
        }
        else
        {
            obj->Name = name;
        }

        // Add PrimitiveComponent
        auto* mesh = obj->AddComponent<PrimitiveComponent>();
        mesh->MeshData = CreateMeshForType(type);
        mesh->PrimitiveType = type;

        // Floor gets default scale/position; color is controlled by Material
        if (type == EPrimitiveType::Floor)
        {
            mesh->Scale = { 10.0f, 1.0f, 10.0f };
            mesh->Position.y = -0.5f;
        }

        return AddObject(std::move(obj));
    }

    SceneObject* Scene::AddCameraObject(const std::string& name)
    {
        auto obj = std::make_unique<SceneObject>();
        obj->ID = GenerateID();
        obj->Name = name.empty() ? ("Camera_" + std::to_string(obj->ID)) : name;

        // Add CameraComponent with defaults
        auto* cam = obj->AddComponent<CameraComponent>();
        cam->Position = { 0.0f, 3.0f, -6.0f };
        cam->FieldOfView = 45.0f;
        cam->NearPlane = 0.1f;
        cam->FarPlane = 100.0f;

        // If no Main Camera exists yet, make this one the Main Camera
        if (!GetActiveCamera() || !GetActiveCamera()->IsMainCamera)
        {
            // Clear any existing flags and set this one
            for (auto& existing : Objects)
            {
                auto* existingCam = existing->GetComponent<CameraComponent>();
                if (existingCam) existingCam->IsMainCamera = false;
            }
            cam->IsMainCamera = true;
        }

        return AddObject(std::move(obj));
    }

    SceneObject* Scene::AddDirectionalLightObject(const std::string& name)
    {
        auto obj = std::make_unique<SceneObject>();
        obj->ID = GenerateID();
        obj->Name = name.empty() ? ("DirectionalLight_" + std::to_string(obj->ID)) : name;

        auto* light = obj->AddComponent<DirectionalLightComponent>();
        // Default: pointing down-forward (like sun)
        light->Rotation = { 50.0f, -30.0f, 0.0f };
        light->LightColor = { 1.0f, 1.0f, 0.9f }; // Warm white
        light->Intensity = 1.0f;

        return AddObject(std::move(obj));
    }

    SceneObject* Scene::AddPointLightObject(const std::string& name)
    {
        auto obj = std::make_unique<SceneObject>();
        obj->ID = GenerateID();
        obj->Name = name.empty() ? ("PointLight_" + std::to_string(obj->ID)) : name;

        auto* light = obj->AddComponent<PointLightComponent>();
        light->Position = { 0.0f, 3.0f, 0.0f };
        light->LightColor = { 1.0f, 1.0f, 1.0f };
        light->Intensity = 1.0f;
        light->Radius = 10.0f;

        return AddObject(std::move(obj));
    }

    SceneObject* Scene::AddEmptyObject(const std::string& name)
    {
        auto obj = std::make_unique<SceneObject>();
        obj->ID = GenerateID();
        obj->Name = name.empty() ? ("Empty_" + std::to_string(obj->ID)) : name;

        return AddObject(std::move(obj));
    }

    SceneObject* Scene::AddPostProcessObject(const std::string& name)
    {
        auto obj = std::make_unique<SceneObject>();
        obj->ID = GenerateID();
        obj->Name = name.empty() ? ("PostProcess_" + std::to_string(obj->ID)) : name;

        // Add PostProcessComponent with empty material list
        obj->AddComponent<PostProcessComponent>();

        return AddObject(std::move(obj));
    }

    void Scene::RemoveObject(uint32_t id)
    {
        for (auto& obj : Objects)
        {
            if (obj->ID == id)
                UnregisterObject(*obj);
        }
        auto it = std::remove_if(Objects.begin(), Objects.end(),
            [id](const std::unique_ptr<SceneObject>& obj) { return obj->ID == id; });
        Objects.erase(it, Objects.end());

        if (SelectedID == (int32_t)id)
            SelectedID = -1;
    }

    SceneObject* Scene::AddObject(std::unique_ptr<SceneObject> Object)
    {
        SceneObject* Ptr = Object.get();
        Objects.push_back(std::move(Object));
        RegisterObject(*Ptr);
        return Ptr;
    }

    void Scene::RegisterObject(SceneObject& Object)
    {
        if (!RenderSceneInterface)
            return;
        for (PrimitiveComponent* Mesh : Object.GetComponents<PrimitiveComponent>())
        {
            Mesh->RegisteredScene = RenderSceneInterface;
            RenderSceneInterface->AddPrimitive(Mesh);
        }
        for (LightComponent* Light : Object.GetComponents<LightComponent>())
        {
            Light->RegisteredScene = RenderSceneInterface;
            RenderSceneInterface->AddLight(Light);
        }
    }

    void Scene::UnregisterObject(SceneObject& Object)
    {
        for (PrimitiveComponent* Mesh : Object.GetComponents<PrimitiveComponent>())
        {
            if (Mesh->RegisteredScene)
                Mesh->RegisteredScene->RemovePrimitive(Mesh);
            Mesh->RegisteredScene = nullptr;
        }
        for (LightComponent* Light : Object.GetComponents<LightComponent>())
        {
            if (Light->RegisteredScene)
                Light->RegisteredScene->RemoveLight(Light);
            Light->RegisteredScene = nullptr;
        }
    }

    void Scene::UpdateSelectedState(SceneObject& Object)
    {
        for (PrimitiveComponent* Mesh : Object.GetComponents<PrimitiveComponent>())
        {
            if (Mesh->RegisteredScene)
                Mesh->RegisteredScene->UpdatePrimitiveSelectedState(Mesh);
        }
    }

    void Scene::SetSceneInterface(SceneInterface* InSceneInterface)
    {
        if (RenderSceneInterface == InSceneInterface)
            return;
        for (auto& Object : Objects)
            UnregisterObject(*Object);
        RenderSceneInterface = InSceneInterface;
        for (auto& Object : Objects)
            RegisterObject(*Object);
    }

    SceneObject* Scene::GetObject(uint32_t id)
    {
        for (auto& obj : Objects)
        {
            if (obj->ID == id) return obj.get();
        }
        return nullptr;
    }

    void Scene::SelectObject(uint32_t id)
    {
        DeselectAll();
        for (auto& obj : Objects)
        {
            if (obj->ID == id)
            {
                obj->Selected = true;
                SelectedID = (int32_t)id;
                UpdateSelectedState(*obj);
                return;
            }
        }
    }

    void Scene::DeselectAll()
    {
        for (auto& obj : Objects)
        {
            if (!obj->Selected)
                continue;
            obj->Selected = false;
            UpdateSelectedState(*obj);
        }
        SelectedID = -1;
    }

    SceneObject* Scene::GetSelectedObject()
    {
        if (SelectedID < 0) return nullptr;
        return GetObject((uint32_t)SelectedID);
    }

    void Scene::Clear()
    {
        for (auto& obj : Objects)
            UnregisterObject(*obj);
        Objects.clear();
        SelectedID = -1;
        NextID = 1;
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

    CameraComponent* Scene::GetActiveCamera() const
    {
        // Priority 1: Find the camera marked as Main Camera
        CameraComponent* firstEnabled = nullptr;
        for (auto& obj : Objects)
        {
            auto* cam = obj->GetComponent<CameraComponent>();
            if (cam && cam->Enabled)
            {
                if (cam->IsMainCamera)
                    return cam;
                if (!firstEnabled)
                    firstEnabled = cam;
            }
        }
        // Priority 2: Fallback to first enabled camera
        return firstEnabled;
    }

    void Scene::SetMainCamera(CameraComponent* targetCam)
    {
        // Clear IsMainCamera on all cameras, then set the target
        for (auto& obj : Objects)
        {
            auto* cam = obj->GetComponent<CameraComponent>();
            if (cam)
                cam->IsMainCamera = false;
        }
        if (targetCam)
            targetCam->IsMainCamera = true;
    }

    uint32_t Scene::GenerateID()
    {
        return NextID++;
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
        file << "  \"name\": \"" << EscapeString(Name) << "\",\n";
        file << "  \"objects\": [\n";

        for (size_t i = 0; i < Objects.size(); i++)
        {
            const auto& obj = *Objects[i];
            file << "    {\n";
            file << "      \"name\": \"" << EscapeString(obj.Name) << "\",\n";

            // Serialize components
            file << "      \"components\": [\n";

            for (size_t c = 0; c < obj.Components.size(); c++)
            {
                const auto& comp = *obj.Components[c];
                file << "        {\n";
                file << "          \"type\": \"" << comp.GetTypeName() << "\",\n";
                file << "          \"enabled\": " << (comp.Enabled ? "true" : "false") << ",\n";
                file << "          \"position\": [" << comp.Position.x << ", " << comp.Position.y << ", " << comp.Position.z << "],\n";
                file << "          \"rotation\": [" << comp.Rotation.x << ", " << comp.Rotation.y << ", " << comp.Rotation.z << "],\n";
                file << "          \"scale\": [" << comp.Scale.x << ", " << comp.Scale.y << ", " << comp.Scale.z << "]";

                if (comp.GetType() == EComponentType::Primitive)
                {
                    const auto& mesh = static_cast<const PrimitiveComponent&>(comp);
                    file << ",\n";
                    file << "          \"materialName\": \"" << EscapeString(mesh.Material.Parent) << "\"";
                    WriteMaterialOverrides(file, mesh.Material);
                    file << ",\n";
                    file << "          \"primitiveType\": \"" << PrimitiveTypeToString(mesh.PrimitiveType) << "\",\n";
                    file << "          \"sortOrder\": " << mesh.SortOrder << ",\n";
                    const char* cullName = "Back";
                    if (mesh.CullMode == ECullMode::Front) cullName = "Front";
                    else if (mesh.CullMode == ECullMode::None) cullName = "None";
                    file << "          \"cullMode\": \"" << cullName << "\"\n";
                }
                else if (comp.GetType() == EComponentType::Camera)
                {
                    const auto& cam = static_cast<const CameraComponent&>(comp);
                    file << ",\n";
                    file << "          \"projection\": \"" << (cam.Projection == ECameraProjection::Perspective ? "Perspective" : "Orthographic") << "\",\n";
                    file << "          \"fov\": " << cam.FieldOfView << ",\n";
                    file << "          \"nearPlane\": " << cam.NearPlane << ",\n";
                    file << "          \"farPlane\": " << cam.FarPlane << ",\n";
                    file << "          \"orthoWidth\": " << cam.OrthoWidth << ",\n";
                    file << "          \"orthoHeight\": " << cam.OrthoHeight << ",\n";
                    file << "          \"isMainCamera\": " << (cam.IsMainCamera ? "true" : "false") << "\n";
                }
                else if (comp.GetType() == EComponentType::Light)
                {
                    const auto& light = static_cast<const LightComponent&>(comp);
                    file << ",\n";
                    file << "          \"lightType\": \"" << light.GetLightTypeName() << "\",\n";
                    file << "          \"lightColor\": [" << light.LightColor.x << ", " << light.LightColor.y << ", " << light.LightColor.z << "],\n";
                    file << "          \"intensity\": " << light.Intensity << ",\n";
                    file << "          \"affectWorld\": " << (light.AffectWorld ? "true" : "false");

                    if (light.GetLightType() == ELightType::Point)
                    {
                        const auto& point = static_cast<const PointLightComponent&>(light);
                        file << ",\n";
                        file << "          \"radius\": " << point.Radius << "\n";
                    }
                    else if (light.GetLightType() == ELightType::Directional)
                    {
                        const auto& dirLight = static_cast<const DirectionalLightComponent&>(light);
                        file << ",\n";
                        file << "          \"castShadow\": " << (dirLight.CastShadow ? "true" : "false") << ",\n";
                        file << "          \"numCascades\": " << dirLight.NumCascades << ",\n";
                        file << "          \"shadowMapResolution\": " << dirLight.ShadowMapResolution << ",\n";
                        file << "          \"shadowDistance\": " << dirLight.ShadowDistance << ",\n";
                        file << "          \"cascadeSplitLambda\": " << dirLight.CascadeSplitLambda << ",\n";
                        file << "          \"shadowBias\": " << dirLight.ShadowBias << ",\n";
                        file << "          \"normalBias\": " << dirLight.NormalBias << ",\n";
                        file << "          \"shadowStrength\": " << dirLight.ShadowStrength << "\n";
                    }
                    else
                    {
                        file << "\n";
                    }
                }
                else if (comp.GetType() == EComponentType::PostProcess)
                {
                    const auto& pp = static_cast<const PostProcessComponent&>(comp);
                    file << ",\n";
                    file << "          \"materials\": [\n";
                    for (size_t mi = 0; mi < pp.Materials.size(); mi++)
                    {
                        const auto& mat = pp.Materials[mi];
                        file << "            { \"shaderName\": \"" << mat.ShaderName << "\", ";
                        file << "\"enabled\": " << (mat.Enabled ? "true" : "false") << ", ";
                        file << "\"intensity\": " << mat.Intensity << " }";
                        if (mi + 1 < pp.Materials.size()) file << ",";
                        file << "\n";
                    }
                    file << "          ]\n";
                }
                else
                {
                    file << "\n";
                }

                file << "        }";
                if (c + 1 < obj.Components.size()) file << ",";
                file << "\n";
            }

            file << "      ]\n";
            file << "    }";
            if (i + 1 < Objects.size()) file << ",";
            file << "\n";
        }

        file << "  ]\n";
        file << "}\n";

        file.close();
        std::cout << "[Kiwi] Scene saved to: " << filepath << std::endl;
        return true;
    }

    // Simple JSON parser helpers
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

    static bool ReadInt(const std::string& line, const std::string& key, int32_t& out)
    {
        size_t pos = line.find("\"" + key + "\"");
        if (pos == std::string::npos) return false;
        pos = line.find(':', pos);
        if (pos == std::string::npos) return false;
        pos++;
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
        if (sscanf(line.c_str() + pos, "%d", &out) == 1)
            return true;
        return false;
    }

    static bool ReadFloat(const std::string& line, const std::string& key, float& out)
    {
        size_t pos = line.find("\"" + key + "\"");
        if (pos == std::string::npos) return false;
        pos = line.find(':', pos);
        if (pos == std::string::npos) return false;
        pos++;
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
        if (sscanf(line.c_str() + pos, "%f", &out) == 1)
            return true;
        return false;
    }

    static bool HasJsonKey(const std::string& line, const std::string& key)
    {
        return line.find("\"" + key + "\"") != std::string::npos;
    }

    static void WriteMaterialOverrides(std::ofstream& file, const MaterialInstance& inst)
    {
        auto writeColor = [&](const char* prop, const char* key)
        {
            if (!inst.HasOverride(prop))
                return;
            Vec4 c = inst.GetColor(nullptr, prop);
            file << ",\n          \"" << key << "\": [" << c.x << ", " << c.y << ", " << c.z << ", " << c.w << "]";
        };
        auto writeFloat = [&](const char* prop, const char* key)
        {
            if (!inst.HasOverride(prop))
                return;
            file << ",\n          \"" << key << "\": " << inst.GetFloat(nullptr, prop);
        };
        auto writeTex = [&](const char* prop, const char* key)
        {
            if (!inst.HasOverride(prop))
                return;
            file << ",\n          \"" << key << "\": \"" << EscapeString(inst.GetTexture(nullptr, prop)) << "\"";
        };
        writeColor("_Color", "ovColor");
        writeColor("_Emissive", "ovEmissive");
        writeFloat("_Roughness", "ovRoughness");
        writeFloat("_Metallic", "ovMetallic");
        writeTex("_BaseColorTex", "ovBaseColorTex");
        writeTex("_NormalTex", "ovNormalTex");
        writeTex("_MetallicRoughnessTex", "ovMRTex");
    }

    static void ReadMaterialOverrides(const std::string& json, MaterialInstance& inst)
    {
        Vec4 color;
        if (ReadVec4(json, "ovColor", color))
            inst.SetColor("_Color", color);
        if (ReadVec4(json, "ovEmissive", color))
            inst.SetColor("_Emissive", color);
        float value = 0.0f;
        if (ReadFloat(json, "ovRoughness", value))
            inst.SetFloat("_Roughness", value);
        if (ReadFloat(json, "ovMetallic", value))
            inst.SetFloat("_Metallic", value);
        if (HasJsonKey(json, "ovBaseColorTex"))
            inst.SetTexture("_BaseColorTex", ReadQuotedString(json, "ovBaseColorTex"));
        if (HasJsonKey(json, "ovNormalTex"))
            inst.SetTexture("_NormalTex", ReadQuotedString(json, "ovNormalTex"));
        if (HasJsonKey(json, "ovMRTex"))
            inst.SetTexture("_MetallicRoughnessTex", ReadQuotedString(json, "ovMRTex"));
    }

    static bool ReadBool(const std::string& line, const std::string& key, bool& out)
    {
        size_t pos = line.find("\"" + key + "\"");
        if (pos == std::string::npos) return false;
        pos = line.find(':', pos);
        if (pos == std::string::npos) return false;
        if (line.find("true", pos) != std::string::npos) { out = true; return true; }
        if (line.find("false", pos) != std::string::npos) { out = false; return true; }
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
            if (!name.empty()) Name = name;
        }

        // Check if this is the new component-based format or legacy format
        bool hasComponents = (content.find("\"components\"") != std::string::npos);

        if (hasComponents)
        {
            // New component-based format
            // Parse objects: find each object block
            size_t searchPos = content.find("\"objects\"");
            if (searchPos == std::string::npos) return true;

            // Find each object's { ... } block (containing "name" and "components")
            size_t objArrayStart = content.find('[', searchPos);
            if (objArrayStart == std::string::npos) return true;

            // Simple brace-matching parser for objects
            size_t pos = objArrayStart + 1;
            while (pos < content.size())
            {
                // Find next object start
                size_t objStart = content.find('{', pos);
                if (objStart == std::string::npos) break;

                // Match braces to find object end
                int depth = 1;
                size_t objEnd = objStart + 1;
                while (objEnd < content.size() && depth > 0)
                {
                    if (content[objEnd] == '{') depth++;
                    else if (content[objEnd] == '}') depth--;
                    if (depth > 0) objEnd++;
                }

                std::string objStr = content.substr(objStart, objEnd - objStart + 1);
                pos = objEnd + 1;

                // Read object name
                std::string objName = ReadQuotedString(objStr, "name");
                if (objName.empty()) continue;

                // Find components array
                size_t compArrayPos = objStr.find("\"components\"");
                if (compArrayPos == std::string::npos) continue;

                size_t compArrayStart = objStr.find('[', compArrayPos);
                if (compArrayStart == std::string::npos) continue;

                // Create the object first as empty, then add components
                auto sceneObj = std::make_unique<SceneObject>();
                sceneObj->ID = GenerateID();
                sceneObj->Name = objName;

                // Parse each component block
                size_t cPos = compArrayStart + 1;
                while (cPos < objStr.size())
                {
                    size_t compStart = objStr.find('{', cPos);
                    if (compStart == std::string::npos) break;

                    // Check if we've gone past the components array
                    size_t compArrayEnd = objStr.find(']', compArrayStart);
                    if (compArrayEnd != std::string::npos && compStart > compArrayEnd) break;

                    int cDepth = 1;
                    size_t compEnd = compStart + 1;
                    while (compEnd < objStr.size() && cDepth > 0)
                    {
                        if (objStr[compEnd] == '{') cDepth++;
                        else if (objStr[compEnd] == '}') cDepth--;
                        if (cDepth > 0) compEnd++;
                    }

                    std::string compStr = objStr.substr(compStart, compEnd - compStart + 1);
                    cPos = compEnd + 1;

                    std::string compType = ReadQuotedString(compStr, "type");

                    // Read common transform
                    Vec3 compPos, compRot, compScale = { 1, 1, 1 };
                    bool enabled = true;
                    ReadVec3(compStr, "position", compPos);
                    ReadVec3(compStr, "rotation", compRot);
                    ReadVec3(compStr, "scale", compScale);
                    ReadBool(compStr, "enabled", enabled);

                    // "MeshComponent" is the pre-rename type name still present in older scene files.
                    if (compType == "PrimitiveComponent" || compType == "MeshComponent")
                    {
                        auto* mesh = sceneObj->AddComponent<PrimitiveComponent>();
                        mesh->Position = compPos;
                        mesh->Rotation = compRot;
                        mesh->Scale = compScale;
                        mesh->Enabled = enabled;

                        // Material reference
                        std::string matName = ReadQuotedString(compStr, "materialName");
                        if (!matName.empty()) mesh->Material.Parent = matName;
                        ReadMaterialOverrides(compStr, mesh->Material);

                        int32_t sortOrder = 0;
                        if (ReadInt(compStr, "sortOrder", sortOrder)) mesh->SortOrder = sortOrder;

                        std::string cullName = ReadQuotedString(compStr, "cullMode");
                        if (cullName == "Front") mesh->CullMode = ECullMode::Front;
                        else if (cullName == "None") mesh->CullMode = ECullMode::None;
                        else mesh->CullMode = ECullMode::Back;

                        // Read primitive type and rebuild mesh geometry
                        std::string primTypeStr = ReadQuotedString(compStr, "primitiveType");
                        EPrimitiveType primType = primTypeStr.empty()
                            ? EPrimitiveType::Cube
                            : StringToPrimitiveType(primTypeStr);
                        mesh->PrimitiveType = primType;
                        mesh->MeshData = CreateMeshForType(primType);
                    }
                    else if (compType == "CameraComponent")
                    {
                        auto* cam = sceneObj->AddComponent<CameraComponent>();
                        cam->Position = compPos;
                        cam->Rotation = compRot;
                        cam->Scale = compScale;
                        cam->Enabled = enabled;

                        std::string projStr = ReadQuotedString(compStr, "projection");
                        if (projStr == "Orthographic")
                            cam->Projection = ECameraProjection::Orthographic;

                        ReadFloat(compStr, "fov", cam->FieldOfView);
                        ReadFloat(compStr, "nearPlane", cam->NearPlane);
                        ReadFloat(compStr, "farPlane", cam->FarPlane);
                        ReadFloat(compStr, "orthoWidth", cam->OrthoWidth);
                        ReadFloat(compStr, "orthoHeight", cam->OrthoHeight);
                        ReadBool(compStr, "isMainCamera", cam->IsMainCamera);
                    }
                    else if (compType == "DirectionalLightComponent")
                    {
                        auto* light = sceneObj->AddComponent<DirectionalLightComponent>();
                        light->Position = compPos;
                        light->Rotation = compRot;
                        light->Scale = compScale;
                        light->Enabled = enabled;

                        Vec3 lightColor;
                        if (ReadVec3(compStr, "lightColor", lightColor)) light->LightColor = lightColor;
                        ReadFloat(compStr, "intensity", light->Intensity);
                        ReadBool(compStr, "affectWorld", light->AffectWorld);

                        // Shadow (CSM) parameters
                        ReadBool(compStr, "castShadow", light->CastShadow);
                        { int32_t v; if (ReadInt(compStr, "numCascades", v)) light->NumCascades = v; }
                        { int32_t v; if (ReadInt(compStr, "shadowMapResolution", v)) light->ShadowMapResolution = v; }
                        ReadFloat(compStr, "shadowDistance", light->ShadowDistance);
                        ReadFloat(compStr, "cascadeSplitLambda", light->CascadeSplitLambda);
                        ReadFloat(compStr, "shadowBias", light->ShadowBias);
                        ReadFloat(compStr, "normalBias", light->NormalBias);
                        ReadFloat(compStr, "shadowStrength", light->ShadowStrength);
                    }
                    else if (compType == "PointLightComponent")
                    {
                        auto* light = sceneObj->AddComponent<PointLightComponent>();
                        light->Position = compPos;
                        light->Rotation = compRot;
                        light->Scale = compScale;
                        light->Enabled = enabled;

                        Vec3 lightColor;
                        if (ReadVec3(compStr, "lightColor", lightColor)) light->LightColor = lightColor;
                        ReadFloat(compStr, "intensity", light->Intensity);
                        ReadBool(compStr, "affectWorld", light->AffectWorld);
                        ReadFloat(compStr, "radius", light->Radius);
                    }
                    else if (compType == "PostProcessComponent")
                    {
                        auto* pp = sceneObj->AddComponent<PostProcessComponent>();
                        pp->Position = compPos;
                        pp->Rotation = compRot;
                        pp->Scale = compScale;
                        pp->Enabled = enabled;

                        // Parse materials array
                        size_t matArrayStart = compStr.find("\"materials\"");
                        if (matArrayStart != std::string::npos)
                        {
                            size_t arrStart = compStr.find('[', matArrayStart);
                            size_t arrEnd = compStr.find(']', arrStart);
                            if (arrStart != std::string::npos && arrEnd != std::string::npos)
                            {
                                std::string matArrayStr = compStr.substr(arrStart, arrEnd - arrStart + 1);
                                // Parse each material object
                                size_t matStart = 0;
                                while ((matStart = matArrayStr.find('{', matStart)) != std::string::npos)
                                {
                                    size_t matEnd = matArrayStr.find('}', matStart);
                                    if (matEnd == std::string::npos) break;

                                    std::string matStr = matArrayStr.substr(matStart, matEnd - matStart + 1);

                                    PostProcessMaterial mat;
                                    mat.ShaderName = ReadQuotedString(matStr, "shaderName");
                                    ReadBool(matStr, "enabled", mat.Enabled);
                                    ReadFloat(matStr, "intensity", mat.Intensity);

                                    if (!mat.ShaderName.empty())
                                        pp->Materials.push_back(std::move(mat));

                                    matStart = matEnd + 1;
                                }
                            }
                        }
                    }
                }

                AddObject(std::move(sceneObj));
            }
        }
        else
        {
            // Legacy format: flat SceneObject with direct fields
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
                SceneObject* obj = AddMeshObject(type, name);

                auto* mesh = obj->GetComponent<PrimitiveComponent>();
                if (mesh)
                {
                    Vec3 pos, rot, scale;
                    if (ReadVec3(objStr, "position", pos)) mesh->Position = pos;
                    if (ReadVec3(objStr, "rotation", rot)) mesh->Rotation = rot;
                    if (ReadVec3(objStr, "scale", scale))  mesh->Scale = scale;

                    std::string matName = ReadQuotedString(objStr, "materialName");
                    if (!matName.empty()) mesh->Material.Parent = matName;
                    ReadMaterialOverrides(objStr, mesh->Material);

                    int32_t sortOrder = 0;
                    if (ReadInt(objStr, "sortOrder", sortOrder)) mesh->SortOrder = sortOrder;

                    std::string cullName = ReadQuotedString(objStr, "cullMode");
                    if (cullName == "Front") mesh->CullMode = ECullMode::Front;
                    else if (cullName == "None") mesh->CullMode = ECullMode::None;
                }
            }
        }

        std::cout << "[Kiwi] Scene loaded from: " << filepath
                  << " (" << Objects.size() << " objects)" << std::endl;
        return true;
    }

} // namespace Kiwi
