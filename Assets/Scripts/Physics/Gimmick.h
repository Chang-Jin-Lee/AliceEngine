#pragma once

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"
#include <string>
#include <vector>
#include <memory>
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
        // Legacy 오브젝트 (원본)
        EntityId m_legacyEntity = InvalidEntityId;
        
        // 파츠 엔티티들 (parts_1 ~ parts_5)
        std::vector<EntityId> m_parts;
        
        // 조인트 엔티티들 (각 파츠에 연결된 조인트)
        std::vector<EntityId> m_jointEntities;
        
        // Legacy의 원래 컴포넌트 정보 저장
        struct LegacyComponentInfo
        {
            bool hasSkinnedMesh = false;
            bool hasMaterial = false;
            bool hasRigidBody = false;
            bool hasCollider = false;
            bool hasMeshCollider = false;
            // 컴포넌트 데이터 저장
            std::string skinnedMeshAssetPath;
            std::string materialAssetPath;
            DirectX::XMFLOAT3 materialColor;
            float materialRoughness = 0.5f;
            float materialMetalness = 0.0f;
            DirectX::XMFLOAT3 legacyPosition;
            DirectX::XMFLOAT3 legacyRotation;
            DirectX::XMFLOAT3 legacyScale;
        } m_legacyInfo;
        
        // Parts의 원래 Collider trigger 상태 저장
        std::vector<bool> m_originalPartTriggerStates;
        
        // 조립 상태
        enum class AssemblyState
        {
            Assembled,      // 조립됨 (legacy 활성화, parts 비활성화)
            Disassembled,   // 해체됨 (legacy 비활성화, parts 활성화, 물리 작용)
            Assembling      // 조립 시도 중 (parts들이 조인트로 뭉치며 legacy 위치로 이동)
        };
        AssemblyState m_state = AssemblyState::Assembled;
        
        // 조립 시 유지할 거리
        float m_assemblyDistance = 0.3f;
        
        // 폭발 힘 적용 재시도 플래그
        bool m_pendingExplosion = false;
        
        
        // 파츠 찾기 및 초기화
        void FindLegacy();
        void FindParts();
        void SaveLegacyTransform();
        
        // 활성화/비활성화
        void ActivateLegacy();
        void DeactivateLegacy();
        void ActivateParts();
        void DeactivateParts();
        
        // 조인트 관련
        void RemoveJoints();
        void StartAssembling();
        void UpdateAssembling(float deltaTime);
        
        // 파츠에 힘을 가해서 튀어나가게 만들기
        void ApplyExplosionForce();
        
        // Parts의 물리 충돌 켜기/끄기
        void EnablePartsCollision();
        void DisablePartsCollision();
    };
}
