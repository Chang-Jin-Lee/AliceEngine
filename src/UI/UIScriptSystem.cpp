#include "UIScriptSystem.h"

#include "IUIScript.h"
#include "UI_ScriptComponent.h"
#include "UISceneManager.h"
#include "UIBase.h"
#include "UIScriptFactory.h"
#include "Core/Logger.h"

DynamicUIScriptFactory UIScriptSystem::s_factory = nullptr;
DynamicUIScriptCountFunc UIScriptSystem::s_dynCount = nullptr;
DynamicUIScriptGetNameFunc UIScriptSystem::s_dynGetName = nullptr;

void UIScriptSystem::SetFactory(DynamicUIScriptFactory factory)
{
	s_factory = std::move(factory);
}

void UIScriptSystem::SetDynamicUIScriptFunctions(DynamicUIScriptCountFunc countFn, DynamicUIScriptGetNameFunc getNameFn)
{
	s_dynCount = countFn;
	s_dynGetName = getNameFn;
}

std::vector<std::string> UIScriptSystem::GetRegisteredUIScriptNames()
{
	std::vector<std::string> result;

	// 1) 정적(내장) UI 스크립트들
	auto staticNames = UIScriptFactory::GetRegisteredUIScriptNames();
	result.insert(result.end(), staticNames.begin(), staticNames.end());

	// 2) 동적 UI 스크립트 DLL이 제공하는 스크립트들
	if (s_dynCount && s_dynGetName)
	{
		const int count = s_dynCount();
		for (int i = 0; i < count; ++i)
		{
			char buffer[128] = {};
			if (s_dynGetName(i, buffer, static_cast<int>(sizeof(buffer))))
			{
				result.emplace_back(buffer);
			}
		}
	}

	return result;
}

void UIScriptSystem::Tick(UIWorld& world, float dt)
{
	//ALICE_LOG_INFO("[UIScriptSystem::Tick] Called (dt=%.3f)", dt);
	TickRoot(world, dt);
}

void UIScriptSystem::TickRoot(UIWorld& world, float dt)
{
	auto rootIDs = world.GetRootIDs();
	//ALICE_LOG_INFO("[UIScriptSystem::TickRoot] Root count: %zu", rootIDs.size());
	for (auto rootID : rootIDs)
	{
		if (auto* root = world.Get(rootID))
		{
			//ALICE_LOG_INFO("[UIScriptSystem::TickRoot] Processing root ID: %lu", rootID);
			TickNode(world, root, dt);
		}
	}
}

void UIScriptSystem::TickNode(UIWorld& world, UIBase* node, float dt)
{
	if (!node)
	{
		//ALICE_LOG_WARN("[UIScriptSystem::TickNode] node is null!");
		return;
	}

	if (auto* comp = node->TryGetComponent<UI_ScriptComponent>())
	{
		//ALICE_LOG_INFO("[UIScriptSystem::TickNode] Found UI_ScriptComponent: %s", comp->scriptName.c_str());
		TickComponent(*comp, dt);
	}
	else
	{
		//ALICE_LOG_INFO("[UIScriptSystem::TickNode] No UI_ScriptComponent found on node ID: %lu", node->ID);
	}

	for (auto childID : node->childIDStorage)
	{
		if (auto* child = world.Get(childID))
		{
			TickNode(world, child, dt);
		}
	}
}

void UIScriptSystem::TickComponent(UI_ScriptComponent& comp, float dt)
{
	if (!comp.enabled)
	{
		//ALICE_LOG_INFO("[UIScriptSystem::TickComponent] Script disabled: %s", comp.scriptName.c_str());
		return;
	}

	if (!comp.instance)
	{
		//ALICE_LOG_INFO("[UIScriptSystem::TickComponent] Instance is null, ensuring instance for: %s", comp.scriptName.c_str());
		EnsureInstance(comp);
	}

	auto* inst = comp.instance.get();
	if (!inst)
	{
		//ALICE_LOG_WARN("[UIScriptSystem::TickComponent] Instance is still null after EnsureInstance for: %s", comp.scriptName.c_str());
		return;
	}

	// Awake 역할: OnAdded가 아직 호출되지 않았다면 호출
	if (!comp.awoken)
	{
		comp.awoken = true;
		if (comp.Owner)
		{
			//ALICE_LOG_INFO("[UIScriptSystem::TickComponent] Calling OnAdded for: %s", comp.scriptName.c_str());
			inst->OnAdded(*comp.Owner);
		}
		/*else
		{
			ALICE_LOG_WARN("[UIScriptSystem::TickComponent] Owner is null for: %s", comp.scriptName.c_str());
		}*/
	}

	// Start 역할
	if (!comp.started)
	{
		comp.started = true;
		//ALICE_LOG_INFO("[UIScriptSystem::TickComponent] Calling OnStart for: %s", comp.scriptName.c_str());
		inst->OnStart();
	}

	inst->Update(dt);
}

void UIScriptSystem::EnsureInstance(UI_ScriptComponent& comp)
{
	if (comp.instance || comp.scriptName.empty())
	{
		if (comp.scriptName.empty())
		{
			//ALICE_LOG_WARN("[UIScriptSystem::EnsureInstance] scriptName is empty!");
			//ALICE_LOG_INFO("Registered count=%d", (int)comp.scriptName.size());
		}
		return;
	}

	// 등록된 스크립트 목록 확인
	auto registeredNames = UIScriptFactory::GetRegisteredUIScriptNames();

	// 1) 정적(내장) UI 스크립트 팩토리에서 먼저 찾기
	comp.instance = UIScriptFactory::Create(comp.scriptName.c_str());
	if (comp.instance)
	{
		comp.instance->Owner = comp.Owner;
		comp.instance->OwnerID = comp.OwnerID;
		//ALICE_LOG_INFO("[UIScriptSystem::EnsureInstance] Script created from UIScriptFactory: %s", comp.scriptName.c_str());
		return;
	}

	// 2) 동적 UI 스크립트 DLL 팩토리에서 찾기
	if (s_factory)
	{
		comp.instance = s_factory(comp.scriptName);
		if (comp.instance)
		{
			comp.instance->Owner = comp.Owner;
			comp.instance->OwnerID = comp.OwnerID;
			//ALICE_LOG_INFO("[UIScriptSystem::EnsureInstance] Script created from dynamic factory: %s", comp.scriptName.c_str());
		}
		//else
		//{
		//	ALICE_LOG_WARN("[UIScriptSystem::EnsureInstance] Dynamic factory returned nullptr for: %s", comp.scriptName.c_str());
		//}
	}
	else
	{
		ALICE_LOG_WARN("[UIScriptSystem::EnsureInstance] Dynamic factory is null!");
	}
}
