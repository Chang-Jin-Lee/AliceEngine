#pragma once

#include "C_CombatContracts.h"
#include "C_Fighter.h"

namespace Alice::Combat
{
    class CombatResolver
    {
    public:
        ResolveOutput ResolveBatch(const std::vector<HitEvent>& hits,
                                   const FighterSnapshot& attackerSnap,
                                   const FighterSnapshot& victimSnap) const;

        ResolveOutput ResolveOne(const HitEvent& hit,
                                 const FighterSnapshot& attacker,
                                 const FighterSnapshot& victim) const;
    };
}
