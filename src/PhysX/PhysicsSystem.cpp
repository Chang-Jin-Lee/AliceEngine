#include "PhysicsSystem.h"
#include "Core/World.h"
#include "Components/TransformComponent.h"
#include <DirectXMath.h>
#include <algorithm>
#include <unordered_set>
#include <cmath>

using namespace DirectX;
using namespace Alice;

PhysicsSystem::PhysicsSystem(World& world)
    : m_world(world)
{
}

PhysicsSystem::~PhysicsSystem()
{
    // 모든 물리 액터 정리 (컴포넌트 핸들도 함께 정리)
    std::vector<EntityId> entityIds;
    entityIds.reserve(m_entityToActor.size());
    for (const auto& [entityId, handle] : m_entityToActor)
    {
        entityIds.push_back(entityId);
    }
    
    for (EntityId entityId : entityIds)
    {
        DestroyPhysicsActor(entityId);
    }
    
    m_entityToActor.clear();
}

void PhysicsSystem::SetPhysicsWorld(IPhysicsWorld* physicsWorld)
{
    // 기존 액터들 정리 (컴포넌트 핸들도 함께 정리)
    std::vector<EntityId> entityIds;
    entityIds.reserve(m_entityToActor.size());
    for (const auto& [entityId, handle] : m_entityToActor)
    {
        entityIds.push_back(entityId);
    }
    
    for (EntityId entityId : entityIds)
    {
        DestroyPhysicsActor(entityId);
    }
    
    m_entityToActor.clear();
    m_lastTransforms.clear();

    m_physicsWorld = physicsWorld;
}

void PhysicsSystem::SetEventCallback(EventCallback callback, void* userData)
{
    m_eventCallback = callback;
    m_eventCallbackUserData = userData;
}

void PhysicsSystem::Update(float deltaTime)
{
    if (!m_physicsWorld) return;

    // 1. 컴포넌트 변경 감지 및 물리 액터 생성/삭제
    {
        // RigidBodyComponent가 있는 엔티티 확인
        auto rigidBodies = m_world.GetComponents<RigidBodyComponent>();
        std::unordered_set<EntityId> entitiesWithRigidBody;
        
        for (const auto& [entityId, rb] : rigidBodies)
        {
            entitiesWithRigidBody.insert(entityId);
            
            // 새로 추가된 경우 또는 핸들이 없는 경우
            if (rb.physicsActorHandle == nullptr)
            {
                CreatePhysicsActor(entityId);
            }
        }

        // ColliderComponent만 있는 엔티티 확인 (Static Actor)
        auto colliders = m_world.GetComponents<ColliderComponent>();
        for (const auto& [entityId, collider] : colliders)
        {
            // RigidBodyComponent가 없고, Collider만 있는 경우
            if (entitiesWithRigidBody.find(entityId) == entitiesWithRigidBody.end())
            {
                if (collider.physicsActorHandle == nullptr)
                {
                    CreatePhysicsActor(entityId);
                }
            }
        }

        // TerrainHeightFieldComponent가 있는 엔티티 확인
        auto terrains = m_world.GetComponents<TerrainHeightFieldComponent>();
        for (const auto& [entityId, terrain] : terrains)
        {
            // 새로 추가된 경우 또는 핸들이 없는 경우
            if (terrain.physicsActorHandle == nullptr)
            {
                CreateTerrainHeightField(entityId);
            }
        }

        // 제거된 컴포넌트 확인 (m_entityToActor에 있지만 컴포넌트가 없는 경우)
        std::vector<EntityId> toRemove;
        for (const auto& [entityId, handle] : m_entityToActor)
        {
            auto* rb = m_world.GetComponent<RigidBodyComponent>(entityId);
            auto* collider = m_world.GetComponent<ColliderComponent>(entityId);
            auto* terrain = m_world.GetComponent<TerrainHeightFieldComponent>(entityId);
            
            if (!rb && !collider && !terrain)
            {
                toRemove.push_back(entityId);
            }
        }

        for (EntityId entityId : toRemove)
        {
            DestroyPhysicsActor(entityId);
        }
    }

    // 2. Game → Physics 동기화 (Transform 변경 감지)
    {
        auto transforms = m_world.GetComponents<TransformComponent>();
        for (const auto& [entityId, transform] : transforms)
        {
            // RigidBody 또는 Collider가 있는 엔티티만 동기화
            auto* rb = m_world.GetComponent<RigidBodyComponent>(entityId);
            auto* collider = m_world.GetComponent<ColliderComponent>(entityId);
            
            if (!rb && !collider) continue;

            // 이전 상태 확인
            auto it = m_lastTransforms.find(entityId);
            bool needsSync = false;

            if (it == m_lastTransforms.end())
            {
                // 첫 프레임
                needsSync = true;
                m_lastTransforms[entityId] = {
                    transform.position,
                    transform.rotation,
                    transform.scale
                };
            }
            else
            {
                // 변경 감지
                const auto& last = it->second;
                if (transform.position.x != last.position.x || transform.position.y != last.position.y || transform.position.z != last.position.z ||
                    transform.rotation.x != last.rotation.x || transform.rotation.y != last.rotation.y || transform.rotation.z != last.rotation.z ||
                    transform.scale.x != last.scale.x || transform.scale.y != last.scale.y || transform.scale.z != last.scale.z)
                {
                    needsSync = true;
                    it->second = {
                        transform.position,
                        transform.rotation,
                        transform.scale
                    };
                }
            }

            if (needsSync)
            {
                SyncGameToPhysics(entityId, transform.position, transform.rotation);
            }
        }
    }

    // 3. Collider/Scale 변경 감지 및 Shape 재구성
    {
        auto colliders = m_world.GetComponents<ColliderComponent>();
        for (const auto& [entityId, collider] : colliders)
        {
            auto* transform = m_world.GetComponent<TransformComponent>(entityId);
            if (!transform) continue;

            // 이전 상태 확인
            auto it = m_lastColliders.find(entityId);
            bool needsRebuild = false;

            if (it == m_lastColliders.end())
            {
                // 첫 프레임 - 상태 저장만
                ColliderState state{};
                state.type = collider.type;
                state.halfExtents = collider.halfExtents;
                state.radius = collider.radius;
                state.capsuleRadius = collider.capsuleRadius;
                state.capsuleHalfHeight = collider.capsuleHalfHeight;
                state.capsuleAlignYAxis = collider.capsuleAlignYAxis;
                state.staticFriction = collider.staticFriction;
                state.dynamicFriction = collider.dynamicFriction;
                state.restitution = collider.restitution;
                state.layerBits = collider.layerBits;
                state.collideMask = collider.collideMask;
                state.queryMask = collider.queryMask;
                state.isTrigger = collider.isTrigger;
                state.scale = transform->scale;
                m_lastColliders[entityId] = state;
            }
            else
            {
                // 변경 감지
                const auto& last = it->second;
                bool changed = false;

                // Collider 파라미터 변경
                if (collider.type != last.type ||
                    collider.halfExtents.x != last.halfExtents.x || collider.halfExtents.y != last.halfExtents.y || collider.halfExtents.z != last.halfExtents.z ||
                    collider.radius != last.radius ||
                    collider.capsuleRadius != last.capsuleRadius ||
                    collider.capsuleHalfHeight != last.capsuleHalfHeight ||
                    collider.capsuleAlignYAxis != last.capsuleAlignYAxis ||
                    collider.staticFriction != last.staticFriction ||
                    collider.dynamicFriction != last.dynamicFriction ||
                    collider.restitution != last.restitution ||
                    collider.layerBits != last.layerBits ||
                    collider.collideMask != last.collideMask ||
                    collider.queryMask != last.queryMask ||
                    collider.isTrigger != last.isTrigger)
                {
                    changed = true;
                }

                // Scale 변경
                if (transform->scale.x != last.scale.x || 
                    transform->scale.y != last.scale.y || 
                    transform->scale.z != last.scale.z)
                {
                    changed = true;
                }

                if (changed)
                {
                    needsRebuild = true;
                    // 상태 업데이트
                    it->second.type = collider.type;
                    it->second.halfExtents = collider.halfExtents;
                    it->second.radius = collider.radius;
                    it->second.capsuleRadius = collider.capsuleRadius;
                    it->second.capsuleHalfHeight = collider.capsuleHalfHeight;
                    it->second.capsuleAlignYAxis = collider.capsuleAlignYAxis;
                    it->second.staticFriction = collider.staticFriction;
                    it->second.dynamicFriction = collider.dynamicFriction;
                    it->second.restitution = collider.restitution;
                    it->second.layerBits = collider.layerBits;
                    it->second.collideMask = collider.collideMask;
                    it->second.queryMask = collider.queryMask;
                    it->second.isTrigger = collider.isTrigger;
                    it->second.scale = transform->scale;
                }
            }

            if (needsRebuild)
            {
                RebuildShapes(entityId);
            }
        }

        // 제거된 Collider의 상태도 정리
        std::vector<EntityId> collidersToRemove;
        for (const auto& [entityId, state] : m_lastColliders)
        {
            auto* collider = m_world.GetComponent<ColliderComponent>(entityId);
            if (!collider)
            {
                collidersToRemove.push_back(entityId);
            }
        }
        for (EntityId entityId : collidersToRemove)
        {
            m_lastColliders.erase(entityId);
        }
    }
}

void PhysicsSystem::CreatePhysicsActor(EntityId entityId)
{
    if (!m_physicsWorld) return;

    auto* transform = m_world.GetComponent<TransformComponent>(entityId);
    if (!transform) return; // Transform이 없으면 생성 불가

    auto* rb = m_world.GetComponent<RigidBodyComponent>(entityId);
    auto* collider = m_world.GetComponent<ColliderComponent>(entityId);

    if (!rb && !collider) return; // 둘 다 없으면 생성 불가

    Vec3 pos = ToVec3(transform->position);
    Quat rot = ToQuat(transform->rotation);

    if (rb)
    {
        // Dynamic RigidBody 생성
        RigidBodyDesc rbDesc{};
        rbDesc.density = rb->density;
        rbDesc.massOverride = rb->massOverride;
        rbDesc.isKinematic = rb->isKinematic;
        rbDesc.gravityEnabled = rb->gravityEnabled;
        rbDesc.startAwake = rb->startAwake;
        rbDesc.enableCCD = rb->enableCCD;
        rbDesc.enableSpeculativeCCD = rb->enableSpeculativeCCD;
        rbDesc.lockFlags = rb->lockFlags;
        rbDesc.linearDamping = rb->linearDamping;
        rbDesc.angularDamping = rb->angularDamping;
        rbDesc.maxLinearVelocity = rb->maxLinearVelocity;
        rbDesc.maxAngularVelocity = rb->maxAngularVelocity;
        rbDesc.solverPositionIterations = rb->solverPositionIterations;
        rbDesc.solverVelocityIterations = rb->solverVelocityIterations;
        rbDesc.sleepThreshold = rb->sleepThreshold;
        rbDesc.stabilizationThreshold = rb->stabilizationThreshold;
        rbDesc.userData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(entityId));

        if (collider)
        {
            // Collider 타입에 따라 바디 생성
            switch (collider->type)
            {
            case ColliderType::Box:
            {
                BoxColliderDesc boxDesc{};
                // Scale 반영
                Vec3 scale = Vec3(std::abs(transform->scale.x), std::abs(transform->scale.y), std::abs(transform->scale.z));
                Vec3 he = ToVec3(collider->halfExtents);
                he.x *= scale.x;
                he.y *= scale.y;
                he.z *= scale.z;
                boxDesc.halfExtents = he;
                boxDesc.staticFriction = collider->staticFriction;
                boxDesc.dynamicFriction = collider->dynamicFriction;
                boxDesc.restitution = collider->restitution;
                boxDesc.layerBits = collider->layerBits;
                boxDesc.collideMask = collider->collideMask;
                boxDesc.queryMask = collider->queryMask;
                boxDesc.isTrigger = collider->isTrigger;
                boxDesc.userData = rbDesc.userData;

                auto bodyPtr = m_physicsWorld->CreateDynamicBox(pos, rot, rbDesc, boxDesc);
                if (bodyPtr)
                {
                    // unique_ptr을 그대로 move하여 소유권 유지
                    // IRigidBody는 IPhysicsActor를 상속하므로 자동 변환됨
                    ActorHandle handle(std::move(bodyPtr));
                    IRigidBody* body = handle.GetRigidBody();
                    
                    rb->physicsActorHandle = body;
                    collider->physicsActorHandle = body;
                    m_entityToActor[entityId] = std::move(handle);
                }
                break;
            }
            case ColliderType::Sphere:
            {
                SphereColliderDesc sphereDesc{};
                // Scale 반영 (최대값 사용)
                Vec3 scale = Vec3(std::abs(transform->scale.x), std::abs(transform->scale.y), std::abs(transform->scale.z));
                float sMax = std::max({ scale.x, scale.y, scale.z });
                sphereDesc.radius = collider->radius * sMax;
                sphereDesc.staticFriction = collider->staticFriction;
                sphereDesc.dynamicFriction = collider->dynamicFriction;
                sphereDesc.restitution = collider->restitution;
                sphereDesc.layerBits = collider->layerBits;
                sphereDesc.collideMask = collider->collideMask;
                sphereDesc.queryMask = collider->queryMask;
                sphereDesc.isTrigger = collider->isTrigger;
                sphereDesc.userData = rbDesc.userData;

                auto bodyPtr = m_physicsWorld->CreateDynamicSphere(pos, rot, rbDesc, sphereDesc);
                if (bodyPtr)
                {
                    ActorHandle handle(std::move(bodyPtr));
                    IRigidBody* body = handle.GetRigidBody();
                    
                    rb->physicsActorHandle = body;
                    collider->physicsActorHandle = body;
                    m_entityToActor[entityId] = std::move(handle);
                }
                break;
            }
            case ColliderType::Capsule:
            {
                CapsuleColliderDesc capsuleDesc{};
                // Scale 반영
                Vec3 scale = Vec3(std::abs(transform->scale.x), std::abs(transform->scale.y), std::abs(transform->scale.z));
                if (collider->capsuleAlignYAxis)
                {
                    float radial = std::max(scale.x, scale.z);
                    capsuleDesc.radius = collider->capsuleRadius * radial;
                    capsuleDesc.halfHeight = collider->capsuleHalfHeight * scale.y;
                }
                else
                {
                    float radial = std::max(scale.y, scale.z);
                    capsuleDesc.radius = collider->capsuleRadius * radial;
                    capsuleDesc.halfHeight = collider->capsuleHalfHeight * scale.x;
                }
                capsuleDesc.alignYAxis = collider->capsuleAlignYAxis;
                capsuleDesc.staticFriction = collider->staticFriction;
                capsuleDesc.dynamicFriction = collider->dynamicFriction;
                capsuleDesc.restitution = collider->restitution;
                capsuleDesc.layerBits = collider->layerBits;
                capsuleDesc.collideMask = collider->collideMask;
                capsuleDesc.queryMask = collider->queryMask;
                capsuleDesc.isTrigger = collider->isTrigger;
                capsuleDesc.userData = rbDesc.userData;

                auto bodyPtr = m_physicsWorld->CreateDynamicCapsule(pos, rot, rbDesc, capsuleDesc);
                if (bodyPtr)
                {
                    ActorHandle handle(std::move(bodyPtr));
                    IRigidBody* body = handle.GetRigidBody();
                    
                    rb->physicsActorHandle = body;
                    collider->physicsActorHandle = body;
                    m_entityToActor[entityId] = std::move(handle);
                }
                break;
            }
            }

            // body는 이미 m_entityToActor에 저장됨
        }
        else
        {
            // Collider 없이 빈 바디 생성
            auto bodyPtr = m_physicsWorld->CreateDynamicEmpty(pos, rot, rbDesc);
            if (bodyPtr)
            {
                ActorHandle handle(std::move(bodyPtr));
                IRigidBody* body = handle.GetRigidBody();
                
                rb->physicsActorHandle = body;
                m_entityToActor[entityId] = std::move(handle);
            }
        }
    }
    else if (collider)
    {
        // Static Actor 생성 (RigidBody 없이 Collider만)
        FilterDesc filterDesc{};
        filterDesc.layerBits = collider->layerBits;
        filterDesc.collideMask = collider->collideMask;
        filterDesc.queryMask = collider->queryMask;
        filterDesc.isTrigger = collider->isTrigger;
        filterDesc.userData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(entityId));

        MaterialDesc materialDesc{};
        materialDesc.staticFriction = collider->staticFriction;
        materialDesc.dynamicFriction = collider->dynamicFriction;
        materialDesc.restitution = collider->restitution;

        switch (collider->type)
        {
        case ColliderType::Box:
        {
            BoxColliderDesc boxDesc{};
            // Scale 반영
            Vec3 scale = Vec3(std::abs(transform->scale.x), std::abs(transform->scale.y), std::abs(transform->scale.z));
            Vec3 he = ToVec3(collider->halfExtents);
            he.x *= scale.x;
            he.y *= scale.y;
            he.z *= scale.z;
            boxDesc.halfExtents = he;
            boxDesc.staticFriction = collider->staticFriction;
            boxDesc.dynamicFriction = collider->dynamicFriction;
            boxDesc.restitution = collider->restitution;
            boxDesc.layerBits = collider->layerBits;
            boxDesc.collideMask = collider->collideMask;
            boxDesc.queryMask = collider->queryMask;
            boxDesc.isTrigger = collider->isTrigger;
            boxDesc.userData = filterDesc.userData;

            auto actorPtr = m_physicsWorld->CreateStaticBox(pos, rot, boxDesc);
            if (actorPtr)
            {
                ActorHandle handle(std::move(actorPtr));
                IPhysicsActor* actor = handle.GetActor();
                
                collider->physicsActorHandle = actor;
                m_entityToActor[entityId] = std::move(handle);
            }
            break;
        }
        case ColliderType::Sphere:
        {
            SphereColliderDesc sphereDesc{};
            // Scale 반영 (최대값 사용)
            Vec3 scale = Vec3(std::abs(transform->scale.x), std::abs(transform->scale.y), std::abs(transform->scale.z));
            float sMax = std::max({ scale.x, scale.y, scale.z });
            sphereDesc.radius = collider->radius * sMax;
            sphereDesc.staticFriction = collider->staticFriction;
            sphereDesc.dynamicFriction = collider->dynamicFriction;
            sphereDesc.restitution = collider->restitution;
            sphereDesc.layerBits = collider->layerBits;
            sphereDesc.collideMask = collider->collideMask;
            sphereDesc.queryMask = collider->queryMask;
            sphereDesc.isTrigger = collider->isTrigger;
            sphereDesc.userData = filterDesc.userData;

            auto actorPtr = m_physicsWorld->CreateStaticSphere(pos, rot, sphereDesc);
            if (actorPtr)
            {
                ActorHandle handle(std::move(actorPtr));
                IPhysicsActor* actor = handle.GetActor();
                
                collider->physicsActorHandle = actor;
                m_entityToActor[entityId] = std::move(handle);
            }
            break;
        }
        case ColliderType::Capsule:
        {
            CapsuleColliderDesc capsuleDesc{};
            // Scale 반영
            Vec3 scale = Vec3(std::abs(transform->scale.x), std::abs(transform->scale.y), std::abs(transform->scale.z));
            if (collider->capsuleAlignYAxis)
            {
                float radial = std::max(scale.x, scale.z);
                capsuleDesc.radius = collider->capsuleRadius * radial;
                capsuleDesc.halfHeight = collider->capsuleHalfHeight * scale.y;
            }
            else
            {
                float radial = std::max(scale.y, scale.z);
                capsuleDesc.radius = collider->capsuleRadius * radial;
                capsuleDesc.halfHeight = collider->capsuleHalfHeight * scale.x;
            }
            capsuleDesc.alignYAxis = collider->capsuleAlignYAxis;
            capsuleDesc.staticFriction = collider->staticFriction;
            capsuleDesc.dynamicFriction = collider->dynamicFriction;
            capsuleDesc.restitution = collider->restitution;
            capsuleDesc.layerBits = collider->layerBits;
            capsuleDesc.collideMask = collider->collideMask;
            capsuleDesc.queryMask = collider->queryMask;
            capsuleDesc.isTrigger = collider->isTrigger;
            capsuleDesc.userData = filterDesc.userData;

            auto actorPtr = m_physicsWorld->CreateStaticCapsule(pos, rot, capsuleDesc);
            if (actorPtr)
            {
                ActorHandle handle(std::move(actorPtr));
                IPhysicsActor* actor = handle.GetActor();
                
                collider->physicsActorHandle = actor;
                m_entityToActor[entityId] = std::move(handle);
            }
            break;
        }
        }

    }
}

void PhysicsSystem::DestroyPhysicsActor(EntityId entityId)
{
    auto it = m_entityToActor.find(entityId);
    if (it == m_entityToActor.end()) return;

    it->second.Destroy();

    // 컴포넌트의 핸들도 초기화
    auto* rb = m_world.GetComponent<RigidBodyComponent>(entityId);
    if (rb) rb->physicsActorHandle = nullptr;

    auto* collider = m_world.GetComponent<ColliderComponent>(entityId);
    if (collider) collider->physicsActorHandle = nullptr;

    auto* terrain = m_world.GetComponent<TerrainHeightFieldComponent>(entityId);
    if (terrain) terrain->physicsActorHandle = nullptr;

    m_entityToActor.erase(it);
    m_lastTransforms.erase(entityId);
    m_lastColliders.erase(entityId);
}

void PhysicsSystem::CreateTerrainHeightField(EntityId entityId)
{
    if (!m_physicsWorld) return;

    auto* transform = m_world.GetComponent<TransformComponent>(entityId);
    if (!transform) return; // Transform이 없으면 생성 불가

    auto* terrain = m_world.GetComponent<TerrainHeightFieldComponent>(entityId);
    if (!terrain) return;

    // HeightField 데이터 검증
    if (terrain->heightSamples.empty() || terrain->numRows < 2 || terrain->numCols < 2)
    {
        return; // 유효하지 않은 데이터
    }

    if (terrain->heightScale <= 0.0f || terrain->rowScale <= 0.0f || terrain->colScale <= 0.0f)
    {
        return; // 유효하지 않은 스케일
    }

    // 기존 액터가 있으면 제거
    if (terrain->physicsActorHandle != nullptr)
    {
        DestroyPhysicsActor(entityId);
    }

    Vec3 pos = ToVec3(transform->position);
    Quat rot = ToQuat(transform->rotation);

    // HeightFieldColliderDesc 구성
    HeightFieldColliderDesc hfDesc{};
    hfDesc.heightSamples = terrain->heightSamples.data();
    hfDesc.numRows = terrain->numRows;
    hfDesc.numCols = terrain->numCols;
    hfDesc.heightScale = terrain->heightScale;
    hfDesc.rowScale = terrain->rowScale;
    hfDesc.colScale = terrain->colScale;
    hfDesc.staticFriction = terrain->staticFriction;
    hfDesc.dynamicFriction = terrain->dynamicFriction;
    hfDesc.restitution = terrain->restitution;
    hfDesc.layerBits = terrain->layerBits;
    hfDesc.collideMask = terrain->collideMask;
    hfDesc.queryMask = terrain->queryMask;
    hfDesc.isTrigger = false; // HeightField는 절대 트리거 불가 (PhysX 제약)
    hfDesc.userData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(entityId));

    // 피벗 보정: centerPivot이 true면 지형 중앙으로 localPos 조정
    Vec3 localPos = Vec3::Zero;
    Quat localRot = Quat::Identity;

    if (terrain->centerPivot)
    {
        const float halfW = 0.5f * static_cast<float>(terrain->numCols - 1) * terrain->colScale;
        const float halfD = 0.5f * static_cast<float>(terrain->numRows - 1) * terrain->rowScale;
        localPos = Vec3(-halfW, 0.0f, -halfD);
    }

    // RigidStatic + HeightField 생성
    // localPos/localRot를 적용하기 위해 빈 액터를 만들고 shape를 직접 추가
    auto actorPtr = m_physicsWorld->CreateStaticEmpty(pos, rot, hfDesc.userData);
    if (actorPtr)
    {
        ActorHandle handle(std::move(actorPtr));
        IPhysicsActor* actor = handle.GetActor();
        
        // HeightField shape를 localPos/localRot로 추가
        if (!actor->AddHeightFieldShape(hfDesc, localPos, localRot))
        {
            // 실패 시 액터 정리
            handle.Destroy();
            return;
        }
        
        terrain->physicsActorHandle = actor;
        m_entityToActor[entityId] = std::move(handle);
    }
}

void PhysicsSystem::RebuildShapes(EntityId entityId)
{
    auto it = m_entityToActor.find(entityId);
    if (it == m_entityToActor.end()) return;

    ActorHandle& handle = it->second;
    if (!handle.IsValid()) return;

    IPhysicsActor* actor = handle.GetActor();
    if (!actor || !actor->IsValid()) return;

    auto* transform = m_world.GetComponent<TransformComponent>(entityId);
    auto* collider = m_world.GetComponent<ColliderComponent>(entityId);
    if (!transform || !collider) return;

    // Scale 반영을 위한 헬퍼 함수
    auto AbsScale = [](const DirectX::XMFLOAT3& s) -> Vec3 {
        return Vec3(std::abs(s.x), std::abs(s.y), std::abs(s.z));
    };

    Vec3 scale = AbsScale(transform->scale);

    // 기존 Shape 제거
    actor->ClearShapes();

    // Collider 타입에 따라 Shape 재생성 (scale 반영)
    switch (collider->type)
    {
    case ColliderType::Box:
    {
        BoxColliderDesc boxDesc{};
        Vec3 he = ToVec3(collider->halfExtents);
        he.x *= scale.x;
        he.y *= scale.y;
        he.z *= scale.z;
        boxDesc.halfExtents = he;
        boxDesc.staticFriction = collider->staticFriction;
        boxDesc.dynamicFriction = collider->dynamicFriction;
        boxDesc.restitution = collider->restitution;
        boxDesc.layerBits = collider->layerBits;
        boxDesc.collideMask = collider->collideMask;
        boxDesc.queryMask = collider->queryMask;
        boxDesc.isTrigger = collider->isTrigger;
        boxDesc.userData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(entityId));

        actor->AddBoxShape(boxDesc, Vec3::Zero, Quat::Identity);
        break;
    }
    case ColliderType::Sphere:
    {
        SphereColliderDesc sphereDesc{};
        float sMax = std::max({ scale.x, scale.y, scale.z });
        sphereDesc.radius = collider->radius * sMax;
        sphereDesc.staticFriction = collider->staticFriction;
        sphereDesc.dynamicFriction = collider->dynamicFriction;
        sphereDesc.restitution = collider->restitution;
        sphereDesc.layerBits = collider->layerBits;
        sphereDesc.collideMask = collider->collideMask;
        sphereDesc.queryMask = collider->queryMask;
        sphereDesc.isTrigger = collider->isTrigger;
        sphereDesc.userData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(entityId));

        actor->AddSphereShape(sphereDesc, Vec3::Zero, Quat::Identity);
        break;
    }
    case ColliderType::Capsule:
    {
        CapsuleColliderDesc capsuleDesc{};
        if (collider->capsuleAlignYAxis)
        {
            float radial = std::max(scale.x, scale.z);
            capsuleDesc.radius = collider->capsuleRadius * radial;
            capsuleDesc.halfHeight = collider->capsuleHalfHeight * scale.y;
        }
        else
        {
            // X축 정렬 (일반적이지 않지만 지원)
            float radial = std::max(scale.y, scale.z);
            capsuleDesc.radius = collider->capsuleRadius * radial;
            capsuleDesc.halfHeight = collider->capsuleHalfHeight * scale.x;
        }
        capsuleDesc.alignYAxis = collider->capsuleAlignYAxis;
        capsuleDesc.staticFriction = collider->staticFriction;
        capsuleDesc.dynamicFriction = collider->dynamicFriction;
        capsuleDesc.restitution = collider->restitution;
        capsuleDesc.layerBits = collider->layerBits;
        capsuleDesc.collideMask = collider->collideMask;
        capsuleDesc.queryMask = collider->queryMask;
        capsuleDesc.isTrigger = collider->isTrigger;
        capsuleDesc.userData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(entityId));

        actor->AddCapsuleShape(capsuleDesc, Vec3::Zero, Quat::Identity);
        break;
    }
    }

    // Dynamic RigidBody인 경우 질량 재계산
    IRigidBody* body = handle.GetRigidBody();
    if (body && body->IsValid())
    {
        body->RecomputeMass();
    }

    // 필터/재질/트리거 변경도 반영
    // (Shape 재생성 시 이미 desc에 포함되어 있음)
}

void PhysicsSystem::SyncGameToPhysics(EntityId entityId, const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& rotation)
{
    auto it = m_entityToActor.find(entityId);
    if (it == m_entityToActor.end()) return;

    ActorHandle& handle = it->second;
    if (!handle.IsValid()) return;
    
    IPhysicsActor* actor = handle.GetActor();

    Vec3 pos = ToVec3(position);
    Quat rot = ToQuat(rotation);

    auto* rb = m_world.GetComponent<RigidBodyComponent>(entityId);
    if (rb && rb->isKinematic)
    {
        // Kinematic 바디는 SetKinematicTarget 사용
        IRigidBody* body = handle.GetRigidBody();
        if (body && body->IsValid())
        {
            body->SetKinematicTarget(pos, rot);
        }
    }
    else
    {
        // Static Actor 또는 Dynamic 바디는 SetTransform 사용
        if (actor && actor->IsValid())
        {
            actor->SetTransform(pos, rot);
        }
    }
}

void PhysicsSystem::SyncPhysicsToGame(const ActiveTransform& transform)
{
    if (!transform.userData) return;

    EntityId entityId = static_cast<EntityId>(reinterpret_cast<std::uintptr_t>(transform.userData));
    auto* transformComp = m_world.GetComponent<TransformComponent>(entityId);
    if (!transformComp) return;

    // 위치 및 회전 동기화
    transformComp->position = ToXMFLOAT3(transform.position);
    
    // Quat → Euler 변환
    DirectX::XMFLOAT3 euler = ToEulerRadians(transform.rotation);
    transformComp->rotation = euler;

    // 마지막 상태 업데이트
    m_lastTransforms[entityId] = {
        transformComp->position,
        transformComp->rotation,
        transformComp->scale
    };
}

Vec3 PhysicsSystem::ToVec3(const DirectX::XMFLOAT3& v)
{
    return Vec3(v.x, v.y, v.z);
}

Quat PhysicsSystem::ToQuat(const DirectX::XMFLOAT3& eulerRadians)
{
    // Euler (라디안) → Quaternion
    // DirectX::SimpleMath::Quaternion 사용
    // Roll (X), Pitch (Y), Yaw (Z) 순서
    return Quat::CreateFromYawPitchRoll(eulerRadians.z, eulerRadians.y, eulerRadians.x);
}

DirectX::XMFLOAT3 PhysicsSystem::ToXMFLOAT3(const Vec3& v)
{
    return DirectX::XMFLOAT3(v.x, v.y, v.z);
}

DirectX::XMFLOAT3 PhysicsSystem::ToEulerRadians(const Quat& q)
{
    // Quaternion → Euler (라디안)
    // DirectX::SimpleMath::Quaternion에서 직접 계산
    float x = q.x, y = q.y, z = q.z, w = q.w;
    
    // Roll (X-axis rotation)
    float sinr_cosp = 2.0f * (w * x + y * z);
    float cosr_cosp = 1.0f - 2.0f * (x * x + y * y);
    float roll = std::atan2(sinr_cosp, cosr_cosp);
    
    // Pitch (Y-axis rotation)
    float sinp = 2.0f * (w * y - z * x);
    float pitch;
    if (std::abs(sinp) >= 1.0f)
        pitch = std::copysign(3.14159265358979323846f / 2.0f, sinp); // Use 90 degrees if out of range
    else
        pitch = std::asin(sinp);
    
    // Yaw (Z-axis rotation)
    float siny_cosp = 2.0f * (w * z + x * y);
    float cosy_cosp = 1.0f - 2.0f * (y * y + z * z);
    float yaw = std::atan2(siny_cosp, cosy_cosp);
    
    return DirectX::XMFLOAT3(roll, pitch, yaw);
}
