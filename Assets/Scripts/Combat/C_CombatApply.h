#pragma once

#include <unordered_map>

#include "C_CombatContracts.h"

namespace Alice
{
    class World;
}

namespace Alice::Combat
{
    class CombatEventBus;
    class Fighter;

    class CombatApply
    {
    public:
        void ApplyImmediate(World& world,
                            std::unordered_map<EntityId, Fighter*>& fighters,
                            CombatEventBus& bus,
                            const std::vector<Command>& cmds,
                            bool skipDamage);
    };
}
