#pragma once

#include "Core/Script.h"
#include "Core/ScriptReflection.h"

namespace Alice
{
    /// 유니티 스타일의 아주 단순한 캐릭터 이동 스크립트입니다.
    /// - WASD: 이동
    /// - Space: 점프(바닥(y<=0)에서만)
    /// - FixedUpdate 기반(프레임레이트 영향 최소)
    class CharacterMovement : public IScript
    {
    public:
        const char* GetName() const override { return "CharacterMovement"; }

        void Update(float DeltaTime) override;

    private:
        // private + SerializeField 처럼 쓰고 싶으면 이 매크로로 선언합니다.
        ALICE_SERIALIZE_FIELD(float, m_moveSpeed, 10.0f);
        ALICE_SERIALIZE_FIELD(float, m_jumpSpeed, 6.5f);
        ALICE_SERIALIZE_FIELD(float, m_gravity,   18.0f);

        float m_velY = 0.0f; // 런타임 상태(저장/노출 X)
    };
}


