#pragma once

#include <string>

#include <DirectXMath.h>

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"

namespace Alice
{
    // 캐릭터의 동작 상태 정의
    enum class CharState
    {
        Standing,    // 서기
        Crouching,   // 앉는 중
        Crouched,    // 앉음
        StandingUp,  // 일어서는 중
        Attacking    // 공격 중
    };

    class CharacterAnimatorComponent : public IScript
    {
        ALICE_BODY(CharacterAnimatorComponent);

    public:
        void Update(float DeltaTime) override;

        // 노티파이에서 호출될 함수 (리플렉션)
        void OnAttackHit();
        void OnCrouchHalfway(); // 앉기 애니메이션 중간에 호출되는 함수

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

        // --- Crouch clips ---
        ALICE_PROPERTY(std::string, m_crouchClip, "CrouchDown");        // 앉는 동작 (끝나면 멈춤)
        ALICE_PROPERTY(std::string, m_crouchFireClip, "CrouchFire");    // 앉아서 사격 (Additive)
        ALICE_PROPERTY(float, m_crouchDuration, 1.0f);                  // 앉기 애니메이션 길이 (수동 제어용)

        // --- Attack montage clips ---
        ALICE_PROPERTY(std::string, m_attackClip, "Attack01");          // 공격 몽타주 클립
        ALICE_PROPERTY(float, m_attackDuration, 1.5f);                 // 공격 애니메이션 길이
        ALICE_PROPERTY(float, m_attackHitTime, 0.7f);                   // 타격 판정 시간 (Notify 발생 지점)

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

        // --- Foot IK settings (발 지형 적응) ---
        ALICE_PROPERTY(bool, m_enableFootIK, true);
        ALICE_PROPERTY(std::string, m_leftFootBone, "Ball_L"); // 또는 Foot_L
        ALICE_PROPERTY(float, m_ikLiftSpeed, 5.0f);            // 발 드는 속도 (보간 속도)
        ALICE_PROPERTY(float, m_maxLiftHeight, 0.5f);          // Y키 눌렀을 때 목표 높이
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_leftFootBasePos, DirectX::XMFLOAT3(-0.2f, 0.0f, 0.1f)); // 왼발 기본 위치 (모델 공간)

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

        // 상태 관리 변수
        CharState m_state = CharState::Standing;
        float m_currentCrouchTime = 0.0f; // 앉기 애니메이션 현재 시간 수동 제어
        std::string m_currentFireClip;    // 현재 발동된 사격 클립 저장

        // 노티파이 등록 여부 체크
        bool m_notifyRegistered = false;
        float m_currentAttackTime = 0.0f;

        // Foot IK 런타임 변수
        float m_currentLeftFootHeight = 0.0f; // 현재 발 높이 (보간용)
    };
}

