#include "PhysicsSystem.h"
#include "Core/World.h"
#include "Components/TransformComponent.h"
#include "Components/PhysicsSceneSettingsComponent.h"
#include "Core/Logger.h"
#include "Core/ThreadSafety.h"
#include <DirectXMath.h>
#include <algorithm>
#include <unordered_set>
#include <cmath>
#include <bit>
#include <cstring>

using namespace DirectX;
using namespace Alice;

// 레이어 비트에서 첫 번째 레이어 인덱스를 찾는 헬퍼 함수
static int FirstLayerIndex(uint32_t bits)
{
	if (bits == 0) return -1;
	return (int)std::countr_zero(bits); // C++20
}

static void* MakeUserData(uint64_t worldEpoch, EntityId entityId) noexcept
{
	const uint64_t combined = (worldEpoch << 32) | (static_cast<uint64_t>(entityId) + 1u);
	return reinterpret_cast<void*>(static_cast<std::uintptr_t>(combined));
}

static uint64_t HashCombine64(uint64_t a, uint64_t b) noexcept
{
	a ^= b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2);
	return a;
}

static uint64_t MakeTerrainGeomKey(const TerrainHeightFieldComponent& t) noexcept
{
	uint64_t key = 0;
	key = HashCombine64(key, static_cast<uint64_t>(t.numRows));
	key = HashCombine64(key, static_cast<uint64_t>(t.numCols));
	
	uint32_t heightScaleBits = 0;
	std::memcpy(&heightScaleBits, &t.heightScale, sizeof(float));
	key = HashCombine64(key, static_cast<uint64_t>(heightScaleBits));
	
	uint32_t rowScaleBits = 0;
	std::memcpy(&rowScaleBits, &t.rowScale, sizeof(float));
	key = HashCombine64(key, static_cast<uint64_t>(rowScaleBits));
	
	uint32_t colScaleBits = 0;
	std::memcpy(&colScaleBits, &t.colScale, sizeof(float));
	key = HashCombine64(key, static_cast<uint64_t>(colScaleBits));
	
	key = HashCombine64(key, static_cast<uint64_t>(t.heightSamples.size()));
	
	return key;
}

PhysicsSystem::LayerMaskArray PhysicsSystem::MakeAllMaskArray() noexcept
{
	LayerMaskArray a{};
	const uint32_t ALL = AllLayersMask();
	for (auto& v : a) v = ALL;
	return a;
}

PhysicsSystem::PhysicsSystem(World& world)
    : m_world(world)
{
}

PhysicsSystem::~PhysicsSystem()
{
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

    for (auto& [entityId, handle] : m_entityToCCT)
    {
        handle.Destroy();
    }
    m_entityToCCT.clear();
}

void PhysicsSystem::SetPhysicsWorld(IPhysicsWorld* physicsWorld)
{
	ThreadSafety::AssertMainThread();
	IPhysicsWorld* oldWorld = m_physicsWorld;
	if (physicsWorld == nullptr && oldWorld != nullptr)
	{
		oldWorld->Flush();
	}

	m_physicsWorld = physicsWorld;

    // 기존 액터들 정리
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
		if (handle.IsValid())
    {
        handle.Destroy();
		}
    }
    m_entityToCCT.clear();
    m_lastCCTs.clear();

	if (oldWorld != nullptr)
	{
		oldWorld->Flush();
	}
	m_lastFilterRevision = 0xFFFFFFFFu; // 강제로 다음 Update에서 1회 갱신

}

void PhysicsSystem::SetEventCallback(EventCallback callback, void* userData)
{
    m_eventCallback = callback;
    m_eventCallbackUserData = userData;
}

void PhysicsSystem::Update(float deltaTime)
{
	ThreadSafety::AssertMainThread();
    IPhysicsWorld* current = m_world.GetPhysicsWorld();
    if (current != m_physicsWorld) {
        SetPhysicsWorld(current); // 바뀌었으면 정리+재바인딩
        // SetPhysicsWorld(nullptr)가 호출되면 m_entityToActor가 모두 클리어됨
        // 이후 로직은 실행할 필요 없음
        if (!m_physicsWorld) return;
    }

    if (!m_physicsWorld) return;

	// (추가) Scene Settings -> 각 컴포넌트 mask로 반영 (전역 매트릭스 기반)
	// 매 프레임 collideByLayer/queryByLayer를 계산하여 layerBits/ignoreLayers 변경 시에도 사용 가능하게 함
	// PhysicsSceneSettingsComponent가 없어도 기본값으로 전부 허용
	LayerMaskArray collideByLayer = MakeAllMaskArray();
	LayerMaskArray queryByLayer = MakeAllMaskArray();
	
	// PhysicsSceneSettingsComponent가 있으면 매트릭스 기반으로 덮어쓰기
	{
		const auto& settingsMap = m_world.GetComponents<PhysicsSceneSettingsComponent>();
		if (!settingsMap.empty())
		{
			auto& s = const_cast<PhysicsSceneSettingsComponent&>(settingsMap.begin()->second);

			// filterRevision 변경 감지: 전역 매트릭스가 변경되었는지 확인
			bool filterMatrixChanged = (s.filterRevision != m_lastFilterRevision);

			// 매 프레임 collideByLayer/queryByLayer 계산 (성능 부담 거의 없음)
			// collide: row 기반 (layerCollideMatrix[i][j] = true면 레이어 i와 j가 충돌)
			for (int i = 0; i < MAX_PHYSICS_LAYERS; ++i)
			{
				uint32_t mask = 0;
				for (int j = 0; j < MAX_PHYSICS_LAYERS; ++j)
					if (s.layerCollideMatrix[i][j]) mask |= (1u << j);

				// 원하면 자기 레이어는 자동 off:
				// mask &= ~(1u << i);

				collideByLayer[i] = mask;
			}

			// query: column 기반 (layerQueryMatrix[querier][target] = true면 querier가 target을 쿼리 가능)
			// target 레이어 입장에서 "누가 나를 쿼리할 수 있는가"를 마스크로 저장
			for (int target = 0; target < MAX_PHYSICS_LAYERS; ++target)
			{
				uint32_t mask = 0;
				for (int querier = 0; querier < MAX_PHYSICS_LAYERS; ++querier)
					if (s.layerQueryMatrix[querier][target]) mask |= (1u << querier);

				queryByLayer[target] = mask;
			}

			if (filterMatrixChanged)
			{
				m_lastFilterRevision = s.filterRevision;

				// Collider들에 적용 (전역 매트릭스 변경 시에만 전체 재계산)
				auto colliders = m_world.GetComponents<ColliderComponent>();
				for (auto&& [id, col] : colliders)
				{
					int li = FirstLayerIndex(col.layerBits);
					if (li < 0 || li >= MAX_PHYSICS_LAYERS) continue;

					uint32_t newCollide = collideByLayer[li];
					uint32_t newQuery = queryByLayer[li];

					newCollide &= ~col.ignoreLayers;
					newQuery &= ~col.ignoreLayers;

					bool maskChanged = false;
					if (col.collideMask != newCollide)
					{
						col.collideMask = newCollide;
						maskChanged = true;
					}
					if (col.queryMask != newQuery)
					{
						col.queryMask = newQuery;
						maskChanged = true;
					}

					if (maskChanged)
					{
						auto it = m_entityToActor.find(id);
						if (it != m_entityToActor.end())
						{
							ActorHandle& handle = it->second;
							if (handle.IsValid() && handle.GetActor())
							{
								auto* collider = m_world.GetComponent<ColliderComponent>(id);
								if (collider)
								{
									handle.GetActor()->SetLayerMasks(col.layerBits, col.collideMask, col.queryMask);
								}
							}
						}
					}
    }

				// Terrain에 적용 (전역 매트릭스 변경 시에만)
				auto terrains = m_world.GetComponents<TerrainHeightFieldComponent>();
				for (auto&& [id, terrain] : terrains)
				{
					int li = FirstLayerIndex(terrain.layerBits);
					if (li < 0 || li >= MAX_PHYSICS_LAYERS) continue;

					uint32_t newCollide = collideByLayer[li];
					uint32_t newQuery = queryByLayer[li];

					newCollide &= ~terrain.ignoreLayers;
					newQuery &= ~terrain.ignoreLayers;

					bool maskChanged = false;
					if (terrain.collideMask != newCollide)
					{
						terrain.collideMask = newCollide;
						maskChanged = true;
					}
					if (terrain.queryMask != newQuery)
					{
						terrain.queryMask = newQuery;
						maskChanged = true;
					}

					// Terrain 필터 변경 시 SetLayerMasks 사용 (재생성 대신)
					if (maskChanged)
					{
						auto itA = m_entityToActor.find(id);
						if (itA != m_entityToActor.end() && itA->second.IsValid() && itA->second.GetActor())
						{
							itA->second.GetActor()->SetLayerMasks(terrain.layerBits, terrain.collideMask, terrain.queryMask);
						}
					}
				}

				// CCT에 적용 (전역 매트릭스 변경 시에만)
				auto ccts = m_world.GetComponents<CharacterControllerComponent>();
				for (auto&& [id, cct] : ccts)
        {
					int li = FirstLayerIndex(cct.layerBits);
					if (li < 0 || li >= MAX_PHYSICS_LAYERS) continue;

					uint32_t newCollide = collideByLayer[li];
					uint32_t newQuery = queryByLayer[li];

					newCollide &= ~cct.ignoreLayers;
					newQuery &= ~cct.ignoreLayers;

					bool maskChanged = false;
					if (cct.collideMask != newCollide)
					{
						cct.collideMask = newCollide;
						maskChanged = true;
					}
					if (cct.queryMask != newQuery)
					{
						cct.queryMask = newQuery;
						maskChanged = true;
					}

					if (maskChanged)
					{
						auto itCCT = m_entityToCCT.find(id);
						if (itCCT != m_entityToCCT.end() && itCCT->second.IsValid())
						{
							itCCT->second.cct->SetLayerMasks(cct.layerBits, cct.collideMask, cct.queryMask);
        }
    }
				}
			}
		}

	}


    // 1. 컴포넌트 변경 감지 및 물리 액터 생성/삭제
    {
        auto rigidBodies = m_world.GetComponents<RigidBodyComponent>();
        std::unordered_set<EntityId> entitiesWithRigidBody;
        
        for (const auto& [entityId, rb] : rigidBodies)
        {
            entitiesWithRigidBody.insert(entityId);
            
            if (rb.physicsActorHandle == nullptr)
            {
                CreatePhysicsActor(entityId);
            }
        }

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
                auto* rb = m_world.GetComponent<RigidBodyComponent>(entityId);
				auto* col = m_world.GetComponent<ColliderComponent>(entityId);
				if (rb && rb->physicsActorHandle && col && col->physicsActorHandle == nullptr)
                {
					col->physicsActorHandle = rb->physicsActorHandle;
                    RebuildShapes(entityId);
                }
            }
        }

        {
            auto ccts = m_world.GetComponents<CharacterControllerComponent>();
            for (const auto& [entityId, cct] : ccts)
            {
                auto* ccc = m_world.GetComponent<CharacterControllerComponent>(entityId);
                if (!ccc) continue;

                auto itCCT = m_entityToCCT.find(entityId);
                if ((itCCT == m_entityToCCT.end() || !itCCT->second.IsValid()) || ccc->controllerHandle == nullptr)
                {
                    CreateCharacterController(entityId);
                }
            }
        }

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

    // 2. Game → Physics 동기화
    {
        auto transforms = m_world.GetComponents<TransformComponent>();
        for (const auto& [entityId, transform] : transforms)
        {
            auto* rb = m_world.GetComponent<RigidBodyComponent>(entityId);
            auto* collider = m_world.GetComponent<ColliderComponent>(entityId);
            
            if (!rb && !collider) continue;

            auto it = m_lastTransforms.find(entityId);
            bool needsSync = false;

            if (it == m_lastTransforms.end())
            {
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

    // 3. Collider 변경 감지 및 Shape 재구성
    {
        auto colliders = m_world.GetComponents<ColliderComponent>();
        for (const auto& [entityId, collider] : colliders)
        {
            auto* transform = m_world.GetComponent<TransformComponent>(entityId);
            if (!transform) continue;

            auto it = m_lastColliders.find(entityId);
            bool needsRebuild = false;

            if (it == m_lastColliders.end())
            {
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
				state.ignoreLayers = collider.ignoreLayers;
                state.isTrigger = collider.isTrigger;
                state.scale = transform->scale;
                m_lastColliders[entityId] = state;
            }
            else
            {
                const auto& last = it->second;
                bool changed = false;
				bool maskOnlyChanged = false;

				bool layerOrIgnoreChanged = (collider.layerBits != last.layerBits || collider.ignoreLayers != last.ignoreLayers);
				if (layerOrIgnoreChanged)
				{
					int li = FirstLayerIndex(collider.layerBits);
					if (li >= 0 && li < MAX_PHYSICS_LAYERS)
					{
						uint32_t newCollide = collideByLayer[li];
						uint32_t newQuery = queryByLayer[li];
						
						newCollide &= ~collider.ignoreLayers;
						newQuery &= ~collider.ignoreLayers;
						
						collider.collideMask = newCollide;
						collider.queryMask = newQuery;
						maskOnlyChanged = true;
					}
				}

                if (collider.type != last.type ||
                    collider.halfExtents.x != last.halfExtents.x || collider.halfExtents.y != last.halfExtents.y || collider.halfExtents.z != last.halfExtents.z ||
                    collider.radius != last.radius ||
                    collider.capsuleRadius != last.capsuleRadius ||
                    collider.capsuleHalfHeight != last.capsuleHalfHeight ||
                    collider.capsuleAlignYAxis != last.capsuleAlignYAxis ||
                    collider.staticFriction != last.staticFriction ||
                    collider.dynamicFriction != last.dynamicFriction ||
                    collider.restitution != last.restitution ||
                    collider.isTrigger != last.isTrigger)
                {
                    changed = true;
                }

				if (collider.collideMask != last.collideMask || collider.queryMask != last.queryMask)
				{
					if (!maskOnlyChanged) maskOnlyChanged = true;
				}

                // Scale 변경
                if (transform->scale.x != last.scale.x || 
                    transform->scale.y != last.scale.y || 
                    transform->scale.z != last.scale.z)
                {
                    changed = true;
                }

				if (maskOnlyChanged && !changed)
				{
					auto itActor = m_entityToActor.find(entityId);
					if (itActor != m_entityToActor.end())
					{
						ActorHandle& handle = itActor->second;
						if (handle.IsValid() && handle.GetActor())
						{
							handle.GetActor()->SetLayerMasks(collider.layerBits, collider.collideMask, collider.queryMask);
						}
					}
				}

				if (changed || maskOnlyChanged)
				{
					if (changed) needsRebuild = true;
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
					it->second.ignoreLayers = collider.ignoreLayers;
                    it->second.isTrigger = collider.isTrigger;
                    it->second.scale = transform->scale;
                }
            }

            if (needsRebuild)
            {
                RebuildShapes(entityId);
            }
        }

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

                        IRigidBody* body = handle.GetRigidBody();
                        if (body && body->IsValid())
                            body->RecomputeMass();
                    }
                }
            }

            m_lastColliders.erase(entityId);
        }
    }

    //  4. RigidBodyComponent 변경 감지
    {
        auto rigidBodies = m_world.GetComponents<RigidBodyComponent>();
        for (const auto& [entityId, rb] : rigidBodies)
        {
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

            if (cur.startAwake != prev.startAwake)
            {
                if (cur.startAwake) body->WakeUp();
                else body->PutToSleep();
            }

            m_lastRigidBodies[entityId] = cur;
        }
    }

	// 4. Terrain 변경 감지
    {
        auto terrains = m_world.GetComponents<TerrainHeightFieldComponent>();
        for (const auto& [entityId, terrain] : terrains)
        {
            auto* transform = m_world.GetComponent<TransformComponent>(entityId);
            if (!transform) continue;

            int li = FirstLayerIndex(terrain.layerBits);
            if (li < 0 || li >= MAX_PHYSICS_LAYERS) continue;

            uint32_t newCollide = collideByLayer[li];
            uint32_t newQuery = queryByLayer[li];

            newCollide &= ~terrain.ignoreLayers;
            newQuery &= ~terrain.ignoreLayers;

            terrain.collideMask = newCollide;
            terrain.queryMask = newQuery;

            if (terrain.heightSamples.empty() && terrain.numRows >= 2 && terrain.numCols >= 2)
            {
                const size_t expectedSamples = static_cast<size_t>(terrain.numRows) * static_cast<size_t>(terrain.numCols);
                terrain.heightSamples.resize(expectedSamples, 0.0f);
            }

            const uint64_t geomKey = MakeTerrainGeomKey(terrain);

            auto it = m_lastTerrains.find(entityId);
            if (it == m_lastTerrains.end())
            {
                TerrainState state{};
                state.layerBits = terrain.layerBits;
                state.ignoreLayers = terrain.ignoreLayers;
                state.collideMask = newCollide;
                state.queryMask = newQuery;
                state.lastGeomKey = geomKey;

                m_lastTerrains[entityId] = state;

                if (!terrain.heightSamples.empty() && terrain.numRows >= 2 && terrain.numCols >= 2)
                {
                    CreateTerrainHeightField(entityId);
                }
                continue;
            }

            TerrainState prev = it->second;

            const bool geomChanged = (geomKey != prev.lastGeomKey);

            const bool maskChanged =
                (terrain.layerBits != prev.layerBits) ||
                (terrain.ignoreLayers != prev.ignoreLayers) ||
                (newCollide != prev.collideMask) ||
                (newQuery != prev.queryMask);

            if (geomChanged)
            {
                if (terrain.physicsActorHandle != nullptr)
                {
                    DestroyPhysicsActor(entityId);

                    if (m_physicsWorld)
                    {
                        m_physicsWorld->Flush();
                    }
                }

                if (!terrain.heightSamples.empty() && terrain.numRows >= 2 && terrain.numCols >= 2)
                {
                    CreateTerrainHeightField(entityId);
                }

                TerrainState state{};
                state.layerBits = terrain.layerBits;
                state.ignoreLayers = terrain.ignoreLayers;
                state.collideMask = newCollide;
                state.queryMask = newQuery;
                state.lastGeomKey = geomKey;
                m_lastTerrains[entityId] = state;

                continue;
            }

            if (maskChanged)
            {
                auto itActor = m_entityToActor.find(entityId);
                if (itActor != m_entityToActor.end())
                {
                    ActorHandle& handle = itActor->second;
                    if (handle.IsValid() && handle.GetActor())
                    {
                        handle.GetActor()->SetLayerMasks(terrain.layerBits, newCollide, newQuery);
                    }
                }
            }

            it = m_lastTerrains.find(entityId);
            if (it != m_lastTerrains.end())
            {
                it->second.layerBits = terrain.layerBits;
                it->second.ignoreLayers = terrain.ignoreLayers;
                it->second.collideMask = newCollide;
                it->second.queryMask = newQuery;
            }
        }

        std::vector<EntityId> toErase;
        for (auto& [eid, st] : m_lastTerrains)
        {
            if (!m_world.GetComponent<TerrainHeightFieldComponent>(eid))
                toErase.push_back(eid);
        }
        for (auto eid : toErase) m_lastTerrains.erase(eid);
    }

    // 5. CCT 변경 감지
    {
        auto ccts = m_world.GetComponents<CharacterControllerComponent>();
        
        for (const auto& [entityId, ccc] : ccts)
        {
            auto* transform = m_world.GetComponent<TransformComponent>(entityId);
            if (!transform) continue;

            auto itCCT = m_entityToCCT.find(entityId);
            
            CCTState cur{};
            cur.radius = ccc.radius;
            cur.halfHeight = ccc.halfHeight;
            cur.stepOffset = ccc.stepOffset;
            cur.contactOffset = ccc.contactOffset;
            cur.slopeLimitRadians = ccc.slopeLimitRadians;
            cur.nonWalkableMode = ccc.nonWalkableMode;
            cur.climbingMode = ccc.climbingMode;
            cur.density = ccc.density;
            cur.enableQueries = ccc.enableQueries;
            cur.layerBits = ccc.layerBits;
            cur.collideMask = ccc.collideMask;
            cur.queryMask = ccc.queryMask;
			cur.ignoreLayers = ccc.ignoreLayers;
            cur.hitTriggers = ccc.hitTriggers;
            cur.scale = transform->scale;

            auto itState = m_lastCCTs.find(entityId);
            if (itState == m_lastCCTs.end())
            {
                m_lastCCTs[entityId] = cur;
                auto* cccPtr = m_world.GetComponent<CharacterControllerComponent>(entityId);
                if (!cccPtr) continue;

                bool shouldCreate = (itCCT == m_entityToCCT.end() || !itCCT->second.IsValid() || cccPtr->controllerHandle == nullptr);
                if (shouldCreate)
                {
                    CreateCharacterController(entityId);
                }
            }
            else
            {
                const auto& prev = itState->second;
                bool needsRebuild = false;

				bool layerOrIgnoreChanged = (ccc.layerBits != prev.layerBits || ccc.ignoreLayers != prev.ignoreLayers);
				if (layerOrIgnoreChanged)
				{
					int li = FirstLayerIndex(ccc.layerBits);
					if (li >= 0 && li < MAX_PHYSICS_LAYERS)
					{
						uint32_t newCollide = collideByLayer[li];
						uint32_t newQuery = queryByLayer[li];
						
						newCollide &= ~ccc.ignoreLayers;
						newQuery &= ~ccc.ignoreLayers;
						
						ccc.collideMask = newCollide;
						ccc.queryMask = newQuery;
						cur.collideMask = newCollide;
						cur.queryMask = newQuery;
                    }
				}
                
                if (cur.radius != prev.radius ||
                    cur.halfHeight != prev.halfHeight ||
                    cur.stepOffset != prev.stepOffset ||
                    cur.contactOffset != prev.contactOffset ||
                    cur.slopeLimitRadians != prev.slopeLimitRadians ||
                    cur.nonWalkableMode != prev.nonWalkableMode ||
                    cur.climbingMode != prev.climbingMode ||
                    cur.density != prev.density ||
                    cur.enableQueries != prev.enableQueries ||
                    cur.scale.x != prev.scale.x ||
                    cur.scale.y != prev.scale.y ||
                    cur.scale.z != prev.scale.z)
                {
                    needsRebuild = true;
                }

                if (itCCT == m_entityToCCT.end() || !itCCT->second.IsValid() || ccc.controllerHandle == nullptr)
                {
                    CreateCharacterController(entityId);
                    m_lastCCTs[entityId] = cur;
                }
                else if (needsRebuild)
                {
                    DestroyCharacterController(entityId);
                    CreateCharacterController(entityId);
                    m_lastCCTs[entityId] = cur;
                }
                else
                {
                    if (cur.layerBits != prev.layerBits ||
                        cur.collideMask != prev.collideMask ||
                        cur.queryMask != prev.queryMask ||
						cur.ignoreLayers != prev.ignoreLayers ||
                        cur.hitTriggers != prev.hitTriggers)
                    {
                        ICharacterController* ctrl = itCCT->second.cct;
                        if (ctrl)
                        {
                            ctrl->SetLayerMasks(cur.layerBits, cur.collideMask, cur.queryMask);
                        }
                        m_lastCCTs[entityId] = cur;
                    }
                }
            }
        }

        std::vector<EntityId> cctsToRemove;
        for (const auto& [entityId, state] : m_lastCCTs)
        {
            auto* ccc = m_world.GetComponent<CharacterControllerComponent>(entityId);
            if (!ccc)
            {
                cctsToRemove.push_back(entityId);
            }
        }
        for (auto eid : cctsToRemove)
        {
            DestroyCharacterController(eid);
            m_lastCCTs.erase(eid);
        }
    }

    // 6. CCT 이동 및 Transform 갱신
    {        
        auto ccts = m_world.GetComponents<CharacterControllerComponent>();

        for (const auto& [entityId, ccc] : ccts)
        {
            auto* transform = m_world.GetComponent<TransformComponent>(entityId);
            if (!transform) continue;

            auto it = m_entityToCCT.find(entityId);
            if (it == m_entityToCCT.end() || !it->second.IsValid())
            {
                static std::unordered_set<EntityId> warnedEntities;
                if (warnedEntities.find(entityId) == warnedEntities.end())
                {
                    ALICE_LOG_WARN("[PhysicsSystem] CCT not found for entity %llu (controllerHandle: %p). Check if CreateCharacterController succeeded.",
                        (unsigned long long)entityId, ccc.controllerHandle);
                    
                    auto* rb = m_world.GetComponent<RigidBodyComponent>(entityId);
                    auto* collider = m_world.GetComponent<ColliderComponent>(entityId);
                    if (rb || collider)
                    {
                        ALICE_LOG_WARN("[PhysicsSystem] Entity %llu has RigidBody or Collider, which conflicts with CCT!", 
                            (unsigned long long)entityId);
                    }
                    
                    warnedEntities.insert(entityId);
                }
                continue;
            }

            ICharacterController* ctrl = it->second.cct;
            if (!ctrl) continue;

            // teleport: 발 위치를 Transform.position으로 강제
            if (ccc.teleport)
            {
                ctrl->SetFootPosition(ToVec3(transform->position));
                ccc.verticalVelocity = 0.0f;
                ccc.teleport = false;
            }

            CharacterControllerState st0 = ctrl->GetState(ccc.collideMask, ccc.layerBits, 0.2f, ccc.hitTriggers);
            const bool wasGrounded = st0.onGround;

            if (ccc.jumpRequested && wasGrounded)
                ccc.verticalVelocity = ccc.jumpSpeed;
            ccc.jumpRequested = false;

            if (ccc.applyGravity)
            {
                if (wasGrounded && ccc.verticalVelocity < 0.0f)
                    ccc.verticalVelocity = 0.0f;
                else
                    ccc.verticalVelocity += ccc.gravity * deltaTime;
            }

            Vec3 disp;
            disp.x = ccc.desiredVelocity.x * deltaTime;
            disp.z = ccc.desiredVelocity.z * deltaTime;
			disp.y = ccc.verticalVelocity * deltaTime;

            CCTCollisionFlags cf = ctrl->Move(
                disp,
                deltaTime,
                ccc.collideMask,
				ccc.layerBits,
                ccc.hitTriggers);

            CharacterControllerState st = ctrl->GetState(ccc.collideMask, ccc.layerBits, 0.2f, ccc.hitTriggers);
            ccc.onGround = st.onGround;
            ccc.groundNormal = ToXMFLOAT3(st.groundNormal);
            ccc.groundDistance = st.groundDistance;
            ccc.collisionFlags = static_cast<uint8_t>(cf);

            transform->position = ToXMFLOAT3(ctrl->GetFootPosition());
        }
    }
}

void PhysicsSystem::CreatePhysicsActor(EntityId entityId)
{
    if (!m_physicsWorld) return;

    auto* transform = m_world.GetComponent<TransformComponent>(entityId);
    if (!transform) return;

    auto* rb = m_world.GetComponent<RigidBodyComponent>(entityId);
    auto* collider = m_world.GetComponent<ColliderComponent>(entityId);

    if (!rb && !collider) return;

    Vec3 pos = ToVec3(transform->position);
    Quat rot = ToQuat(transform->rotation);

    if (rb)
    {
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
		rbDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

        if (collider)
        {
            switch (collider->type)
            {
            case ColliderType::Box:
            {
                BoxColliderDesc boxDesc{};
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
				boxDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

				auto bodyPtr = m_physicsWorld->CreateDynamicBox(pos, rot, rbDesc, boxDesc);
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
            case ColliderType::Sphere:
            {
                SphereColliderDesc sphereDesc{};
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
				sphereDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

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
				capsuleDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

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
        }
        else
        {
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
        FilterDesc filterDesc{};
        filterDesc.layerBits = collider->layerBits;
        filterDesc.collideMask = collider->collideMask;
        filterDesc.queryMask = collider->queryMask;
        filterDesc.isTrigger = collider->isTrigger;
        filterDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

        MaterialDesc materialDesc{};
        materialDesc.staticFriction = collider->staticFriction;
        materialDesc.dynamicFriction = collider->dynamicFriction;
        materialDesc.restitution = collider->restitution;

        switch (collider->type)
        {
        case ColliderType::Box:
        {
            BoxColliderDesc boxDesc{};
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
            boxDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

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
            sphereDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

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
            capsuleDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

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

	if (!it->second.owned)
	{
		m_entityToActor.erase(it);
		return;
	}
    auto* rb = m_world.GetComponent<RigidBodyComponent>(entityId);
    if (rb) rb->physicsActorHandle = nullptr;

    auto* collider = m_world.GetComponent<ColliderComponent>(entityId);
    if (collider) collider->physicsActorHandle = nullptr;

    auto* terrain = m_world.GetComponent<TerrainHeightFieldComponent>(entityId);
    if (terrain) terrain->physicsActorHandle = nullptr;

	ActorHandle handle = std::move(it->second);
	
	if (handle.owned)
	{
		handle.Destroy();
	}
	
    m_entityToActor.erase(it);
    m_lastTransforms.erase(entityId);
    m_lastColliders.erase(entityId);
    m_lastRigidBodies.erase(entityId);
}

void PhysicsSystem::CreateTerrainHeightField(EntityId entityId)
{
    if (!m_physicsWorld)
    {
        ALICE_LOG_WARN("[PhysicsSystem] CreateTerrainHeightField: m_physicsWorld is null!");
        return;
    }

    auto* transform = m_world.GetComponent<TransformComponent>(entityId);
    if (!transform)
    {
        ALICE_LOG_WARN("[PhysicsSystem] CreateTerrainHeightField: Transform component missing!");
        return;
    }

    auto* terrain = m_world.GetComponent<TerrainHeightFieldComponent>(entityId);
    if (!terrain)
    {
        ALICE_LOG_WARN("[PhysicsSystem] CreateTerrainHeightField: TerrainHeightFieldComponent missing!");
        return;
    }

	const size_t expectedSamples = static_cast<size_t>(terrain->numRows) * static_cast<size_t>(terrain->numCols);
    if (terrain->heightSamples.empty() || terrain->numRows < 2 || terrain->numCols < 2)
    {
		ALICE_LOG_WARN("[PhysicsSystem] CreateTerrainHeightField: Invalid terrain data (entity: %llu, rows: %u, cols: %u, samples: %zu, expected: %zu)!",
			(unsigned long long)entityId, terrain->numRows, terrain->numCols, terrain->heightSamples.size(), expectedSamples);
        return;
    }

	if (terrain->heightSamples.size() != expectedSamples)
	{
		ALICE_LOG_WARN("[PhysicsSystem] CreateTerrainHeightField: HeightSamples size mismatch (entity: %llu, rows: %u, cols: %u, samples: %zu, expected: %zu)!",
			(unsigned long long)entityId, terrain->numRows, terrain->numCols, terrain->heightSamples.size(), expectedSamples);
		return;
	}

    if (terrain->heightScale <= 0.0f || terrain->rowScale <= 0.0f || terrain->colScale <= 0.0f)
    {
        ALICE_LOG_WARN("[PhysicsSystem] CreateTerrainHeightField: Invalid terrain scale (entity: %llu, heightScale: %.2f, rowScale: %.2f, colScale: %.2f)!",
            (unsigned long long)entityId, terrain->heightScale, terrain->rowScale, terrain->colScale);
        return;
    }

	auto itActor = m_entityToActor.find(entityId);
	if (itActor != m_entityToActor.end())
	{
		ALICE_LOG_WARN("[PhysicsSystem] CreateTerrainHeightField called but actor already exists (entity=%llu). Skipping.",
			(unsigned long long)entityId);
		return;
	}

    if (terrain->physicsActorHandle != nullptr)
	{
		ALICE_LOG_WARN("[PhysicsSystem] Terrain component has stale physicsActorHandle. Forcing null (entity=%llu)",
			(unsigned long long)entityId);
		terrain->physicsActorHandle = nullptr;
    }

    Vec3 pos = ToVec3(transform->position);
    Quat rot = ToQuat(transform->rotation);

    auto AbsScale = [](const DirectX::XMFLOAT3& s) -> Vec3 {
        return Vec3(std::abs(s.x), std::abs(s.y), std::abs(s.z));
    };
    const Vec3 s = AbsScale(transform->scale);

    HeightFieldColliderDesc hfDesc{};
    hfDesc.heightSamples = terrain->heightSamples.data();
    hfDesc.numRows = terrain->numRows;
    hfDesc.numCols = terrain->numCols;

	hfDesc.colScale = terrain->colScale * s.x;
	hfDesc.rowScale = terrain->rowScale * s.z;
    hfDesc.heightScale = terrain->heightScale * s.y;
    hfDesc.staticFriction = terrain->staticFriction;
    hfDesc.dynamicFriction = terrain->dynamicFriction;
    hfDesc.restitution = terrain->restitution;
    hfDesc.layerBits = terrain->layerBits;
    hfDesc.collideMask = terrain->collideMask;
    hfDesc.queryMask = terrain->queryMask;
    hfDesc.isTrigger = false;
    hfDesc.doubleSidedQueries = terrain->doubleSidedQueries; 
    hfDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

    Vec3 localPos = Vec3::Zero;
    Quat localRot = Quat::Identity;

    if (terrain->centerPivot)
    {
        const float halfW = 0.5f * static_cast<float>(terrain->numCols - 1) * hfDesc.colScale;
        const float halfD = 0.5f * static_cast<float>(terrain->numRows - 1) * hfDesc.rowScale;
        localPos = Vec3(-halfW, 0.0f, -halfD);
    }

    if (!m_physicsWorld)
    {
        ALICE_LOG_WARN("[PhysicsSystem] CreateTerrainHeightField: m_physicsWorld became null during creation!");
        return;
    }
    
    auto actorPtr = m_physicsWorld->CreateStaticEmpty(pos, rot, hfDesc.userData);
    if (actorPtr)
    {
        ActorHandle handle(std::move(actorPtr));
        IPhysicsActor* actor = handle.GetActor();
        
        if (!actor->AddHeightFieldShape(hfDesc, localPos, localRot))
        {
            handle.Destroy();
            return;
        }
        
        terrain->physicsActorHandle = actor;
        m_entityToActor[entityId] = std::move(handle);
    }
    else
    {
        ALICE_LOG_ERRORF("[PhysicsSystem] CreateTerrainHeightField: Failed to create terrain actor!");
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

    auto AbsScale = [](const DirectX::XMFLOAT3& s) -> Vec3 {
        return Vec3(std::abs(s.x), std::abs(s.y), std::abs(s.z));
    };

    Vec3 scale = AbsScale(transform->scale);
    actor->ClearShapes();

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
        boxDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

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
        sphereDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

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
        capsuleDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

        actor->AddCapsuleShape(capsuleDesc, Vec3::Zero, Quat::Identity);
        break;
    }
    }

    IRigidBody* body = handle.GetRigidBody();
    if (body && body->IsValid())
    {
        body->RecomputeMass();
    }
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

    if (actor && actor->IsValid())
        actor->SetTransform(pos, rot);
}

void PhysicsSystem::SyncPhysicsToGame(const ActiveTransform& transform)
{
    if (!transform.userData) return;

    EntityId entityId = m_world.ExtractEntityIdFromUserData(transform.userData);
    if (entityId == InvalidEntityId) return;

    if (!IsTrackedEntity(entityId))
        return;

    auto* transformComp = m_world.GetComponent<TransformComponent>(entityId);
    if (!transformComp) return;

    transformComp->position = ToXMFLOAT3(transform.position);
    DirectX::XMFLOAT3 euler = ToEulerRadians(transform.rotation);
    transformComp->rotation = euler;

    m_lastTransforms[entityId] = {
        transformComp->position,
        transformComp->rotation,
        transformComp->scale
    };
}

bool PhysicsSystem::IsTrackedEntity(Alice::EntityId id) const noexcept
{
    return (m_entityToActor.find(id) != m_entityToActor.end()) ||
           (m_entityToCCT.find(id) != m_entityToCCT.end());
}

Vec3 PhysicsSystem::ToVec3(const DirectX::XMFLOAT3& v)
{
    return Vec3(v.x, v.y, v.z);
}

Quat PhysicsSystem::ToQuat(const DirectX::XMFLOAT3& eulerRadians)
{
	return Quat::CreateFromYawPitchRoll(
		eulerRadians.y,
		eulerRadians.x,
		eulerRadians.z
	);
}

DirectX::XMFLOAT3 PhysicsSystem::ToXMFLOAT3(const Vec3& v)
{
    return DirectX::XMFLOAT3(v.x, v.y, v.z);
}

DirectX::XMFLOAT3 PhysicsSystem::ToEulerRadians(const Quat& q)
{
	const float x = q.x, y = q.y, z = q.z, w = q.w;
    
	float sinp = 2.0f * (w * x - y * z);
	float pitch = (std::abs(sinp) >= 1.0f)
		? std::copysign(DirectX::XM_PIDIV2, sinp)
		: std::asin(sinp);
    
	float siny_cosp = 2.0f * (w * y + x * z);
	float cosy_cosp = 1.0f - 2.0f * (x * x + y * y);
    float yaw = std::atan2(siny_cosp, cosy_cosp);
    
	float sinr_cosp = 2.0f * (w * z + x * y);
	float cosr_cosp = 1.0f - 2.0f * (x * x + z * z);
	float roll = std::atan2(sinr_cosp, cosr_cosp);

	return DirectX::XMFLOAT3(pitch, yaw, roll);
}

void PhysicsSystem::CreateCharacterController(EntityId entityId)
{
    if (!m_physicsWorld)
    {
        ALICE_LOG_ERRORF("[PhysicsSystem] CreateCharacterController: m_physicsWorld is null!");
        return;
    }
    if (!m_physicsWorld->SupportsCharacterControllers())
    {
        ALICE_LOG_ERRORF("[PhysicsSystem] CreateCharacterController: CharacterControllers not supported!");
        return;
    }

    auto* transform = m_world.GetComponent<TransformComponent>(entityId);
    auto* ccc = m_world.GetComponent<CharacterControllerComponent>(entityId);
    if (!transform || !ccc)
    {
        ALICE_LOG_ERRORF("[PhysicsSystem] CreateCharacterController: Transform or CCT component missing! (transform: %p, ccc: %p)",
            (void*)transform, (void*)ccc);
        return;
    }

    auto* rb = m_world.GetComponent<RigidBodyComponent>(entityId);
    auto* collider = m_world.GetComponent<ColliderComponent>(entityId);
    if (rb || collider)
    {
        ALICE_LOG_ERRORF("[PhysicsSystem] CreateCharacterController: Entity has RigidBody (%p) or Collider (%p), cannot create CCT!",
            (void*)rb, (void*)collider);
        return;
    }

    auto AbsScale = [](const DirectX::XMFLOAT3& s) -> Vec3 {
        return Vec3(std::abs(s.x), std::abs(s.y), std::abs(s.z));
    };
    const Vec3 s = AbsScale(transform->scale);
	const float radial = std::max(s.x, s.z);

    CharacterControllerDesc desc{};
	desc.type = CCTType::Capsule;

    desc.radius = ccc->radius * radial;
    desc.halfHeight = ccc->halfHeight * s.y;

	desc.stepOffset = ccc->stepOffset * s.y;
	desc.contactOffset = ccc->contactOffset * std::min(radial, s.y);

	float maxStep = 0.0f;
	if (desc.type == CCTType::Capsule)
	{
		maxStep = desc.halfHeight * 2.0f + desc.radius * 2.0f;
	}
	else
	{
		maxStep = desc.halfHeight * 2.0f;
	}

	desc.stepOffset = std::clamp(desc.stepOffset, 0.0f, maxStep);
	desc.contactOffset = std::max(desc.contactOffset, 0.001f);

    desc.slopeLimitRadians = ccc->slopeLimitRadians;
    desc.nonWalkableMode = ccc->nonWalkableMode;
    desc.climbingMode = ccc->climbingMode;
    desc.density = ccc->density;
    desc.enableQueries = ccc->enableQueries;

    desc.layerBits = ccc->layerBits;
    desc.collideMask = ccc->collideMask;
    desc.queryMask = ccc->queryMask;
    desc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);
    desc.footPosition = ToVec3(transform->position);
    desc.upDirection = Vec3::UnitY;

    auto ctrl = m_physicsWorld->CreateCharacterController(desc);
    if (!ctrl)
    {
        ALICE_LOG_ERRORF("[PhysicsSystem] CreateCharacterController: Failed to create CCT (CreateCharacterController returned null)!");
        return;
    }

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
