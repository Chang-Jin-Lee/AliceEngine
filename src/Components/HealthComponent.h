#pragma once

#include <cstdint>

namespace Alice
{
    struct HealthComponent
    {
        float maxHealth = 100.0f;
        float currentHealth = 100.0f;

        float invulnDuration = 0.0f;
        float invulnRemaining = 0.0f;

        // 프레임 기반 상태 (AttackDriver가 갱신)
        bool dodgeActive = false;
        bool guardActive = false;

        // Guard 시 데미지 배율 (0.5 = 50% 피해)
        float guardDamageScale = 0.5f;

        bool alive = true;
        uint32_t teamId = 0;
    };
}
