#pragma once

#include "C_CombatContracts.h"

namespace Alice
{
    class World;
}

namespace Alice::Combat
{
    struct FighterSnapshot
    {
        EntityId id = InvalidEntityId;
        Team team = Team::Player;
        ActionState state = ActionState::Idle;
        ActionFlags flags{};
        float hp = 100.0f;
        float stamina = 100.0f;
        bool targetInFront = true;
    };

    class Fighter
    {
    public:
        EntityId id = InvalidEntityId;
        Team team = Team::Player;

        float hp = 100.0f;
        float stamina = 100.0f;
        float moveSpeed = 5.0f;

        ActionState state = ActionState::Idle;
        ActionFlags flags{};

        bool lastTargetInFront = true;

        Sensors BuildSensors(World& world, EntityId targetId, float dt);
        FighterSnapshot Snapshot() const;
    };
}
