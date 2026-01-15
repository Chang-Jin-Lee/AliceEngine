#include "Scenes/PhysXTestScene.h"

#include <DirectXMath.h>

#include "Core/ResourceManager.h"

namespace Alice
{
    using namespace DirectX;

    void PhysXTestScene::OnEnter(World& world, ResourceManager& /*resources*/)
    {
        // 큐브 엔티티 생성 및 기본 Transform 설정
        m_cubeEntity = world.CreateEntity();
        auto& transform = world.AddComponent<TransformComponent>(m_cubeEntity);
        transform
            .SetPosition(0.0f, 0.0f, 0.0f)
            .SetScale(1.0f, 1.0f, 1.0f);
    }

    void PhysXTestScene::OnExit(World& world, ResourceManager& /*resources*/)
    {
        if (m_cubeEntity != InvalidEntityId)
        {
            world.DestroyEntity(m_cubeEntity);
            m_cubeEntity = InvalidEntityId;
        }
    }

    void PhysXTestScene::Update(World& world, ResourceManager& /*resources*/, float deltaTime)
    {
        if (m_cubeEntity == InvalidEntityId) return;

        auto* transform = world.GetComponent<TransformComponent>(m_cubeEntity);
        if (!transform) return;

        // 시간에 따라 Y축 회전
        transform->rotation.y += m_rotationSpeed * deltaTime;
    }

    // 이 씬을 리플렉션/팩토리 시스템에 등록합니다.
    REGISTER_SCENE(PhysXTestScene);
}



