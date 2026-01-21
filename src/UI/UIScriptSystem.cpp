#include "UIScriptSystem.h"

#include "IUIScript.h"
#include "UI_ScriptComponent.h"
#include "UISceneManager.h"
#include "UIBase.h"

UIScriptFactory UIScriptSystem::s_factory = nullptr;

void UIScriptSystem::SetFactory(UIScriptFactory factory)
{
	s_factory = std::move(factory);
}

void UIScriptSystem::Tick(UIWorld& world, float dt)
{
	TickRoot(world, dt);
}

void UIScriptSystem::TickRoot(UIWorld& world, float dt)
{
	for (auto rootID : world.GetRootIDs())
	{
		if (auto* root = world.Get(rootID))
		{
			TickNode(world, root, dt);
		}
	}
}

void UIScriptSystem::TickNode(UIWorld& world, UIBase* node, float dt)
{
	if (auto* comp = node->TryGetComponent<UI_ScriptComponent>())
	{
		TickComponent(*comp, dt);
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
		return;

	if (!comp.instance)
	{
		EnsureInstance(comp);
	}

	auto* inst = comp.instance.get();
	if (!inst)
		return;

	// Awake 역할: OnAdded가 아직 호출되지 않았다면 호출
	if (!comp.awoken)
	{
		comp.awoken = true;
		if (comp.Owner)
		{
			inst->OnAdded(*comp.Owner);
		}
	}

	// Start 역할
	if (!comp.started)
	{
		comp.started = true;
		inst->OnStart();
	}

	inst->Update(dt);
}

void UIScriptSystem::EnsureInstance(UI_ScriptComponent& comp)
{
	if (comp.instance || comp.scriptName.empty() || !s_factory)
		return;

	comp.instance = s_factory(comp.scriptName);
	if (comp.instance)
	{
		comp.instance->Owner = comp.Owner;
		comp.instance->OwnerID = comp.OwnerID;
	}
}
