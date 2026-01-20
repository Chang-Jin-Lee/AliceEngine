#include "Gimmick.h"
#include "Core/ScriptFactory.h"
#include "Core/Logger.h"
#include "Core/World.h"
#include "Core/Input.h"
#include "Core/GameObject.h"
#include "Components/TransformComponent.h"
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
        // 파츠 찾기 및 조인트 초기화
        FindParts();
        if (m_parts.size() == 5)
        {
            CreateJoints();
            ALICE_LOG_INFO("[Gimmick] Found 5 parts and created joints");
        }
        else
        {
            ALICE_LOG_WARN("[Gimmick] Expected 5 parts, found %zu", m_parts.size());
        }
    }

    void Gimmick::Update(float deltaTime)
    {
        auto* input = Input();
        if (!input)
            return;

        // 스페이스 키 입력 감지
        if (input->GetKeyDown(KeyCode::Space))
        {
            if (m_jointsConnected)
            {
                // 조인트 해제
                RemoveJoints();
                ApplyExplosionForce();
                m_jointsConnected = false;
                m_reconnectProgress = 0.0f;
                ALICE_LOG_INFO("[Gimmick] Joints disconnected");
            }
            else
            {
                // 조인트 재연결 시작
                CreateJoints();
                m_jointsConnected = true;
                m_reconnectProgress = 0.0f;
                ALICE_LOG_INFO("[Gimmick] Joints reconnected, starting animation");
            }
        }

        // 조인트 재연결 애니메이션
        if (m_jointsConnected && m_reconnectProgress < 1.0f)
        {
            ReconnectJoints(deltaTime);
            
            // 충분히 가까워지면 Fixed Joint로 전환
            if (!m_isFixed && m_reconnectProgress >= 0.9f) // 90% 진행되면 Fixed로 전환
            {
                ConvertToFixedJoints();
            }
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

    void Gimmick::CreateJoints()
    {
        auto* world = GetWorld();
        if (!world || m_parts.size() < 2)
            return;

        // 기존 조인트 제거
        RemoveJoints();
        
        // Fixed 상태 초기화
        m_isFixed = false;

        // 각 파츠를 다음 파츠와 연결 (parts_1 -> parts_2 -> parts_3 -> parts_4 -> parts_5)
        for (size_t i = 0; i < m_parts.size() - 1; ++i)
        {
            EntityId partA = m_parts[i];
            EntityId partB = m_parts[i + 1];

            // 조인트를 위한 엔티티 생성 (또는 partA에 직접 추가)
            // 여기서는 partA에 조인트 컴포넌트를 추가하는 방식 사용
            Phy_JointComponent& joint = world->AddComponent<Phy_JointComponent>(partA);
            joint.type = Phy_JointType::Distance;
            
            // targetName 설정 (partB의 이름)
            std::string partBName = "parts_" + std::to_string(i + 2);
            joint.targetName = partBName;
            
            // Distance Joint 설정
            joint.distance.minDistance = 0.0f;
            joint.distance.maxDistance = m_initialMaxDistance; // 초기 거리
            joint.distance.tolerance = 0.1f;
            joint.distance.enableMinDistance = false;
            joint.distance.enableMaxDistance = true;
            joint.distance.enableSpring = true;
            joint.distance.stiffness = 100.0f;
            joint.distance.damping = 10.0f;
            
            // 조인트끼리 충돌하지 않음
            joint.collideConnected = false;
            
            // 조인트 엔티티 저장
            m_jointEntities.push_back(partA);
        }

        // 모든 파츠의 중력 비활성화
        for (EntityId partId : m_parts)
        {
            if (auto* rb = world->GetComponent<Phy_RigidBodyComponent>(partId))
            {
                rb->gravityEnabled = false;
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
                // parts_1에 여러 개의 조인트가 있을 수 있으므로 모두 제거
                // (Phy_JointComponent는 엔티티당 하나만 있을 수 있으므로, 
                //  실제로는 parts_1에 있는 조인트만 제거하면 됨)
                world->RemoveComponent<Phy_JointComponent>(jointEntity);
            }
        }
        m_jointEntities.clear();
        
        // Fixed 상태 초기화
        m_isFixed = false;

        // 모든 파츠의 중력 활성화
        for (EntityId partId : m_parts)
        {
            if (auto* rb = world->GetComponent<Phy_RigidBodyComponent>(partId))
            {
                rb->gravityEnabled = true;
            }
        }
        
        // Collider의 trigger 상태 복원
        if (m_originalTriggerStates.size() == m_parts.size())
        {
            for (size_t i = 0; i < m_parts.size(); ++i)
            {
                EntityId partId = m_parts[i];
                bool originalTrigger = m_originalTriggerStates[i];
                
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
        m_originalTriggerStates.clear();
    }

    void Gimmick::ReconnectJoints(float deltaTime)
    {
        auto* world = GetWorld();
        if (!world)
            return;

        // 이미 Fixed Joint로 전환되었으면 더 이상 진행하지 않음
        if (m_isFixed)
            return;

        // 진행도 업데이트
        m_reconnectProgress += m_reconnectSpeed * deltaTime;
        if (m_reconnectProgress > 1.0f)
            m_reconnectProgress = 1.0f;

        // maxDistance를 점점 줄여서 스프링처럼 붙게 만들기
        // Lerp: initialMaxDistance -> 0.0f
        float targetMaxDistance = m_initialMaxDistance * (1.0f - m_reconnectProgress);

        // 각 조인트의 maxDistance 업데이트
        for (EntityId jointEntity : m_jointEntities)
        {
            if (auto* joint = world->GetComponent<Phy_JointComponent>(jointEntity))
            {
                joint->distance.maxDistance = targetMaxDistance;
            }
        }
    }

    void Gimmick::ConvertToFixedJoints()
    {
        auto* world = GetWorld();
        if (!world || m_parts.empty())
            return;

        // 첫 번째 파츠의 위치를 기준으로 모든 파츠를 겹치게 만들기
        DirectX::XMFLOAT3 targetPos = { 0.0f, 0.0f, 0.0f };
        if (auto* firstTransform = world->GetComponent<TransformComponent>(m_parts[0]))
        {
            targetPos = firstTransform->position;
        }
        
        // 원래 trigger 상태 저장 및 모든 Collider를 trigger로 설정
        m_originalTriggerStates.clear();
        m_originalTriggerStates.reserve(m_parts.size());
        
        for (EntityId partId : m_parts)
        {
            // Collider의 원래 trigger 상태 저장
            bool originalTrigger = false;
            if (auto* collider = world->GetComponent<Phy_ColliderComponent>(partId))
            {
                originalTrigger = collider->isTrigger;
                m_originalTriggerStates.push_back(originalTrigger);
                collider->isTrigger = true; // trigger로 설정하여 겹침 허용
            }
            else if (auto* meshCollider = world->GetComponent<Phy_MeshColliderComponent>(partId))
            {
                originalTrigger = meshCollider->isTrigger;
                m_originalTriggerStates.push_back(originalTrigger);
                meshCollider->isTrigger = true; // trigger로 설정하여 겹침 허용
            }
            else
            {
                m_originalTriggerStates.push_back(false);
            }
            
            // 모든 파츠를 첫 번째 파츠 위치로 이동
            if (auto* transform = world->GetComponent<TransformComponent>(partId))
            {
                transform->position = targetPos;
                
                // RigidBody가 있으면 텔레포트 플래그 설정
                if (auto* rb = world->GetComponent<Phy_RigidBodyComponent>(partId))
                {
                    rb->teleport = true;
                }
            }
        }

        // 기존 체인 구조 조인트 모두 제거
        for (EntityId jointEntity : m_jointEntities)
        {
            if (jointEntity != InvalidEntityId)
            {
                world->RemoveComponent<Phy_JointComponent>(jointEntity);
            }
        }
        m_jointEntities.clear();

        // Star 구조로 변경: 모든 파츠(parts_2~5)를 parts_1에 연결
        std::string centerPartName = "parts_1";
        
        for (size_t i = 1; i < m_parts.size(); ++i)
        {
            EntityId partId = m_parts[i];
            
            // 각 파츠에 조인트 추가 (모두 parts_1을 타겟으로)
            Phy_JointComponent& joint = world->AddComponent<Phy_JointComponent>(partId);
            joint.type = Phy_JointType::Fixed;
            
            // targetName 설정 (parts_1)
            joint.targetName = centerPartName;
            
            // Fixed Joint 설정
            joint.collideConnected = false; // 충돌하지 않음
            joint.frameA.position = { 0.0f, 0.0f, 0.0f };
            joint.frameA.rotation = { 0.0f, 0.0f, 0.0f };
            joint.frameB.position = { 0.0f, 0.0f, 0.0f };
            joint.frameB.rotation = { 0.0f, 0.0f, 0.0f };
            
            // 조인트 엔티티 저장
            m_jointEntities.push_back(partId);
        }

        m_isFixed = true;
        ALICE_LOG_INFO("[Gimmick] Converted to Fixed Joints (star structure) - all parts merged with triggers enabled");
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

        // 각 파츠에 다른 방향으로 힘을 가함
        for (size_t i = 0; i < m_parts.size(); ++i)
        {
            EntityId partId = m_parts[i];
            if (auto* rb = world->GetComponent<Phy_RigidBodyComponent>(partId))
            {
                // 각 파츠마다 다른 각도 사용 (균등 분배)
                float angle = angleStep * static_cast<float>(i);
                float force = forceDist(gen); // 힘의 크기는 랜덤
                
                // XZ 평면에서 방향 계산 (각 파츠마다 다른 방향)
                DirectX::XMFLOAT3 velocity;
                velocity.x = std::cos(angle) * force;
                velocity.y = std::sin(angle * 0.3f) * force * 0.6f + 2.0f; // 위로 약간 튀어오르게
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
        
        ALICE_LOG_INFO("[Gimmick] Applied explosion force to %zu parts", m_parts.size());
    }
}
