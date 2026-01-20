#pragma once

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"

namespace Alice
{
    /// 코드 기반 상태 머신 + 고급 애니메이션 예시
    class AdvancedAnimControllerExample : public IScript
    {
        ALICE_BODY(AdvancedAnimControllerExample);

    public:
        void Start() override;
        void Update(float deltaTime) override;

    public:
        // ===== Base Locomotion =====
        ALICE_PROPERTY(std::string, idleClip, std::string("Idle"));
        ALICE_PROPERTY(std::string, walkClip, std::string("Walk"));
        ALICE_PROPERTY(std::string, runClip, std::string("Run"));
        ALICE_PROPERTY(std::string, jumpClip, std::string("Jump"));
        ALICE_PROPERTY(float, jumpDuration, 0.6f);

        // ===== Upper Body =====
        ALICE_PROPERTY(bool, enableUpper, true);
        ALICE_PROPERTY(std::string, upperAimClip, std::string("Aim_Upper"));
        ALICE_PROPERTY(std::string, upperShootClip, std::string("Shoot_Upper"));
        ALICE_PROPERTY(float, upperAlpha, 1.0f);

        // ===== Additive =====
        ALICE_PROPERTY(bool, enableAdditive, true);
        ALICE_PROPERTY(std::string, additiveClip, std::string("Recoil_Add"));
        ALICE_PROPERTY(std::string, additiveRefClip, std::string("Idle"));
        ALICE_PROPERTY(float, additiveWeight, 1.0f);
        ALICE_PROPERTY(float, additiveDuration, 0.2f);

        // ===== IK =====
        ALICE_PROPERTY(bool, enableIK, false);
        ALICE_PROPERTY(std::string, ikTipBone, std::string("Hand_L"));
        ALICE_PROPERTY(int, ikChainLength, 3);
        ALICE_PROPERTY(std::string, ikTargetName, std::string("IKTarget"));
        ALICE_PROPERTY(int, ikIterations, 8);

    private:
        enum class State
        {
            Idle,
            Move,
            Jump
        };

        State m_state = State::Idle;
        float m_jumpTimer = 0.0f;
        float m_fireTimer = 0.0f;
    };
}

