#include "NewScript.h"
#include "Core/World.h"

#include <cmath>

namespace Alice
{
    // 이 스크립트를 리플렉션/팩토리 시스템에 등록합니다.
    REGISTER_SCRIPT(NewScript);

    void NewScript::OnCreate(World& world, EntityId entity)
    {
        if (auto* t = transform())
        {
            // 초기 위치/스케일을 설정해 줍니다.
            t->SetPosition(0.0f, 0.0f, 0.0f)
             .SetScale(1.0f, 1.0f, 1.0f);
        }
    }

    void NewScript::OnUpdate(World& world, EntityId entity, float deltaTime)
    {
        m_elapsedTime += deltaTime;

        auto* t = transform();
        if (!t)
            return;

        // 1) Y축 회전 예시 (초당 45도 회전)
        const float angularSpeed = 3.14159265f / 4.0f; // 45도/초
        t->rotation.y += angularSpeed * deltaTime;

        // 2) 스케일을 시간에 따라 살짝 펄싱 시킵니다.
        const float baseScale = 1.0f;
        const float pulseAmp  = 0.25f;
        const float pulseFreq = 2.0f; // 초당 2회 진동
        const float s = baseScale + pulseAmp * std::sinf(m_elapsedTime * pulseFreq);

        t->SetScale(s, s, s);
    }
}
