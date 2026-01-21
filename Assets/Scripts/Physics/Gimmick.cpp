#include "Gimmick.h"
#include "Core/ScriptFactory.h"
#include "Core/Logger.h"
#include "Core/World.h"
#include "Core/Input.h"
#include "Core/GameObject.h"
#include "Components/TransformComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/MaterialComponent.h"
#include "PhysX/Components/Phy_RigidBodyComponent.h"
#include "PhysX/Components/Phy_JointComponent.h"
#include "PhysX/Components/Phy_ColliderComponent.h"
#include "PhysX/Components/Phy_MeshColliderComponent.h"
#include "PhysX/IPhysicsWorld.h"
#include <cmath>
#include <random>

namespace Alice
{
    // 이 스크립트를 리플렉션/팩토리 시스템에 등록합니다.
    REGISTER_SCRIPT(Gimmick);

    void Gimmick::Start()
    {
        // Legacy와 파츠 찾기
        FindLegacy();
        FindParts();
        
        if (m_parts.size() == 5 && m_legacyEntity != InvalidEntityId)
        {
            // 초기 상태: 조립됨 (legacy 활성화, parts 비활성화)
            m_state = AssemblyState::Assembled;
            m_assemblingPartIndex = 0;
            m_originalPartTriggerStates.resize(5, false);
            
            // Legacy 트랜스폼 저장
            SaveLegacyTransform();
            
            // Legacy 활성화 (이미 활성화되어 있을 수 있음)
            ActivateLegacy();
            
            // Parts 비활성화
            DeactivateParts();
            
            ALICE_LOG_INFO("[Gimmick] Initialized - Legacy and 5 parts found");
        }
        else
        {
            ALICE_LOG_WARN("[Gimmick] Expected 5 parts and legacy, found %zu parts, legacy: %u", 
                m_parts.size(), m_legacyEntity);
        }
    }

    void Gimmick::Update(float deltaTime)
    {
        auto* input = Input();
        if (!input)
            return;

        // 스페이스 키 입력 감지 - 상태 전환
        if (input->GetKeyDown(KeyCode::Space))
        {
            if (m_state == AssemblyState::Assembled)
            {
                // 해체: Legacy 비활성화, Parts 활성화 후 폭발
                SaveLegacyTransform();
                DeactivateLegacy();
                ActivateParts();
                ApplyExplosionForce();
                m_state = AssemblyState::Disassembled;
                ALICE_LOG_INFO("[Gimmick] Disassembled - Legacy disabled, Parts activated");
            }
            else if (m_state == AssemblyState::Disassembled)
            {
                // 조립 시도 시작: 조인트 생성 및 물리 충돌 끄기
                StartAssembling();
                m_state = AssemblyState::Assembling;
                ALICE_LOG_INFO("[Gimmick] Assembling started - Parts will gather");
            }
            else if (m_state == AssemblyState::Assembling)
            {
                // 조립 완료: Parts 비활성화, Legacy 활성화
                RemoveJoints();
                EnablePartsCollision(); // 충돌 복원
                DeactivateParts();
                ActivateLegacy();
                m_state = AssemblyState::Assembled;
                m_assemblingPartIndex = 0;
                ALICE_LOG_INFO("[Gimmick] Assembled - Legacy activated, Parts deactivated");
            }
        }

        // 조립 시도 중 업데이트
        if (m_state == AssemblyState::Assembling)
        {
            UpdateAssembling(deltaTime);
        }
    }

    void Gimmick::FindLegacy()
    {
        auto* world = GetWorld();
        if (!world)
            return;

        // parts_legacy 오브젝트 찾기 (이 스크립트가 붙은 오브젝트)
        m_legacyEntity = this->gameObject().id();

        if (m_legacyEntity != InvalidEntityId)
        {
            // Legacy의 컴포넌트 정보 저장
            if (auto* skinnedMesh = world->GetComponent<SkinnedMeshComponent>(m_legacyEntity))
            {
                m_legacyInfo.hasSkinnedMesh = true;
                m_legacyInfo.skinnedMeshAssetPath = skinnedMesh->meshAssetPath;
            }
            
            if (auto* material = world->GetComponent<MaterialComponent>(m_legacyEntity))
            {
                m_legacyInfo.hasMaterial = true;
                m_legacyInfo.materialColor = material->color;
                m_legacyInfo.materialRoughness = material->roughness;
                m_legacyInfo.materialMetalness = material->metalness;
                m_legacyInfo.materialAssetPath = material->assetPath;
            }
            
            if (auto* rb = world->GetComponent<Phy_RigidBodyComponent>(m_legacyEntity))
            {
                m_legacyInfo.hasRigidBody = true;
            }
            
            if (auto* collider = world->GetComponent<Phy_ColliderComponent>(m_legacyEntity))
            {
                m_legacyInfo.hasCollider = true;
            }
            
            if (auto* meshCollider = world->GetComponent<Phy_MeshColliderComponent>(m_legacyEntity))
            {
                m_legacyInfo.hasMeshCollider = true;
            }
            
            // 트랜스폼 저장
            SaveLegacyTransform();
            
            ALICE_LOG_INFO("[Gimmick] Legacy found: %u", m_legacyEntity);
        }
        else
        {
            ALICE_LOG_WARN("[Gimmick] Legacy not found");
        }
    }

    void Gimmick::SaveLegacyTransform()
    {
        auto* world = GetWorld();
        if (!world || m_legacyEntity == InvalidEntityId)
            return;

        if (auto* transform = world->GetComponent<TransformComponent>(m_legacyEntity))
        {
            m_legacyInfo.legacyPosition = transform->position;
            m_legacyInfo.legacyRotation = transform->rotation;
            m_legacyInfo.legacyScale = transform->scale;
        }
    }

    void Gimmick::FindParts()
    {
        auto* world = GetWorld();
        if (!world)
            return;

        m_parts.clear();
        for (int i = 1; i <= 5; ++i)
        {
            std::string partName = "parts_" + std::to_string(i);
            GameObject go = world->FindGameObject(partName);
            if (go.IsValid())
            {
                m_parts.push_back(go.id());
            }
            else
            {
                ALICE_LOG_WARN("[Gimmick] Part not found: %s", partName.c_str());
            }
        }
    }

    void Gimmick::ActivateLegacy()
    {
        auto* world = GetWorld();
        if (!world || m_legacyEntity == InvalidEntityId)
            return;

        // Legacy의 컴포넌트 복원
        if (m_legacyInfo.hasSkinnedMesh)
        {
            if (!world->GetComponent<SkinnedMeshComponent>(m_legacyEntity))
            {
                auto& skinnedMesh = world->AddComponent<SkinnedMeshComponent>(m_legacyEntity);
                skinnedMesh.meshAssetPath = m_legacyInfo.skinnedMeshAssetPath;
            }
        }

        if (m_legacyInfo.hasMaterial)
        {
            if (!world->GetComponent<MaterialComponent>(m_legacyEntity))
            {
                auto& material = world->AddComponent<MaterialComponent>(m_legacyEntity);
                material.color = m_legacyInfo.materialColor;
                material.roughness = m_legacyInfo.materialRoughness;
                material.metalness = m_legacyInfo.materialMetalness;
                material.assetPath = m_legacyInfo.materialAssetPath;
            }
        }

        if (m_legacyInfo.hasRigidBody)
        {
            if (!world->GetComponent<Phy_RigidBodyComponent>(m_legacyEntity))
            {
                world->AddComponent<Phy_RigidBodyComponent>(m_legacyEntity);
            }
        }

        if (m_legacyInfo.hasCollider)
        {
            if (!world->GetComponent<Phy_ColliderComponent>(m_legacyEntity))
            {
                world->AddComponent<Phy_ColliderComponent>(m_legacyEntity);
            }
        }

        if (m_legacyInfo.hasMeshCollider)
        {
            if (!world->GetComponent<Phy_MeshColliderComponent>(m_legacyEntity))
            {
                world->AddComponent<Phy_MeshColliderComponent>(m_legacyEntity);
            }
        }

        // Legacy 위치 복원
        if (auto* transform = world->GetComponent<TransformComponent>(m_legacyEntity))
        {
            transform->position = m_legacyInfo.legacyPosition;
        }
    }

    void Gimmick::DeactivateLegacy()
    {
        auto* world = GetWorld();
        if (!world || m_legacyEntity == InvalidEntityId)
            return;

        // Legacy 위치 저장
        if (auto* transform = world->GetComponent<TransformComponent>(m_legacyEntity))
        {
            m_legacyInfo.legacyPosition = transform->position;
        }

        // 렌더링 및 물리 컴포넌트 제거 (Transform은 유지)
        if (m_legacyInfo.hasSkinnedMesh)
        {
            world->RemoveComponent<SkinnedMeshComponent>(m_legacyEntity);
        }

        if (m_legacyInfo.hasMaterial)
        {
            world->RemoveComponent<MaterialComponent>(m_legacyEntity);
        }

        if (m_legacyInfo.hasRigidBody)
        {
            world->RemoveComponent<Phy_RigidBodyComponent>(m_legacyEntity);
        }

        if (m_legacyInfo.hasCollider)
        {
            world->RemoveComponent<Phy_ColliderComponent>(m_legacyEntity);
        }

        if (m_legacyInfo.hasMeshCollider)
        {
            world->RemoveComponent<Phy_MeshColliderComponent>(m_legacyEntity);
        }
    }

    void Gimmick::ActivateParts()
    {
        auto* world = GetWorld();
        if (!world || m_legacyEntity == InvalidEntityId)
            return;

        // Legacy 트랜스폼을 parts에 계승
        for (size_t i = 0; i < m_parts.size(); ++i)
        {
            EntityId partId = m_parts[i];
            
            // Legacy 트랜스폼 계승
            if (auto* transform = world->GetComponent<TransformComponent>(partId))
            {
                transform->position = m_legacyInfo.legacyPosition;
                transform->rotation = m_legacyInfo.legacyRotation;
                transform->scale = m_legacyInfo.legacyScale;
                
                // RigidBody가 있으면 텔레포트
                if (auto* rb = world->GetComponent<Phy_RigidBodyComponent>(partId))
                {
                    rb->teleport = true;
                    rb->gravityEnabled = true; // 중력 활성화 (바닥을 구르기 위해)
                }
            }
        }
    }

    void Gimmick::DeactivateParts()
    {
        auto* world = GetWorld();
        if (!world)
            return;

        // Parts 비활성화: 렌더링 및 물리 컴포넌트 제거 (Transform은 유지)
        for (EntityId partId : m_parts)
        {
            // Legacy 트랜스폼으로 이동
            if (auto* transform = world->GetComponent<TransformComponent>(partId))
            {
                transform->position = m_legacyInfo.legacyPosition;
                transform->rotation = m_legacyInfo.legacyRotation;
                transform->scale = m_legacyInfo.legacyScale;
            }

            // 렌더링 컴포넌트 제거
            if (world->GetComponent<SkinnedMeshComponent>(partId))
            {
                world->RemoveComponent<SkinnedMeshComponent>(partId);
            }
            
            if (world->GetComponent<MaterialComponent>(partId))
            {
                world->RemoveComponent<MaterialComponent>(partId);
            }

            // 물리 컴포넌트 제거
            if (world->GetComponent<Phy_RigidBodyComponent>(partId))
            {
                world->RemoveComponent<Phy_RigidBodyComponent>(partId);
            }
            
            if (world->GetComponent<Phy_ColliderComponent>(partId))
            {
                world->RemoveComponent<Phy_ColliderComponent>(partId);
            }
            
            if (world->GetComponent<Phy_MeshColliderComponent>(partId))
            {
                world->RemoveComponent<Phy_MeshColliderComponent>(partId);
            }
        }
    }

    void Gimmick::StartAssembling()
    {
        auto* world = GetWorld();
        if (!world || m_parts.empty())
            return;

        // 기존 조인트 제거
        RemoveJoints();

        // Parts의 물리 충돌 끄기 (trigger로 설정)
        DisablePartsCollision();

        // 조립 인덱스 초기화
        m_assemblingPartIndex = 0;

        // parts_1을 먼저 Legacy 위치로 이동
        if (m_parts.size() > 0)
        {
            EntityId part1Id = m_parts[0];
            if (auto* transform = world->GetComponent<TransformComponent>(part1Id))
            {
                transform->position = m_legacyInfo.legacyPosition;
                transform->rotation = m_legacyInfo.legacyRotation;
                transform->scale = m_legacyInfo.legacyScale;
                
                if (auto* rb = world->GetComponent<Phy_RigidBodyComponent>(part1Id))
                {
                    rb->teleport = true;
                    rb->gravityEnabled = false;
                    
                    if (rb->physicsActorHandle)
                    {
                        IRigidBody* body = rb->physicsActorHandle;
                        if (body && body->IsValid())
                        {
                            Vec3 zero(0, 0, 0);
                            body->SetLinearVelocity(zero);
                            body->SetAngularVelocity(zero);
                        }
                    }
                }
            }
            m_assemblingPartIndex = 1; // 다음은 parts_2부터
        }
    }

    void Gimmick::UpdateAssembling(float deltaTime)
    {
        auto* world = GetWorld();
        if (!world || m_parts.empty() || m_assemblingPartIndex >= static_cast<int>(m_parts.size()))
            return;

        // 현재 조립할 파츠
        EntityId currentPartId = m_parts[m_assemblingPartIndex];
        
        // 이전 파츠 (이미 조립된 파츠)
        EntityId prevPartId = m_parts[m_assemblingPartIndex - 1];

        // 이전 파츠의 위치 가져오기
        DirectX::XMFLOAT3 prevPos = { 0, 0, 0 };
        if (auto* prevTransform = world->GetComponent<TransformComponent>(prevPartId))
        {
            prevPos = prevTransform->position;
        }

        // 현재 파츠의 위치 가져오기
        DirectX::XMFLOAT3 currentPos = { 0, 0, 0 };
        if (auto* currentTransform = world->GetComponent<TransformComponent>(currentPartId))
        {
            currentPos = currentTransform->position;
        }

        // 거리 계산
        float dx = currentPos.x - prevPos.x;
        float dy = currentPos.y - prevPos.y;
        float dz = currentPos.z - prevPos.z;
        float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

        // 조인트가 없으면 생성
        bool hasJoint = false;
        for (EntityId jointId : m_jointEntities)
        {
            if (jointId == currentPartId)
            {
                hasJoint = true;
                break;
            }
        }

        if (!hasJoint)
        {
            // 조인트 생성: 현재 파츠를 이전 파츠에 연결
            Phy_JointComponent& joint = world->AddComponent<Phy_JointComponent>(currentPartId);
            joint.type = Phy_JointType::Distance;
            
            // 이전 파츠 이름 설정
            std::string prevPartName = "parts_" + std::to_string(m_assemblingPartIndex);
            joint.targetName = prevPartName;
            
            // Distance Joint 설정
            joint.distance.minDistance = 0.0f;
            joint.distance.maxDistance = m_assemblyDistance; // 적당한 거리 유지
            joint.distance.tolerance = 0.1f;
            joint.distance.enableMinDistance = false;
            joint.distance.enableMaxDistance = true;
            joint.distance.enableSpring = true;
            joint.distance.stiffness = 100.0f;
            joint.distance.damping = 20.0f;
            
            joint.collideConnected = false;
            
            m_jointEntities.push_back(currentPartId);
        }

        // 거리가 충분히 가까우면 다음 파츠로
        if (distance <= m_assemblyDistance * 1.5f) // 여유를 두고
        {
            m_assemblingPartIndex++;
        }

        // 모든 파츠가 조립되면 Legacy 위치로 이동 시작
        if (m_assemblingPartIndex >= static_cast<int>(m_parts.size()))
        {
            // parts_1을 Legacy 위치로 점진적으로 이동
            // 조인트가 있으므로 다른 파츠들도 따라올 것
            EntityId part1Id = m_parts[0];
            if (auto* transform = world->GetComponent<TransformComponent>(part1Id))
            {
                DirectX::XMFLOAT3 currentPos = transform->position;
                DirectX::XMFLOAT3 targetPos = m_legacyInfo.legacyPosition;
                
                // Legacy 위치로 점진적으로 이동 (Lerp)
                float moveSpeed = 2.0f; // 초당 이동 속도
                float dx = targetPos.x - currentPos.x;
                float dy = targetPos.y - currentPos.y;
                float dz = targetPos.z - currentPos.z;
                float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
                
                if (distance > 0.1f)
                {
                    // 방향 벡터 정규화
                    float invDist = 1.0f / distance;
                    dx *= invDist;
                    dy *= invDist;
                    dz *= invDist;
                    
                    // 이동
                    float moveAmount = moveSpeed * deltaTime;
                    if (moveAmount > distance)
                        moveAmount = distance;
                    
                    transform->position.x += dx * moveAmount;
                    transform->position.y += dy * moveAmount;
                    transform->position.z += dz * moveAmount;
                    
                    // 회전도 점진적으로
                    transform->rotation = m_legacyInfo.legacyRotation;
                    transform->scale = m_legacyInfo.legacyScale;
                }
                else
                {
                    // 충분히 가까우면 정확히 Legacy 위치로
                    transform->position = targetPos;
                    transform->rotation = m_legacyInfo.legacyRotation;
                    transform->scale = m_legacyInfo.legacyScale;
                }
            }
        }
    }

    void Gimmick::EnablePartsCollision()
    {
        auto* world = GetWorld();
        if (!world)
            return;

        // Parts의 Collider trigger 상태 복원
        for (size_t i = 0; i < m_parts.size() && i < m_originalPartTriggerStates.size(); ++i)
        {
            EntityId partId = m_parts[i];
            bool originalTrigger = m_originalPartTriggerStates[i];
            
            if (auto* collider = world->GetComponent<Phy_ColliderComponent>(partId))
            {
                collider->isTrigger = originalTrigger;
            }
            else if (auto* meshCollider = world->GetComponent<Phy_MeshColliderComponent>(partId))
            {
                meshCollider->isTrigger = originalTrigger;
            }
        }
    }

    void Gimmick::DisablePartsCollision()
    {
        auto* world = GetWorld();
        if (!world)
            return;

        // Parts의 Collider trigger 상태 저장 및 trigger로 설정
        m_originalPartTriggerStates.clear();
        m_originalPartTriggerStates.reserve(m_parts.size());

        for (EntityId partId : m_parts)
        {
            bool originalTrigger = false;
            
            if (auto* collider = world->GetComponent<Phy_ColliderComponent>(partId))
            {
                originalTrigger = collider->isTrigger;
                m_originalPartTriggerStates.push_back(originalTrigger);
                collider->isTrigger = true; // 충돌 끄기
            }
            else if (auto* meshCollider = world->GetComponent<Phy_MeshColliderComponent>(partId))
            {
                originalTrigger = meshCollider->isTrigger;
                m_originalPartTriggerStates.push_back(originalTrigger);
                meshCollider->isTrigger = true; // 충돌 끄기
            }
            else
            {
                m_originalPartTriggerStates.push_back(false);
            }
        }
    }


    void Gimmick::RemoveJoints()
    {
        auto* world = GetWorld();
        if (!world)
            return;

        // 모든 조인트 제거
        for (EntityId jointEntity : m_jointEntities)
        {
            if (jointEntity != InvalidEntityId)
            {
                world->RemoveComponent<Phy_JointComponent>(jointEntity);
            }
        }
        m_jointEntities.clear();
    }



    void Gimmick::ApplyExplosionForce()
    {
        auto* world = GetWorld();
        if (!world)
            return;

        // 각 파츠마다 다른 방향으로 날아가도록 각도를 균등하게 분배
        const float angleStep = (2.0f * 3.14159265f) / static_cast<float>(m_parts.size());
        std::uniform_real_distribution<float> forceDist(8.0f, 15.0f);
        
        // 랜덤 생성기 (힘의 크기만 랜덤)
        std::random_device rd;
        std::mt19937 gen(rd());

        // 각 파츠에 다른 방향으로 힘을 가함 (바닥을 구르도록)
        for (size_t i = 0; i < m_parts.size(); ++i)
        {
            EntityId partId = m_parts[i];
            if (auto* rb = world->GetComponent<Phy_RigidBodyComponent>(partId))
            {
                // 중력 활성화 (바닥을 구르기 위해)
                rb->gravityEnabled = true;
                
                // 각 파츠마다 다른 각도 사용 (균등 분배)
                float angle = angleStep * static_cast<float>(i);
                float force = forceDist(gen); // 힘의 크기는 랜덤
                
                // XZ 평면에서 방향 계산 (각 파츠마다 다른 방향)
                DirectX::XMFLOAT3 velocity;
                velocity.x = std::cos(angle) * force;
                velocity.y = std::sin(angle * 0.3f) * force * 0.5f + 1.5f; // 위로 약간 튀어오르게
                velocity.z = std::sin(angle) * force;
                
                // RigidBody 핸들을 통해 velocity 설정
                if (rb->physicsActorHandle)
                {
                    IRigidBody* body = rb->physicsActorHandle;
                    if (body && body->IsValid())
                    {
                        // Vec3로 변환
                        Vec3 vel(velocity.x, velocity.y, velocity.z);
                        body->SetLinearVelocity(vel);
                    }
                }
            }
        }
        
        ALICE_LOG_INFO("[Gimmick] Applied explosion force to %zu parts - Parts will roll on ground", m_parts.size());
    }
}
