#pragma once

#include "Scene/SceneObject.h"
#include <vector>
#include <string>
#include <memory>

namespace Kiwi
{

    class Scene
    {
    public:
        Scene();
        ~Scene() = default;

        // Object management
        SceneObject* AddMeshObject(EPrimitiveType type, const std::string& name = "");
        SceneObject* AddCameraObject(const std::string& name = "");
        SceneObject* AddDirectionalLightObject(const std::string& name = "");
        SceneObject* AddPointLightObject(const std::string& name = "");
        SceneObject* AddEmptyObject(const std::string& name = "");
        SceneObject* AddPostProcessObject(const std::string& name = "");
        void RemoveObject(uint32_t id);
        SceneObject* GetObject(uint32_t id);

        // Access all objects
        const std::vector<std::unique_ptr<SceneObject>>& GetObjects() const { return m_Objects; }
        std::vector<std::unique_ptr<SceneObject>>& GetObjects() { return m_Objects; }

        // Selection management
        void SelectObject(uint32_t id);
        void DeselectAll();
        SceneObject* GetSelectedObject();
        int32_t GetSelectedID() const { return m_SelectedID; }

        // Serialization
        bool SaveToFile(const std::string& filepath) const;
        bool LoadFromFile(const std::string& filepath);

        // Clear scene
        void Clear();

        // Scene name
        const std::string& GetName() const { return m_Name; }
        void SetName(const std::string& name) { m_Name = name; }

        // Find the active camera: returns the camera marked as Main Camera.
        // If no camera is marked, returns the first enabled CameraComponent.
        CameraComponent* GetActiveCamera() const;

        // Set a specific camera as the Main Camera (clears IsMainCamera on all others).
        // Pass nullptr to clear all main camera flags.
        void SetMainCamera(CameraComponent* cam);

    private:
        static Mesh CreateMeshForType(EPrimitiveType type);
        uint32_t GenerateID();

        std::vector<std::unique_ptr<SceneObject>> m_Objects;
        int32_t m_SelectedID = -1;
        uint32_t m_NextID = 1;
        std::string m_Name = "Untitled Scene";
    };

} // namespace Kiwi
