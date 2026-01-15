#pragma once

#include "Core/Scene.h"

namespace Alice
{
    // 물리 테스트 할려고 씬 복붙햇슴

    /// 가장 기본이 되는 테스트용 씬입니다.
    /// - 큐브 엔티티 하나를 생성하고
    /// - 시간에 따라 Y축으로 회전시킵니다.
    class PhysXTestScene : public IScene
    {
    public:
        PhysXTestScene() = default;
        ~PhysXTestScene() override = default;

        const char* GetName() const override { return "PhysXTestScene"; }

        void OnEnter(World& world, ResourceManager& resources) override;
        void OnExit(World& world, ResourceManager& resources) override;
        void Update(World& world, ResourceManager& resources, float deltaTime) override;

        EntityId GetPrimaryRenderableEntity() const override { return m_cubeEntity; }

    private:
        EntityId m_cubeEntity { InvalidEntityId };
        float    m_rotationSpeed = 1.0f; // rad/sec
    };
}



