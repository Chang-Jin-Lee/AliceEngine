#include "Core/World.h"
#include "Core/GameObject.h"

namespace Alice {
	void World::Clear()
	{
		// 1. 스크립트 컴포넌트들의 정리(Cleanup) 함수 호출
		RemoveAllScript();
		// 2. 모든 컴포넌트 컨테이너 비우기 (메모리 해제)
		m_names.clear();
		m_transforms.clear();
		m_scripts.clear();
		m_materials.clear();
		m_skinnedMeshes.clear();
		m_skinnedAnimations.clear();
		m_cameras.clear();

		// 3. 엔티티 ID 카운터 초기화 (선택 사항이지만 권장)
		//    새 씬을 로드할 때 ID가 1번부터 다시 시작하도록 함.
		m_nextEntityId = 1;
	}
	EntityId World::CreateEntity()
	{
		// 간단한 증가형 ID를 사용합니다.
		const EntityId newId = m_nextEntityId++;
		return newId;
	}

	void World::DestroyEntity(EntityId id)
	{
		if (id == InvalidEntityId)
			return;

		m_names.erase(id);
		m_transforms.erase(id);
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
		m_materials.erase(id);
		m_skinnedMeshes.erase(id);
		m_skinnedAnimations.erase(id);
		m_cameras.erase(id);
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
		auto& list = m_scripts[id];
		ScriptComponent comp{};
		comp.scriptName = scriptName;
		comp.instance = ScriptFactory::Create(scriptName.c_str());
		if (comp.instance)
			comp.instance->SetContext(this, id);

		list.push_back(std::move(comp));
		return list.back();
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
		if (m_cameras.empty())
			return InvalidEntityId;
		return m_cameras.begin()->first;
	}
} // namespace Alice
