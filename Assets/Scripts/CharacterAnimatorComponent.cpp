#include "CharacterAnimatorComponent.h"

#include <algorithm>
#include <cmath>

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
        // 여기에 실제 타격 로직 (충돌체 활성화, 데미지 주기, 사운드 재생 등)
        ALICE_LOG_INFO("SWOOSH! Attack Notify Fired!");
        
        // 예: 사운드 재생
        // SoundManager::Get().Play("SwordSwing");
    }

    void CharacterAnimatorComponent::OnCrouchHalfway()
    {
        // 앉기 동작이 절반쯤 진행되었을 때 호출됨
        ALICE_LOG_INFO("Body is half-crouched! (Time: 0.5s)");
    }

    void CharacterAnimatorComponent::Update(float DeltaTime)
    {
        auto* input = Input();
        auto go = gameObject();
        if (!input || !go.IsValid())
            return;

        auto* t = go.GetComponent<TransformComponent>();
        if (!t)
            return;

        auto* anim = go.GetComponent<AdvancedAnimationComponent>();
        if (!anim)
            anim = &go.AddComponent<AdvancedAnimationComponent>();

        // [초기화] 노티파이 바인딩 (한 번만 실행)
        if (!m_notifyRegistered)
        {
            // "Attack01" 클립의 0.7초 지점에 OnAttackHit 함수를 묶는다.
            anim->AddNotify(Get_m_attackClip(), Get_m_attackHitTime(), 
                [this]() { this->OnAttackHit(); });
            
            m_notifyRegistered = true;
        }

        // ------------------------------------------------------------
        // 1. 상태 변경 입력 (공격 추가)
        // ------------------------------------------------------------
        // 공격 입력 (Standing 상태에서만)
        if (input->GetMouseButtonDown(MouseCode::Left) && m_state == CharState::Standing)
        {
            m_state = CharState::Attacking;
            m_currentAttackTime = 0.0f;
        }

        // Ctrl 키 토글 (기존)
        if (input->GetKeyDown(KeyCode::LeftCtrl))
        {
            if (m_state == CharState::Standing)
            {
                m_state = CharState::Crouching; // 서기 -> 앉기 시작
                m_currentCrouchTime = 0.0f;     // 시간 0부터 시작

                // [애님 몽타주 예시] 앉기 애니메이션 중간에 노티파이 등록
                // "CrouchDown" 클립의 0.5초 지점에 함수 바인딩
                anim->notifies.clear(); // 기존 노티파이 초기화 (안전장치)
                anim->AddNotify(Get_m_crouchClip(), 0.5f, [this]() { this->OnCrouchHalfway(); });
            }
            else if (m_state == CharState::Crouched)
            {
                m_state = CharState::StandingUp; // 앉음 -> 서기 시작 (역재생)
                m_currentCrouchTime = Get_m_crouchDuration(); // 끝 시간부터 시작
            }
        }

        // ------------------------------------------------------------
        // 2. 이동 및 회전 로직 (Standing 상태일 때만 가능)
        // ------------------------------------------------------------
        bool isMoving = false;

        // 공격 중이거나 앉는 중이면 이동 불가
        if (m_state == CharState::Standing)
        {
            float inputX = 0.0f;
            float inputZ = 0.0f;
            if (input->GetKey(KeyCode::W)) inputZ += 1.0f;
            if (input->GetKey(KeyCode::S)) inputZ -= 1.0f;
            if (input->GetKey(KeyCode::D)) inputX += 1.0f;
            if (input->GetKey(KeyCode::A)) inputX -= 1.0f;

            isMoving = (inputX != 0.0f || inputZ != 0.0f);
            const bool isRunning = input->GetKey(KeyCode::LeftShift);

            // 카메라 기준 이동 계산
            float moveX = inputX;
            float moveZ = inputZ;

            auto mainCamObj = GetWorld()->FindGameObject("MainCamera");
            if (isMoving && mainCamObj.IsValid())
            {
                auto* camT = mainCamObj.GetComponent<TransformComponent>();
                if (camT)
                {
                    float fwdX = t->position.x - camT->position.x;
                    float fwdZ = t->position.z - camT->position.z;
                    float len = std::sqrt(fwdX * fwdX + fwdZ * fwdZ);
                    if (len > 0.0001f)
                    {
                        fwdX /= len;
                        fwdZ /= len;
                    }

                    float rightX = fwdZ;
                    float rightZ = -fwdX;

                    moveX = (fwdX * inputZ) + (rightX * inputX);
                    moveZ = (fwdZ * inputZ) + (rightZ * inputX);
                }
            }

            float moveLen = std::sqrt(moveX * moveX + moveZ * moveZ);
            if (moveLen > 0.0001f)
            {
                moveX /= moveLen;
                moveZ /= moveLen;

                const float speed = Get_m_moveSpeed() * (isRunning ? Get_m_runMultiplier() : 1.0f);
                t->position.x += moveX * speed * DeltaTime;
                t->position.z += moveZ * speed * DeltaTime;

                const float rad = std::atan2(moveX, moveZ);
                const float deg = rad * (180.0f / 3.14159265358979323846f);
                t->SetRotation(0.0f, deg + 180.0f, 0.0f);
            }

            // 점프 및 중력
            const bool grounded = (t->position.y <= 0.0f);
            if (grounded)
            {
                t->position.y = 0.0f;
                if (m_velY < 0.0f)
                    m_velY = 0.0f;
                if (input->GetKeyDown(KeyCode::Space))
                    m_velY = Get_m_jumpSpeed();
            }
            m_velY -= Get_m_gravity() * DeltaTime;
            t->position.y += m_velY * DeltaTime;
            if (t->position.y < 0.0f)
                t->position.y = 0.0f;
        }

        // ------------------------------------------------------------
        // 3. 애니메이션 상태 머신
        // ------------------------------------------------------------
        anim->enabled = true;
        anim->playing = true;
        anim->base.enabled = true;
        anim->base.layerAlpha = 1.0f;

        // 상태별 처리
        if (m_state == CharState::Standing)
        {
            // 일반 이동 블렌딩
            const bool isRunning = input->GetKey(KeyCode::LeftShift);
            const std::string moveClip = isRunning ? Get_m_runClip() : Get_m_walkClip();

            if (moveClip != m_lastMoveClip)
            {
                anim->base.timeB = 0.0f;
                m_lastMoveClip = moveClip;
            }

            const float targetBlend = isMoving ? 1.0f : 0.0f;
            m_moveBlend = SmoothApproach(m_moveBlend, targetBlend, Get_m_blendSpeed(), DeltaTime);

            anim->base.autoAdvance = true; // 자동 시간 진행
            anim->base.clipA = Get_m_idleClip();
            anim->base.clipB = moveClip;
            anim->base.blend01 = m_moveBlend;
            anim->base.speedA = 1.0f;
            anim->base.speedB = 1.0f;
        }
        else if (m_state == CharState::Attacking)
        {
            // 몽타주 재생 로직
            m_currentAttackTime += DeltaTime;

            // 애니메이션 설정
            anim->base.autoAdvance = true; // 시스템의 자동 시간 진행 사용
            anim->base.loopA = false;      // 몽타주는 반복 안 함
            anim->base.clipA = Get_m_attackClip();
            anim->base.clipB = Get_m_attackClip(); // 블렌딩 없이 즉시 재생
            anim->base.blend01 = 0.0f;
            anim->base.speedA = 1.0f;
            anim->base.speedB = 1.0f;
            
            // 현재 애니메이션 시간이 끝났는지 체크 (시스템이 업데이트한 timeA 활용)
            if (anim->base.timeA >= Get_m_attackDuration() || m_currentAttackTime >= Get_m_attackDuration())
            {
                m_state = CharState::Standing; // 복귀
                anim->base.timeA = 0.0f;       // 리셋
            }
        }
        else
        {
            // 앉기 관련 상태 (Crouching, Crouched, StandingUp)
            // 앉기 애니메이션은 하나를 가지고 시간 제어로 처리

            // 1. 시간 업데이트 및 노티파이 체크
            if (m_state == CharState::Crouching)
            {
                float prevTime = m_currentCrouchTime;
                m_currentCrouchTime += DeltaTime; // 정재생
                
                // [중요] 수동 시간 제어 시 직접 노티파이 체크 함수 호출
                anim->CheckAndFireNotifies(Get_m_crouchClip(), prevTime, m_currentCrouchTime);
                
                if (m_currentCrouchTime >= Get_m_crouchDuration())
                {
                    m_currentCrouchTime = Get_m_crouchDuration();
                    m_state = CharState::Crouched; // 다 앉음
                }
            }
            else if (m_state == CharState::StandingUp)
            {
                float prevTime = m_currentCrouchTime;
                m_currentCrouchTime -= DeltaTime; // 역재생
                
                // 역재생 시에도 노티파이 체크 (역순으로 체크)
                anim->CheckAndFireNotifies(Get_m_crouchClip(), prevTime, m_currentCrouchTime);
                
                if (m_currentCrouchTime <= 0.0f)
                {
                    m_currentCrouchTime = 0.0f;
                    m_state = CharState::Standing; // 다 일어섬
                }
            }
            // Crouched 상태면 시간 변경 없음 (고정)

            // 2. 애니메이션 설정 (수동 시간 제어)
            anim->base.autoAdvance = false; // 엔진 시간 진행 끔
            anim->base.clipA = Get_m_crouchClip();
            anim->base.clipB = Get_m_crouchClip();
            anim->base.timeA = m_currentCrouchTime;
            anim->base.timeB = m_currentCrouchTime;
            anim->base.blend01 = 0.0f;
            
            // ------------------------------------------------------------
            // [애니메이션 속도 변형 예시] 특정 구간을 느리게 재생
            // ------------------------------------------------------------
            // 예: 1초 ~ 2초 구간을 1초만큼 더 느리게(총 2초 동안 재생) 하고 싶다.
            // 원래 구간 길이: 1.0초 (2.0 - 1.0)
            // 목표 재생 시간: 2.0초 (1.0 + 1.0)
            // 배속 = 원래길이 / 목표시간 = 1.0 / 2.0 = 0.5배속
            float currentSpeed = 1.0f;
            if (m_currentCrouchTime >= 1.0f && m_currentCrouchTime < 2.0f)
            {
                currentSpeed = 0.5f; // 0.5배속 (슬로우 모션)
            }
            else
            {
                currentSpeed = 1.0f; // 정상 속도
            }
            anim->base.speedA = currentSpeed;
            anim->base.speedB = currentSpeed;
        }

        // ------------------------------------------------------------
        // 4. 사격 (Additive Layer)
        // ------------------------------------------------------------
        bool tryFire = false;
        std::string fireClip = Get_m_additiveClip(); // 기본 반동

        // 앉은 상태에서의 사격
        if (m_state == CharState::Crouched)
        {
            if (input->GetMouseButtonDown(MouseCode::Left))
            {
                ALICE_LOG_ERRORF("ALICE FIRE!!!");
                tryFire = true;
                fireClip = Get_m_crouchFireClip(); // 앉아 쏴 애니메이션
                m_currentFireClip = fireClip;      // 현재 발동된 클립 저장
            }
        }
        // 서 있는 상태에서의 일반 행동 (필요시 추가)
        else if (m_state == CharState::Standing)
        {
            if (input->GetMouseButtonDown(MouseCode::Left))
            {
                tryFire = true;
                fireClip = Get_m_additiveClip();
                m_currentFireClip = fireClip;
            }
        }

        // Additive 재생 로직
        if (tryFire)
        {
            m_additiveTimer = 0.0f;
        }

        if (m_additiveTimer >= 0.0f)
        {
            anim->additive.enabled = true;
            anim->additive.autoAdvance = false; // 수동 제어
            anim->additive.clip = m_currentFireClip.empty() ? fireClip : m_currentFireClip;
            anim->additive.refClip = Get_m_additiveRefClip();
            anim->additive.alpha = 1.0f;
            anim->additive.time = m_additiveTimer;
            anim->additive.loop = false;

            m_additiveTimer += DeltaTime;
            if (m_additiveTimer > Get_m_additiveDuration())
            {
                anim->additive.enabled = false;
                m_additiveTimer = -1.0f;
                m_currentFireClip.clear();
            }
        }
        else
        {
            anim->additive.enabled = false;
        }

        // ------------------------------------------------------------
        // 5. 기타 설정 (Upper, Socket, IK, Aim)
        // ------------------------------------------------------------
        // Upper Body (서 있을 때만 적용)
        anim->upper.enabled = Get_m_enableUpperLayer() && (m_state == CharState::Standing);
        anim->upper.autoAdvance = true;
        anim->upper.clipA = Get_m_upperClip();
        anim->upper.clipB.clear();
        anim->upper.blend01 = 0.0f;
        anim->upper.layerAlpha = anim->upper.enabled ? 1.0f : 0.0f;
        anim->upper.speedA = 1.0f;

        // Socket
        if (!m_socketInitialized)
        {
            anim->SetSocketSRT(Get_m_socketName(),
                               Get_m_socketParentBone(),
                               Get_m_socketPos(),
                               Get_m_socketRotDeg(),
                               Get_m_socketScale());
            m_socketInitialized = true;
        }

        // IK & Aim
        anim->ik.enabled = Get_m_enableIK();
        anim->ik.tipBone = Get_m_ikTipBone();
        anim->ik.chainLength = Get_m_ikChainLength();
        anim->ik.weight = Get_m_ikWeight();
        anim->ik.targetMS = Get_m_ikTargetLocal();

        anim->aim.enabled = Get_m_enableAim();
        anim->aim.yawRad = DirectX::XMConvertToRadians(Get_m_aimYawDeg());
        anim->aim.weight = Get_m_aimWeight();
    }
}

