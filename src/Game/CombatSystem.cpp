#include "Game/CombatSystem.h"

#include <algorithm>

#include "Core/World.h"
#include "Core/Logger.h"
#include "Components/HealthComponent.h"
#include "Game/CombatHitEvent.h"

namespace Alice
{
    void CombatSystem::Update(World& world, float dtSec)
    {
        if (dtSec <= 0.0f)
            return;

        for (auto&& [id, health] : world.GetComponents<HealthComponent>())
        {
            if (health.invulnRemaining > 0.0f)
            {
                health.invulnRemaining = std::max(0.0f, health.invulnRemaining - dtSec);
            }

            if (health.currentHealth <= 0.0f)
            {
                health.currentHealth = 0.0f;
                health.alive = false;
            }
        }
    }

    void CombatSystem::ProcessHits(World& world, const std::vector<CombatHitEvent>& hits)
    {
        for (const auto& hit : hits)
        {
            if (hit.victimOwner == InvalidEntityId)
                continue;

            auto* health = world.GetComponent<HealthComponent>(hit.victimOwner);
            if (!health || !health->alive)
                continue;

            if (health->invulnRemaining > 0.0f)
                continue;

            health->currentHealth -= hit.damage;
            if (health->currentHealth <= 0.0f)
            {
                health->currentHealth = 0.0f;
                health->alive = false;
            }

            if (health->invulnDuration > 0.0f)
            {
                health->invulnRemaining = health->invulnDuration;
            }

            if (hit.debugLog)
            {
                ALICE_LOG_INFO("[Combat] Hit attacker=%llu victim=%llu part=%u dmg=%.2f pos=(%.2f,%.2f,%.2f)",
                    static_cast<unsigned long long>(hit.attackerOwner),
                    static_cast<unsigned long long>(hit.victimOwner),
                    hit.part,
                    hit.damage,
                    hit.hitPosWS.x, hit.hitPosWS.y, hit.hitPosWS.z);
            }
        }
    }
}
