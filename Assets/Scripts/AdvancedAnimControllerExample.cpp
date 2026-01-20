#include "AdvancedAnimControllerExample.h"

#include "Core/ScriptFactory.h"
#include "Core/GameObject.h"
#include "Core/Logger.h"
#include "Components/AdvancedAnimComponent.h"
#include "Components/TransformComponent.h"
#include <algorithm>

namespace Alice
{
    REGISTER_SCRIPT(AdvancedAnimControllerExample);

    void AdvancedAnimControllerExample::Start()
    {
        // 컴포넌트가 없으면 자동 추가
        auto go = gameObject();
        if (!go.IsValid())
            return;

        if (!go.GetComponent<AdvancedAnimComponent>())
            go.AddComponent<AdvancedAnimComponent>();
    }

    void AdvancedAnimControllerExample::Update(float deltaTime)
    {
        auto* input = Input();
        auto go = gameObject();
        if (!input || !go.IsValid())
            return;

        auto* anim = go.GetComponent<AdvancedAnimComponent>();
        if (!anim)
            return;

        // ===== 입력 수집 =====
        float inputX = 0.0f;
        float inputZ = 0.0f;
        if (input->GetKey(KeyCode::W)) inputZ += 1.0f;
        if (input->GetKey(KeyCode::S)) inputZ -= 1.0f;
        if (input->GetKey(KeyCode::D)) inputX += 1.0f;
        if (input->GetKey(KeyCode::A)) inputX -= 1.0f;

        const bool moving = (inputX != 0.0f || inputZ != 0.0f);
        const bool running = input->GetKey(KeyCode::LeftShift) || input->GetKey(KeyCode::RightShift);
        const bool jumpPressed = input->GetKeyDown(KeyCode::Space);

        // ===== 간단 상태 머신 =====
        if (jumpPressed)
        {
            m_state = State::Jump;
            m_jumpTimer = jumpDuration;
        }
        else if (m_state == State::Jump)
        {
            m_jumpTimer -= deltaTime;
            if (m_jumpTimer <= 0.0f)
            {
                m_state = moving ? State::Move : State::Idle;
            }
        }
        else
        {
            m_state = moving ? State::Move : State::Idle;
        }

        // ===== Base Layer 세팅 =====
        anim->base.enabled = true;
        if (m_state == State::Idle)
        {
            anim->base.clipA = idleClip;
            anim->base.clipB.clear();
            anim->base.blend01 = 0.0f;
        }
        else if (m_state == State::Move)
        {
            anim->base.clipA = walkClip;
            anim->base.clipB = runClip;
            anim->base.blend01 = running ? 1.0f : 0.0f;
        }
        else // Jump
        {
            anim->base.clipA = jumpClip;
            anim->base.clipB.clear();
            anim->base.blend01 = 0.0f;
        }

        // ===== Upper Layer (Aim/Shoot) =====
        const bool aim = input->GetMouseButton(MouseCode::Right);
        const bool fire = input->GetMouseButtonDown(MouseCode::Left);

        if (fire)
            m_fireTimer = additiveDuration;
        m_fireTimer = std::max(0.0f, m_fireTimer - deltaTime);

        anim->upper.enabled = enableUpper && aim;
        anim->upper.clipA = upperAimClip;
        anim->upper.clipB = upperShootClip;
        anim->upper.blend01 = (m_fireTimer > 0.0f) ? 1.0f : 0.0f;
        anim->upper.alpha = upperAlpha;

        // ===== Additive (Recoil) =====
        anim->additive.enabled = enableAdditive;
        anim->additive.clip = additiveClip;
        anim->additive.refClip = additiveRefClip;
        anim->additive.weight = (m_fireTimer > 0.0f) ? additiveWeight : 0.0f;

        // ===== IK =====
        anim->ik.enabled = enableIK;
        anim->ik.tipBone = ikTipBone;
        anim->ik.chainLength = ikChainLength;
        anim->ik.iterations = ikIterations;

        // 타겟 엔티티가 있으면 그 위치를 사용
        if (enableIK && !ikTargetName.empty())
        {
            auto target = GetWorld()->FindGameObject(ikTargetName);
            if (target.IsValid())
            {
                if (auto* t = target.GetComponent<TransformComponent>())
                    anim->ik.targetWorld = t->position;
            }
        }
    }
}

