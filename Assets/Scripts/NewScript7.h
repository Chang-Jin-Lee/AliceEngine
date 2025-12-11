#pragma once

#include "Core/Script.h"

namespace Alice
{
    // 간단한 예제 스크립트입니다. 필요에 맞게 수정해서 사용하세요.
    class NewScript7 : public IScript
    {
    public:
        const char* GetName() const override { return "NewScript7"; }

        void OnCreate(World& world, EntityId entity) override;
        void OnUpdate(World& world, EntityId entity, float deltaTime) override;
    };
}
