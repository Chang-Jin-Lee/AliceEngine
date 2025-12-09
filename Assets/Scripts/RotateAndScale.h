#pragma once

#include "Core/Script.h"

namespace Alice
{
    // 예제용 스크립트:
    // - OnCreate: 기준 스케일을 기억합니다.
    // - OnUpdate: Y축으로 천천히 회전시키고, 시간에 따라 스케일이 살짝 커졌다/작아졌다 합니다.
    class RotateAndScale : public IScript
    {
    public:
        const char* GetName() const override { return "RotateAndScale"; }

        void OnCreate(World& world, EntityId entity) override;
        void OnUpdate(World& world, EntityId entity, float deltaTime) override;

    private:
        float m_timeSeconds = 0.0f; // 누적 시간
        float m_baseScale   = 1.0f; // 기준 스케일 (OnCreate 시점의 scale.x)
    };
}
