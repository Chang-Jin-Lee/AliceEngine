#include "Core/Scene.h"
#include "Core/ResourceManager.h"

namespace Alice
{
    namespace
    {
        using Registry = std::unordered_map<std::string, SceneCreateFunc>;

        Registry& GetRegistry()
        {
            static Registry s_registry;
            return s_registry;
        }
    }

    void SceneFactory::Register(const char* name, SceneCreateFunc func)
    {
        if (!name || !func) return;
        GetRegistry()[name] = func;
    }

    std::unique_ptr<IScene> SceneFactory::Create(const char* name)
    {
        if (!name) return nullptr;

        auto& registry = GetRegistry();
        auto  it       = registry.find(name);
        if (it == registry.end()) return nullptr;

        return std::unique_ptr<IScene>(it->second());
    }

    SceneManager::SceneManager(World& world, ResourceManager& resources) : m_world(world)
        , m_resources(resources)
    {
    }

    bool SceneManager::SwitchTo(const char* sceneName)
    {
        auto newScene = SceneFactory::Create(sceneName);
        if (!newScene) return false;

        if (m_currentScene) m_currentScene->OnExit(m_world, m_resources);

        m_currentScene = std::move(newScene);
        m_currentScene->OnEnter(m_world, m_resources);
        return true;
    }

    void SceneManager::Update(float deltaTime)
    {
        if (!m_currentScene) return;

        m_currentScene->Update(m_world, m_resources, deltaTime);
    }

    EntityId SceneManager::GetPrimaryRenderableEntity() const
    {
        if (!m_currentScene) return InvalidEntityId;

        return m_currentScene->GetPrimaryRenderableEntity();
    }
}



