#pragma once

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"
#include <DirectXMath.h>
#include <vector>

namespace Alice
{
    class SwordEffectComponent;
    /// 쉐이더 기반 Catmull-Rom 스플라인 검기 이펙트 스크립트
    /// - 하나의 빈 오브젝트만 사용
    /// - 쉐이더를 통해 스플라인 기반 검기 렌더링
    /// - 검기 생성 후 점차 사라지는 효과
    class SwordSlashEffect : public IScript
    {
        ALICE_BODY(SwordSlashEffect);

    public:
        void Awake() override;
        void Start() override;
        void Update(float deltaTime) override;
        void OnDestroy() override;

        // Inspector 속성
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_startPoint, DirectX::XMFLOAT3(-2.0f, 1.5f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_endPoint, DirectX::XMFLOAT3(2.0f, 1.5f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_controlPoint1, DirectX::XMFLOAT3(-1.0f, 1.5f, -0.5f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_controlPoint2, DirectX::XMFLOAT3(1.0f, 1.5f, 0.5f));
        ALICE_PROPERTY(float, m_speed, 2.0f);              // 검기 이동 속도
        ALICE_PROPERTY(bool, m_loop, false);               // 반복할지 여부
        ALICE_PROPERTY(float, m_fadeDuration, 1.0f);      // 페이드 아웃 시간 (초)
        ALICE_PROPERTY(int, m_segmentCount, 64);          // 스플라인 세그먼트 수

    private:
        void CalculateSplinePoints(SwordEffectComponent* effect);
        DirectX::XMFLOAT3 CalculateCatmullRomSpline(float t, SwordEffectComponent* effect);

        float m_currentProgress;     // 현재 진행도 (0.0 ~ 1.0)
        float m_currentAlpha;        // 현재 알파 값 (1.0 ~ 0.0)
        float m_elapsedTime;         // 경과 시간
        bool m_isActive;             // 활성화 여부
        bool m_hasStarted;           // 시작 여부
    };
}
