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

    // 모든 CCT 정리
    for (auto& [entityId, handle] : m_entityToCCT)
    {
        handle.Destroy();
    }
    m_entityToCCT.clear();
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
    m_lastColliders.clear();
    m_lastRigidBodies.clear();
    m_lastTerrains.clear();

    // CCT 정리
    for (auto& [entityId, handle] : m_entityToCCT)
    {
        handle.Destroy();
    }
    m_entityToCCT.clear();

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

    // 
    // (A) 이전 시뮬 결과 반영: ActiveTransform → TransformComponent
    {
        std::vector<ActiveTransform> ats;
        m_physicsWorld->DrainActiveTransforms(ats);
        for (const auto& at : ats)
            SyncPhysicsToGame(at);
    }

    // (B) 이벤트 라우팅: Trigger/Contact/JointBreak
    {
        std::vector<PhysicsEvent> events;
        m_physicsWorld->DrainEvents(events);
        if (m_eventCallback)
        {
            for (const auto& e : events)
                m_eventCallback(e, m_eventCallbackUserData);
        }
    }

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
            const bool hasRB = (entitiesWithRigidBody.find(entityId) != entitiesWithRigidBody.end());

            if (!hasRB)
            {
                if (collider.physicsActorHandle == nullptr)
                    CreatePhysicsActor(entityId);
            }
            else
            {
                // RB가 이미 있는데 Collider가 새로 붙은 경우:
                // collider.physicsActorHandle이 null이면 기존 RB 액터에 연결하고 shape 빌드
                auto* rb = m_world.GetComponent<RigidBodyComponent>(entityId);
                if (rb && rb->physicsActorHandle && collider.physicsActorHandle == nullptr)
                {
                    collider.physicsActorHandle = rb->physicsActorHandle;
                    RebuildShapes(entityId); // 초기 shape 생성
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

        // CCT 생성
        {
            auto ccts = m_world.GetComponents<CharacterControllerComponent>();
            for (const auto& [entityId, cct] : ccts)
            {
                if (cct.controllerHandle == nullptr)
                    CreateCharacterController(entityId);
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

        // CCT 제거 (컴포넌트 사라진 엔티티 정리)
        {
            std::vector<EntityId> cctToRemove;
            for (const auto& [entityId, h] : m_entityToCCT)
            {
                if (!m_world.GetComponent<CharacterControllerComponent>(entityId))
                    cctToRemove.push_back(entityId);
            }
            for (auto eid : cctToRemove)
                DestroyCharacterController(eid);
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
            // Collider 제거 시: RB는 남아도 shape는 제거되어야 함
            auto itActor = m_entityToActor.find(entityId);
            if (itActor != m_entityToActor.end())
            {
                ActorHandle& handle = itActor->second;
                if (handle.IsValid())
                {
                    IPhysicsActor* actor = handle.GetActor();
                    if (actor && actor->IsValid())
                    {
                        actor->ClearShapes();

                        // dynamic이면 질량 다시 계산
                        IRigidBody* body = handle.GetRigidBody();
                        if (body && body->IsValid())
                            body->RecomputeMass();
                    }
                }
            }

            m_lastColliders.erase(entityId);
        }
    }

    //  4. RigidBodyComponent "런타임 변경 감지 → PhysX 적용"
    {
        auto rigidBodies = m_world.GetComponents<RigidBodyComponent>();
        for (const auto& [entityId, rb] : rigidBodies)
        {
            // 핸들이 없으면 생성 파트에서 만들어질 거라 여기선 패스 가능
            IRigidBody* body = nullptr;
            auto it = m_entityToActor.find(entityId);
            if (it != m_entityToActor.end())
                body = it->second.GetRigidBody();
            if (!body || !body->IsValid())
                continue;

            RigidBodyState cur{};
            cur.density = rb.density;
            cur.massOverride = rb.massOverride;
            cur.isKinematic = rb.isKinematic;
            cur.gravityEnabled = rb.gravityEnabled;
            cur.startAwake = rb.startAwake;
            cur.enableCCD = rb.enableCCD;
            cur.enableSpeculativeCCD = rb.enableSpeculativeCCD;
            cur.lockFlags = rb.lockFlags;
            cur.linearDamping = rb.linearDamping;
            cur.angularDamping = rb.angularDamping;
            cur.maxLinearVelocity = rb.maxLinearVelocity;
            cur.maxAngularVelocity = rb.maxAngularVelocity;
            cur.solverPositionIterations = rb.solverPositionIterations;
            cur.solverVelocityIterations = rb.solverVelocityIterations;
            cur.sleepThreshold = rb.sleepThreshold;
            cur.stabilizationThreshold = rb.stabilizationThreshold;

            auto lastIt = m_lastRigidBodies.find(entityId);
            if (lastIt == m_lastRigidBodies.end())
            {
                m_lastRigidBodies[entityId] = cur;
                continue;
            }

            const RigidBodyState& prev = lastIt->second;

            if (cur.isKinematic != prev.isKinematic) body->SetKinematic(cur.isKinematic);
            if (cur.gravityEnabled != prev.gravityEnabled) body->SetGravityEnabled(cur.gravityEnabled);

            if (cur.enableCCD != prev.enableCCD || cur.enableSpeculativeCCD != prev.enableSpeculativeCCD)
                body->SetCCDEnabled(cur.enableCCD, cur.enableSpeculativeCCD);

            if (cur.lockFlags != prev.lockFlags) body->SetLockFlags(cur.lockFlags);

            if (cur.linearDamping != prev.linearDamping || cur.angularDamping != prev.angularDamping)
                body->SetDamping(cur.linearDamping, cur.angularDamping);

            if (cur.maxLinearVelocity != prev.maxLinearVelocity || cur.maxAngularVelocity != prev.maxAngularVelocity)
                body->SetMaxVelocities(cur.maxLinearVelocity, cur.maxAngularVelocity);

            if (cur.density != prev.density || cur.massOverride != prev.massOverride)
                body->SetMassProperties(cur.density, cur.massOverride);

            if (cur.solverPositionIterations != prev.solverPositionIterations ||
                cur.solverVelocityIterations != prev.solverVelocityIterations)
                body->SetSolverIterations(cur.solverPositionIterations, cur.solverVelocityIterations);

            if (cur.sleepThreshold != prev.sleepThreshold)
                body->SetSleepThreshold(cur.sleepThreshold);

            if (cur.stabilizationThreshold != prev.stabilizationThreshold)
                body->SetStabilizationThreshold(cur.stabilizationThreshold);

            // startAwake 변화는 "생성 시" 의미가 크지만, 런타임 토글도 대응 가능
            if (cur.startAwake != prev.startAwake)
            {
                if (cur.startAwake) body->WakeUp();
                else body->PutToSleep();
            }

            m_lastRigidBodies[entityId] = cur;
        }
    }

    // 5. Terrain 변경 감지 및 재생성
    {
        auto terrains = m_world.GetComponents<TerrainHeightFieldComponent>();
        for (const auto& [entityId, terrain] : terrains)
        {
            auto* transform = m_world.GetComponent<TransformComponent>(entityId);
            if (!transform) continue;

            TerrainState cur{};
            cur.numRows = terrain.numRows;
            cur.numCols = terrain.numCols;
            cur.rowScale = terrain.rowScale;
            cur.colScale = terrain.colScale;
            cur.heightScale = terrain.heightScale;
            cur.centerPivot = terrain.centerPivot;
            cur.doubleSidedQueries = terrain.doubleSidedQueries;
            cur.staticFriction = terrain.staticFriction;
            cur.dynamicFriction = terrain.dynamicFriction;
            cur.restitution = terrain.restitution;
            cur.layerBits = terrain.layerBits;
            cur.collideMask = terrain.collideMask;
            cur.queryMask = terrain.queryMask;
            cur.scale = transform->scale;

            auto it = m_lastTerrains.find(entityId);
            if (it == m_lastTerrains.end())
            {
                m_lastTerrains[entityId] = cur;
                continue;
            }

            const TerrainState& prev = it->second;
            const bool changed =
                cur.numRows != prev.numRows || cur.numCols != prev.numCols ||
                cur.rowScale != prev.rowScale || cur.colScale != prev.colScale || cur.heightScale != prev.heightScale ||
                cur.centerPivot != prev.centerPivot || cur.doubleSidedQueries != prev.doubleSidedQueries ||
                cur.staticFriction != prev.staticFriction || cur.dynamicFriction != prev.dynamicFriction || cur.restitution != prev.restitution ||
                cur.layerBits != prev.layerBits || cur.collideMask != prev.collideMask || cur.queryMask != prev.queryMask ||
                cur.scale.x != prev.scale.x || cur.scale.y != prev.scale.y || cur.scale.z != prev.scale.z;

            if (changed)
            {
                m_lastTerrains[entityId] = cur;
                CreateTerrainHeightField(entityId); // 내부에서 기존 actor 정리 후 재생성
            }
        }

        // 제거된 terrain 상태 정리
        std::vector<EntityId> toErase;
        for (auto& [eid, st] : m_lastTerrains)
        {
            if (!m_world.GetComponent<TerrainHeightFieldComponent>(eid))
                toErase.push_back(eid);
        }
        for (auto eid : toErase) m_lastTerrains.erase(eid);
    }

    // 6. CCT 이동 + 중력/점프 처리 + Transform 갱신
    {        
        auto ccts = m_world.GetComponents<CharacterControllerComponent>();

        for (const auto& [entityId, ccc] : ccts)
        {
            auto* transform = m_world.GetComponent<TransformComponent>(entityId);
            if (!transform) continue;

            auto it = m_entityToCCT.find(entityId);
            if (it == m_entityToCCT.end() || !it->second.IsValid()) continue;

            ICharacterController* ctrl = it->second.cct;
            if (!ctrl) continue;

            // teleport: 발 위치를 Transform.position으로 강제
            if (ccc.teleport)
            {
                ctrl->SetFootPosition(ToVec3(transform->position));
                ccc.verticalVelocity = 0.0f;
                ccc.teleport = false;
            }

            // 현재 지면 상태(점프/중력에 필요)
            CharacterControllerState st0 = ctrl->GetState(ccc.collideMask, ccc.queryMask, 0.2f, ccc.hitTriggers);
            const bool wasGrounded = st0.onGround;

            // 점프
            if (ccc.jumpRequested && wasGrounded)
                ccc.verticalVelocity = ccc.jumpSpeed;
            ccc.jumpRequested = false;

            // 중력
            if (ccc.applyGravity)
            {
                if (wasGrounded && ccc.verticalVelocity < 0.0f)
                    ccc.verticalVelocity = 0.0f;
                else
                    ccc.verticalVelocity += ccc.gravity * deltaTime;
            }

            // 이동량 계산 (m/s * dt)
            Vec3 disp;
            disp.x = ccc.desiredVelocity.x * deltaTime;
            disp.z = ccc.desiredVelocity.z * deltaTime;
            disp.y = ccc.verticalVelocity * deltaTime;

            CCTCollisionFlags cf = ctrl->Move(
                disp,
                deltaTime,
                ccc.collideMask,
                ccc.queryMask,
                ccc.hitTriggers);

            // 최종 상태 저장
            CharacterControllerState st = ctrl->GetState(ccc.collideMask, ccc.queryMask, 0.2f, ccc.hitTriggers);
            ccc.onGround = st.onGround;
            ccc.groundNormal = ToXMFLOAT3(st.groundNormal);
            ccc.groundDistance = st.groundDistance;
            ccc.collisionFlags = static_cast<uint8_t>(cf);

            // Transform 반영: foot 위치로 동기화
            transform->position = ToXMFLOAT3(ctrl->GetFootPosition());
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
    m_lastRigidBodies.erase(entityId);
    m_lastTerrains.erase(entityId);
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

    auto AbsScale = [](const DirectX::XMFLOAT3& s) -> Vec3 {
        return Vec3(std::abs(s.x), std::abs(s.y), std::abs(s.z));
    };
    const Vec3 s = AbsScale(transform->scale);

    // HeightFieldColliderDesc 구성
    HeightFieldColliderDesc hfDesc{};
    hfDesc.heightSamples = terrain->heightSamples.data();
    hfDesc.numRows = terrain->numRows;
    hfDesc.numCols = terrain->numCols;

    // Transform scale 반영: X=col, Z=row, Y=height
    hfDesc.colScale    = terrain->colScale    * s.x;
    hfDesc.rowScale    = terrain->rowScale    * s.z;
    hfDesc.heightScale = terrain->heightScale * s.y;
    hfDesc.staticFriction = terrain->staticFriction;
    hfDesc.dynamicFriction = terrain->dynamicFriction;
    hfDesc.restitution = terrain->restitution;
    hfDesc.layerBits = terrain->layerBits;
    hfDesc.collideMask = terrain->collideMask;
    hfDesc.queryMask = terrain->queryMask;
    hfDesc.isTrigger = false; // HeightField는 절대 트리거 불가 (PhysX 제약)
    hfDesc.doubleSidedQueries = terrain->doubleSidedQueries; 
    hfDesc.userData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(entityId));

    // 피벗 보정: centerPivot이 true면 지형 중앙으로 localPos 조정
    Vec3 localPos = Vec3::Zero;
    Quat localRot = Quat::Identity;

    if (terrain->centerPivot)
    {
        const float halfW = 0.5f * static_cast<float>(terrain->numCols - 1) * hfDesc.colScale;
        const float halfD = 0.5f * static_cast<float>(terrain->numRows - 1) * hfDesc.rowScale;
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
    IRigidBody* body = handle.GetRigidBody();

    Vec3 pos = ToVec3(position);
    Quat rot = ToQuat(rotation);

    auto* rb = m_world.GetComponent<RigidBodyComponent>(entityId);

    if (rb && body && body->IsValid())
    {
        if (rb->isKinematic)
        {
            body->SetKinematicTarget(pos, rot);
            return;
        }

        //  Dynamic: teleport일 때만 transform을 물리에 밀어 넣는다
        if (rb->teleport)
        {
            actor->SetTransform(pos, rot);

            if (rb->resetVelocityOnTeleport)
            {
                body->SetLinearVelocity(Vec3::Zero);
                body->SetAngularVelocity(Vec3::Zero);
            }

            rb->teleport = false;
            return;
        }

        //  teleport 아니면 "게임이 건드린 Transform을 되돌림"
        if (actor && actor->IsValid())
        {
            auto* t = m_world.GetComponent<TransformComponent>(entityId);
            if (t)
            {
                t->position = ToXMFLOAT3(actor->GetPosition());
                t->rotation = ToEulerRadians(actor->GetRotation());
                m_lastTransforms[entityId] = { t->position, t->rotation, t->scale };
            }
        }
        return;
    }

    // Static actor(또는 RB 없는 static collider): 기존대로
    if (actor && actor->IsValid())
        actor->SetTransform(pos, rot);
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

void PhysicsSystem::CreateCharacterController(EntityId entityId)
{
    if (!m_physicsWorld) return;
    if (!m_physicsWorld->SupportsCharacterControllers()) return;

    auto* transform = m_world.GetComponent<TransformComponent>(entityId);
    auto* ccc = m_world.GetComponent<CharacterControllerComponent>(entityId);
    if (!transform || !ccc) return;

    // ⚠️ 같은 엔티티에 RB/Collider 같이 두지 마라. 충돌/동기화 싸움 난다.
    if (m_world.GetComponent<RigidBodyComponent>(entityId) || m_world.GetComponent<ColliderComponent>(entityId))
        return;

    auto AbsScale = [](const DirectX::XMFLOAT3& s) -> Vec3 {
        return Vec3(std::abs(s.x), std::abs(s.y), std::abs(s.z));
    };
    const Vec3 s = AbsScale(transform->scale);

    CharacterControllerDesc desc{};
    desc.type = CCTType::Capsule;

    // Capsule 스케일 규칙: Y는 높이, X/Z는 반경(비균일이면 큰 축 선택)
    const float radial = std::max(s.x, s.z);
    desc.radius = ccc->radius * radial;
    desc.halfHeight = ccc->halfHeight * s.y;

    desc.stepOffset = ccc->stepOffset;
    desc.contactOffset = ccc->contactOffset;
    desc.slopeLimitRadians = ccc->slopeLimitRadians;
    desc.nonWalkableMode = ccc->nonWalkableMode;
    desc.climbingMode = ccc->climbingMode;
    desc.density = ccc->density;
    desc.enableQueries = ccc->enableQueries;

    desc.layerBits = ccc->layerBits;
    desc.collideMask = ccc->collideMask;
    desc.queryMask = ccc->queryMask;
    desc.userData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(entityId));

    // foot 기준: Transform.position을 발 위치로 사용
    desc.footPosition = ToVec3(transform->position);
    desc.upDirection = Vec3::UnitY;

    auto ctrl = m_physicsWorld->CreateCharacterController(desc);
    if (!ctrl) return;

    CCTHandle handle(std::move(ctrl));
    ccc->controllerHandle = handle.cct;
    m_entityToCCT[entityId] = std::move(handle);
}

void PhysicsSystem::DestroyCharacterController(EntityId entityId)
{
    auto it = m_entityToCCT.find(entityId);
    if (it == m_entityToCCT.end()) return;
    it->second.Destroy();
    m_entityToCCT.erase(it);

    auto* ccc = m_world.GetComponent<CharacterControllerComponent>(entityId);
    if (ccc) ccc->controllerHandle = nullptr;
}
