#include "C_CombatResolver.h"

namespace Alice::Combat
{
    ResolveOutput CombatResolver::ResolveBatch(const std::vector<HitEvent>& hits,
                                               const FighterSnapshot& attackerSnap,
                                               const FighterSnapshot& victimSnap) const
    {
        ResolveOutput out{};
        for (const auto& h : hits)
        {
            auto one = ResolveOne(h, attackerSnap, victimSnap);
            out.immediate.insert(out.immediate.end(), one.immediate.begin(), one.immediate.end());
            out.deferred.insert(out.deferred.end(), one.deferred.begin(), one.deferred.end());
        }
        return out;
    }

    ResolveOutput CombatResolver::ResolveOne(const HitEvent& hit,
                                             const FighterSnapshot& attacker,
                                             const FighterSnapshot& victim) const
    {
        ResolveOutput out{};
        if (hit.victimOwner != victim.id)
            return out;

        if (victim.flags.invulnActive)
            return out;

        if (victim.flags.parryWindowActive && victim.targetInFront)
        {
            out.deferred.push_back({ CombatEventType::OnParried, victim.id, attacker.id, hit.attackInstanceId, 0.0f });
            return out;
        }

        if (victim.flags.guardActive && victim.targetInFront)
        {
            out.deferred.push_back({ CombatEventType::OnGuarded, victim.id, attacker.id, hit.attackInstanceId, 0.0f });
            return out;
        }

        out.deferred.push_back({ CombatEventType::OnHit, victim.id, attacker.id, hit.attackInstanceId, hit.damage });
        return out;
    }
}
