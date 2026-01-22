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
                std::bind(&CharacterAnimatorComponent::OnAttackHit, this));
            
            m_notifyRegistered = true;
        }

        // ------------------------------------------------------------
        // 0. 애니메이션 속도 제어 (2번/3번/4번/5번 키 - 누르고 있을 때만 적용)
        // ------------------------------------------------------------
        // 키를 떼면 기본 속도(1.0f)로 돌아가야 하므로 매 프레임 초기화
        m_animSpeed = 1.0f;

        // 2번 키: 2배 빠르게
        if (input->GetKey(KeyCode::Alpha2))
        {
            m_animSpeed = 2.0f;
        }
        // 3번 키: 3배 빠르게
        else if (input->GetKey(KeyCode::Alpha3))
        {
            m_animSpeed = 3.0f;
        }
        // 4번 키: 0.5배 (느리게)
        else if (input->GetKey(KeyCode::Alpha4))
        {
            m_animSpeed = 0.5f;
        }
        // 5번 키: 0.25배 (아주 느리게)
        else if (input->GetKey(KeyCode::Alpha5))
        {
            m_animSpeed = 0.25f;
        }

        // ------------------------------------------------------------
        // 1. 상태 변경 입력 (공격 추가)
        // ------------------------------------------------------------
        // 공격 입력 (Standing 상태에서만) - 마우스 좌클릭 사용
        if (input->GetMouseButtonDown(MouseCode::Left) && m_state == CharState::Standing)
        {
            m_state = CharState::Attacking;
            m_currentAttackTime = 0.0f;
        }

        // Ctrl 키 토글 (기존) 및 6번 키 토글 (구간 늘리기 모드)
        bool toggleCrouch = false;
        bool useStretch = false;

        if (input->GetKeyDown(KeyCode::LeftCtrl))
        {
            toggleCrouch = true;
            useStretch = false; // 일반 모드
        }
        else if (input->GetKeyDown(KeyCode::Alpha6))
        {
            toggleCrouch = true;
            useStretch = true; // 6번 키: 구간 늘리기 모드
        }

        if (toggleCrouch)
        {
            if (m_state == CharState::Standing)
            {
                m_state = CharState::Crouching; // 서기 -> 앉기 시작
                m_currentCrouchTime = 0.0f;     // 시간 0부터 시작
                m_isStretchedMode = useStretch;  // 모드 설정

                // [애님 몽타주 예시] 앉기 애니메이션 중간에 노티파이 등록
                // "CrouchDown" 클립의 0.5초 지점에 함수 바인딩
                anim->notifies.clear(); // 기존 노티파이 초기화 (안전장치)
                anim->AddNotify(Get_m_crouchClip(), 0.5f, 
                    std::bind(&CharacterAnimatorComponent::OnCrouchHalfway, this));
            }
            else if (m_state == CharState::Crouched)
            {
                m_state = CharState::StandingUp; // 앉음 -> 서기 시작 (역재생)
                m_currentCrouchTime = Get_m_crouchDuration(); // 끝 시간부터 시작
                m_isStretchedMode = useStretch;  // 모드 설정
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
            // [속도 적용] 서 있을 때도 설정한 배속 적용
            anim->base.speedA = m_animSpeed;
            anim->base.speedB = m_animSpeed;
        }
        else if (m_state == CharState::Attacking)
        {
            // 몽타주 재생 로직 (속도 적용)
            m_currentAttackTime += DeltaTime * m_animSpeed;

            // 애니메이션 설정
            anim->base.autoAdvance = true; // 시스템의 자동 시간 진행 사용
            anim->base.loopA = false;      // 몽타주는 반복 안 함
            anim->base.clipA = Get_m_attackClip();
            anim->base.clipB = Get_m_attackClip(); // 블렌딩 없이 즉시 재생
            anim->base.blend01 = 0.0f;
            // [속도 적용] 공격 애니메이션에도 배속 적용
            anim->base.speedA = m_animSpeed;
            anim->base.speedB = m_animSpeed;
            
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

            // 1. 시간 업데이트 및 노티파이 체크 (속도 적용)
            float prevTime = m_currentCrouchTime;
            float stepSpeed = m_animSpeed; // 기본 속도

            // [6번 키 로직] 1.0초 ~ 2.0초 구간을 2초 늘려서(총 3초) 재생 -> 1/3 배속
            // 원래 구간: 1초 (1.0초 ~ 2.0초)
            // 목표 재생 시간: 3초 (1.0초 + 2.0초)
            // 배속 = 원래길이 / 목표시간 = 1.0 / 3.0 = 0.333...배속
            if (m_isStretchedMode)
            {
                if (m_currentCrouchTime >= 0.01f && m_currentCrouchTime < 0.5f)
                {
                    stepSpeed = 1.0f / 3.0f; // 0.333...배속 (1초 구간을 3초로 늘림)
                }
            }

            if (m_state == CharState::Crouching)
            {
                m_currentCrouchTime += DeltaTime * stepSpeed; // 구간별 속도 적용
                
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
                m_currentCrouchTime -= DeltaTime * stepSpeed; // 구간별 속도 적용 (역재생)
                
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
            
            // 속도 적용 (구간별 속도 사용)
            anim->base.speedA = stepSpeed;
            anim->base.speedB = stepSpeed;
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

            // [속도 적용] 반동 애니메이션에도 배속 적용
            m_additiveTimer += DeltaTime * m_animSpeed;
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
        // [속도 적용] Upper 레이어에도 배속 적용
        anim->upper.speedA = m_animSpeed;

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

        // ------------------------------------------------------------
        // 6. Foot IK 로직 (Y키로 발 들어올리기 / 지형 적응)
        // ------------------------------------------------------------
        // [중요] Y키는 캐릭터 Transform을 건드리지 않고, 오직 발 본의 IK만 조절합니다.
        // IK는 애니메이션 시스템 내부에서 본 체인을 조정하여 발만 움직이게 합니다.
        if (Get_m_enableFootIK())
        {
            // =========================================================
            // [물리 구현 시 수정할 곳] - 목표 높이 결정
            // =========================================================
            // 현재는 Y키 입력으로 targetHeight를 강제로 설정합니다.
            // 나중에 물리 충돌(Raycast)을 구현할 때는 아래 주석 부분을 활성화하고
            // Y키 입력 부분을 제거하면 됩니다.
            // 
            // 물리 프로그래머가 해야 할 일:
            // 1. 발 본의 현재 위치에서 아래로 Raycast를 쏜다
            // 2. 바닥에 닿은 지점까지의 거리를 계산한다
            // 3. 그 거리를 targetHeight 변수에 넣는다
            // 4. SmoothApproach 함수가 알아서 부드럽게 발을 올리거나 내린다
            // =========================================================
            
            float targetHeight = 0.0f;

            // [현재 구현] Y키 입력으로 시뮬레이션
            // Y키를 누르면 IK 타겟 높이만 설정 (캐릭터 Transform 이동 X)
            if (input->GetKey(KeyCode::Y))
            {
                targetHeight = Get_m_maxLiftHeight(); // Y키 누르면 최대 높이까지 들어올림
            }
            else
            {
                targetHeight = 0.0f; // 안 누르면 바닥(0.0)
            }

            // [물리 구현 시 이 부분을 주석 처리하고 아래 코드로 대체]
            /*
            // =========================================================
            // 물리 Raycast 기반 Foot Placement 구현 예시
            // =========================================================
            // 1. 발 본의 현재 위치 가져오기 (월드 공간)
            // (엔진에 GetBonePosition 함수가 있다고 가정)
            // DirectX::XMVECTOR leftFootPosWS = anim->GetBonePosition("Foot_L");
            // 
            // 2. 레이캐스트 발사 (발 위치보다 조금 위에서 아래로)
            // float rayStartHeight = 0.5f;  // 발 위 0.5m에서 시작
            // float rayLength = 1.0f;       // 최대 1m까지 쏨
            // 
            // DirectX::XMVECTOR rayStart = leftFootPosWS + DirectX::XMVectorSet(0, rayStartHeight, 0, 0);
            // DirectX::XMVECTOR rayDir = DirectX::XMVectorSet(0, -1.0f, 0, 0); // 아래 방향
            // 
            // RaycastHit leftHit;
            // bool lHit = GetWorld()->Raycast(rayStart, rayDir, rayLength, &leftHit);
            // 
            // if (lHit)
            // {
            //     // 발바닥이 땅에 닿아야 할 높이 계산
            //     // hit.point.y는 월드 공간의 바닥 높이
            //     // characterPos.y는 캐릭터 위치 (모델 공간 기준 0)
            //     // footOffset은 발바닥 두께 (예: 0.1f)
            //     float footOffset = 0.1f;
            //     float groundHeightWS = leftHit.point.y;
            //     float characterHeightWS = t->position.y;
            //     
            //     // 모델 공간 기준으로 변환 (캐릭터 위치를 0으로 가정)
            //     targetHeight = (groundHeightWS - characterHeightWS) + footOffset;
            //     
            //     // 음수 방지 (발이 땅 아래로 가지 않도록)
            //     if (targetHeight < 0.0f)
            //         targetHeight = 0.0f;
            // }
            // else
            // {
            //     // Raycast가 실패하면 기본값 (바닥)
            //     targetHeight = 0.0f;
            // }
            // =========================================================
            */

            // 2. 부드러운 움직임 (Interpolation)
            // 갑자기 팍 튀지 않게 현재 높이에서 목표 높이로 서서히 이동
            m_currentLeftFootHeight = SmoothApproach(m_currentLeftFootHeight, targetHeight, Get_m_ikLiftSpeed(), DeltaTime);

            // 3. IK 타겟 위치 계산 (Model Space)
            // 캐릭터 기준(0,0,0)에서 왼발의 기본 위치를 알아야 합니다.
            // 여기서는 m_leftFootBasePos로 설정된 기본 위치를 사용하고 Y값만 더합니다.
            // *정확히 하려면*: anim->GetBoneTransform("Ball_L")로 현재 위치를 가져와야 함.
            
            DirectX::XMFLOAT3 targetPos = Get_m_leftFootBasePos();
            targetPos.y = m_currentLeftFootHeight; // [핵심] 계산된 높이 적용

            // 4. 엔진에 IK 적용 요청 (인덱스 0번: 왼발)
            // ChainLength 2: 발 -> 종아리 -> 허벅지까지 영향을 줌 (무릎이 굽혀짐)
            anim->SetIK(0, Get_m_leftFootBone(), 2, targetPos, 1.0f);
        }
        else
        {
            // Foot IK가 비활성화되면 IK 체인 비활성화
            anim->DisableIK(0);
        }

        // 기존 단일 IK 설정 (하위 호환성)
        anim->ik.enabled = Get_m_enableIK();
        anim->ik.tipBone = Get_m_ikTipBone();
        anim->ik.chainLength = Get_m_ikChainLength();
        anim->ik.weight = Get_m_ikWeight();
        anim->ik.targetMS = Get_m_ikTargetLocal();
        
        // [하위 호환성] 기존 ik를 ikChains[0]에 동기화 (Foot IK가 사용하지 않을 때만)
        if (anim->ik.enabled && !Get_m_enableFootIK() && anim->ikChains.empty())
        {
            anim->SetIK(0, anim->ik.tipBone, anim->ik.chainLength, anim->ik.targetMS, anim->ik.weight);
        }

        anim->aim.enabled = Get_m_enableAim();
        anim->aim.yawRad = DirectX::XMConvertToRadians(Get_m_aimYawDeg());
        anim->aim.weight = Get_m_aimWeight();
    }
}

