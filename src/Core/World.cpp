#include "Core/World.h"
#include "Core/GameObject.h"
#include "Core/ScriptFactory.h"
#include "Core/ThreadSafety.h"

namespace Alice {
	void World::Clear()
	{
		ThreadSafety::AssertMainThread();
		// 0. Clear 전 콜백 호출 (Engine에서 물리 시스템 정리)
		if (m_onBeforeClear)
		{
			m_onBeforeClear();
		}
		
		// 1. 스크립트 컴포넌트들의 정리(Cleanup) 함수 호출
		RemoveAllScript();
		// 2. 모든 컴포넌트 컨테이너 비우기 (메모리 해제)

		// 1.5 물리 월드 정리
		// (Clear 전 콜백에서 PhysicsSystem 정리가 이미 완료되었을 수 있음)
		m_physicsWorld.reset();

		// 2. 엔티티 이름 비우기
		m_names.clear();
		
		// 모든 엔진 컴포넌트 저장소 클리어
		for (auto& [typeIndex, storage] : m_engineStorages)
		{
			storage->Clear();
		}
		
		m_scripts.clear();
		m_delayedDestructions.clear();
		m_entityGenerations.clear();

		// 3. World Epoch 증가 (씬 전환 시 이전 userData 무효화)
		//    EntityId는 재사용하지 않고 단조 증가하여 안전성 보장
		//    worldEpoch를 증가시켜 이전 씬의 PhysX userData를 무효화
		++m_worldEpoch;

		// 주의: m_nextEntityId는 리셋하지 않음 (EntityId 재사용 방지)
		//       대신 worldEpoch를 증가시켜 userData 매칭 안전성 보장
	}

	EntityId World::ExtractEntityIdFromUserData(void* userData) const
	{
		if (!userData) return InvalidEntityId;
		
		const uint64_t combined = static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(userData));
		const uint64_t userDataEpoch = (combined >> 32) & 0xFFFFFFFFull;
		
		// worldEpoch 검증: 이전 씬의 userData는 무시
		if (userDataEpoch != m_worldEpoch)
		{
			return InvalidEntityId;
		}
		
		// +1 오프셋 제거 (인코딩 시 +1을 했으므로)
		const uint64_t encodedEntityId = combined & 0xFFFFFFFFull;
		if (encodedEntityId == 0) return InvalidEntityId; // 오프셋 후 0이면 원래 InvalidEntityId
		return static_cast<EntityId>(encodedEntityId - 1u);
	}
	EntityId World::CreateEntity()
	{
		// 간단한 증가형 ID를 사용합니다.
		const EntityId newId = m_nextEntityId++;
		// SlotMap: 새로 생성된 엔티티의 generation을 0으로 초기화합니다.
		m_entityGenerations[newId] = 0;
		return newId;
	}

	void World::DestroyEntity(EntityId id)
	{
		if (id == InvalidEntityId)
			return;

		// 지연 파괴 예약이 있으면 제거
		m_delayedDestructions.erase(id);

		// SlotMap: 엔티티가 파괴될 때 generation을 증가시켜 이전 참조를 무효화합니다.
		auto genIt = m_entityGenerations.find(id);
		if (genIt != m_entityGenerations.end())
		{
		    genIt->second++; // generation 증가
		}

		m_names.erase(id);
		
		// 모든 엔진 컴포넌트 저장소에서 해당 엔티티 제거
		for (auto& [typeIndex, storage] : m_engineStorages)
		{
			storage->Remove(id);
		}
		
		// 스크립트 제거
		auto it = m_scripts.find(id);
		if (it != m_scripts.end()) {
			for (auto& sc : it->second)
			{
				if (!sc.instance)
					continue;
				sc.instance->OnDisable();
				sc.instance->OnDestroy();
			}
			m_scripts.erase(it);
		}
	}

	GameObject World::FindGameObject(const std::string& name)
	{
		// 이름으로 엔티티 검색 (선형 검색)
		// 엔티티가 많아지면 별도 Map<String, EntityId> 관리 권장
		for (const auto& [id, entityName] : m_names)
		{
			if (entityName == name)
			{
				// GameObject 생성 (ScriptServices는 nullptr로 전달)
				// 스크립트에서 사용할 때는 IScript::gameObject()를 통해 ScriptServices가 포함된 GameObject를 얻을 수 있음
				return GameObject(this, id, nullptr);
			}
		}
		// 찾지 못한 경우 빈 GameObject 반환 (IsValid() == false)
		return GameObject();
	}

	void World::SetEntityName(EntityId id, const std::string& name)
	{
		if (id == InvalidEntityId)
			return;
		if (name.empty()) {
			m_names.erase(id);
			return;
		}
		m_names[id] = name;
	}

	std::string World::GetEntityName(EntityId id) const
	{
		auto it = m_names.find(id);
		if (it == m_names.end())
			return {};
		return it->second;
	}

	ScriptComponent& World::AddScript(EntityId id, const std::string& scriptName)
	{
		ScriptComponent comp{};
		comp.scriptName = scriptName;
		comp.instance = ScriptFactory::Create(scriptName.c_str());
		if (comp.instance) comp.instance->SetContext(this, id);

		m_scripts[id].push_back(std::move(comp));
		return m_scripts[id].back();
	}

	std::vector<ScriptComponent>* World::GetScripts(EntityId id)
	{
		auto it = m_scripts.find(id);
		if (it == m_scripts.end())
			return nullptr;
		return &it->second;
	}

	const std::vector<ScriptComponent>* World::GetScripts(EntityId id) const
	{
		auto it = m_scripts.find(id);
		if (it == m_scripts.end())
			return nullptr;
		return &it->second;
	}

	void World::RemoveScript(EntityId id, std::size_t index)
	{
		auto it = m_scripts.find(id);
		if (it == m_scripts.end())
			return;

		auto& list = it->second;
		if (index >= list.size())
			return;

		if (list[index].instance) {
			list[index].instance->OnDisable();
			list[index].instance->OnDestroy();
		}

		list.erase(list.begin() + (std::ptrdiff_t)index);
		if (list.empty())
			m_scripts.erase(it);
	}

	void World::RemoveAllScript() {
		for (auto& [id, list] : m_scripts) {
			for (auto& scriptComp : list) {
				if (!scriptComp.instance)
					continue;
				scriptComp.instance->OnDisable();
				scriptComp.instance->OnDestroy();
			}
		}
		m_scripts.clear();
	}

	EntityId World::GetMainCameraEntityId() {
		// Sparse Set 기반: 카메라 컴포넌트 뷰에서 첫 번째 EntityId를 반환
		auto cameras = GetComponents<CameraComponent>();
		if (cameras.empty())
			return InvalidEntityId;
		return cameras.begin()->first;
	}

	void World::ScheduleDelayedDestruction(EntityId id, float delay)
	{
		if (id == InvalidEntityId || delay <= 0.0f)
			return;

		// 이미 예약된 파괴가 있으면 더 짧은 시간으로 업데이트
		auto it = m_delayedDestructions.find(id);
		if (it != m_delayedDestructions.end())
		{
			it->second = std::min(it->second, delay);
		}
		else
		{
			m_delayedDestructions[id] = delay;
		}
	}

	void World::UpdateDelayedDestruction(float deltaTime)
	{
		// 역순으로 순회하여 삭제 시 iterator 무효화 방지
		std::vector<EntityId> toDestroy;
		toDestroy.reserve(m_delayedDestructions.size());

		for (auto& [id, remainingTime] : m_delayedDestructions)
		{
			remainingTime -= deltaTime;
			if (remainingTime <= 0.0f)
			{
				toDestroy.push_back(id);
			}
		}

		// 시간이 지난 엔티티들을 파괴
		for (EntityId id : toDestroy)
		{
			m_delayedDestructions.erase(id);
			DestroyEntity(id);
		}
	}

	std::uint32_t World::GetEntityGeneration(EntityId id) const
	{
		auto it = m_entityGenerations.find(id);
		if (it == m_entityGenerations.end())
    		return 0; // 존재하지 않는 엔티티는 generation 0
		return it->second;
	}

	bool World::IsEntityValid(EntityId id, std::uint32_t generation) const
	{
		if (id == InvalidEntityId)
			return false;

		auto it = m_entityGenerations.find(id);
		if (it == m_entityGenerations.end())
    		return false; // 엔티티가 존재하지 않음

		// generation이 일치하면 유효, 다르면 무효 (파괴 후 재사용된 경우)
		return it->second == generation;
	}

	EntityId World::CreateEmpty()
	{
		EntityId e = CreateEntity();
		auto& t = AddComponent<TransformComponent>(e);
		t.SetPosition(0.0f, 0.0f, 0.0f)
			.SetScale(1.0f, 1.0f, 1.0f);
		SetEntityName(e, "GameObject" + std::to_string((std::uint32_t)e));
		return e;
	}

	EntityId World::CreateCube()
	{
		EntityId e = CreateEntity();
		auto& t = AddComponent<TransformComponent>(e);
		t.SetPosition(0.0f, 0.0f, 0.0f)
		 .SetScale(1.0f, 1.0f, 1.0f);
		
		// 기본 회색 머티리얼을 함께 추가합니다.
		DirectX::XMFLOAT3 defaultColor(0.7f, 0.7f, 0.7f);
		AddComponent<MaterialComponent>(e, defaultColor);

		SetEntityName(e, "Entity" + std::to_string((std::uint32_t)e));
		return e;
	}

	EntityId World::CreateCamera()
	{
		const bool hasCamera = GetComponents<CameraComponent>().empty();
		const std::uint32_t camIndex = static_cast<std::uint32_t>(GetComponents<CameraComponent>().size() + 1);

		EntityId e = CreateEntity();
		auto& t = AddComponent<TransformComponent>(e);
		t.position = { 0.0f, 2.0f, -5.0f };
		t.rotation = { 0.0f, 0.0f, 0.0f };
		t.scale = { 1.0f, 1.0f, 1.0f };

		auto& c = AddComponent<CameraComponent>(e);
		c.primary = !hasCamera;

		SetEntityName(e, "Camera" + std::to_string(camIndex));
		return e;
	}

	EntityId World::CreatePointLight()
	{
		EntityId e = CreateEntity();
		AddComponent<TransformComponent>(e);
		AddComponent<PointLightComponent>(e);
		SetEntityName(e, "Point Light");
		return e;
	}

	EntityId World::CreateSpotLight()
	{
		EntityId e = CreateEntity();
		AddComponent<TransformComponent>(e);
		AddComponent<SpotLightComponent>(e);
		SetEntityName(e, "Spot Light");
		return e;
	}

	EntityId World::CreateRectLight()
	{
		EntityId e = CreateEntity();
		AddComponent<TransformComponent>(e);
		AddComponent<RectLightComponent>(e);
		SetEntityName(e, "Rect Light");
		return e;
	}




	//========================================================
	// ���� ���� �Լ�
	void World::SetPhysicsWorld(std::shared_ptr<IPhysicsWorld> physicsWorld) { m_physicsWorld = std::move(physicsWorld); }
	IPhysicsWorld* World::GetPhysicsWorld() { return m_physicsWorld.get(); }
	const IPhysicsWorld* World::GetPhysicsWorld() const { return m_physicsWorld.get(); }
	//========================================================


} // namespace Alice
