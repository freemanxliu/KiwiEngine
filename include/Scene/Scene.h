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

        // 物体管理
        SceneObject* AddObject(EPrimitiveType type, const std::string& name = "");
        void RemoveObject(uint32_t id);
        SceneObject* GetObject(uint32_t id);
        const std::vector<SceneObject>& GetObjects() const { return m_Objects; }
        std::vector<SceneObject>& GetObjects() { return m_Objects; }

        // 选择管理
        void SelectObject(uint32_t id);
        void DeselectAll();
        SceneObject* GetSelectedObject();
        int32_t GetSelectedID() const { return m_SelectedID; }

        // 序列化
        bool SaveToFile(const std::string& filepath) const;
        bool LoadFromFile(const std::string& filepath);

        // 清空场景
        void Clear();

        // 场景名称
        const std::string& GetName() const { return m_Name; }
        void SetName(const std::string& name) { m_Name = name; }

    private:
        static Mesh CreateMeshForType(EPrimitiveType type);
        uint32_t GenerateID();

        std::vector<SceneObject> m_Objects;
        int32_t m_SelectedID = -1;
        uint32_t m_NextID = 1;
        std::string m_Name = "Untitled Scene";
    };

} // namespace Kiwi
