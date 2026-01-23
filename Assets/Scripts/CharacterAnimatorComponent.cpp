#include "CharacterAnimatorComponent.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "Core/ScriptFactory.h"
#include "Core/GameObject.h"
#include "Core/InputTypes.h"
#include "Core/Logger.h"
#include "Components/AdvancedAnimationComponent.h"
#include "Components/TransformComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(CharacterAnimatorComponent);

    namespace
    {
        float SmoothApproach(float current, float target, float speed, float dt)
        {
            float t = std::clamp(speed * dt, 0.0f, 1.0f);
            return current + (target - current) * t;
        }
    }

    void CharacterAnimatorComponent::OnAttackHit()
    {
        ALICE_LOG_INFO("SWOOSH! Attack Hit!");
    }

    void CharacterAnimatorComponent::OnCrouchHalfway()
    {
        ALICE_LOG_INFO("Body is half-crouched! (Time: 0.5s)");
    }

    void CharacterAnimatorComponent::Update(float DeltaTime)
    {
        auto* input = Input();
        auto go = gameObject();
        if (!input || !go.IsValid()) return;

        auto* t = go.GetComponent<TransformComponent>();
        if (!t) return;

        auto* anim = go.GetComponent<AdvancedAnimationComponent>();
        if (!anim) anim = &go.AddComponent<AdvancedAnimationComponent>();

        // [초기화] 노티파이 바인딩
        if (!m_notifyRegistered)
        {
            anim->AddNotify(Get_m_attackClip(), Get_m_attackHitTime(),
                std::bind(&CharacterAnimatorComponent::OnAttackHit, this));
            m_notifyRegistered = true;
        }

        // ------------------------------------------------------------
        // 0. 애니메이션 속도 제어
        // ------------------------------------------------------------
        m_animSpeed = 1.0f;
        if (input->GetKey(KeyCode::Alpha2)) m_animSpeed = 2.0f;
        else if (input->GetKey(KeyCode::Alpha3)) m_animSpeed = 3.0f;
        else if (input->GetKey(KeyCode::Alpha4)) m_animSpeed = 0.5f;
        else if (input->GetKey(KeyCode::Alpha5)) m_animSpeed = 0.25f;

        // ------------------------------------------------------------
        // 1. 상태 변경 입력 (Z키 공격)
        // ------------------------------------------------------------
        if (input->GetKeyDown(KeyCode::Z) && m_state == CharState::Standing)
        {
            m_state = CharState::Attacking;
            m_currentAttackTime = 0.0f;
        }

        // ------------------------------------------------------------
        // 2. 앉기/서기 전환 및 즉시 취소 (8번 키)
        // ------------------------------------------------------------
        bool toggleCrouch = false;
        bool useStretch = false;
        bool cancelCrouch = false; // 8번 키: 즉시 취소 (역재생)

        if (input->GetKeyDown(KeyCode::LeftCtrl)) { toggleCrouch = true; useStretch = false; }
        else if (input->GetKeyDown(KeyCode::Alpha6)) { toggleCrouch = true; useStretch = true; }
        else if (input->GetKeyDown(KeyCode::Alpha8)) { cancelCrouch = true; } // 8번 키 입력 감지

        // [8번 키 로직] 앉는 도중(Crouching)이라면 즉시 StandingUp 상태로 전환 (현재 시간 유지 -> 역재생)
        if (cancelCrouch && m_state == CharState::Crouching)
        {
            m_state = CharState::StandingUp;
            // m_currentCrouchTime은 그대로 유지되므로, 현재 시점부터 시간이 줄어들며 역재생됨
            ALICE_LOG_INFO("Crouch Cancelled! Reversing...");
        }
        else if (toggleCrouch)
        {
            if (m_state == CharState::Standing)
            {
                m_state = CharState::Crouching; m_currentCrouchTime = 0.0f; m_isStretchedMode = useStretch;
                anim->notifies.clear();
                anim->AddNotify(Get_m_crouchClip(), 0.5f, std::bind(&CharacterAnimatorComponent::OnCrouchHalfway, this));
            }
            else if (m_state == CharState::Crouched)
            {
                m_state = CharState::StandingUp; m_currentCrouchTime = Get_m_crouchDuration(); m_isStretchedMode = useStretch;
            }
        }

        // ------------------------------------------------------------
        // 3. 이동 및 회전 로직 (Standing일 때만 가능)
        // ------------------------------------------------------------
        bool isMoving = false;
        if (m_state == CharState::Standing)
        {
            float inputX = 0.0f, inputZ = 0.0f;
            if (input->GetKey(KeyCode::W)) inputZ += 1.0f;
            if (input->GetKey(KeyCode::S)) inputZ -= 1.0f;
            if (input->GetKey(KeyCode::D)) inputX += 1.0f;
            if (input->GetKey(KeyCode::A)) inputX -= 1.0f;

            isMoving = (inputX != 0.0f || inputZ != 0.0f);

            auto mainCamObj = GetWorld()->FindGameObject("MainCamera");
            float moveX = inputX, moveZ = inputZ;

            if (isMoving && mainCamObj.IsValid())
            {
                auto* camT = mainCamObj.GetComponent<TransformComponent>();
                if (camT)
                {
                    float fwdX = t->position.x - camT->position.x;
                    float fwdZ = t->position.z - camT->position.z;
                    float len = std::sqrt(fwdX * fwdX + fwdZ * fwdZ);
                    if (len > 0.0001f) { fwdX /= len; fwdZ /= len; }
                    moveX = (fwdX * inputZ) + (fwdZ * inputX);
                    moveZ = (fwdZ * inputZ) - (fwdX * inputX);
                }
            }

            float moveLen = std::sqrt(moveX * moveX + moveZ * moveZ);
            if (moveLen > 0.0001f)
            {
                moveX /= moveLen; moveZ /= moveLen;
                const float speed = Get_m_moveSpeed() * (input->GetKey(KeyCode::LeftShift) ? Get_m_runMultiplier() : 1.0f);
                t->position.x += moveX * speed * DeltaTime;
                t->position.z += moveZ * speed * DeltaTime;
                t->SetRotation(0.0f, std::atan2(moveX, moveZ) * 57.2958f + 180.0f, 0.0f);
            }

            if (t->position.y <= 0.0f) { t->position.y = 0.0f; if (m_velY < 0.0f) m_velY = 0.0f; if (input->GetKeyDown(KeyCode::Space)) m_velY = Get_m_jumpSpeed(); }
            m_velY -= Get_m_gravity() * DeltaTime; t->position.y += m_velY * DeltaTime; if (t->position.y < 0.0f) t->position.y = 0.0f;
        }

        // ------------------------------------------------------------
        // 4. 애니메이션 상태 머신
        // ------------------------------------------------------------
        anim->enabled = true; anim->playing = true; anim->base.enabled = true;

        if (m_state == CharState::Standing)
        {
            m_moveBlend = SmoothApproach(m_moveBlend, isMoving ? 1.0f : 0.0f, Get_m_blendSpeed(), DeltaTime);
            anim->base.autoAdvance = true;
            anim->base.clipA = Get_m_idleClip();
            anim->base.clipB = input->GetKey(KeyCode::LeftShift) ? Get_m_runClip() : Get_m_walkClip();
            anim->base.blend01 = m_moveBlend;
            anim->base.speedA = m_animSpeed; anim->base.speedB = m_animSpeed;
        }
        else if (m_state == CharState::Attacking)
        {
            m_currentAttackTime += DeltaTime * m_animSpeed;
            anim->base.autoAdvance = true; anim->base.loopA = false;
            anim->base.clipA = Get_m_attackClip(); anim->base.clipB = Get_m_attackClip();
            anim->base.speedA = m_animSpeed;

            if (anim->base.timeA >= Get_m_attackDuration() || m_currentAttackTime >= Get_m_attackDuration())
            {
                m_state = CharState::Standing; anim->base.timeA = 0.0f;
            }
        }
        else
        {
            // 앉기 관련 상태 (Crouching, Crouched, StandingUp)
            float prevTime = m_currentCrouchTime;
            float stepSpeed = m_animSpeed;
            if (m_isStretchedMode) { if (m_currentCrouchTime >= 0.01f && m_currentCrouchTime < 0.5f) stepSpeed = 1.0f / 3.0f; }

            if (m_state == CharState::Crouching) {
                m_currentCrouchTime += DeltaTime * stepSpeed;
                anim->CheckAndFireNotifies(Get_m_crouchClip(), prevTime, m_currentCrouchTime);
                if (m_currentCrouchTime >= Get_m_crouchDuration()) { m_currentCrouchTime = Get_m_crouchDuration(); m_state = CharState::Crouched; }
            }
            else if (m_state == CharState::StandingUp) {
                m_currentCrouchTime -= DeltaTime * stepSpeed;
                anim->CheckAndFireNotifies(Get_m_crouchClip(), prevTime, m_currentCrouchTime);
                if (m_currentCrouchTime <= 0.0f) { m_currentCrouchTime = 0.0f; m_state = CharState::Standing; }
            }
            anim->base.autoAdvance = false;
            anim->base.clipA = Get_m_crouchClip(); anim->base.clipB = Get_m_crouchClip();
            anim->base.timeA = m_currentCrouchTime; anim->base.timeB = m_currentCrouchTime;
            anim->base.speedA = stepSpeed;
        }

        // ------------------------------------------------------------
        // 5. 기타 설정 (Additive, Upper, Socket, IK)
        // ------------------------------------------------------------
        anim->additive.enabled = false; // Additive 비활성화 (Z키 공격 사용)

        anim->upper.enabled = Get_m_enableUpperLayer() && (m_state == CharState::Standing);
        anim->upper.clipA = Get_m_upperClip(); anim->upper.speedA = m_animSpeed;

        if (!m_socketInitialized) {
            anim->SetSocketSRT(Get_m_socketName(), Get_m_socketParentBone(), Get_m_socketPos(), Get_m_socketRotDeg(), Get_m_socketScale());
            m_socketInitialized = true;
        }

        if (Get_m_enableFootIK()) {
            float th = input->GetKey(KeyCode::Y) ? Get_m_maxLiftHeight() : 0.0f;
            m_currentLeftFootHeight = SmoothApproach(m_currentLeftFootHeight, th, Get_m_ikLiftSpeed(), DeltaTime);
            DirectX::XMFLOAT3 tp = Get_m_leftFootBasePos(); tp.y = m_currentLeftFootHeight;
            anim->SetIK(0, Get_m_leftFootBone(), 2, tp, 1.0f);
        }
        else anim->DisableIK(0);

        // ------------------------------------------------------------
        // 7. Weapon Attachment Logic (I key)
        // ------------------------------------------------------------
        if (input->GetKeyDown(KeyCode::I))
        {
            m_weaponGo = GetWorld()->FindGameObject(Get_m_weaponObjName());
            if (m_weaponGo.IsValid())
            {
                m_isWeaponAttached = !m_isWeaponAttached;
                ALICE_LOG_INFO(m_isWeaponAttached ? "Weapon Attached!" : "Weapon Detached!");
            }
        }

        if (m_isWeaponAttached && m_weaponGo.IsValid())
        {
            auto* weaponT = m_weaponGo.GetComponent<TransformComponent>();
            if (weaponT)
            {
                DirectX::XMFLOAT3 sPos, sRot;
                if (anim->GetSocketWorldTransform(Get_m_socketName(), sPos, sRot))
                {
                    weaponT->position = sPos;
                    weaponT->SetRotation(sRot.x, sRot.y, sRot.z);
                }
            }
        }
    }
}