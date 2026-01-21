#pragma once

#include <string>

#include <DirectXMath.h>

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"

namespace Alice
{
    class CharacterAnimatorComponent : public IScript
    {
        ALICE_BODY(CharacterAnimatorComponent);

    public:
        void Update(float DeltaTime) override;

        // --- Movement settings ---
        ALICE_PROPERTY(float, m_moveSpeed, 10.0f);
        ALICE_PROPERTY(float, m_runMultiplier, 1.6f);
        ALICE_PROPERTY(float, m_jumpSpeed, 6.5f);
        ALICE_PROPERTY(float, m_gravity, 18.0f);
        ALICE_PROPERTY(float, m_blendSpeed, 8.0f);

        // --- Base layer clips ---
        ALICE_PROPERTY(std::string, m_idleClip, "Idle");
        ALICE_PROPERTY(std::string, m_walkClip, "Walk");
        ALICE_PROPERTY(std::string, m_runClip, "Run");

        // --- Upper layer clips ---
        ALICE_PROPERTY(bool, m_enableUpperLayer, false);
        ALICE_PROPERTY(std::string, m_upperClip, "Aim");

        // --- Additive clips ---
        ALICE_PROPERTY(bool, m_enableAdditive, false);
        ALICE_PROPERTY(std::string, m_additiveClip, "Recoil");
        ALICE_PROPERTY(std::string, m_additiveRefClip, "Idle");
        ALICE_PROPERTY(float, m_additiveDuration, 0.25f);

        // --- Socket setup ---
        ALICE_PROPERTY(std::string, m_socketName, "WeaponPoint");
        ALICE_PROPERTY(std::string, m_socketParentBone, "Hand_R");
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_socketPos, DirectX::XMFLOAT3(0.1f, 0.05f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_socketRotDeg, DirectX::XMFLOAT3(0.0f, 90.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_socketScale, DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));

        // --- IK settings ---
        ALICE_PROPERTY(bool, m_enableIK, false);
        ALICE_PROPERTY(std::string, m_ikTipBone, "Hand_L");
        ALICE_PROPERTY(int, m_ikChainLength, 3);
        ALICE_PROPERTY(float, m_ikWeight, 1.0f);
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_ikTargetLocal, DirectX::XMFLOAT3(0.0f, 1.2f, 0.2f));

        // --- Aim (optional) ---
        ALICE_PROPERTY(bool, m_enableAim, false);
        ALICE_PROPERTY(float, m_aimYawDeg, 0.0f);
        ALICE_PROPERTY(float, m_aimWeight, 1.0f);

    private:
        float m_velY = 0.0f;
        float m_moveBlend = 0.0f;
        float m_additiveTimer = -1.0f;
        bool m_socketInitialized = false;
        std::string m_lastMoveClip;
    };
}

