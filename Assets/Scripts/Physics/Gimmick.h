#pragma once

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"
#include <string>
#include <vector>
#include "Core/Entity.h"
#include "DirectXMath.h"

namespace Alice
{
    // 조인트를 이용한 파츠 연결/해제 스크립트
    class Gimmick : public IScript
    {
        ALICE_BODY(Gimmick);

    public:
        void Start() override;
        void Update(float deltaTime) override;

    private:
        // 파츠 엔티티들 (parts_1 ~ parts_5)
        std::vector<EntityId> m_parts;
        
        // 조인트 엔티티들 (각 파츠에 연결된 조인트)
        std::vector<EntityId> m_jointEntities;
        
        // 원래 Collider의 trigger 상태 저장 (복원용)
        std::vector<bool> m_originalTriggerStates;
        
        // 조인트 연결 상태
        bool m_jointsConnected = true;
        
        // 조인트 재연결 시 사용할 초기 maxDistance
        float m_initialMaxDistance = 5.0f;
        
        // 조인트 재연결 애니메이션 진행도 (0.0 ~ 1.0)
        float m_reconnectProgress = 0.0f;
        
        // 조인트 재연결 속도
        float m_reconnectSpeed = 2.0f; // 초당 진행도
        
        // Fixed Joint로 전환할 거리 임계값
        float m_fixedJointThreshold = 0.1f;
        
        // Fixed Joint로 전환되었는지 여부
        bool m_isFixed = false;
        
        // 파츠 찾기 및 조인트 초기화
        void FindParts();
        void CreateJoints();
        void RemoveJoints();
        void ReconnectJoints(float deltaTime);
        void ConvertToFixedJoints();
        
        // 파츠에 힘을 가해서 튀어나가게 만들기
        void ApplyExplosionForce();
    };
}
