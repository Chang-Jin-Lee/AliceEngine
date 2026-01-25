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

        bool alive = true;
        uint32_t teamId = 0;
    };
}
