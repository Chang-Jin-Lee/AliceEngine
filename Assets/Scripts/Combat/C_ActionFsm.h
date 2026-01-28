#pragma once

#include <vector>

#include "C_CombatContracts.h"
#include "C_ActionData.h"

namespace Alice::Combat
{
    class ActionFsm
    {
    public:
        explicit ActionFsm(const ActionDatabase* db = nullptr) : m_db(db) {}

        void Reset();

        FsmOutput Update(EntityId self,
                         const Intent& intent,
                         const Sensors& sensors,
                         const std::vector<CombatEvent>& events,
                         float dtSec);

        ActionState State() const { return m_state; }
        float StateTime() const { return m_stateTime; }

    private:
        const ActionDatabase* m_db = nullptr;
        ActionState m_state = ActionState::Idle;
        float m_stateTime = 0.0f;

        void Enter(ActionState next);
    };
}
