#pragma once

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"
#include "Core/Entity.h"
#include <memory>

namespace Alice
{
    class C_CombatSession : public IScript
    {
        ALICE_BODY(C_CombatSession);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void PostCombatUpdate(float deltaTime) override;
        void OnEnable() override;
        void OnDisable() override;

        ALICE_PROPERTY(uint64_t, m_playerGuid, 0);
        ALICE_PROPERTY(uint64_t, m_bossGuid, 0);
        ALICE_PROPERTY(float, m_defaultDamage, 10.0f);
        ALICE_PROPERTY(bool, m_overrideWeaponTraceDamage, true);
        ALICE_PROPERTY(bool, m_applyDamageInScript, true);
        ALICE_PROPERTY(bool, m_enableLogs, false);

        void ForceReset();
        ALICE_FUNC(ForceReset);

    private:
        EntityId ResolveEntity(uint64_t guid) const;
        EntityId ResolveTraceEntity(EntityId ownerId) const;
        void OverrideWeaponTraceDamage(EntityId ownerId) const;

        struct SessionState;
        std::unique_ptr<SessionState> m_state;
    };
}
