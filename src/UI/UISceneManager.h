#pragma once
#include <memory>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cassert>
#include <concepts>
#include <typeindex>
#include <typeinfo>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dwrite.h>
//#include "UIComponent/UITransformClass.h"
#include "IUIComponent.h"
#include "UIRenderStruct.h"
// Core delegate wrapper (BindLambda/Execute style)
#include "Core/Delegate.h"
#include "Core/InputSystem.h"
#include "UITransform.h"
#include "UIBase.h"
#include "UICompDelegate.h"
#include "UI_ImageComponent.h"
#include "UI_ScriptComponent.h"
#include "UIScriptSystem.h"


// !!!�߰� ����!!!
// ���Ŀ� ID ��� �ڵ�� �����ϱ�
// void SetParent()�� ����� ��쿡�� �θ��� rect�� �ڽı��� �����ϵ��� �����
// -> collider ���� rect�� ���� ������ �ҵ�?

class UIBase;

// ============================================================================
// UIWorld: ��ƼƼ�� ������Ʈ�� �����ϴ� ������ �����
// ============================================================================
class UIWorld
{
	friend class UISceneManager;
	friend class UILayoutSystem;
	friend class UIHitTestSystem;
	friend class UIEventSystem;
	friend class UIRenderSystem;
	friend class UIImageSystem;
	friend class UIScriptSystem;

public:
	~UIWorld()
	{
		Clear();
	}

private:
	// ��ƼƼ �����
	std::unordered_map<long unsigned, std::unique_ptr<UIBase>> pUIObjStorage; // UI Object ����
	std::vector<long unsigned> m_rootID; // �θ� ���� UI Object�� ID

	// ������Ʈ ����� (���ø� ���� ���� ����)
	//std::unordered_map<unsigned long, std::unique_ptr<UITextComponent>> m_compStorage;      // UITextComponent �����
	std::unordered_map<unsigned long, std::unique_ptr<UITransform>> m_transformStorage;     // UITransform �����
	std::unordered_map<unsigned long, std::unique_ptr<UI_ImageComponent>> m_imageComponentStorage; // UI_ImageComponent �����
	std::unordered_map<unsigned long, std::unique_ptr<UI_ScriptComponent>> m_scriptComponentStorage; // UI_ScriptComponent �����

	// ���� ���� ������Ʈ ������ ���� ��������Ʈ
	CompDelegates m_worldDelegates{};

	// World Epoch: ��ȿ�� üũ�� ���� ���� ��ȣ
	unsigned long long m_worldEpoch{ 1 };

	long unsigned nowInteger{ 1 };

	UIRenderStruct* m_UIRenderStruct{ nullptr };

public:
	// �ʱ�ȭ
	void Initialize(UIRenderStruct* UIRst);

	// ----- �ٽ� �������̽� (World ����) -----
	// ��ƼƼ ����
	template<typename T, typename... Args>
		requires std::derived_from<T, UIBase>
	T* CreateEntity(Args&&... args);

	// ��ƼƼ ���� + �θ� ����
	template<typename T, typename... Args>
		requires std::derived_from<T, UIBase>
	T* CreateChildEntity(long unsigned parentID, Args&&... args);

	// ��ƼƼ ���� (����Ʈ�� ��ü ���� ����)
	bool DestroyEntity(long unsigned int handle);

	// ��ƼƼ ��ȸ
	UIBase* Get(long unsigned int handle);

	// ��ü ���� Ŭ����
	void Clear();

	// �� ���� UI�� Ŭ���� (���� ����)
	void ClearSceneUI() { Clear(); } // �ϴ� ��ü Ŭ����
	 
	// World Epoch ��ȸ (��ȿ�� üũ��)
	unsigned long long GetWorldEpoch() const { return m_worldEpoch; }

	// ��Ʈ ID ��� ��ȸ (�ý��ۿ�)
	const std::vector<long unsigned>& GetRootIDs() const { return m_rootID; }

	// �θ� ����
	template<typename T, typename K>
		requires std::derived_from<T, UIBase> || std::derived_from<K, UIBase>
	void SetParent(T* parentUI, K* childUI);

	// ----- ������Ʈ ���� �Լ� -----
	void BindWorldDelegates(const CompDelegates& d) { m_worldDelegates = d; }
	CompDelegates& GetDelegates() { return m_worldDelegates; }

	// UITextComponent ����/��ȸ/����
	//UITextComponent* CreateTextComponent(unsigned long ownerID);
	///UITextComponent* FindTextComponent(unsigned long ownerID);
	//void RemoveTextComponent(unsigned long ownerID);

	// UITransform ����/��ȸ/����
	UITransform* CreateTransformComponent(unsigned long ownerID);
	UITransform* FindTransformComponent(unsigned long ownerID);
	void RemoveTransformComponent(unsigned long ownerID);

	// UI_ImageComponent ����/��ȸ/����
	UI_ImageComponent* CreateImageComponent(unsigned long ownerID);
	UI_ImageComponent* FindImageComponent(unsigned long ownerID);
	void RemoveImageComponent(unsigned long ownerID);

	// UI_ScriptComponent 생성/조회/삭제
	UI_ScriptComponent* CreateScriptComponent(unsigned long ownerID);
	UI_ScriptComponent* FindScriptComponent(unsigned long ownerID);
	void RemoveScriptComponent(unsigned long ownerID);

	// ������Ʈ ���� ���ø� �Լ�
	template<class T, class... Args>
		requires std::derived_from<T, IUIComponent>
	T* CreateComponent(unsigned long ownerID, Args&&... args);

	//������Ʈ ��ȸ (���ø� wrapper)
	template<class T>
		requires std::derived_from<T, IUIComponent>
	T* TryGetComponent(unsigned long ownerID);

	template<class T>
		requires std::derived_from<T, IUIComponent>
	T& GetComponent(unsigned long ownerID);

	// ������Ʈ ����
	template<class T>
		requires std::derived_from<T, IUIComponent>
	void RemoveComponent(unsigned long ownerID);

	// ��� �Լ��� ���� UIBase�� �Լ��� ȣ���ϰ� ���� ��� ���
	template <typename Func, typename... Args>
	void Traverse(UIBase* node, Func action, Args... args)
	{
		if (!node) return;

		for (auto childID : node->childIDStorage)
		{
			if (auto* child = Get(childID))
			{
				action(child, args...);
				Traverse(child, action, args...);
			}
		}
	}

private:
	bool DeleteChildObjects(long unsigned int ID);
};

// ============================================================================
// UILayoutSystem: Ʈ��/Transform ���� �ý���
// ============================================================================
class UILayoutSystem
{
public:
	static void UpdateTransforms(UIWorld& world);
	static void UpdateUI(UIWorld& world);

private:
	static void UpdateTransformChild(UIWorld& world, UIBase* node, const D2D1::Matrix3x2F& parentWorld);
};

// ============================================================================
// UIHitTestSystem: ���콺 �˻� �ý���
// ============================================================================
class UIHitTestSystem
{
public:
	static long unsigned FindUIUnderPointer(UIWorld& world, XMFLOAT2 MousePos, UIRenderStruct* renderStruct);

private:
	static void AABBRoot(UIWorld& world, XMFLOAT2 MousePos, UIBase* node, std::vector<long unsigned>& IDStorage);
	static void UIRotRoot(UIWorld& world, XMFLOAT2 MousePos, UIBase* node, std::vector<long unsigned>& hitMouseID);
};

// ============================================================================
// UIEventSystem: �̺�Ʈ ó�� �ý��� (���콺 �Է� �� UI ����)
// ============================================================================
class UIEventSystem
{
public:
	static void UpdatePointer(UIWorld& world, Alice::InputSystem* inputSystem, UIRenderStruct* renderStruct);
};

// ============================================================================
// UIRenderSystem: ������ �ý���
// ============================================================================
class UIRenderSystem
{
public:
	static void Render(UIWorld& world, UIRenderStruct* renderStruct);

private:
	static void RenderRoot(UIWorld& world, UIRenderStruct* renderStruct);
	static void RenderRootChild(UIWorld& world, UIBase* node);
};

// ============================================================================
// UIImageSystem: UI_ImageComponent 업데이트/렌더링 시스템
// ============================================================================
class UIImageSystem
{
public:
	// 모든 UI_ImageComponent를 업데이트
	static void Update(UIWorld& world);

	// 모든 UI_ImageComponent를 렌더링
	static void Render(UIWorld& world, UIRenderStruct* renderStruct);

private:
	// 루트부터 시작하여 모든 엔티티의 ImageComponent 업데이트
	static void UpdateRoot(UIWorld& world);
	static void UpdateRootChild(UIWorld& world, UIBase* node);

	// 루트부터 시작하여 모든 엔티티의 ImageComponent 렌더링
	static void RenderRoot(UIWorld& world, UIRenderStruct* renderStruct);
	static void RenderRootChild(UIWorld& world, UIBase* node, UIRenderStruct* renderStruct);
};

// ============================================================================
// UISceneManager: UI ��/���̾� ��ȯ�� ��� (���丮 + ���� ����)
// ============================================================================

class UISceneManager
{
public:
	~UISceneManager()
	{
		// UIWorld�� �ڵ����� Clear() ȣ���
	}

private:
	ID3D11Device* pDev = nullptr;
	ID3D11DeviceContext* pDevCon = nullptr;
	Alice::InputSystem* m_InputSystem{ nullptr };
	UIRenderStruct* m_UIRenderStruct{ nullptr };

	// UIWorld �ν��Ͻ�
	UIWorld m_world;

	// �� ��ȯ ���� (���� Ȯ��)
	bool isLayerChange = false;
	void SortCanvas() {}; // ���� �߰��ϱ�!!!!

public:
	void initalize(ID3D11Device* Dev, ID3D11DeviceContext* DevCon, UIRenderStruct* UIRst, Alice::InputSystem* tmpSystem);
	void Update();
	void Render();

	// UIWorld ����
	UIWorld& GetWorld() { return m_world; }
	const UIWorld& GetWorld() const { return m_world; }

	// ----- ���Ž� �������̽� (���� ȣȯ��) -----
	template<typename T, typename... Args>
		requires std::derived_from<T, UIBase>
	T* CreateUIObjects(Args&&... args) { return m_world.CreateEntity<T>(std::forward<Args>(args)...); }

	template<typename T, typename... Args>
		requires std::derived_from<T, UIBase>
	T* CreateChildUIObjects(long unsigned parentID, Args&&... args) { return m_world.CreateChildEntity<T>(parentID, std::forward<Args>(args)...); }

	UIBase* Get(long unsigned int ID) { return m_world.Get(ID); }
	bool DeleteUIObjects(long unsigned int ID) { return m_world.DestroyEntity(ID); }

	void BindWorldDelegates(const CompDelegates& d) { m_world.BindWorldDelegates(d); }

	template<class T, class... Args>
		requires std::derived_from<T, IUIComponent>
	T* CreateComponent(unsigned long ownerID, Args&&... args) { return m_world.CreateComponent<T>(ownerID, std::forward<Args>(args)...); }

	template<class T>
		requires std::derived_from<T, IUIComponent>
	T* TryGetComponent(unsigned long ownerID) { return m_world.TryGetComponent<T>(ownerID); }

	template<class T>
		requires std::derived_from<T, IUIComponent>
	T& GetComponent(unsigned long ownerID) { return m_world.GetComponent<T>(ownerID); }

	template<class T>
		requires std::derived_from<T, IUIComponent>
	void RemoveComponent(unsigned long ownerID) { m_world.RemoveComponent<T>(ownerID); }
};

// ============================================================================
// UIWorld ���ø� ����
// ============================================================================
template<typename T, typename... Args>
	requires std::derived_from<T, UIBase>
T* UIWorld::CreateEntity(Args&&... args)
{
	assert(m_UIRenderStruct && "UIWorld::Initialize() must be called before CreateEntity");

	auto pUIObj = std::unique_ptr<T>(new T(std::forward<Args>(args)...));
	T* ObjPtr = pUIObj.get();
	ObjPtr->SetID(this->nowInteger);

	pUIObjStorage.emplace(this->nowInteger, std::move(pUIObj));
	m_rootID.push_back(this->nowInteger); // ��Ʈ ���
	this->nowInteger++;

	ObjPtr->Initalize(*m_UIRenderStruct, m_worldDelegates);
	return ObjPtr;
}

template<typename T, typename... Args>
	requires std::derived_from<T, UIBase>
T* UIWorld::CreateChildEntity(long unsigned parentID, Args&&... args)
{
	assert(m_UIRenderStruct && "UIWorld::Initialize() must be called before CreateChildEntity");

	auto pUIObj = std::unique_ptr<T>(new T(std::forward<Args>(args)...));
	pUIObj->ID = this->nowInteger++; // ID �Է� �Ŀ� ID++

	UIBase* parentNode = Get(parentID);
	assert(parentNode && "CreateChildEntity: parentNode must not be null");

	// �θ� ����
	parentNode->childIDStorage.push_back(pUIObj->ID);
	pUIObj->parentID = parentID;

	// child�� root�� ���� �ʵ��� Ȯ�� (�̹� parent�� �����Ƿ� root�� �ƴ�)
	// m_rootID���� ���� (Ȥ�� �� ��� ���)
	m_rootID.erase(std::remove(m_rootID.begin(), m_rootID.end(), pUIObj->ID), m_rootID.end());

	T* ObjPtr = pUIObj.get();
	pUIObjStorage.emplace(pUIObj->ID, std::move(pUIObj));

	// �ڽĵ� �ʱ�ȭ
	ObjPtr->Initalize(*m_UIRenderStruct, m_worldDelegates);

	return ObjPtr;
}

template<typename T, typename K>
	requires std::derived_from<T, UIBase> || std::derived_from<K, UIBase>
void UIWorld::SetParent(T* parentUI, K* childUI)
{
	assert(parentUI && childUI && "SetParent: parentUI and childUI must not be null");

	// ���� �θ� ������ ���� �θ��� children���� ����
	if (childUI->parentID != 0)
	{
		UIBase* oldParent = Get(childUI->parentID);
		if (oldParent)
		{
			auto& oldParentChildren = oldParent->childIDStorage;
			oldParentChildren.erase(
				std::remove(oldParentChildren.begin(), oldParentChildren.end(), childUI->ID),
				oldParentChildren.end()
			);
		}
	}

	// ���ο� �θ� ����
	parentUI->childIDStorage.push_back(childUI->ID);
	childUI->parentID = parentUI->ID;

	// m_rootID���� child ���� (���� �θ� �����Ƿ� root�� �ƴ�)
	m_rootID.erase(std::remove_if(m_rootID.begin(), m_rootID.end(),
		[&](long unsigned ID) {
			return ID == childUI->ID;
		}), m_rootID.end());
}

template<class T>
	requires std::derived_from<T, IUIComponent>
T* UIWorld::TryGetComponent(unsigned long ownerID)
{
	// if constexpr (std::is_same_v<T, UITextComponent>)
	// 	return FindTextComponent(ownerID);

	if constexpr (std::is_same_v<T, UITransform>)
		return FindTransformComponent(ownerID);

	if constexpr (std::is_same_v<T, UI_ImageComponent>)
		return FindImageComponent(ownerID);

	if constexpr (std::is_same_v<T, UI_ScriptComponent>)
		return FindScriptComponent(ownerID);

	// Delegates�� ���� ��ȸ
	if (!m_worldDelegates.FindComponent.IsBound()) return nullptr;
	void* raw = nullptr;
	m_worldDelegates.FindComponent.Execute(ownerID, typeid(T), &raw);
	return static_cast<T*>(raw);
}

template<class T>
	requires std::derived_from<T, IUIComponent>
T& UIWorld::GetComponent(unsigned long ownerID)
{
	T* p = TryGetComponent<T>(ownerID);
	assert(p && "Component not found");
	return *p;
}

template<class T>
	requires std::derived_from<T, IUIComponent>
void UIWorld::RemoveComponent(unsigned long ownerID)
{
	// if constexpr (std::is_same_v<T, UITextComponent>)
	// {
	// 	RemoveTextComponent(ownerID);
	// 	return;
	// }

	if constexpr (std::is_same_v<T, UITransform>)
	{
		RemoveTransformComponent(ownerID);
		return;
	}

	if constexpr (std::is_same_v<T, UI_ImageComponent>)
	{
		RemoveImageComponent(ownerID);
		return;
	}

	if constexpr (std::is_same_v<T, UI_ScriptComponent>)
	{
		RemoveScriptComponent(ownerID);
		return;
	}

	if (m_worldDelegates.RemoveComponent.IsBound())
		m_worldDelegates.RemoveComponent.Execute(ownerID, typeid(T));
}

template<class T, class... Args>
	requires std::derived_from<T, IUIComponent>
T* UIWorld::CreateComponent(unsigned long ownerID, Args&&... args)
{
	// if constexpr (std::is_same_v<T, UITextComponent>)
	// 	return CreateTextComponent(ownerID);

	if constexpr (std::is_same_v<T, UITransform>)
		return CreateTransformComponent(ownerID);

	if constexpr (std::is_same_v<T, UI_ImageComponent>)
		return CreateImageComponent(ownerID);

	if constexpr (std::is_same_v<T, UI_ScriptComponent>)
		return CreateScriptComponent(ownerID);

	assert(m_worldDelegates.AddComponent.IsBound() && "World AddComponent delegate not bound");
	auto factory = [&]() -> void*
		{
			return static_cast<void*>(new T(std::forward<Args>(args)...));
		};
	void* raw = m_worldDelegates.AddComponent.Execute(ownerID, typeid(T), factory);
	return static_cast<T*>(raw);
}
