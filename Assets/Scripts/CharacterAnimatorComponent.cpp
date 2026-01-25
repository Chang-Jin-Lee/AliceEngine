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

            // [초기화] 공격 시작 변수 리셋
            m_isAttackReversing = false;
            m_currentAttackTime = 0.0f;

            // [블렌딩] Idle -> Attack 전환을 위한 타이머 리셋
            m_blendTimer = 0.0f;

            // 실제 애니메이션 시간 리셋 (Attack 클립)
            // 시스템상 clipB가 바뀔 때 timeB를 리셋해주는 게 안전하므로 
            // 아래 Attacking 로직에서 clipB가 할당될 때 0부터 시작하도록 유도합니다.
            if (anim)
            {
                anim->base.timeB = 0.0f; // B슬롯(Attack) 시간 0으로
                // A슬롯(Idle) 시간은 유지 (자연스러운 블렌딩을 위해)
            }
        }

        // ------------------------------------------------------------
        // 2. 앉기/서기 전환 (8번 키 취소 포함)
        // ------------------------------------------------------------
        bool toggleCrouch = false;
        bool useStretch = false;
        bool cancelCrouch = false;

        if (input->GetKeyDown(KeyCode::LeftCtrl)) { toggleCrouch = true; useStretch = false; }
        else if (input->GetKeyDown(KeyCode::Alpha6)) { toggleCrouch = true; useStretch = true; }
        else if (input->GetKeyDown(KeyCode::Alpha8)) { cancelCrouch = true; }

        if (cancelCrouch && m_state == CharState::Crouching)
        {
            m_state = CharState::StandingUp;
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
            // [블렌딩 로직: Attack -> Idle 복귀]
            // 공격 역재생이 끝난 직후, '공격 0초 자세'에서 'Idle 자세'로 부드럽게 섞어줍니다.
            if (m_isBlendingOut)
            {
                m_blendTimer += DeltaTime;
                float blendAlpha = std::clamp(m_blendTimer / Get_m_transitionDuration(), 0.0f, 1.0f);

                // clipA: Attack (멈춤), clipB: Idle (재생)
                // blendAlpha 0 -> 1 로 가면서 Idle이 100%가 됨
                anim->base.clipA = Get_m_attackClip();
                anim->base.clipB = Get_m_idleClip();
                anim->base.blend01 = blendAlpha;

                anim->base.speedA = 0.0f; // 공격 자세는 0초 프레임 고정
                anim->base.timeA = 0.0f;
                anim->base.speedB = m_animSpeed;

                // 블렌딩 완료 시 일반 Standing 로직으로 복귀
                if (blendAlpha >= 1.0f)
                {
                    m_isBlendingOut = false;
                }
            }
            else
            {
                // [일반 Standing 로직] Idle <-> Run 블렌딩
                m_moveBlend = SmoothApproach(m_moveBlend, isMoving ? 1.0f : 0.0f, Get_m_blendSpeed(), DeltaTime);
                anim->base.autoAdvance = true;
                anim->base.clipA = Get_m_idleClip();
                anim->base.clipB = input->GetKey(KeyCode::LeftShift) ? Get_m_runClip() : Get_m_walkClip();
                anim->base.blend01 = m_moveBlend;
                anim->base.speedA = m_animSpeed; anim->base.speedB = m_animSpeed;
            }
        }
        else if (m_state == CharState::Attacking)
        {
            float currentSpeed = Get_m_attackPlaySpeed();

            // [블렌딩 로직: Idle -> Attack 진입]
            // 상태 진입 초반(m_blendTimer < duration)에는 Idle(A)과 Attack(B)을 섞어줍니다.
            m_blendTimer += DeltaTime;
            float blendInAlpha = std::clamp(m_blendTimer / Get_m_transitionDuration(), 0.0f, 1.0f);

            if (blendInAlpha < 1.0f)
            {
                // Blend In 중: A=Idle, B=Attack
                anim->base.clipA = Get_m_idleClip();
                anim->base.clipB = Get_m_attackClip();
                anim->base.blend01 = blendInAlpha; // 0 -> 1 로 증가 (Attack 비율 증가)

                // Idle은 계속 움직이거나 멈춰도 됨 (여기선 자연스럽게 재생)
                anim->base.speedA = m_animSpeed;
                anim->base.speedB = currentSpeed; // Attack 재생 시작
            }
            else
            {
                // Blend 완료 후: 완전한 Attack 상태
                anim->base.clipA = Get_m_attackClip();
                anim->base.clipB = Get_m_attackClip();
                anim->base.blend01 = 0.0f;
            }

            // ------------------------------------
            // 공격 진행 로직 (정방향 -> 역방향)
            // ------------------------------------
            if (!m_isAttackReversing)
            {
                // [정방향]
                if (blendInAlpha >= 1.0f) anim->base.speedA = currentSpeed; // 블렌드 끝나면 A슬롯 사용

                m_currentAttackTime += DeltaTime * currentSpeed;

                // 종료 감지 (설정 시간 도달 OR 실제 애니메이션 종료)
                bool timeout = (m_currentAttackTime >= Get_m_attackDuration());
                // 실제 timeA가 더 이상 안 늘어나면(엔진 Clamp) 종료로 판단 (0.05f 오차 허용)
                bool finished = (blendInAlpha >= 1.0f && m_currentAttackTime > anim->base.timeA + 0.05f);

                if (timeout || finished)
                {
                    m_isAttackReversing = true;
                    // 부드러운 전환을 위해 논리 시간을 실제 엔진 시간과 동기화
                    if (blendInAlpha >= 1.0f) m_currentAttackTime = anim->base.timeA;
                }
            }
            else
            {
                // [역방향]
                anim->base.speedA = -currentSpeed; // 뒤로 재생
                anim->base.speedB = -currentSpeed; // (블렌딩 중일 경우를 대비해 B도 설정)

                // 0초 지점(시작점)에 도달하면 종료
                // 엔진 timeA가 0이 되면 종료
                if (anim->base.timeA <= 0.0f)
                {
                    anim->base.timeA = 0.0f;
                    m_state = CharState::Standing;

                    // Standing으로 가면서 블렌딩 아웃 시작 (Attack -> Idle)
                    m_isBlendingOut = true;
                    m_blendTimer = 0.0f; // 타이머 리셋

                    m_isAttackReversing = false;
                }
            }

            anim->base.autoAdvance = true;
            anim->base.loopA = false;
        }
        else
        {
            // ... (Crouch 등 나머지 로직 유지) ...
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
        // 5. 기타 설정 (Socket, IK 등 - 기존 유지)
        // ------------------------------------------------------------
        anim->additive.enabled = false;
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