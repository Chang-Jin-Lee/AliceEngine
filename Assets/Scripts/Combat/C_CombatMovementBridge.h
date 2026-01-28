#pragma once

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"
#include "C_CombatContracts.h"

#include <string>

namespace Alice
{
    class C_CombatMovementBridge : public IScript
    {
        ALICE_BODY(C_CombatMovementBridge);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void OnDisable() override;

        void Dispatch(const std::vector<Combat::Command>& cmds);

        ALICE_PROPERTY(std::string, m_cameraName, "MainCamera");
        ALICE_PROPERTY(float, m_rotationOffsetDeg, 180.0f);
    };
}
