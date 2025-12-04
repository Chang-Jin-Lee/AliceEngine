#include "Core/Script.h"

#include "Core/World.h"

namespace Alice
{
    // === IScript 기본 헬퍼 구현 ===

    TransformComponent* IScript::GetTransform()
    {
        if (!m_world || m_entity == InvalidEntityId)
            return nullptr;

        return m_world->GetTransform(m_entity);
    }

    namespace
    {
        // 전역 스크립트 레지스트리 (간단한 이름 → 생성 함수 매핑)
        std::unordered_map<std::string, ScriptCreateFunc>& GetScriptRegistry()
        {
            static std::unordered_map<std::string, ScriptCreateFunc> s_registry;
            return s_registry;
        }
    }

    // === ScriptFactory 구현 ===

    void ScriptFactory::Register(const char* name, ScriptCreateFunc func)
    {
        if (!name || !func)
            return;

        auto& registry = GetScriptRegistry();
        registry[name] = func;
    }

    std::unique_ptr<IScript> ScriptFactory::Create(const char* name)
    {
        if (!name)
            return nullptr;

        auto& registry = GetScriptRegistry();
        auto  it       = registry.find(name);
        if (it == registry.end())
            return nullptr;

        IScript* raw = it->second();
        return std::unique_ptr<IScript>(raw);
    }

    std::vector<std::string> ScriptFactory::GetRegisteredScriptNames()
    {
        std::vector<std::string> result;
        auto& registry = GetScriptRegistry();
        result.reserve(registry.size());

        for (const auto& [name, _] : registry)
        {
            (void)_;
            result.push_back(name);
        }
        return result;
    }

    // === ScriptSystem 구현 ===

    void ScriptSystem::Update(World& world, float deltaTime)
    {
        const auto& scripts = world.GetScripts();
        for (const auto& [entityId, scriptComp] : scripts)
        {
            if (!scriptComp.instance)
                continue;

            scriptComp.instance->OnUpdate(world, entityId, deltaTime);
        }
    }
}



