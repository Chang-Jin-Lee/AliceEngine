#pragma once

#include "Core/Script.h"

namespace Alice
{
    /// 유니티 느낌의 간단한 카메라 팔로우 스크립트입니다.
    /// - FixedUpdate에서 "첫번째 SkinnedMesh 엔티티"를 따라갑니다.
    /// - 목표가 없으면 아무 것도 하지 않습니다.
    class CameraFollow : public IScript
    {
    public:
        const char* GetName() const override { return "CameraFollow"; }

        void FixedUpdate(float fixedDeltaTime) override;

    private:
        // 간단 오프셋 (유니티의 third-person 카메라 느낌)
        float m_offsetX = 0.0f;
        float m_offsetY = 2.0f;
        float m_offsetZ = -5.0f;
    };
}


