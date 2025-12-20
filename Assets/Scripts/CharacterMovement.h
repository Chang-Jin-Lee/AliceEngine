#pragma once

#include "Core/Script.h"

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

        void FixedUpdate(float fixedDeltaTime) override;

    private:
        float m_moveSpeed = 4.0f;
        float m_jumpSpeed = 6.5f;
        float m_gravity   = 18.0f;

        float m_velY = 0.0f;
    };
}


