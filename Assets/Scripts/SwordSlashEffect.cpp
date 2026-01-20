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
            effect->maxSamples = m_maxSamples;
            effect->sampleInterval = m_sampleInterval;
            effect->fadeDuration = m_fadeDuration;
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
			effect->maxSamples = m_maxSamples;
			effect->sampleInterval = m_sampleInterval;
			effect->fadeDuration = m_fadeDuration;
		}

        // 시간 초기화 (deltaTime 누적 방식)
        m_currentTime = 0.0f;
        m_lastSampleTime = -effect->sampleInterval; // 첫 샘플을 바로 추가하도록
        m_isActive = true;
        m_hasStarted = false;

        AddTrailSample(effect, m_rootPoint, m_tipPoint, m_currentTime);
        ALICE_LOG_INFO("[SwordSlashEffect] 트레일 기반 검기 효과 초기화 완료");
    }

    void SwordSlashEffect::Update(float deltaTime)
    {
        auto* effect = GetComponent<SwordEffectComponent>();
        if (!effect) return;

        // Inspector 속성 동기화
		effect->maxSamples = m_maxSamples;
        effect->sampleInterval = m_sampleInterval;
        effect->fadeDuration = m_fadeDuration;

        if (!m_isActive)
        {
            effect->enabled = false;
            return;
        }

        // deltaTime 누적하여 현재 시간 계산
        m_currentTime += deltaTime;
        effect->currentTime = m_currentTime; // 렌더 시스템에서 사용할 수 있도록 동기화
        m_hasStarted = true;

        // 샘플링 간격에 따라 새 샘플 추가
        if (m_currentTime - m_lastSampleTime >= effect->sampleInterval)
        {
			DirectX::XMFLOAT3 rootPos = m_rootPoint;
            DirectX::XMFLOAT3 tipPos = m_tipPoint;
            DirectX::XMFLOAT3 ownerPos = GetTransform()->position;
            // 자동 이동 시뮬레이션 (테스트용)
            //if (m_autoMove)
            //{
            //    float angle = m_currentTime * m_moveSpeed;
            //    float radius = 2.0f;
            //    rootPos = DirectX::XMFLOAT3(
            //        std::cos(angle) * radius,
            //        1.5f + std::sin(angle * 0.5f) * 0.5f,
            //        std::sin(angle) * radius
            //    );
            //    tipPos = DirectX::XMFLOAT3(
            //        std::cos(angle + 0.3f) * (radius + 0.5f),
            //        rootPos.y + 0.3f,
            //        std::sin(angle + 0.3f) * (radius + 0.5f)
            //    );
            //}

			rootPos = DirectX::XMFLOAT3(ownerPos.x, ownerPos.y, ownerPos.z);
            tipPos = DirectX::XMFLOAT3(rootPos.x + 0.3f, rootPos.y + 0.3f, rootPos.z + 0.3f);

            AddTrailSample(effect, rootPos, tipPos, m_currentTime);
            m_lastSampleTime = m_currentTime;
        }

        // 오래된 샘플 제거 (age 기반)
        float fadeStartTime = m_currentTime - effect->fadeDuration;
        auto& samples = effect->trailSamples;
        samples.erase(
            std::remove_if(samples.begin(), samples.end(),
                [fadeStartTime](const TrailSample& s) { return s.birthTime < fadeStartTime; }),
            samples.end()
        );

        // 트레일 길이 업데이트
        UpdateTrailLength(effect);
    }

    void SwordSlashEffect::OnDestroy()
    {
        auto* effect = GetComponent<SwordEffectComponent>();
        if (effect)
        {
            effect->trailSamples.clear();
            effect->totalLength = 0.0f;
        }
    }

    void SwordSlashEffect::AddTrailSample(SwordEffectComponent* effect, const DirectX::XMFLOAT3& rootPos, const DirectX::XMFLOAT3& tipPos, float currentTime)
    {
        if (!effect) return;

        TrailSample sample;
        sample.rootPos = rootPos;
        sample.tipPos = tipPos;
        sample.birthTime = currentTime;
        
        // 누적 길이 계산
        auto& samples = effect->trailSamples;
        if (samples.empty())
        {
            sample.length = 0.0f;
        }
        else
        {
            const auto& lastSample = samples.back();
            using namespace DirectX;
            XMVECTOR v0 = XMLoadFloat3(&lastSample.tipPos);
            XMVECTOR v1 = XMLoadFloat3(&tipPos);
            XMVECTOR diff = XMVectorSubtract(v1, v0);
            float segmentLength = XMVectorGetX(XMVector3Length(diff));
            sample.length = lastSample.length + segmentLength;
        }

        samples.push_back(sample);

        // 링 버퍼: 최대 샘플 수 초과 시 오래된 샘플 제거
        if (samples.size() > static_cast<size_t>(effect->maxSamples))
        {
            samples.erase(samples.begin());
            // 제거 후 길이 재계산 필요 (간단하게 전체 재계산)
            UpdateTrailLength(effect);
        }
    }

    void SwordSlashEffect::UpdateTrailLength(SwordEffectComponent* effect)
    {
        if (!effect || effect->trailSamples.empty()) 
        {
            effect->totalLength = 0.0f;
            return;
        }

        // 전체 길이는 마지막 샘플의 누적 길이
        effect->totalLength = effect->trailSamples.back().length;
        
        // 길이 기반으로 각 샘플의 누적 길이 재계산 (링 버퍼로 인한 제거 후)
        if (effect->trailSamples.size() > 1)
        {
            auto& samples = effect->trailSamples;
            samples[0].length = 0.0f;
            for (size_t i = 1; i < samples.size(); ++i)
            {
                using namespace DirectX;
                XMVECTOR v0 = XMLoadFloat3(&samples[i - 1].tipPos);
                XMVECTOR v1 = XMLoadFloat3(&samples[i].tipPos);
                XMVECTOR diff = XMVectorSubtract(v1, v0);
                float segmentLength = XMVectorGetX(XMVector3Length(diff));
                samples[i].length = samples[i - 1].length + segmentLength;
            }
            effect->totalLength = samples.back().length;
        }
    }
}
