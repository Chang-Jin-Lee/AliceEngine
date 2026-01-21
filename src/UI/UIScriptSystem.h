#pragma once

#include <functional>
#include <memory>
#include <string>

class UIWorld;
class UIBase;
class UI_ScriptComponent;
class IUIScript;

/// UI 스크립트 생성 함수 타입
using UIScriptFactory = std::function<std::unique_ptr<IUIScript>(const std::string&)>;

/// UIWorld 내의 모든 UI_ScriptComponent를 갱신하는 시스템
class UIScriptSystem
{
public:
	/// 스크립트 생성 팩토리 등록 (동적 DLL 로더에서 주입 예상)
	static void SetFactory(UIScriptFactory factory);

	/// UIWorld 전체 Tick
	static void Tick(UIWorld& world, float dt);

private:
	static UIScriptFactory s_factory;

	static void TickRoot(UIWorld& world, float dt);
	static void TickNode(UIWorld& world, UIBase* node, float dt);
	static void TickComponent(UI_ScriptComponent& comp, float dt);
	static void EnsureInstance(UI_ScriptComponent& comp);
};
