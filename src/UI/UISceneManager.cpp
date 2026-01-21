#include "UISceneManager.h"
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dwrite.h>
#pragma comment(lib, "dwrite.lib")
#include <d2d1.h>
#pragma comment(lib, "d2d1.lib")
#include <wrl/client.h>
#include <string>
#include <stdexcept>
#include "Core/InputSystem.h"
#include "UITransform.h"
#include "UI_ImageComponent.h"
#include "UI_ScriptComponent.h"
#include "IUIComponent.h"
#include "UIBase.h"
#include "UIScriptSystem.h"
// ============================================================================
// UIWorld ����
// ============================================================================
void UIWorld::Initialize(UIRenderStruct* UIRst)
{
	assert(UIRst && "UIWorld::Initialize: UIRst must not be null");
	m_UIRenderStruct = UIRst;
	nowInteger = 1;

	// ��������Ʈ ���ε� (���ø� ���� ���� ����)
	m_worldDelegates.AddComponent.BindLambda(
		[this](ObjectID id, std::type_index type, std::function<void* ()> factory) -> void*
		{
			void* raw = nullptr;

			// UITextComponent 없음 (추후 추가 시 복구)
			// if (type == typeid(UITextComponent))
			// {
			// 	if (out) *out = static_cast<void*>(this->CreateTextComponent(static_cast<unsigned long>(id)));
			// 	return;
			// }

			// 1) type 별 고정 생성(월드 소유)
			if (type == typeid(UITransform))
			{
				raw = static_cast<void*>(this->CreateTransformComponent(static_cast<unsigned long>(id)));
			}
			else if (type == typeid(UI_ImageComponent))
			{
				raw = static_cast<void*>(this->CreateImageComponent(static_cast<unsigned long>(id)));
			}
			else if (type == typeid(UI_ScriptComponent))
			{
				raw = static_cast<void*>(this->CreateScriptComponent(static_cast<unsigned long>(id)));
			}
			// 2) 기타 타입: factory 생성(현재는 소유권 TODO)
			else if (factory)
			{
				raw = factory();
			}

			// 3) owner 세팅 (공통)
			if (raw)
			{
				UIBase* owner = this->Get(static_cast<unsigned long>(id)); // UIWorld::Get
				// owner가 없을 수도 있으니 체크
				if (owner)
				{
					// raw가 IUIComponent이면 세팅
					IUIComponent* comp = static_cast<IUIComponent*>(raw);
					if (comp != nullptr)
					{
						comp->Owner = owner;
						comp->OwnerID = static_cast<unsigned long>(id);
						comp->OnAdded();
					}
				}
			}

			return raw;
		}
	);

	m_worldDelegates.FindComponent.BindLambda(
		[this](ObjectID id, std::type_index type) -> void*
		{

			// UITextComponent 없음 (추후 추가 시 복구)
			// if (type == typeid(UITextComponent))
			// {
			// 	if (out) *out = static_cast<void*>(this->FindTextComponent(static_cast<unsigned long>(id)));
			// 	return;
			// }

			// UITransform 조회
			if (type == typeid(UITransform))
			{
				return static_cast<void*>(this->FindTransformComponent(static_cast<unsigned long>(id)));
			}
			// UI_ImageComponent 조회
			if (type == typeid(UI_ImageComponent))
			{
				return static_cast<void*>(this->FindImageComponent(static_cast<unsigned long>(id)));
			}
			if (type == typeid(UI_ScriptComponent))
			{
				return static_cast<void*>(this->FindScriptComponent(static_cast<unsigned long>(id)));
			}
			return nullptr;
		}
	);

	// 추후에 bool type으로 고치기!!
	m_worldDelegates.RemoveComponent.BindLambda(
		[this](ObjectID id, std::type_index type)
		{
			// UITextComponent 없음 (추후 추가 시 복구)
			// if (type == typeid(UITextComponent))
			// {
			// 	this->RemoveTextComponent(static_cast<unsigned long>(id));
			// 	return;
			// }

			// UITransform 삭제
			if (type == typeid(UITransform))
			{
				this->RemoveTransformComponent(static_cast<unsigned long>(id));
				return;
			}
			// UI_ImageComponent 삭제
			if (type == typeid(UI_ImageComponent))
			{
				this->RemoveImageComponent(static_cast<unsigned long>(id));
				return;
			}
			if (type == typeid(UI_ScriptComponent))
			{
				this->RemoveScriptComponent(static_cast<unsigned long>(id));
				return;
			}
		}
	);
}

//UITextComponent* UIWorld::CreateTextComponent(unsigned long ownerID)
//{
//	// 이미 해당 ownerID에 컴포넌트가 있다면 그대로 반환
//	auto it = m_compStorage.find(ownerID);
//	if (it != m_compStorage.end())
//		return it->second.get();
//
//	// 새 컴포넌트 생성 후 저장
//	auto comp = std::make_unique<UITextComponent>();
//	comp->owner = ownerID;
//	UITextComponent* raw = comp.get();
//	m_compStorage.emplace(ownerID, std::move(comp));
//	return raw;
//}

UITransform* UIWorld::CreateTransformComponent(unsigned long ownerID)
{
	// �̹� �ش� ownerID�� ������Ʈ�� �ִٸ� �״�� ��ȯ
	auto it = m_transformStorage.find(ownerID);
	if (it != m_transformStorage.end())
		return it->second.get();

	// �� ������Ʈ ���� �� ������ ���� ����
	auto comp = std::make_unique<UITransform>();
	comp->owner = ownerID;
	UITransform* raw = comp.get();
	
	m_transformStorage.emplace(ownerID, std::move(comp));
	return raw;
}

/*
UITextComponent* UIWorld::FindTextComponent(unsigned long ownerID)
{
	auto it = m_compStorage.find(ownerID);
	return (it == m_compStorage.end()) ? nullptr : it->second.get();
}
*/

UITransform* UIWorld::FindTransformComponent(unsigned long ownerID)
{
	auto it = m_transformStorage.find(ownerID);
	return (it == m_transformStorage.end()) ? nullptr : it->second.get();
}

/*
void UIWorld::RemoveTextComponent(unsigned long ownerID)
{
	m_compStorage.erase(ownerID);
}
*/

void UIWorld::RemoveTransformComponent(unsigned long ownerID)
{
	m_transformStorage.erase(ownerID);
}

UI_ImageComponent* UIWorld::CreateImageComponent(unsigned long ownerID)
{
	// 이미 해당 ownerID에 컴포넌트가 있다면 그대로 반환
	auto it = m_imageComponentStorage.find(ownerID);
	if (it != m_imageComponentStorage.end())
		return it->second.get();

	// 새 컴포넌트 생성 후 저장
	auto comp = std::make_unique<UI_ImageComponent>();
	comp->owner = ownerID;
	UI_ImageComponent* raw = comp.get();
	m_imageComponentStorage.emplace(ownerID, std::move(comp));
	return raw;
}

UI_ImageComponent* UIWorld::FindImageComponent(unsigned long ownerID)
{
	auto it = m_imageComponentStorage.find(ownerID);
	return (it == m_imageComponentStorage.end()) ? nullptr : it->second.get();
}

void UIWorld::RemoveImageComponent(unsigned long ownerID)
{
	m_imageComponentStorage.erase(ownerID);
}

UI_ScriptComponent* UIWorld::CreateScriptComponent(unsigned long ownerID)
{
	// 이미 해당 ownerID에 컴포넌트가 있다면 그대로 반환
	auto it = m_scriptComponentStorage.find(ownerID);
	if (it != m_scriptComponentStorage.end())
		return it->second.get();

	auto comp = std::make_unique<UI_ScriptComponent>();
	comp->owner = ownerID;
	UI_ScriptComponent* raw = comp.get();
	m_scriptComponentStorage.emplace(ownerID, std::move(comp));
	return raw;
}

UI_ScriptComponent* UIWorld::FindScriptComponent(unsigned long ownerID)
{
	auto it = m_scriptComponentStorage.find(ownerID);
	return (it == m_scriptComponentStorage.end()) ? nullptr : it->second.get();
}

void UIWorld::RemoveScriptComponent(unsigned long ownerID)
{
	auto it = m_scriptComponentStorage.find(ownerID);
	if (it != m_scriptComponentStorage.end())
	{
		if (it->second)
		{
			it->second->OnRemoved();
		}
		m_scriptComponentStorage.erase(it);
	}
}

UIBase* UIWorld::Get(long unsigned int ID)
{
	auto it = pUIObjStorage.find(ID);
	if (it == pUIObjStorage.end())
		return nullptr;
	return it->second.get();
}

bool UIWorld::DestroyEntity(long unsigned int ID)
{
	UIBase* tmpNode = Get(ID);
	if (!tmpNode) return false;

	// 1. �θ𿡼� ��� ����
	long unsigned int parentID{ tmpNode->parentID };
	if (parentID != 0)
	{
		UIBase* parentNode = Get(parentID);
		if (parentNode != nullptr)
		{
			auto& ChildVect = parentNode->childIDStorage;
			ChildVect.erase(
				std::remove(ChildVect.begin(), ChildVect.end(), ID),
				ChildVect.end()
			);
		}
	}
	else
	{   // ��Ʈ����� ���!
		m_rootID.erase(
			std::remove(m_rootID.begin(), m_rootID.end(), ID),
			m_rootID.end()
		);
	}

	// 2. ������Ʈ�� �Բ� ����
	RemoveTransformComponent(ID);
	RemoveImageComponent(ID);
	RemoveScriptComponent(ID);

	// 3. ����Ʈ�� ��ü ���� (��������� �ڽĵ鵵 ����)
	return DeleteChildObjects(ID);
}

bool UIWorld::DeleteChildObjects(long unsigned int ID)
{
	UIBase* tmpNode = Get(ID);
	if (!tmpNode) return false;

	// �ڽĵ��� ���� ���� (���纻 ��� - ������ �����ǹǷ�)
	auto children = tmpNode->childIDStorage; // ����
	for (auto cid : children)
	{
		// �� �ڽ��� ������Ʈ�� ����
		RemoveTransformComponent(cid);
		RemoveImageComponent(cid);
		RemoveScriptComponent(cid);
		DeleteChildObjects(cid);
	}

	// ��ƼƼ ����
	pUIObjStorage.erase(ID);
	return true;
}

void UIWorld::Clear()
{
	// ��� ��ƼƼ ����
	pUIObjStorage.clear();
	m_rootID.clear();

	// ��� ������Ʈ ����
	// m_compStorage.clear(); // UITextComponent 없음
	m_transformStorage.clear();
	m_imageComponentStorage.clear();
	m_scriptComponentStorage.clear();

	// World Epoch ���� (���� �ڵ���� ��ȿȭ��)
	m_worldEpoch++;

	// nowInteger�� reset���� ���� (���� ��å ����)
}

// ============================================================================
// UILayoutSystem ����
// ============================================================================
void UILayoutSystem::UpdateTransforms(UIWorld& world)
{
	// ��Ʈ UI ����� ���� 
	for (auto rootID : world.GetRootIDs())
	{
		if (auto* root = world.Get(rootID))
			UpdateTransformChild(world, root, D2D1::Matrix3x2F::Identity());
	}
}

void UILayoutSystem::UpdateTransformChild(UIWorld& world, UIBase* node, const D2D1::Matrix3x2F& parentWorld)
{
	// Transform은 UIBase 캐시로 접근 (생성 직후 1회 부착 정책)
	auto& tr = node->GetTransform();
	D2D1::Matrix3x2F worldMat = tr.WorldMatrix(parentWorld);

	for (auto childID : node->childIDStorage)
	{
		if (auto* child = world.Get(childID))
			UpdateTransformChild(world, child, worldMat);
	}
}

void UILayoutSystem::UpdateUI(UIWorld& world)
{
	for (auto rootID : world.GetRootIDs())
	{
		if (auto* root = world.Get(rootID))
		{
			root->Update();
			world.Traverse(root,
				[](UIBase* node)
				{
					node->Update();
				}
			);
		}
	}
}

// ============================================================================
// UIHitTestSystem ����
// ============================================================================
long unsigned UIHitTestSystem::FindUIUnderPointer(UIWorld& world, XMFLOAT2 MousePos, UIRenderStruct* renderStruct)
{
	std::vector<long unsigned> hitMouseID;
	hitMouseID.clear();

	XMFLOAT2 unityMousePos = {
		MousePos.x - renderStruct->m_width * 0.5f,
		renderStruct->m_height * 0.5f - MousePos.y
	};

	std::vector<long unsigned> tmpStorage;
	long unsigned tmpID{ 0 };

	// AABB(ȸ�� ���� ��ü AABB)
	for (auto rootID : world.GetRootIDs())
	{
		if (auto* root = world.Get(rootID))
		{
			if (!root->IsMouseOverUIAABB(unityMousePos, tmpStorage))
				continue;

			AABBRoot(world, unityMousePos, root, tmpStorage);
		}
	}

	// ȸ���ִ� ��ü �˻�
	for (auto rootID : tmpStorage)
	{
		if (auto* root = world.Get(rootID))
		{
			if (root->IsMouseOverUIRot(unityMousePos))
			{
				hitMouseID.push_back(root->getID());
			}

			UIRotRoot(world, unityMousePos, root, hitMouseID);
		}
	}

	if (hitMouseID.size() != 0)
		tmpID = hitMouseID.back();

	return tmpID;
}

void UIHitTestSystem::AABBRoot(UIWorld& world, XMFLOAT2 MousePos, UIBase* node, std::vector<long unsigned>& IDStorage)
{
	for (auto childID : node->childIDStorage)
	{
		if (auto* child = world.Get(childID))
		{
			if (!child->IsMouseOverUIAABB(MousePos, IDStorage))
				continue;

			AABBRoot(world, MousePos, child, IDStorage);
		}
	}
}

void UIHitTestSystem::UIRotRoot(UIWorld& world, XMFLOAT2 MousePos, UIBase* node, std::vector<long unsigned>& hitMouseID)
{
	for (auto childID : node->childIDStorage)
	{
		if (auto* child = world.Get(childID))
		{
			if (!child->IsMouseOverUIRot(MousePos))
				continue;
			hitMouseID.push_back(child->getID());
			UIRotRoot(world, MousePos, child, hitMouseID);
		}
	}
}

// ============================================================================
// UIEventSystem ����
// ============================================================================
void UIEventSystem::UpdatePointer(UIWorld& world, Alice::InputSystem* inputSystem, UIRenderStruct* renderStruct)
{
	/*auto mouseState = inputSystem->m_Mouse->GetState();
	XMFLOAT2 MousePos = { (float)mouseState.x, (float)mouseState.y };

	auto nowID = UIHitTestSystem::FindUIUnderPointer(world, MousePos, renderStruct);
	if (nowID == 0) return;

	auto* nowUI = world.Get(nowID);
	if (!nowUI) return;

	auto& mouseTracker = inputSystem->m_MouseStateTracker;
	if (mouseTracker.leftButton == Mouse::ButtonStateTracker::UP)
	{
		nowUI->m_uiState = UIState::Normal;
	}

	if (mouseTracker.leftButton == Mouse::ButtonStateTracker::PRESSED)
	{
		nowUI->m_uiState = UIState::Pressed;
	}

	if (mouseTracker.leftButton == Mouse::ButtonStateTracker::RELEASED)
	{
		nowUI->m_uiState = UIState::Release;
	}

	if (mouseTracker.leftButton == Mouse::ButtonStateTracker::HELD)
	{
		nowUI->m_uiState = UIState::Hold;
	}*/
}

// ============================================================================
// UIRenderSystem ����
// ============================================================================
void UIRenderSystem::Render(UIWorld& world, UIRenderStruct* renderStruct)
{
	renderStruct->m_d2DdevCon->BeginDraw();
	renderStruct->m_d2DdevCon->Clear(D2D1::ColorF(0, 0, 0, 0));

	RenderRoot(world, renderStruct);

	renderStruct->m_d2DdevCon->EndDraw();
}

void UIRenderSystem::RenderRoot(UIWorld& world, UIRenderStruct* renderStruct)
{
	// ��Ʈ UI ����� ���� 
	for (auto rootID : world.GetRootIDs())
	{
		if (auto* root = world.Get(rootID))
		{
			root->Render();
			RenderRootChild(world, root);
		}
	}
}

void UIRenderSystem::RenderRootChild(UIWorld& world, UIBase* node)
{
	for (auto childID : node->childIDStorage)
	{
		if (auto* child = world.Get(childID))
		{
			child->Render();
			RenderRootChild(world, child);
		}
	}
}

// ============================================================================
// UISceneManager ����
// ============================================================================
void UISceneManager::initalize(ID3D11Device* Dev, ID3D11DeviceContext* DevCon, UIRenderStruct* UIRst, Alice::InputSystem* tmpInput)
{
	pDev = Dev;
	pDevCon = DevCon;
	m_UIRenderStruct = UIRst;
	m_InputSystem = tmpInput;

	// UIWorld �ʱ�ȭ
	m_world.Initialize(UIRst);
}

void UISceneManager::Update()
{
	// Layout System: Transform ����
	UILayoutSystem::UpdateTransforms(m_world);

	// Layout System: UI Update
	UILayoutSystem::UpdateUI(m_world);

	// Script System: UI_ScriptComponent 업데이트
	UIScriptSystem::Tick(m_world, 0.0f); // dt가 아직 별도로 관리되지 않아 0 전달

	// Image System: UI_ImageComponent 업데이트
	UIImageSystem::Update(m_world);

	// Event System: ���콺 �Է� ó��
	UIEventSystem::UpdatePointer(m_world, m_InputSystem, m_UIRenderStruct);
}

void UISceneManager::Render()
{
	// Render System: 기본 렌더링
	UIRenderSystem::Render(m_world, m_UIRenderStruct);

	// Image System: UI_ImageComponent 렌더링
	UIImageSystem::Render(m_world, m_UIRenderStruct);
}

// ============================================================================
// UIImageSystem 구현
// ============================================================================
void UIImageSystem::Update(UIWorld& world)
{
	UpdateRoot(world);
}

void UIImageSystem::UpdateRoot(UIWorld& world)
{
	// 루트 UI 엔티티들 순회
	for (auto rootID : world.GetRootIDs())
	{
		if (auto* root = world.Get(rootID))
		{
			// ImageComponent 업데이트
			if (auto* imageComp = root->TryGetComponent<UI_ImageComponent>())
			{
				imageComp->Update();
			}
			// 자식들도 재귀적으로 업데이트
			UpdateRootChild(world, root);
		}
	}
}

void UIImageSystem::UpdateRootChild(UIWorld& world, UIBase* node)
{
	for (auto childID : node->childIDStorage)
	{
		if (auto* child = world.Get(childID))
		{
			// ImageComponent 업데이트
			if (auto* imageComp = child->TryGetComponent<UI_ImageComponent>())
			{
				imageComp->Update();
			}
			// 재귀적으로 자식들도 업데이트
			UpdateRootChild(world, child);
		}
	}
}

void UIImageSystem::Render(UIWorld& world, UIRenderStruct* renderStruct)
{
	if (!renderStruct) return;
	RenderRoot(world, renderStruct);
}

void UIImageSystem::RenderRoot(UIWorld& world, UIRenderStruct* renderStruct)
{
	// 루트 UI 엔티티들 순회
	for (auto rootID : world.GetRootIDs())
	{
		if (auto* root = world.Get(rootID))
		{
			// ImageComponent 렌더링
			if (auto* imageComp = root->TryGetComponent<UI_ImageComponent>())
			{
				imageComp->Render();
			}
			// 자식들도 재귀적으로 렌더링
			RenderRootChild(world, root, renderStruct);
		}
	}
}

void UIImageSystem::RenderRootChild(UIWorld& world, UIBase* node, UIRenderStruct* renderStruct)
{
	for (auto childID : node->childIDStorage)
	{
		if (auto* child = world.Get(childID))
		{
			// ImageComponent 렌더링
			if (auto* imageComp = child->TryGetComponent<UI_ImageComponent>())
			{
				imageComp->Render();
			}
			// 재귀적으로 자식들도 렌더링
			RenderRootChild(world, child, renderStruct);
		}
	}
}
