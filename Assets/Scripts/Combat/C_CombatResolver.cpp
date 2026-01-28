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
            const float staminaLoss = 10.0f;
            out.immediate.push_back({ CommandType::ConsumeStamina, CmdConsumeStamina{ victim.id, staminaLoss } });
            out.deferred.push_back({ CombatEventType::OnGuarded, victim.id, attacker.id, hit.attackInstanceId, staminaLoss });
            return out;
        }

        out.immediate.push_back({ CommandType::ApplyDamage, CmdApplyDamage{ victim.id, hit.damage } });
        out.immediate.push_back({ CommandType::ForceCancelAttack, CmdForceCancelAttack{ attacker.id } });
        out.immediate.push_back({ CommandType::DisableTrace, CmdDisableTrace{ attacker.id } });

        out.deferred.push_back({ CombatEventType::OnHit, victim.id, attacker.id, hit.attackInstanceId, hit.damage });
        return out;
    }
}
