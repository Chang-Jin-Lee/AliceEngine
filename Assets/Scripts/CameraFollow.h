//#pragma once
//
//#include "Core/Script.h"
//#include "Core/ScriptReflection.h"
//
//namespace Alice
//{
//    /// 유니티 느낌의 간단한 카메라 팔로우 스크립트입니다.
//    /// - FixedUpdate에서 "첫번째 SkinnedMesh 엔티티"를 따라갑니다.
//    /// - 목표가 없으면 아무 것도 하지 않습니다.
//    class CameraFollow : public IScript
//    {
//    public:
//        const char* GetName() const override { return "CameraFollow"; }
//
//        void LateUpdate(float lateDeltaTime) override;
//
//    private:
//        // 간단 오프셋 (유니티의 third-person 카메라 느낌)
//        ALICE_SERIALIZE_FIELD(float, m_distance, 0.0f);         // 타겟과의 거리
//        ALICE_SERIALIZE_FIELD(float, m_sensitivity, 28.0f);     // 마우스 회전 감도
//        ALICE_SERIALIZE_FIELD(float, m_heightOffset, -13.0f);   // 타겟의 높이 보정 (머리 위 등)
//    };
//}


#pragma once

#include "Core/Script.h"
#include "Core/ScriptReflection.h"

namespace Alice
{
    /// 유니티 느낌의 간단한 카메라 팔로우 스크립트입니다.
    /// - FixedUpdate에서 "첫번째 SkinnedMesh 엔티티"를 따라갑니다.
    /// - 목표가 없으면 아무 것도 하지 않습니다.
    class CameraFollow : public IScript
    {
    public:
        const char* GetName() const override { return "CameraFollow"; }

        void MoveDirectly();
        void MoveLerp(const float& lateDeltaTime);

        void LateUpdate(float lateDeltaTime) override;

    private:
        // 간단 오프셋 (유니티의 third-person 카메라 느낌)
        ALICE_SERIALIZE_FIELD(float, m_offsetX, 0.0f);
        ALICE_SERIALIZE_FIELD(float, m_offsetY, 28.0f);
        ALICE_SERIALIZE_FIELD(float, m_offsetZ, -13.0f);
        ALICE_SERIALIZE_FIELD(float, m_smoothSpeed, 0.0f);
    };
}


