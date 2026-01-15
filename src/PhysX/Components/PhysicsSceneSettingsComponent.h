#pragma once
#include <DirectXMath.h>

// opt-in 방식 이라고 하더라
// 표식 컴포넌트
// 해당 컴포넌트가 있는지 없는지로 물리를 사용하는지 안하는지 확인함

struct PhysicsSceneSettingsComponent
{
    bool enablePhysics = true;

    // Transform이 DirectX 기준이니까 gravity도 XMFLOAT3로
    DirectX::XMFLOAT3 gravity = { 0.0f, -9.81f, 0.0f };

    float fixedDt = 1.0f / 60.0f;
    int   maxSubsteps = 4;
};
