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

//
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
//        void MoveDirectly();
//        void MoveLerp(const float& lateDeltaTime);
//
//        void LateUpdate(float lateDeltaTime) override;
//
//    private:
//        // 간단 오프셋 (유니티의 third-person 카메라 느낌)
//        ALICE_SERIALIZE_FIELD(float, m_offsetX, 0.0f);
//        ALICE_SERIALIZE_FIELD(float, m_offsetY, 28.0f);
//        ALICE_SERIALIZE_FIELD(float, m_offsetZ, -13.0f);
//        ALICE_SERIALIZE_FIELD(float, m_smoothSpeed, 0.0f);
//    };
//}
//
//



#pragma once

#include "Core/Script.h"
#include "Core/ScriptReflection.h"

namespace Alice
{
    /// 유니티 느낌의 간단한 카메라 팔로우 스크립트
    class CameraFollow : public IScript
    {
        // 리플렉션 설정 (생성자, 타입정보 등록)
        ALICE_BODY(CameraFollow);

    public:
        // 메인 로직 (엔진이 호출하므로 별도 등록 불필요)
        void LateUpdate(float lateDeltaTime) override;

        // [2] 에디터에서 테스트할 수 있게 함수 등록
        void MoveDirectly();
        ALICE_FUNC(MoveDirectly); // 버튼으로 노출됨

        void MoveLerp(const float& lateDeltaTime);
        ALICE_FUNC(MoveLerp);     // 인자가 있는 함수도 등록 가능

        // [3] 변수 선언 + 직렬화 + Getter/Setter 자동 생성
        ALICE_PROPERTY(float, m_offsetX, 0.0f);
        ALICE_PROPERTY(float, m_offsetY, 28.0f);
        ALICE_PROPERTY(float, m_offsetZ, -13.0f);
        ALICE_PROPERTY(float, m_smoothSpeed, 0.0f);
    };
}