#pragma once

#include "Core/Script.h"
#include "Core/ScriptReflection.h"

namespace Alice
{
    // 예제용 스크립트:
    // - OnCreate: 기준 스케일을 기억합니다.
    // - OnUpdate: Y축으로 천천히 회전시키고, 시간에 따라 스케일이 살짝 커졌다/작아졌다 합니다.
    class RotateAndScale : public IScript
    {
    public:
        const char* GetName() const override { return "RotateAndScale"; }

        void Start() override;
        void Update(float deltaTime) override;

    private:
        // 노출/저장하고 싶은 값은 SerializeField로 선언합니다.
        ALICE_SERIALIZE_FIELD(float, m_spinSpeed, 1.0f);
        ALICE_SERIALIZE_FIELD(float, m_pulseSpeed, 2.0f);
        ALICE_SERIALIZE_FIELD(float, m_pulseAmplitude, 0.25f);

        float m_timeSeconds = 0.0f; // 런타임 상태
        float m_baseScale   = 1.0f; // 런타임 상태
    };
}
