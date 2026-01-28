#pragma once

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"
#include "C_CombatContracts.h"

namespace Alice
{
    class C_CombatAnimBridge : public IScript
    {
        ALICE_BODY(C_CombatAnimBridge);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void OnDisable() override;

        void Dispatch(const std::vector<Combat::Command>& cmds);
    };
}
