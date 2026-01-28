#include "C_ActionFsm.h"

#include <cmath>

namespace Alice::Combat
{
    namespace
    {
        bool HasEvent(const std::vector<CombatEvent>& events, CombatEventType type)
        {
            for (const auto& ev : events)
                if (ev.type == type)
                    return true;
            return false;
        }

        float Abs(float v) { return (v < 0.0f) ? -v : v; }
    }

    void ActionFsm::Reset()
    {
        m_state = ActionState::Idle;
        m_stateTime = 0.0f;
    }

    void ActionFsm::Enter(ActionState next)
    {
        if (m_state != next)
        {
            m_state = next;
            m_stateTime = 0.0f;
        }
    }

    FsmOutput ActionFsm::Update(EntityId self,
                                const Intent& intent,
                                const Sensors& sensors,
                                const std::vector<CombatEvent>& events,
                                float dtSec)
    {
        FsmOutput out{};

        m_stateTime += dtSec;

        if (sensors.hp <= 0.0f || HasEvent(events, CombatEventType::OnDeath))
        {
            Enter(ActionState::Dead);
        }

        if (HasEvent(events, CombatEventType::OnHit) && m_state != ActionState::Dead)
        {
            Enter(ActionState::Hitstun);
        }

        if (m_state != ActionState::Dead && m_state != ActionState::Hitstun)
        {
            if (intent.dodgePressed && sensors.stamina >= 10.0f)
            {
                Enter(ActionState::Dodge);
            }
            else if (intent.guardHeld)
            {
                if (m_state != ActionState::Guard)
                {
                    Enter(ActionState::Guard);
                }
            }
            else if (intent.attackPressed && sensors.stamina >= 15.0f)
            {
                Enter(ActionState::Attack);
            }
            else
            {
                const bool hasMove = (Abs(intent.move.x) + Abs(intent.move.y)) > 0.001f;
                if (hasMove)
                {
                    Enter(ActionState::Move);
                    out.commands.push_back({ CommandType::RequestMove, CmdRequestMove{ self, intent.move, sensors.moveSpeed, true, true } });
                }
                else
                {
                    Enter(ActionState::Idle);
                    out.commands.push_back({ CommandType::RequestMove, CmdRequestMove{ self, {0.0f, 0.0f}, 0.0f, true, false } });
                }
            }
        }

        ActionFlags flags{};
        flags.hitActive = sensors.attackWindowActive;
        flags.guardActive = sensors.guardWindowActive;
        flags.invulnActive = sensors.dodgeWindowActive || sensors.invulnActive;
        flags.parryWindowActive = false;
        flags.canBeInterrupted = (m_state != ActionState::Dodge);

        if (m_state == ActionState::Hitstun)
        {
            flags.canBeInterrupted = false;
            if (m_stateTime > 0.4f)
                Enter(ActionState::Idle);
        }

        out.state = m_state;
        out.flags = flags;
        return out;
    }
}
