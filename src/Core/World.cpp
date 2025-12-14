#include "Core/World.h"

namespace Alice
{
    EntityId World::CreateEntity()
    {
        // 간단한 증가형 ID를 사용합니다.
        const EntityId newId = m_nextEntityId++;
        return newId;
    }

    void World::DestroyEntity(EntityId id)
    {
        if (id == InvalidEntityId) return;

        m_transforms.erase(id);
        RemoveScript(id);
        m_materials.erase(id);
        m_skinnedMeshes.erase(id);
        m_skinnedAnimations.erase(id);
    }

    TransformComponent& World::AddTransform(EntityId id)
    {
        // 없는 키일 경우 기본 값으로 새로 생성됩니다.
        return m_transforms[id];
    }

    TransformComponent* World::GetTransform(EntityId id)
    {
        auto it = m_transforms.find(id);
        if (it == m_transforms.end()) return nullptr;
        return &it->second;
    }

    const TransformComponent* World::GetTransform(EntityId id) const
    {
        auto it = m_transforms.find(id);
        if (it == m_transforms.end()) return nullptr;
        return &it->second;
    }

    ScriptComponent& World::AddScript(EntityId id, const std::string& scriptName)
    {
        ScriptComponent& comp = m_scripts[id];
        comp.scriptName = scriptName;
        comp.instance   = ScriptFactory::Create(scriptName.c_str());

        if (comp.instance)
            comp.instance->SetContext(this, id);

        return comp;
    }

    ScriptComponent* World::GetScript(EntityId id)
    {
        auto it = m_scripts.find(id);
        if (it == m_scripts.end()) return nullptr;
        return &it->second;
    }

    const ScriptComponent* World::GetScript(EntityId id) const
    {
        auto it = m_scripts.find(id);
        if (it == m_scripts.end()) return nullptr;
        return &it->second;
    }

    void World::RemoveScript(EntityId id)
    {
        auto it = m_scripts.find(id);
        if (it == m_scripts.end())
            return;

        if (it->second.instance)
        {
            it->second.instance->OnDisable();
            it->second.instance->OnDestroy();
        }
        m_scripts.erase(it);
    }

    MaterialComponent& World::AddMaterial(EntityId id,
                                          const DirectX::XMFLOAT3& color,
                                          const std::string& assetPath)
    {
        MaterialComponent& mat = m_materials[id];
        mat.color     = color;
        mat.assetPath = assetPath;
        return mat;
    }

    MaterialComponent* World::GetMaterial(EntityId id)
    {
        auto it = m_materials.find(id);
        if (it == m_materials.end())
            return nullptr;
        return &it->second;
    }

    const MaterialComponent* World::GetMaterial(EntityId id) const
    {
        auto it = m_materials.find(id);
        if (it == m_materials.end())
            return nullptr;
        return &it->second;
    }

    void World::RemoveMaterial(EntityId id)
    {
        m_materials.erase(id);
    }

    SkinnedMeshComponent& World::AddSkinnedMesh(EntityId id, const std::string& meshAssetPath)
    {
        SkinnedMeshComponent& comp = m_skinnedMeshes[id];
        comp.meshAssetPath = meshAssetPath;
        return comp;
    }

    SkinnedMeshComponent* World::GetSkinnedMesh(EntityId id)
    {
        auto it = m_skinnedMeshes.find(id);
        if (it == m_skinnedMeshes.end())
            return nullptr;
        return &it->second;
    }

    const SkinnedMeshComponent* World::GetSkinnedMesh(EntityId id) const
    {
        auto it = m_skinnedMeshes.find(id);
        if (it == m_skinnedMeshes.end())
            return nullptr;
        return &it->second;
    }

    void World::RemoveSkinnedMesh(EntityId id)
    {
        m_skinnedMeshes.erase(id);
    }

    SkinnedAnimationComponent& World::AddSkinnedAnimation(EntityId id)
    {
        return m_skinnedAnimations[id];
    }

    SkinnedAnimationComponent* World::GetSkinnedAnimation(EntityId id)
    {
        auto it = m_skinnedAnimations.find(id);
        if (it == m_skinnedAnimations.end())
            return nullptr;
        return &it->second;
    }

    const SkinnedAnimationComponent* World::GetSkinnedAnimation(EntityId id) const
    {
        auto it = m_skinnedAnimations.find(id);
        if (it == m_skinnedAnimations.end())
            return nullptr;
        return &it->second;
    }

    void World::RemoveSkinnedAnimation(EntityId id)
    {
        m_skinnedAnimations.erase(id);
    }
}


