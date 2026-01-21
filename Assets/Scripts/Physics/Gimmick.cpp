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
                m_pendingExplosion = true; // 다음 프레임에 폭발 힘 적용
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
                // 조립 완료: 조인트 해제 → Parts 비활성화 → Legacy 활성화
                RemoveJoints(); // 조인트 해제
                EnablePartsCollision(); // 충돌 복원
                DeactivateParts(); // Parts 비활성화
                ActivateLegacy(); // Legacy 활성화
                m_state = AssemblyState::Assembled;
                ALICE_LOG_INFO("[Gimmick] Assembled - Joints removed, Parts deactivated, Legacy activated");
            }
        }

        // 폭발 힘 적용 재시도 (RigidBody가 생성될 때까지)
        if (m_pendingExplosion)
        {
            auto* world = GetWorld();
            if (world)
            {
                bool allReady = true;
                for (EntityId partId : m_parts)
                {
                    if (auto* rb = world->GetComponent<Phy_RigidBodyComponent>(partId))
                    {
                        if (rb->physicsActorHandle)
                        {
                            IRigidBody* body = rb->physicsActorHandle;
                            if (body && body->IsValid())
                            {
                                // RigidBody가 생성되었음
                                continue;
                            }
                        }
                    }
                    // RigidBody가 아직 생성되지 않음
                    allReady = false;
                    break;
                }
                
                if (allReady)
                {
                    // 모든 RigidBody가 생성되었으므로 폭발 힘 적용
                    ApplyExplosionForce();
                    m_pendingExplosion = false;
                }
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

        // Legacy 활성화: enabled 플래그만 설정
        if (auto* transform = world->GetComponent<TransformComponent>(m_legacyEntity))
        {
            transform->enabled = true;
            transform->position = m_legacyInfo.legacyPosition;
            transform->rotation = m_legacyInfo.legacyRotation;
            transform->scale = m_legacyInfo.legacyScale;
        }
    }

    void Gimmick::DeactivateLegacy()
    {
        auto* world = GetWorld();
        if (!world || m_legacyEntity == InvalidEntityId)
            return;

        // Legacy 위치 저장 및 비활성화
        if (auto* transform = world->GetComponent<TransformComponent>(m_legacyEntity))
        {
            m_legacyInfo.legacyPosition = transform->position;
            m_legacyInfo.legacyRotation = transform->rotation;
            m_legacyInfo.legacyScale = transform->scale;
            transform->enabled = false;
        }
    }

    void Gimmick::ActivateParts()
    {
        auto* world = GetWorld();
        if (!world || m_legacyEntity == InvalidEntityId)
            return;

        // Parts 활성화: enabled 플래그 설정 및 Legacy 트랜스폼 계승
        for (size_t i = 0; i < m_parts.size(); ++i)
        {
            EntityId partId = m_parts[i];
            
            if (auto* transform = world->GetComponent<TransformComponent>(partId))
            {
                transform->enabled = true;
                transform->position = m_legacyInfo.legacyPosition;
                transform->rotation = m_legacyInfo.legacyRotation;
                transform->scale = m_legacyInfo.legacyScale;
                
                // RigidBody가 있으면 텔레포트 및 중력 활성화
                if (auto* rb = world->GetComponent<Phy_RigidBodyComponent>(partId))
                {
                    rb->teleport = true;
                    rb->gravityEnabled = true;
                }
            }
        }
    }

    void Gimmick::DeactivateParts()
    {
        auto* world = GetWorld();
        if (!world)
            return;

        // Parts 비활성화: enabled 플래그만 설정
        for (EntityId partId : m_parts)
        {
            if (auto* transform = world->GetComponent<TransformComponent>(partId))
            {
                transform->enabled = false;
                transform->position = m_legacyInfo.legacyPosition;
                transform->rotation = m_legacyInfo.legacyRotation;
                transform->scale = m_legacyInfo.legacyScale;
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

        // 모든 파츠를 순차적으로 조인트로 연결 (1->2->3->4->5)
        for (size_t i = 1; i < m_parts.size(); ++i)
        {
            EntityId currentPartId = m_parts[i];
            EntityId prevPartId = m_parts[i - 1];
            
            // 조인트 생성: 현재 파츠를 이전 파츠에 연결
            Phy_JointComponent& joint = world->AddComponent<Phy_JointComponent>(currentPartId);
            joint.type = Phy_JointType::Distance;
            
            // 이전 파츠 이름 설정
            std::string prevPartName = "parts_" + std::to_string(i);
            joint.targetName = prevPartName;
            
            // Distance Joint 설정 (줄 당기는 효과)
            joint.distance.minDistance = 0.0f;
            joint.distance.maxDistance = m_assemblyDistance;
            joint.distance.tolerance = 0.1f;
            joint.distance.enableMinDistance = false;
            joint.distance.enableMaxDistance = true;
            joint.distance.enableSpring = true;
            joint.distance.stiffness = 100.0f;
            joint.distance.damping = 20.0f;
            
            joint.collideConnected = false;
            
            m_jointEntities.push_back(currentPartId);
        }
        
        // 모든 조인트 생성 완료
    }

    void Gimmick::UpdateAssembling(float deltaTime)
    {
        auto* world = GetWorld();
        if (!world || m_parts.empty())
            return;

        // parts_1을 Legacy 위치로 물리 기반으로 이동
        // 조인트가 있으므로 다른 파츠들도 따라올 것
        EntityId part1Id = m_parts[0];
        if (auto* transform = world->GetComponent<TransformComponent>(part1Id))
        {
            DirectX::XMFLOAT3 currentPos = transform->position;
            DirectX::XMFLOAT3 targetPos = m_legacyInfo.legacyPosition;
            
            // 거리 계산
            float dx = targetPos.x - currentPos.x;
            float dy = targetPos.y - currentPos.y;
            float dz = targetPos.z - currentPos.z;
            float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            
            if (distance > 0.1f)
            {
                // 물리 기반 이동: RigidBody에 힘을 가하거나 kinematic으로 설정
                if (auto* rb = world->GetComponent<Phy_RigidBodyComponent>(part1Id))
                {
                    // Kinematic으로 설정하여 목표 위치로 이동
                    rb->isKinematic = true;
                    
                    // 보간을 사용하여 부드럽게 이동
                    float moveSpeed = 2.0f; // 초당 이동 속도
                    float lerpFactor = std::min(1.0f, moveSpeed * deltaTime / distance);
                    
                    DirectX::XMFLOAT3 newPos;
                    newPos.x = currentPos.x + dx * lerpFactor;
                    newPos.y = currentPos.y + dy * lerpFactor;
                    newPos.z = currentPos.z + dz * lerpFactor;
                    
                    transform->position = newPos;
                    transform->rotation = m_legacyInfo.legacyRotation;
                    transform->scale = m_legacyInfo.legacyScale;
                    
                    // RigidBody 텔레포트로 물리 엔진에 반영
                    rb->teleport = true;
                }
                else
                {
                    // RigidBody가 없으면 직접 보간 이동
                    float moveSpeed = 2.0f;
                    float lerpFactor = std::min(1.0f, moveSpeed * deltaTime / distance);
                    
                    transform->position.x += dx * lerpFactor;
                    transform->position.y += dy * lerpFactor;
                    transform->position.z += dz * lerpFactor;
                }
            }
            else
            {
                // 충분히 가까우면 정확히 Legacy 위치로
                transform->position = targetPos;
                transform->rotation = m_legacyInfo.legacyRotation;
                transform->scale = m_legacyInfo.legacyScale;
                
                // 모든 파츠도 Legacy 위치로 이동 (조인트로 연결되어 있으므로 자연스럽게 따라올 것)
                for (size_t i = 1; i < m_parts.size(); ++i)
                {
                    EntityId partId = m_parts[i];
                    if (auto* partTransform = world->GetComponent<TransformComponent>(partId))
                    {
                        // 조인트로 연결되어 있으므로 parts_1이 목표 위치에 도달하면
                        // 다른 파츠들도 조인트를 통해 자연스럽게 따라올 것
                        // 여기서는 강제로 이동시키지 않음
                    }
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
        std::uniform_real_distribution<float> impulseDist(10.0f, 20.0f); // Impulse는 더 큰 값 필요
        
        // 랜덤 생성기 (힘의 크기만 랜덤)
        std::random_device rd;
        std::mt19937 gen(rd());

        // 각 파츠에 다른 방향으로 힘을 가함 (바닥을 구르도록)
        for (size_t i = 0; i < m_parts.size(); ++i)
        {
            EntityId partId = m_parts[i];
            if (auto* rb = world->GetComponent<Phy_RigidBodyComponent>(partId))
            {
                // 중력 활성화 및 깨어있게 설정 (바닥을 구르기 위해)
                rb->gravityEnabled = true;
                rb->startAwake = true;
                
                // 각 파츠마다 다른 각도 사용 (균등 분배)
                float angle = angleStep * static_cast<float>(i);
                float impulse = impulseDist(gen); // Impulse 크기는 랜덤
                
                // XZ 평면에서 방향 계산 (각 파츠마다 다른 방향)
                DirectX::XMFLOAT3 direction;
                direction.x = std::cos(angle);
                direction.y = 0.3f + std::sin(angle * 0.3f) * 0.2f; // 위로 약간 튀어오르게
                direction.z = std::sin(angle);
                
                // 방향 정규화
                float dirLength = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
                if (dirLength > 0.0f)
                {
                    direction.x /= dirLength;
                    direction.y /= dirLength;
                    direction.z /= dirLength;
                }
                
                // Impulse 벡터 계산
                DirectX::XMFLOAT3 impulseVec;
                impulseVec.x = direction.x * impulse;
                impulseVec.y = direction.y * impulse;
                impulseVec.z = direction.z * impulse;
                
                // RigidBody 핸들을 통해 Impulse 적용
                if (rb->physicsActorHandle)
                {
                    IRigidBody* body = rb->physicsActorHandle;
                    if (body && body->IsValid())
                    {
                        // Vec3로 변환하여 Impulse 적용
                        Vec3 impulse(impulseVec.x, impulseVec.y, impulseVec.z);
                        body->AddImpulse(impulse);
                        body->WakeUp(); // 확실히 깨우기
                    }
                }
                else
                {
                    // RigidBody가 아직 생성되지 않았으면 다음 프레임에 시도하기 위해
                    // velocity를 설정할 수 없으므로, 컴포넌트에 플래그를 설정하거나
                    // 다음 Update에서 다시 시도하는 방법을 사용할 수 있습니다.
                    // 일단 로그만 남깁니다.
                    ALICE_LOG_WARN("[Gimmick] RigidBody not created yet for part %zu, will try next frame", i);
                }
            }
        }
        
        ALICE_LOG_INFO("[Gimmick] Applied explosion impulse to %zu parts - Parts will fly in different directions", m_parts.size());
    }
}
