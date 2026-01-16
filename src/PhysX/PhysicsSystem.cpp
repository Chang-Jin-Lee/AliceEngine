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
    // 모든 물리 액터 정리
    for (auto& [entityId, actor] : m_entityToActor)
    {
        if (actor)
        {
            // IRigidBody 또는 IPhysicsActor의 Destroy 호출
            // 실제로는 shared_ptr로 관리되므로 자동 해제되지만, 명시적으로 정리
            static_cast<IPhysicsActor*>(actor)->Destroy();
        }
    }
    m_entityToActor.clear();
}

void PhysicsSystem::SetPhysicsWorld(IPhysicsWorld* physicsWorld)
{
    // 기존 액터들 정리
    for (auto& [entityId, actor] : m_entityToActor)
    {
        if (actor)
        {
            static_cast<IPhysicsActor*>(actor)->Destroy();
        }
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

        // 제거된 컴포넌트 확인 (m_entityToActor에 있지만 컴포넌트가 없는 경우)
        std::vector<EntityId> toRemove;
        for (const auto& [entityId, actor] : m_entityToActor)
        {
            auto* rb = m_world.GetComponent<RigidBodyComponent>(entityId);
            auto* collider = m_world.GetComponent<ColliderComponent>(entityId);
            
            if (!rb && !collider)
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

        IRigidBody* body = nullptr;

        if (collider)
        {
            // Collider 타입에 따라 바디 생성
            switch (collider->type)
            {
            case ColliderType::Box:
            {
                BoxColliderDesc boxDesc{};
                boxDesc.halfExtents = ToVec3(collider->halfExtents);
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
                    body = bodyPtr.release(); // 소유권 이전 (나중에 수동 삭제)
                }
                break;
            }
            case ColliderType::Sphere:
            {
                SphereColliderDesc sphereDesc{};
                sphereDesc.radius = collider->radius;
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
                    body = bodyPtr.release();
                }
                break;
            }
            case ColliderType::Capsule:
            {
                CapsuleColliderDesc capsuleDesc{};
                capsuleDesc.radius = collider->capsuleRadius;
                capsuleDesc.halfHeight = collider->capsuleHalfHeight;
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
                    body = bodyPtr.release();
                }
                break;
            }
            }

            if (body)
            {
                rb->physicsActorHandle = body;
                collider->physicsActorHandle = body;
                m_entityToActor[entityId] = body;
            }
        }
        else
        {
            // Collider 없이 빈 바디 생성
            auto bodyPtr = m_physicsWorld->CreateDynamicEmpty(pos, rot, rbDesc);
            if (bodyPtr)
            {
                body = bodyPtr.release();
                rb->physicsActorHandle = body;
                m_entityToActor[entityId] = body;
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

        IPhysicsActor* actor = nullptr;

        switch (collider->type)
        {
        case ColliderType::Box:
        {
            BoxColliderDesc boxDesc{};
            boxDesc.halfExtents = ToVec3(collider->halfExtents);
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
                actor = actorPtr.release();
            }
            break;
        }
        case ColliderType::Sphere:
        {
            SphereColliderDesc sphereDesc{};
            sphereDesc.radius = collider->radius;
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
                actor = actorPtr.release();
            }
            break;
        }
        case ColliderType::Capsule:
        {
            CapsuleColliderDesc capsuleDesc{};
            capsuleDesc.radius = collider->capsuleRadius;
            capsuleDesc.halfHeight = collider->capsuleHalfHeight;
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
                actor = actorPtr.release();
            }
            break;
        }
        }

        if (actor)
        {
            collider->physicsActorHandle = actor;
            m_entityToActor[entityId] = actor;
        }
    }
}

void PhysicsSystem::DestroyPhysicsActor(EntityId entityId)
{
    auto it = m_entityToActor.find(entityId);
    if (it == m_entityToActor.end()) return;

    void* actor = it->second;
    if (actor)
    {
        static_cast<IPhysicsActor*>(actor)->Destroy();
    }

    // 컴포넌트의 핸들도 초기화
    auto* rb = m_world.GetComponent<RigidBodyComponent>(entityId);
    if (rb) rb->physicsActorHandle = nullptr;

    auto* collider = m_world.GetComponent<ColliderComponent>(entityId);
    if (collider) collider->physicsActorHandle = nullptr;

    m_entityToActor.erase(it);
    m_lastTransforms.erase(entityId);
}

void PhysicsSystem::SyncGameToPhysics(EntityId entityId, const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& rotation)
{
    auto it = m_entityToActor.find(entityId);
    if (it == m_entityToActor.end()) return;

    void* actor = it->second;
    if (!actor) return;

    Vec3 pos = ToVec3(position);
    Quat rot = ToQuat(rotation);

    auto* rb = m_world.GetComponent<RigidBodyComponent>(entityId);
    if (rb && rb->isKinematic)
    {
        // Kinematic 바디는 SetKinematicTarget 사용
        IRigidBody* body = static_cast<IRigidBody*>(actor);
        if (body && body->IsValid())
        {
            body->SetKinematicTarget(pos, rot);
        }
    }
    else
    {
        // Static Actor 또는 Dynamic 바디는 SetTransform 사용
        IPhysicsActor* physActor = static_cast<IPhysicsActor*>(actor);
        if (physActor && physActor->IsValid())
        {
            physActor->SetTransform(pos, rot);
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
