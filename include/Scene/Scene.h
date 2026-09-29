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
        const std::vector<std::unique_ptr<SceneObject>>& GetObjects() const { return Objects; }
        std::vector<std::unique_ptr<SceneObject>>& GetObjects() { return Objects; }

        // Selection management
        void SelectObject(uint32_t id);
        void DeselectAll();
        SceneObject* GetSelectedObject();
        int32_t GetSelectedID() const { return SelectedID; }

        // Serialization
        bool SaveToFile(const std::string& filepath) const;
        bool LoadFromFile(const std::string& filepath);

        // Clear scene
        void Clear();

        // Scene name
        const std::string& GetName() const { return Name; }
        void SetName(const std::string& name) { Name = name; }

        // Find the active camera: returns the camera marked as Main Camera.
        // If no camera is marked, returns the first enabled CameraComponent.
        CameraComponent* GetActiveCamera() const;

        // Set a specific camera as the Main Camera (clears IsMainCamera on all others).
        // Pass nullptr to clear all main camera flags.
        void SetMainCamera(CameraComponent* cam);

        // Render scene that mirrors this scene's primitives. Existing mesh components are
        // removed from the old one and added to the new one. Pass nullptr to detach.
        void SetSceneInterface(SceneInterface* InSceneInterface);

    private:
        static Mesh CreateMeshForType(EPrimitiveType type);
        uint32_t GenerateID();

        SceneObject* AddObject(std::unique_ptr<SceneObject> Object);
        void RegisterObject(SceneObject& Object);
        void UnregisterObject(SceneObject& Object);
        void UpdateSelectedState(SceneObject& Object);

        SceneInterface* RenderSceneInterface = nullptr;
        std::vector<std::unique_ptr<SceneObject>> Objects;
        int32_t SelectedID = -1;
        uint32_t NextID = 1;
        std::string Name = "Untitled Scene";
    };

} // namespace Kiwi
