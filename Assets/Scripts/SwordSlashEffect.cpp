#include "SwordSlashEffect.h"
#include "Core/World.h"
#include "Core/ScriptFactory.h"
#include "Core/Logger.h"
#include "Components/TransformComponent.h"
#include "Components/SwordEffectComponent.h"
#include <cmath>
#include <DirectXMath.h>

namespace Alice
{
    REGISTER_SCRIPT(SwordSlashEffect);

    void SwordSlashEffect::Awake()
    {
        // Awake에서 SwordEffectComponent 추가 (AddScript 직후에도 바로 추가됨)
        auto* effect = GetComponent<SwordEffectComponent>();
        if (!effect)
        {
            effect = &AddComponent<SwordEffectComponent>();
            effect->color = DirectX::XMFLOAT3(0.8f, 0.2f, 0.9f);
            effect->alpha = 1.0f;
            effect->enabled = true;
            effect->startPoint = Get_m_startPoint();
            effect->endPoint = Get_m_endPoint();
            effect->controlPoint1 = Get_m_controlPoint1();
            effect->controlPoint2 = Get_m_controlPoint2();
            effect->segmentCount = Get_m_segmentCount();
        }
    }

    void SwordSlashEffect::Start()
    {
        auto* transform = this->transform();
        if (!transform)
        {
            transform = &AddComponent<TransformComponent>();
            transform->SetPosition(0.0f, 0.0f, 0.0f);
        }

        // SwordEffectComponent가 없으면 추가 (Awake에서 추가되지 않은 경우 대비)
        auto* effect = GetComponent<SwordEffectComponent>();
        if (!effect)
        {
            effect = &AddComponent<SwordEffectComponent>();
            effect->color = DirectX::XMFLOAT3(0.8f, 0.2f, 0.9f);
            effect->alpha = 1.0f;
            effect->enabled = true;
            effect->startPoint = Get_m_startPoint();
            effect->endPoint = Get_m_endPoint();
            effect->controlPoint1 = Get_m_controlPoint1();
            effect->controlPoint2 = Get_m_controlPoint2();
            effect->segmentCount = Get_m_segmentCount();
        }

        m_currentProgress = 0.0f;
        m_currentAlpha = 1.0f;
        m_elapsedTime = 0.0f;
        m_isActive = true;
        m_hasStarted = false;

        ALICE_LOG_INFO("[SwordSlashEffect] SwordEffectComponent 기반 검기 효과 초기화 완료");
    }

    void SwordSlashEffect::Update(float deltaTime)
    {
        auto* effect = GetComponent<SwordEffectComponent>();
        if (!effect) return;

        // Inspector에서 변경된 제어점들을 컴포넌트에 동기화
        effect->startPoint = Get_m_startPoint();
        effect->endPoint = Get_m_endPoint();
        effect->controlPoint1 = Get_m_controlPoint1();
        effect->controlPoint2 = Get_m_controlPoint2();
        effect->segmentCount = Get_m_segmentCount();

        if (!m_isActive && !Get_m_loop()) 
        {
            effect->enabled = false;
            return;
        }

        m_elapsedTime += deltaTime;

        // 스플라인 점 계산 및 컴포넌트에 저장
        CalculateSplinePoints(effect);

        // 진행도 업데이트
        m_currentProgress += Get_m_speed() * deltaTime;

        if (m_currentProgress >= 1.0f)
        {
            if (Get_m_loop())
            {
                // 반복 모드: 처음부터 다시 시작
                m_currentProgress = 0.0f;
                m_currentAlpha = 1.0f;
                m_elapsedTime = 0.0f;
            }
            else
            {
                // 한 번만 실행: 페이드 아웃 시작
                m_currentProgress = 1.0f;
                m_hasStarted = true;
            }
        }
        else
        {
            m_hasStarted = true;
        }

        // 알파 값 계산 (페이드 아웃)
        if (m_hasStarted)
        {
            float fadeStartTime = 1.0f / Get_m_speed(); // 스플라인 완료 시간
            float fadeElapsed = m_elapsedTime - fadeStartTime;
            
            if (fadeElapsed > 0.0f)
            {
                float fadeRatio = fadeElapsed / Get_m_fadeDuration();
                m_currentAlpha = std::max(0.0f, 1.0f - fadeRatio);
                
                if (m_currentAlpha <= 0.0f && !Get_m_loop())
                {
                    m_isActive = false;
                    effect->enabled = false;
                }
            }
            else
            {
                m_currentAlpha = 1.0f;
            }

            // SwordEffectComponent의 alpha 업데이트
            effect->alpha = m_currentAlpha;
            effect->enabled = m_isActive;
        }
    }

    void SwordSlashEffect::OnDestroy()
    {
        auto* effect = GetComponent<SwordEffectComponent>();
        if (effect)
        {
            effect->splinePoints.clear();
        }
    }

    void SwordSlashEffect::CalculateSplinePoints(SwordEffectComponent* effect)
    {
        if (!effect) return;

        effect->splinePoints.clear();
        effect->splinePoints.reserve(effect->segmentCount + 1);

        for (int i = 0; i <= effect->segmentCount; ++i)
        {
            float t = static_cast<float>(i) / static_cast<float>(effect->segmentCount);
            DirectX::XMFLOAT3 point = CalculateCatmullRomSpline(t, effect);
            effect->splinePoints.push_back(point);
        }
    }

    DirectX::XMFLOAT3 SwordSlashEffect::CalculateCatmullRomSpline(float t, SwordEffectComponent* effect)
    {
        using namespace DirectX;

        if (!effect)
        {
            // 폴백: 스크립트 속성 사용
            XMFLOAT3 p0 = Get_m_startPoint();
            XMFLOAT3 p1 = Get_m_controlPoint1();
            XMFLOAT3 p2 = Get_m_controlPoint2();
            XMFLOAT3 p3 = Get_m_endPoint();

            XMVECTOR v0 = XMLoadFloat3(&p0);
            XMVECTOR v1 = XMLoadFloat3(&p1);
            XMVECTOR v2 = XMLoadFloat3(&p2);
            XMVECTOR v3 = XMLoadFloat3(&p3);

            XMVECTOR result = XMVectorCatmullRom(v0, v1, v2, v3, t);

            XMFLOAT3 resultFloat3;
            XMStoreFloat3(&resultFloat3, result);
            return resultFloat3;
        }

        // 컴포넌트의 제어점 사용
        XMVECTOR v0 = XMLoadFloat3(&effect->startPoint);
        XMVECTOR v1 = XMLoadFloat3(&effect->controlPoint1);
        XMVECTOR v2 = XMLoadFloat3(&effect->controlPoint2);
        XMVECTOR v3 = XMLoadFloat3(&effect->endPoint);

        XMVECTOR result = XMVectorCatmullRom(v0, v1, v2, v3, t);

        XMFLOAT3 resultFloat3;
        XMStoreFloat3(&resultFloat3, result);
        return resultFloat3;
    }
}
