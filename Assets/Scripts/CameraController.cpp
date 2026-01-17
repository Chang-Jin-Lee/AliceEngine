#include "CameraController.h"
#include "Core/ScriptFactory.h"
#include "Core/GameObject.h"
#include "Core/InputTypes.h"

#include "Components/TransformComponent.h"
#include "Components/CameraComponent.h"
#include "Components/CameraBlendComponent.h"
#include "Components/CameraShakeComponent.h"
#include "Components/CameraSpringArmComponent.h"
#include "Components/CameraFollowComponent.h"
#include "Components/CameraLookAtComponent.h"
#include "Components/CameraInputComponent.h"

#include <sstream>
#include <cctype>
#include <algorithm>
#include <DirectXMath.h>

namespace Alice
{
    REGISTER_SCRIPT(CameraController);

    static std::string Trim(const std::string& s)
    {
        size_t a = 0;
        while (a < s.size() && std::isspace((unsigned char)s[a])) ++a;
        size_t b = s.size();
        while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
        return s.substr(a, b - a);
    }

    void CameraController::Awake()
    {
        auto go = gameObject();
        if (!go.IsValid()) return;

        // 필요한 컴포넌트가 이미 있다고 가정하고 쓰지만, 안전빵으로 없으면 생성함
        if (!go.GetComponent<CameraBlendComponent>())  go.AddComponent<CameraBlendComponent>();
        if (!go.GetComponent<CameraShakeComponent>())  go.AddComponent<CameraShakeComponent>();
        if (!go.GetComponent<CameraSpringArmComponent>()) go.AddComponent<CameraSpringArmComponent>();
        if (!go.GetComponent<CameraFollowComponent>()) go.AddComponent<CameraFollowComponent>();
        if (!go.GetComponent<CameraLookAtComponent>()) go.AddComponent<CameraLookAtComponent>();

        // LookAt은 기본 OFF
        if (auto* lookAt = go.GetComponent<CameraLookAtComponent>())
            lookAt->enabled = false;
    }

    void CameraController::Start()
    {
        SetPreview(false);
    }

    std::string CameraController::GetCameraNameFromCsv(const std::string& csv, int idx) const
    {
        std::stringstream ss(csv);
        std::string item;
        for (int i = 0; std::getline(ss, item, ','); ++i)
            if (i == idx) return Trim(item);
        return {};
    }

    void CameraController::SetPreview(bool on)
    {
        auto go = gameObject();
        if (!go.IsValid()) return;

        auto* follow = go.GetComponent<CameraFollowComponent>();
        auto* lookAt = go.GetComponent<CameraLookAtComponent>();
        auto* blend  = go.GetComponent<CameraBlendComponent>();
        auto* tr     = go.GetComponent<TransformComponent>();

        if (!follow || !tr) return;

        if (on)
        {
            if (!m_preview)
            {
                m_savedFollowEnabled = follow->enabled;
                m_savedLookAtEnabled = (lookAt ? lookAt->enabled : false);
            }

            // 프리뷰 모드: Follow/LookAt 끄고 카메라 위치 고정
            follow->enabled = false;
            if (lookAt) lookAt->enabled = false;
            m_preview = true;
            return;
        }

        // 프리뷰 종료: Follow 복귀 (부드럽게 이어지도록 initialized 리셋)
        if (blend) blend->active = false;

        follow->enabled = m_savedFollowEnabled;
        follow->initialized = false;

        // 현재 카메라 방향을 follow의 yaw/pitch로 동기화 (복귀 시 튐 방지)
        follow->yawDeg = DirectX::XMConvertToDegrees(tr->rotation.y);
        follow->pitchDeg = DirectX::XMConvertToDegrees(tr->rotation.x);

        if (lookAt) lookAt->enabled = m_savedLookAtEnabled;

        m_preview = false;
    }

    void CameraController::TriggerCut(const std::string& camName)
    {
        auto go = gameObject();
        if (!go.IsValid()) return;

        auto* world = GetWorld();
        if (!world) return;

        auto* tr = go.GetComponent<TransformComponent>();
        auto* cam = go.GetComponent<CameraComponent>();
        auto* blend = go.GetComponent<CameraBlendComponent>();
        if (!tr || !cam || !blend) return;

        // 타겟 카메라 찾기
        auto targetGo = world->FindGameObject(camName);
        if (!targetGo.IsValid()) return;

        const auto* targetTr = targetGo.GetComponent<TransformComponent>();
        const auto* targetCam = targetGo.GetComponent<CameraComponent>();
        if (!targetTr || !targetCam) return;

        // Blend 컴포넌트는 "타겟 스냅샷을 시스템이 읽어오도록" 준비만 해준다.
        blend->sourcePosition = tr->position;
        blend->sourceRotation = tr->rotation;
        blend->sourceFovY = cam->fovYRad;
        blend->sourceNear = cam->nearPlane;
        blend->sourceFar = cam->farPlane;

        blend->targetId = targetGo.id();
        blend->targetName = camName;

        blend->duration = 0.0f;     // 컷
        blend->elapsed = 0.0f;
        blend->slowTriggered = false;
        blend->slowElapsed = 0.0f;
        blend->active = true;
    }

    void CameraController::TriggerBlend(const std::string& camName, float duration)
    {
        auto go = gameObject();
        if (!go.IsValid()) return;

        auto* world = GetWorld();
        if (!world) return;

        auto* tr = go.GetComponent<TransformComponent>();
        auto* cam = go.GetComponent<CameraComponent>();
        auto* blend = go.GetComponent<CameraBlendComponent>();
        if (!tr || !cam || !blend) return;

        // 타겟 카메라 찾기
        auto targetGo = world->FindGameObject(camName);
        if (!targetGo.IsValid()) return;

        const auto* targetTr = targetGo.GetComponent<TransformComponent>();
        const auto* targetCam = targetGo.GetComponent<CameraComponent>();
        if (!targetTr || !targetCam) return;

        blend->sourcePosition = tr->position;
        blend->sourceRotation = tr->rotation;
        blend->sourceFovY = cam->fovYRad;
        blend->sourceNear = cam->nearPlane;
        blend->sourceFar = cam->farPlane;

        blend->targetId = targetGo.id();
        blend->targetName = camName;

        blend->duration = duration;
        blend->elapsed = 0.0f;
        blend->slowTriggered = false;
        blend->slowElapsed = 0.0f;
        blend->active = true;
    }

    void CameraController::TriggerShake(float amp, float freq, float dur, float decay)
    {
        auto go = gameObject();
        if (!go.IsValid()) return;

        auto* shake = go.GetComponent<CameraShakeComponent>();
        if (!shake) shake = &go.AddComponent<CameraShakeComponent>();

        const bool active = shake->IsActive();

        shake->enabled = true;
        shake->frequency = freq;
        shake->decay = decay;

        if (!active)
        {
            // 쉐이크가 비활성 상태면 새로 시작
            shake->amplitude = amp;
            shake->duration = dur;
            shake->elapsed = 0.0f;
            shake->prevOffset = {};
            return;
        }

        // 이미 쉐이크 중이면: 강도 누적 + 남은 시간 연장
        shake->amplitude += amp;

        const float endTime = shake->duration;           // 기존 종료 시각(=duration)
        const float newEnd  = shake->elapsed + dur;      // 지금부터 dur만큼 더
        shake->duration = std::max(endTime, newEnd);
    }

    void CameraController::ToggleLookAt()
    {
        auto go = gameObject();
        auto* world = GetWorld();
        if (!go.IsValid() || !world) return;

        auto* lookAt = go.GetComponent<CameraLookAtComponent>();
        if (!lookAt) lookAt = &go.AddComponent<CameraLookAtComponent>();

        auto* follow = go.GetComponent<CameraFollowComponent>();
        if (!follow) follow = &go.AddComponent<CameraFollowComponent>();

        lookAt->enabled = !lookAt->enabled;

        // 타깃 이름은 CameraInputComponent(=RigPreset이 채워줌)에서 가져온다
        if (auto* cfg = go.GetComponent<CameraInputComponent>())
            lookAt->targetName = cfg->lookAtTargetName;

        // Follow의 lockOn 상태도 동기화
        follow->lockOnActive = lookAt->enabled;
        follow->lockOnTargetId = InvalidEntityId;

        if (lookAt->enabled)
        {
            auto t = world->FindGameObject(lookAt->targetName);
            if (t.IsValid()) follow->lockOnTargetId = t.id();
            follow->mode = 2; // lock-on 거리/FOV 모드
            follow->shoulderOffset = m_sholderOffset;
        }
        else
        {
            follow->mode = 0; // 기본 모드로 복귀
            follow->shoulderOffset = 0.3f;
        }
    }

    void CameraController::UpdateOrbit()
    {
        auto go = gameObject();
        if (!go.IsValid()) return;

        auto* follow = go.GetComponent<CameraFollowComponent>();
        auto* input = Input();
        if (!follow || !input) return;

        if (!follow->enabled || !follow->enableInput) return;
        if (follow->lockOnActive && !follow->allowManualOrbitInLockOn) return;

        if (input->GetMouseButton(MouseCode::Left))
        {
            follow->yawDeg   -= input->GetMouseDeltaX() * follow->sensitivity;
            follow->pitchDeg -= input->GetMouseDeltaY() * follow->sensitivity;
            follow->pitchDeg = std::clamp(follow->pitchDeg, follow->pitchMinDeg, follow->pitchMaxDeg);
        }
    }

    void CameraController::UpdateZoom()
    {
        auto go = gameObject();
        if (!go.IsValid()) return;

        auto* spring = go.GetComponent<CameraSpringArmComponent>();
        auto* input = Input();
        if (!spring || !input) return;

        if (!spring->enabled || !spring->enableZoom) return;

        const float wheel = input->GetMouseScrollDelta();
        if (wheel == 0.0f) return;

        spring->desiredDistance = std::clamp(
            spring->desiredDistance - wheel * spring->zoomSpeed,
            spring->minDistance,
            spring->maxDistance);
    }

    void CameraController::Update(float /*deltaTime*/)
    {
        auto go = gameObject();
        auto* input = Input();
        if (!go.IsValid() || !input) return;

        auto* cfg = go.GetComponent<CameraInputComponent>();
        const std::string csv = cfg ? cfg->cameraListCsv : "Camera1,Camera2,Camera3,Camera4,Camera5";

        // ---- 프리뷰 종료(=메인 Follow 복귀) ----
        if (input->GetKeyDown(KeyCode::Alpha6))
        {
            SetPreview(false);
            return;
        }

        // ---- LookAt 토글(프리뷰 중에는 꺼둠) ----
        if (!m_preview && input->GetKeyDown(KeyCode::L))
        {
            ToggleLookAt();
        }

        // ---- 1~5: 해당 카메라로 전환 후 프리뷰 고정 ----
        if (input->GetKeyDown(KeyCode::Alpha1))
        {
            SetPreview(true);
            TriggerCut(GetCameraNameFromCsv(csv, 0));
            return;
        }
        if (input->GetKeyDown(KeyCode::Alpha2))
        {
            SetPreview(true);
            TriggerCut(GetCameraNameFromCsv(csv, 1));
            return;
        }
        if (input->GetKeyDown(KeyCode::Alpha3))
        {
            SetPreview(true);
            TriggerBlend(GetCameraNameFromCsv(csv, 2), cfg ? cfg->blendTimeKey3 : 0.6f);
            return;
        }
        if (input->GetKeyDown(KeyCode::Alpha4))
        {
            SetPreview(true);
            TriggerBlend(GetCameraNameFromCsv(csv, 3), cfg ? cfg->blendTimeKey4 : 0.6f);
            if (cfg) TriggerShake(cfg->shakeAmplitudeKey4, cfg->shakeFrequencyKey4, cfg->shakeDurationKey4, cfg->shakeDecayKey4);
            return;
        }
        if (input->GetKeyDown(KeyCode::Alpha5))
        {
            SetPreview(true);
            TriggerBlend(GetCameraNameFromCsv(csv, 4), cfg ? cfg->blendTimeKey5 : 0.8f);

            // 슬로우모션은 BlendComp 파라미터로 제어(기존 로직 그대로 활용)
            if (cfg)
            {
                if (auto* blend = go.GetComponent<CameraBlendComponent>())
                {
                    blend->slowTriggerT = cfg->slowTriggerTKey5;
                    blend->slowDuration = cfg->slowDurationKey5;
                    blend->slowTimeScale = cfg->slowTimeScaleKey5;
                }
            }
            return;
        }
        if (input->GetKeyDown(KeyCode::B))
        {
            SetPreview(true);
            // 리플렉션으로 노출된 쉐이크 파라미터 사용
            TriggerShake(Get_m_shakeAmplitude(), Get_m_shakeFrequency(), 
                        Get_m_shakeDuration(), Get_m_shakeDecay());
        }

        // ---- 게임플레이(프리뷰가 아닐 때만) ----
        if (!m_preview)
        {
            UpdateOrbit();
            UpdateZoom();
        }
    }
}
