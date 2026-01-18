#include "PhysicsSystem.h"
#include "Core/World.h"
#include "Components/TransformComponent.h"
#include "Components/PhysicsSceneSettingsComponent.h"
#include "Core/Logger.h"
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

// 해시 결합 유틸리티
static uint64_t HashCombine64(uint64_t a, uint64_t b) noexcept
{
	// 매우 단순한 해시 결합 (충돌 가능성 낮추는 용도)
	a ^= b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2);
	return a;
}

// Terrain 형상 키 생성
static uint64_t MakeTerrainGeomKey(const TerrainHeightFieldComponent& t) noexcept
{
	uint64_t key = 0;
	key = HashCombine64(key, static_cast<uint64_t>(t.numRows));
	key = HashCombine64(key, static_cast<uint64_t>(t.numCols));
	
	// float를 비트로 변환하여 해시에 포함
	uint32_t heightScaleBits = 0;
	std::memcpy(&heightScaleBits, &t.heightScale, sizeof(float));
	key = HashCombine64(key, static_cast<uint64_t>(heightScaleBits));
	
	uint32_t rowScaleBits = 0;
	std::memcpy(&rowScaleBits, &t.rowScale, sizeof(float));
	key = HashCombine64(key, static_cast<uint64_t>(rowScaleBits));
	
	uint32_t colScaleBits = 0;
	std::memcpy(&colScaleBits, &t.colScale, sizeof(float));
	key = HashCombine64(key, static_cast<uint64_t>(colScaleBits));
	
	// heightSamples 크기로 변경 감지
	key = HashCombine64(key, static_cast<uint64_t>(t.heightSamples.size()));
	
	return key;
}

// MakeAllMaskArray 구현
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
	// physicsWorld가 nullptr이 되는 경우 (씬 전환/저장), 
	// PhysXWorld의 pending 작업을 먼저 Flush하여 안전하게 정리
	if (physicsWorld == nullptr && m_physicsWorld != nullptr)
	{
		// Flush를 통해 pending release/add/remove 작업을 즉시 처리
		// 주의: Flush는 IPhysicsWorld 인터페이스에 없으므로, 
		// World의 shared_ptr을 통해 Flush해야 함
		// 하지만 여기서는 직접 접근할 수 없으므로, 
		// DestroyPhysicsActor에서 안전하게 처리하도록 함
	}

    // 기존 액터들 정리 (컴포넌트 핸들도 함께 정리)
	// 씬 전환 중일 수 있으므로 컴포넌트 접근 시 null 체크 필수
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

	// CCT 정리 (씬 전환 중일 수 있으므로 안전하게 처리)
    for (auto& [entityId, handle] : m_entityToCCT)
	{
		if (handle.IsValid())
    {
        handle.Destroy();
		}
    }
    m_entityToCCT.clear();
    m_lastCCTs.clear();

    m_physicsWorld = physicsWorld;
	m_lastFilterRevision = 0xFFFFFFFFu; // 강제로 다음 Update에서 1회 갱신

}

void PhysicsSystem::SetEventCallback(EventCallback callback, void* userData)
{
    m_eventCallback = callback;
    m_eventCallbackUserData = userData;
}

void PhysicsSystem::Update(float deltaTime)
{
    IPhysicsWorld* current = m_world.GetPhysicsWorld();
    if (current != m_physicsWorld) {
        SetPhysicsWorld(current); // 바뀌었으면 정리+재바인딩
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

					// ignoreLayers 반영: 이그노어 레이어는 마스크에서 제외
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

					// 마스크 변경 시 SetLayerMasks로 필터만 업데이트 (전체 리빌드 대신)
					if (maskChanged)
					{
						auto it = m_entityToActor.find(id);
						if (it != m_entityToActor.end())
						{
							ActorHandle& handle = it->second;
							if (handle.IsValid() && handle.GetActor())
							{
								// 모든 shape에 필터 업데이트 (SetLayerMasks 사용)
								// Shape가 여러 개일 수 있으므로 각 shape별로 업데이트 필요
								// 일단 RebuildShapes 대신 SetLayerMasks 시도 (만약 shape가 1개면 가능)
								auto* collider = m_world.GetComponent<ColliderComponent>(id);
								if (collider)
								{
													// IPhysicsActor는 SetLayerMasks가 있으므로 마스크만 변경 시 SetLayerMasks 사용
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

					// ignoreLayers 반영: 이그노어 레이어는 마스크에서 제외
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

					// ignoreLayers 반영: 이그노어 레이어는 마스크에서 제외
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

					// CCT 필터 변경 시 SetLayerMasks 사용 (재생성 대신)
					if (maskChanged)
					{
						auto itCCT = m_entityToCCT.find(id);
						if (itCCT != m_entityToCCT.end() && itCCT->second.IsValid())
						{
							// CCT는 SetLayerMasks가 있으므로 재생성 없이 필터만 업데이트
							itCCT->second.cct->SetLayerMasks(cct.layerBits, cct.collideMask, cct.queryMask);
        }
    }
				}
			}
		}

	}

	//
	// (A) 이전 시뮬 결과 반영: ActiveTransform → TransformComponent
	// 주의: Engine::TickPhysics()에서 DrainActiveTransforms를 호출하므로 여기서는 제거
	// PhysicsSystem은 SyncPhysicsToGame() 함수만 제공하고, Drain은 Engine에서 전담

	// (B) 이벤트 라우팅: Trigger/Contact/JointBreak
	// 주의: Engine::TickPhysics()에서 DrainEvents를 호출하므로 여기서는 제거
	// PhysicsSystem은 콜백 함수만 제공하고, Drain은 Engine에서 전담

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
				auto* col = m_world.GetComponent<ColliderComponent>(entityId);
				if (rb && rb->physicsActorHandle && col && col->physicsActorHandle == nullptr)
                {
					col->physicsActorHandle = rb->physicsActorHandle;
                    RebuildShapes(entityId); // 초기 shape 생성
                }
            }
        }

		// TerrainHeightFieldComponent는 (5) Terrain 변경 감지 섹션에서만 생성/갱신 처리
		// (중복 생성 방지 및 생성 루트 통일)

        // CCT 초기 생성 (변경 감지 부분에서도 처리하지만, 여기서도 빠르게 처리)
        // 변경 감지 부분이 나중에 실행되므로 여기서 먼저 생성 시도
        {
            auto ccts = m_world.GetComponents<CharacterControllerComponent>();
            for (const auto& [entityId, cct] : ccts)
            {
                auto* ccc = m_world.GetComponent<CharacterControllerComponent>(entityId);
                if (!ccc) continue;

                auto itCCT = m_entityToCCT.find(entityId);
                // CCT가 없거나 핸들이 null이면 생성
                // cct는 const 참조이므로 ccc 포인터로 실제 값을 확인
                if ((itCCT == m_entityToCCT.end() || !itCCT->second.IsValid()) || ccc->controllerHandle == nullptr)
                {
                    CreateCharacterController(entityId);
                }
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
				state.ignoreLayers = collider.ignoreLayers;
                state.isTrigger = collider.isTrigger;
                state.scale = transform->scale;
                m_lastColliders[entityId] = state;
            }
            else
            {
                // 변경 감지
                const auto& last = it->second;
                bool changed = false;
				bool maskOnlyChanged = false;

				// layerBits 또는 ignoreLayers 변경 시 마스크 재계산
				bool layerOrIgnoreChanged = (collider.layerBits != last.layerBits || collider.ignoreLayers != last.ignoreLayers);
				if (layerOrIgnoreChanged)
				{
					// 전역 매트릭스에서 마스크 재계산
					int li = FirstLayerIndex(collider.layerBits);
					if (li >= 0 && li < MAX_PHYSICS_LAYERS)
					{
						uint32_t newCollide = collideByLayer[li];
						uint32_t newQuery = queryByLayer[li];
						
						// ignoreLayers 반영
						newCollide &= ~collider.ignoreLayers;
						newQuery &= ~collider.ignoreLayers;
						
						// 마스크 업데이트
						collider.collideMask = newCollide;
						collider.queryMask = newQuery;
						maskOnlyChanged = true;
					}
				}

				// Collider 파라미터 변경 (형상 관련)
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

				// 마스크 변경 (다른 경로에서 변경된 경우)
				if (collider.collideMask != last.collideMask || collider.queryMask != last.queryMask)
				{
					if (!maskOnlyChanged) maskOnlyChanged = true; // 이미 처리했으면 중복 방지
				}

                // Scale 변경
                if (transform->scale.x != last.scale.x || 
                    transform->scale.y != last.scale.y || 
                    transform->scale.z != last.scale.z)
                {
                    changed = true;
                }

				// 마스크만 변경 시 SetLayerMasks 사용
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

	// 5. Terrain 변경 감지 및 재생성/마스크 업데이트
    {
        auto terrains = m_world.GetComponents<TerrainHeightFieldComponent>();
        for (const auto& [entityId, terrain] : terrains)
        {
            auto* transform = m_world.GetComponent<TransformComponent>(entityId);
            if (!transform) continue;

			// 레이어 인덱스 계산
			int li = FirstLayerIndex(terrain.layerBits);
			if (li < 0 || li >= MAX_PHYSICS_LAYERS) continue;

			// 마스크 계산 (전역 매트릭스 기반)
			uint32_t newCollide = collideByLayer[li];
			uint32_t newQuery = queryByLayer[li];
			
			// ignoreLayers 반영
			newCollide &= ~terrain.ignoreLayers;
			newQuery &= ~terrain.ignoreLayers;
			
			// 마스크 업데이트
			terrain.collideMask = newCollide;
			terrain.queryMask = newQuery;

			// 형상 키 계산
			const uint64_t geomKey = MakeTerrainGeomKey(terrain);

			// 상태 확인/초기화
            auto it = m_lastTerrains.find(entityId);
            if (it == m_lastTerrains.end())
            {
                // 첫 등록 시: 높이 데이터가 있으면 생성 시도
				// heightSamples가 비어있고 numRows/numCols가 유효하면 자동으로 플랫 지형 생성
				if (terrain.heightSamples.empty() && terrain.numRows >= 2 && terrain.numCols >= 2)
				{
					const size_t expectedSamples = static_cast<size_t>(terrain.numRows) * static_cast<size_t>(terrain.numCols);
					terrain.heightSamples.resize(expectedSamples, 0.0f);
				}

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

			TerrainState& state = it->second;
			
			// 형상 변경 감지
			const bool geomChanged = (geomKey != state.lastGeomKey);
			
			// 마스크 변경 감지
			const bool maskChanged = 
				(terrain.layerBits != state.layerBits) ||
				(terrain.ignoreLayers != state.ignoreLayers) ||
				(newCollide != state.collideMask) ||
				(newQuery != state.queryMask);

			if (geomChanged)
			{
				// 형상 변경 시에만 재생성
				if (terrain.physicsActorHandle != nullptr)
            {
					DestroyPhysicsActor(entityId);
				}
				
                if (!terrain.heightSamples.empty() && terrain.numRows >= 2 && terrain.numCols >= 2)
                {
					CreateTerrainHeightField(entityId);
				}
				
				state.lastGeomKey = geomKey;
			}
			else if (maskChanged)
			{
				// 마스크만 변경 시 SetLayerMasks 사용 (재생성 금지)
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

			// 상태 업데이트
			state.layerBits = terrain.layerBits;
			state.ignoreLayers = terrain.ignoreLayers;
			state.collideMask = newCollide;
			state.queryMask = newQuery;
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

    // 5. CCT 변경 감지 및 재생성/업데이트
    {
        auto ccts = m_world.GetComponents<CharacterControllerComponent>();
        
        for (const auto& [entityId, ccc] : ccts)
        {
            auto* transform = m_world.GetComponent<TransformComponent>(entityId);
            if (!transform) continue;

            auto itCCT = m_entityToCCT.find(entityId);
            
            // CCTState 생성 및 변경 감지
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
                // 첫 등록
                m_lastCCTs[entityId] = cur;
                // CCT가 없으면 생성
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
                // 변경 감지
                const auto& prev = itState->second;
                bool needsRebuild = false;

				// layerBits 또는 ignoreLayers 변경 시 마스크 재계산
				bool layerOrIgnoreChanged = (ccc.layerBits != prev.layerBits || ccc.ignoreLayers != prev.ignoreLayers);
				if (layerOrIgnoreChanged)
				{
					// 전역 매트릭스에서 마스크 재계산
					int li = FirstLayerIndex(ccc.layerBits);
					if (li >= 0 && li < MAX_PHYSICS_LAYERS)
					{
						uint32_t newCollide = collideByLayer[li];
						uint32_t newQuery = queryByLayer[li];
						
						// ignoreLayers 반영
						newCollide &= ~ccc.ignoreLayers;
						newQuery &= ~ccc.ignoreLayers;
						
						// 마스크 업데이트
						ccc.collideMask = newCollide;
						ccc.queryMask = newQuery;
						cur.collideMask = newCollide;
						cur.queryMask = newQuery;
					}
				}
                
                // 생성 파라미터 변경 확인 (재생성 필요)
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

                // CCT가 없거나 유효하지 않으면 생성
                if (itCCT == m_entityToCCT.end() || !itCCT->second.IsValid() || ccc.controllerHandle == nullptr)
                {
                    CreateCharacterController(entityId);
                    m_lastCCTs[entityId] = cur;
                }
                else if (needsRebuild)
                {
                    // 생성 파라미터 변경 시 재생성
                    DestroyCharacterController(entityId);
                    CreateCharacterController(entityId);
                    m_lastCCTs[entityId] = cur;
                }
                else
                {
                    // 레이어 마스크만 변경된 경우 업데이트
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

        // 제거된 CCT의 상태도 정리
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

    // 6. CCT 이동 + 중력/점프 처리 + Transform 갱신
    {        
        auto ccts = m_world.GetComponents<CharacterControllerComponent>();

        for (const auto& [entityId, ccc] : ccts)
        {
            auto* transform = m_world.GetComponent<TransformComponent>(entityId);
            if (!transform) continue;

            auto it = m_entityToCCT.find(entityId);
            if (it == m_entityToCCT.end() || !it->second.IsValid())
            {
                // CCT가 생성되지 않은 경우 경고 (첫 프레임이 아닐 때만)
                static std::unordered_set<EntityId> warnedEntities;
                if (warnedEntities.find(entityId) == warnedEntities.end())
                {
                    ALICE_LOG_WARN("[PhysicsSystem] CCT not found for entity %llu (controllerHandle: %p). Check if CreateCharacterController succeeded.",
                        (unsigned long long)entityId, ccc.controllerHandle);
                    
                    // 디버깅 정보 출력
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

            // 현재 지면 상태(점프/중력에 필요)
			// queryMask는 "누가 나를 쿼리할 수 있는가"가 아니라 "내가 무엇을 쿼리할 수 있는가"이므로 layerBits 사용
			CharacterControllerState st0 = ctrl->GetState(ccc.collideMask, ccc.layerBits, 0.2f, ccc.hitTriggers);
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

			// queryMask는 "누가 나를 쿼리할 수 있는가"가 아니라 "내가 무엇을 쿼리할 수 있는가"이므로 layerBits 사용
            CCTCollisionFlags cf = ctrl->Move(
                disp,
                deltaTime,
                ccc.collideMask,
				ccc.layerBits,
                ccc.hitTriggers);

            // 최종 상태 저장
			// queryMask는 "누가 나를 쿼리할 수 있는가"가 아니라 "내가 무엇을 쿼리할 수 있는가"이므로 layerBits 사용
			CharacterControllerState st = ctrl->GetState(ccc.collideMask, ccc.layerBits, 0.2f, ccc.hitTriggers);
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

	// 씬 전환 중일 수 있으므로, 컴포넌트 접근 전에 안전하게 핸들만 가져옴
	// 컴포넌트가 이미 제거되었을 수 있으므로 null 체크는 필수
    auto* rb = m_world.GetComponent<RigidBodyComponent>(entityId);
    if (rb) rb->physicsActorHandle = nullptr;

    auto* collider = m_world.GetComponent<ColliderComponent>(entityId);
    if (collider) collider->physicsActorHandle = nullptr;

    auto* terrain = m_world.GetComponent<TerrainHeightFieldComponent>(entityId);
    if (terrain) terrain->physicsActorHandle = nullptr;

	// ActorHandle의 Destroy 호출
	// Destroy()는 내부에서 world.lock()으로 안전하게 체크하므로,
	// PhysXWorld가 이미 파괴되었어도 문제없음
	// world.lock()이 nullptr을 반환하면 EnqueueRelease가 호출되지 않지만,
	// owned->Destroy()가 호출되어 래퍼 객체는 해제됨
	ActorHandle& handle = it->second;
	// Destroy()는 내부에서 owned가 nullptr인지 체크하고,
	// PhysXActor::Destroy()는 world.lock()으로 world 유효성을 체크하므로 안전
	handle.Destroy();
	
	// erase 호출 (it이 여전히 유효함, Destroy는 owned만 해제하고 it 자체는 안전)
    m_entityToActor.erase(it);
	
	// 상태 정리 (컴포넌트가 없어도 erase는 안전함)
    m_lastTransforms.erase(entityId);
    m_lastColliders.erase(entityId);
    m_lastRigidBodies.erase(entityId);
    m_lastTerrains.erase(entityId);
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

    // HeightField 데이터 검증
	const size_t expectedSamples = static_cast<size_t>(terrain->numRows) * static_cast<size_t>(terrain->numCols);
    if (terrain->heightSamples.empty() || terrain->numRows < 2 || terrain->numCols < 2)
    {
		ALICE_LOG_WARN("[PhysicsSystem] CreateTerrainHeightField: Invalid terrain data (entity: %llu, rows: %u, cols: %u, samples: %zu, expected: %zu)!",
			(unsigned long long)entityId, terrain->numRows, terrain->numCols, terrain->heightSamples.size(), expectedSamples);
        return; // 유효하지 않은 데이터
    }

	// heightSamples 크기 검증 추가 (empty()만 체크하는 것보다 안전)
	if (terrain->heightSamples.size() != expectedSamples)
	{
		ALICE_LOG_WARN("[PhysicsSystem] CreateTerrainHeightField: HeightSamples size mismatch (entity: %llu, rows: %u, cols: %u, samples: %zu, expected: %zu)!",
			(unsigned long long)entityId, terrain->numRows, terrain->numCols, terrain->heightSamples.size(), expectedSamples);
		return; // 크기 불일치
	}

    if (terrain->heightScale <= 0.0f || terrain->rowScale <= 0.0f || terrain->colScale <= 0.0f)
    {
        ALICE_LOG_WARN("[PhysicsSystem] CreateTerrainHeightField: Invalid terrain scale (entity: %llu, heightScale: %.2f, rowScale: %.2f, colScale: %.2f)!",
            (unsigned long long)entityId, terrain->heightScale, terrain->rowScale, terrain->colScale);
        return; // 유효하지 않은 스케일
    }

	// Create는 '진짜 생성만' 한다. 기존 정리는 호출자가 해라.
	auto itActor = m_entityToActor.find(entityId);
	if (itActor != m_entityToActor.end())
	{
		ALICE_LOG_ERRORF("[PhysicsSystem] CreateTerrainHeightField called but actor already exists (entity=%llu)",
			(unsigned long long)entityId);
		return;
	}

	// 컴포넌트 핸들이 남아있으면 (정합성 깨짐) 일단 복구
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

    // HeightFieldColliderDesc 구성
    HeightFieldColliderDesc hfDesc{};
    hfDesc.heightSamples = terrain->heightSamples.data();
    hfDesc.numRows = terrain->numRows;
    hfDesc.numCols = terrain->numCols;

    // Transform scale 반영: X=col, Z=row, Y=height
	hfDesc.colScale = terrain->colScale * s.x;
	hfDesc.rowScale = terrain->rowScale * s.z;
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
	// Transform.rotation은 (x, y, z) = (Pitch, Yaw, Roll) 순서
	// CreateFromYawPitchRoll(yaw, pitch, roll) 순서로 변환
	return Quat::CreateFromYawPitchRoll(
		eulerRadians.y, // yaw around Y
		eulerRadians.x, // pitch around X
		eulerRadians.z  // roll around Z
	);
}

DirectX::XMFLOAT3 PhysicsSystem::ToXMFLOAT3(const Vec3& v)
{
    return DirectX::XMFLOAT3(v.x, v.y, v.z);
}

DirectX::XMFLOAT3 PhysicsSystem::ToEulerRadians(const Quat& q)
{
    // Quaternion → Euler (라디안)
	// CreateFromYawPitchRoll로 변환된 쿼터니언을 다시 Euler로 변환
	// Transform.rotation 순서: (x, y, z) = (Pitch, Yaw, Roll)
	const float x = q.x, y = q.y, z = q.z, w = q.w;
    
	// pitch (X)
	float sinp = 2.0f * (w * x - y * z);
	float pitch = (std::abs(sinp) >= 1.0f)
		? std::copysign(DirectX::XM_PIDIV2, sinp)
		: std::asin(sinp);
    
	// yaw (Y)
	float siny_cosp = 2.0f * (w * y + x * z);
	float cosy_cosp = 1.0f - 2.0f * (x * x + y * y);
    float yaw = std::atan2(siny_cosp, cosy_cosp);
    
	// roll (Z)
	float sinr_cosp = 2.0f * (w * z + x * y);
	float cosr_cosp = 1.0f - 2.0f * (x * x + z * z);
	float roll = std::atan2(sinr_cosp, cosr_cosp);

	// Transform 순서로 반환: (Pitch, Yaw, Roll) = (x, y, z)
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

    // ⚠️ 같은 엔티티에 RB/Collider 같이 두지 마라. 충돌/동기화 싸움 난다.
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
	desc.type = CCTType::Capsule; // 현재는 Capsule만 지원 (나중에 Box 지원 시 ccc->type 사용)

    // Capsule 스케일 규칙: Y는 높이, X/Z는 반경(비균일이면 큰 축 선택)
    desc.radius = ccc->radius * radial;
    desc.halfHeight = ccc->halfHeight * s.y;

	// TODO: Box 타입 지원 시 아래 코드 활성화
	// if (ccc->type == CCTType::Box)
	// {
	//     Vec3 he = ToVec3(ccc->halfExtents);
	//     he.x *= s.x; he.y *= s.y; he.z *= s.z;
	//     desc.halfExtents = he;
	//     desc.halfHeight = he.y; // box isValid에서 stepOffset 비교에 씀
	// }

	// 1) stepOffset 스케일 반영(세로값이니 Y 기준)
	desc.stepOffset = ccc->stepOffset * s.y;

	// 2) contactOffset도 스케일 반영(너무 크면 이상해짐)
	desc.contactOffset = ccc->contactOffset * std::min(radial, s.y);

	// 3) PhysX isValid 통과용 클램프 (중요!)
	float maxStep = 0.0f;
	if (desc.type == CCTType::Capsule)
	{
		// PhysX: stepOffset <= height + 2*radius  (height=2*halfHeight)
		maxStep = desc.halfHeight * 2.0f + desc.radius * 2.0f;
	}
	else // Box
	{
		// PhysX Box: stepOffset <= 2*halfHeight
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
    desc.userData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(entityId));

    // foot 기준: Transform.position을 발 위치로 사용
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
