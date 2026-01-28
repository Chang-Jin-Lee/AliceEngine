#pragma once

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"
#include "C_CombatContracts.h"

#include <unordered_map>

namespace Alice
{
    namespace Combat { class CombatEventBus; class Fighter; }
    class World;

    class C_CombatTraceBridge : public IScript
    {
        ALICE_BODY(C_CombatTraceBridge);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void OnDisable() override;

        void Dispatch(const std::vector<Combat::Command>& cmds);
        void DrainHits(World& world,
                       const std::unordered_map<Combat::EntityId, Combat::Fighter*>& fighters,
                       Combat::CombatEventBus& bus,
                       float defaultDamage,
                       bool overrideDamage);

        ALICE_PROPERTY(bool, m_useHealthComponentHits, true);

    private:
        std::vector<Combat::HitEvent> m_frameHits;
    };
}
