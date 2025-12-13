#include "RotateAndScale.h"
#include "Core/World.h"

#include <cmath> // std::sin
#include <Core/Logger.h>

namespace Alice
{
    // 이 스크립트를 리플렉션/팩토리 시스템에 등록합니다.
    REGISTER_SCRIPT(RotateAndScale);

    void RotateAndScale::OnCreate(World& world, EntityId entity)
    {
        // 이 엔티티에 Transform 이 없으면 하나 추가합니다.
        if (auto* t = world.GetTransform(entity); t)
        {
            t = &world.AddTransform(entity);
            // 기준 스케일(x)을 기억해 두고, 시간이 흐르면서 이 값을 중심으로 진동시킵니다.
            m_baseScale = t->scale.x;
            if (m_baseScale <= 0.0f)
            {
                m_baseScale = 1.0f;
            }
            m_timeSeconds = 0.0f;
        }
    }

    void RotateAndScale::OnUpdate(World& world, EntityId entity, float deltaTime)
    {
        // 경과 시간 누적
        m_timeSeconds += deltaTime;

        if (auto* t = world.GetTransform(entity); t)
        {
            // (1) Y 축으로 초당 약 1라디안씩 회전
            t->rotation.y += 1.0f * deltaTime;

            // (2) 시간에 따라 스케일이 0.75 ~ 1.25 배 사이에서 천천히 진동
            const float amplitude = 0.25f;     // 스케일 변동 폭 (25%)
            float s = m_baseScale * (1.0f + amplitude * std::sin(m_timeSeconds * 2.0f));
            t->scale.x = s;
            t->scale.y = s;
            t->scale.z = s;
        }

    }
}
