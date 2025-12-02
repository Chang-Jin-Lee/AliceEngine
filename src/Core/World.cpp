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
        
        // 현재는 Transform 컴포넌트만 관리합니다.
        m_transforms.erase(id);
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
}


