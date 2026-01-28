#pragma once

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"

#include "C_CombatContracts.h"

namespace Alice
{
    class C_BossBrain : public IScript
    {
        ALICE_BODY(C_BossBrain);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void OnDisable() override;

        Combat::Intent Think(float deltaTime, EntityId targetId);

        ALICE_PROPERTY(float, m_attackRange, 2.5f);
        ALICE_PROPERTY(float, m_attackCooldown, 1.0f);
        ALICE_PROPERTY(float, m_moveBias, 1.0f);

    private:
        float m_cooldownTimer = 0.0f;
    };
}
