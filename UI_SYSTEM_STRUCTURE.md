# UI 시스템 구조 제안

## 📁 파일 구조 및 배치

World와 동일한 패턴으로 UI 시스템을 구성합니다:

```
src/
├── Core/
│   ├── World.h/cpp              # 게임 오브젝트 ECS 시스템
│   ├── GameObject.h              # 게임 오브젝트 래퍼
│   │
│   ├── UIWorld.h/cpp            # ✨ UI 오브젝트 ECS 시스템 (새로 추가)
│   └── UIObject.h                # ✨ UI 오브젝트 래퍼 (새로 추가)
│
├── Components/
│   ├── TransformComponent.h     # 게임 오브젝트용
│   ├── MaterialComponent.h
│   │
│   ├── UIComponent.h             # ✨ UI 컴포넌트 베이스 (새로 추가)
│   ├── UIButtonComponent.h       # ✨ 버튼 컴포넌트 (새로 추가)
│   ├── UIImageComponent.h        # ✨ 이미지 컴포넌트 (새로 추가)
│   ├── UITextComponent.h         # ✨ 텍스트 컴포넌트 (새로 추가)
│   └── UITransformComponent.h    # ✨ UI Transform (Screen Space) (새로 추가)
│
├── Engine/
│   └── Engine.h/cpp
│       └── Engine::Impl
│           ├── World m_world;                    # 기존
│           └── UIWorld m_uiWorld;                # ✨ 새로 추가
│
└── Rendering/ (또는 UI/)
    └── UIRenderSystem.h/cpp      # ✨ UI 렌더링 시스템 (새로 추가)
```

---

## 🏗️ 구조 상세

### 1. Engine::Impl에 선언 (Engine.cpp)

```cpp
// Engine::Impl 구조체 내부
struct Engine::Impl
{
    // 기존 시스템들...
    World          m_world;
    ScriptSystem   m_scriptSystem;
    SceneManager   m_sceneManager;
    
    // ✨ UI 시스템 추가
    UIWorld        m_uiWorld;              // UI 오브젝트 관리
    UIRenderSystem m_uiRenderSystem;       // UI 렌더링 (선택사항)
};
```

### 2. Core/UIWorld.h/cpp (World와 동일한 패턴)

```cpp
// Core/UIWorld.h
namespace Alice
{
    class UIObject;  // 전방 선언
    
    class UIWorld
    {
    public:
        UIWorld() = default;
        
        // ==== 엔티티 관리 ====
        EntityId CreateEntity();
        void DestroyEntity(EntityId id);
        
        // ==== UI 오브젝트 생성 헬퍼 ====
        EntityId CreateEmpty();           // 빈 UI 오브젝트
        EntityId CreateButton();          // 버튼 UI 오브젝트
        EntityId CreateImage();           // 이미지 UI 오브젝트
        EntityId CreateText();            // 텍스트 UI 오브젝트
        
        // ==== 컴포넌트 관리 (World와 동일) ====
        template <typename T, typename... Args>
        T& AddComponent(EntityId id, Args&&... args);
        
        template <typename T>
        T* GetComponent(EntityId id);
        
        template <typename T>
        void RemoveComponent(EntityId id);
        
        // ==== 유틸리티 ====
        UIObject FindUIObject(const std::string& name);
        void SetEntityName(EntityId id, const std::string& name);
        std::string GetEntityName(EntityId id) const;
        
        // ==== 컴포넌트 저장소 ====
        ComponentStorage<UITransformComponent> m_uiTransforms;
        ComponentStorage<UIButtonComponent> m_uiButtons;
        ComponentStorage<UIImageComponent> m_uiImages;
        ComponentStorage<UITextComponent> m_uiTexts;
        
        // 이름 관리
        std::unordered_map<EntityId, std::string> m_names;
        EntityId m_nextEntityId{ 1 };
        
        // SlotMap 기반 Generation 관리 (World와 동일)
        std::unordered_map<EntityId, std::uint32_t> m_entityGenerations;
    };
}
```

### 3. Core/UIObject.h (GameObject와 동일한 패턴)

```cpp
// Core/UIObject.h
namespace Alice
{
    /// Unity 느낌의 UI 엔티티 래퍼
    class UIObject
    {
    public:
        UIObject() = default;
        UIObject(UIWorld* world, EntityId id)
            : m_world(world), m_id(id)
        {
            if (m_world && m_id != InvalidEntityId)
            {
                m_generation = m_world->GetEntityGeneration(m_id);
            }
        }
        
        bool IsValid() const;
        EntityId id() const { return m_id; }
        
        template <typename T>
        T* GetComponent() const;
        
        template <typename T, typename... Args>
        T& AddComponent(Args&&... args) const;
        
        template <typename T>
        void RemoveComponent() const;
        
        void destroy();
        void destroy(float delay);
        
    private:
        UIWorld* m_world = nullptr;
        EntityId m_id = InvalidEntityId;
        std::uint32_t m_generation = 0;
    };
}
```

### 4. Components/UIComponent.h

```cpp
// Components/UIComponent.h
namespace Alice
{
    // UI Transform (Screen Space 좌표계)
    struct UITransformComponent
    {
        DirectX::XMFLOAT2 position{ 0.0f, 0.0f };  // Screen Space (픽셀)
        DirectX::XMFLOAT2 size{ 100.0f, 100.0f }; // 크기 (픽셀)
        float rotation{ 0.0f };                    // 회전 (도)
        DirectX::XMFLOAT2 anchor{ 0.5f, 0.5f };   // 앵커 (0.0~1.0)
        DirectX::XMFLOAT2 pivot{ 0.5f, 0.5f };    // 피벗 (0.0~1.0)
        
        bool visible{ true };
        int zOrder{ 0 };  // 렌더링 순서
    };
    
    // UI Button 컴포넌트
    struct UIButtonComponent
    {
        std::string text;
        DirectX::XMFLOAT4 normalColor{ 0.2f, 0.2f, 0.2f, 1.0f };
        DirectX::XMFLOAT4 hoverColor{ 0.3f, 0.3f, 0.3f, 1.0f };
        DirectX::XMFLOAT4 pressedColor{ 0.1f, 0.1f, 0.1f, 1.0f };
        
        bool isPressed{ false };
        bool isHovered{ false };
        
        // 콜백 (델리게이트 또는 함수 포인터)
        std::function<void()> onClick;
    };
    
    // UI Image 컴포넌트
    struct UIImageComponent
    {
        std::string texturePath;
        DirectX::XMFLOAT4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        bool preserveAspect{ true };
    };
    
    // UI Text 컴포넌트
    struct UITextComponent
    {
        std::string text;
        std::string fontPath;
        float fontSize{ 16.0f };
        DirectX::XMFLOAT4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        int alignment{ 0 };  // 0: Left, 1: Center, 2: Right
    };
}
```

---

## 🔄 Engine에서의 사용

### Engine::Initialize()에서 초기화

```cpp
bool Engine::Initialize(HINSTANCE hInstance, int nCmdShow)
{
    // ... 기존 초기화 코드 ...
    
    // ✨ UI World 초기화
    // (UIWorld는 기본 생성자로 자동 초기화되므로 별도 초기화 불필요)
    
    // ✨ UI 렌더 시스템 초기화 (선택사항)
    pImpl->m_uiRenderSystem.Initialize(*pImpl->m_renderDevice);
    
    return true;
}
```

### Engine::Update()에서 업데이트

```cpp
void Engine::Update()
{
    // ... 기존 업데이트 코드 ...
    
    // ✨ UI 업데이트 (입력 처리, 애니메이션 등)
    pImpl->m_uiWorld.Update(pImpl->m_inputSystem, deltaTime);
    
    // 또는 UI 전용 업데이트 시스템이 있다면
    // pImpl->m_uiSystem.Update(pImpl->m_uiWorld, deltaTime);
}
```

### Engine::Render()에서 렌더링

```cpp
void Engine::Render()
{
    // ... 기존 렌더링 코드 ...
    
    // 게임 렌더링 후 UI 렌더링
    if (pImpl->m_useForwardRendering)
    {
        pImpl->m_forwardRenderSystem->Render(...);
    }
    else
    {
        pImpl->m_deferredRenderSystem->Render(...);
    }
    
    // ✨ UI 렌더링 (가장 마지막에)
    pImpl->m_uiRenderSystem.Render(pImpl->m_uiWorld);
    
    // ImGui 렌더링 (에디터 모드)
    if (pImpl->m_editorMode)
    {
        pImpl->m_editorCore.RenderDrawData();
    }
    
    pImpl->m_renderDevice->EndFrame();
}
```

---

## 📋 요약: 파일 배치

| 파일/클래스 | 위치 | 역할 |
|------------|------|------|
| **UIWorld** | `Core/UIWorld.h/cpp` | UI 오브젝트 및 컴포넌트 관리 (World와 동일한 패턴) |
| **UIObject** | `Core/UIObject.h` | UI 엔티티 래퍼 (GameObject와 동일한 패턴) |
| **UIComponent** | `Components/UI*.h` | UI 컴포넌트 정의 (Transform, Button, Image, Text 등) |
| **UIWorld 선언** | `Engine/Engine.cpp` (Impl 구조체) | Engine에서 UIWorld 소유 |
| **UIRenderSystem** | `Rendering/UIRenderSystem.h/cpp` | UI 렌더링 시스템 (선택사항) |

---

## ✅ 권장 사항

1. **Core/에 배치하는 이유**
   - World와 동일한 레벨의 핵심 시스템
   - 다른 시스템에서도 접근 가능 (씬, 스크립트 등)
   - 일관된 아키텍처 유지

2. **Components/에 배치하는 이유**
   - 게임 컴포넌트와 동일한 위치
   - 컴포넌트 기반 설계 일관성

3. **Engine::Impl에 선언하는 이유**
   - World와 동일한 패턴
   - PIMPL 패턴 유지
   - Engine에서만 직접 접근

4. **선택사항: UIRenderSystem**
   - UI 렌더링이 복잡하면 별도 시스템으로 분리
   - 간단하면 UIWorld 내부에 렌더링 로직 포함 가능

---

## 🎯 구현 순서

1. ✅ `Components/UITransformComponent.h` 생성
2. ✅ `Components/UIButtonComponent.h` 생성 (또는 필요한 컴포넌트들)
3. ✅ `Core/UIWorld.h/cpp` 생성 (World 복사 후 수정)
4. ✅ `Core/UIObject.h` 생성 (GameObject 복사 후 수정)
5. ✅ `Engine/Engine.cpp`의 Impl에 `UIWorld m_uiWorld;` 추가
6. ✅ `Engine::Update()`와 `Engine::Render()`에 UI 업데이트/렌더링 추가
7. (선택) `Rendering/UIRenderSystem.h/cpp` 생성

이 구조로 진행하면 World와 완전히 동일한 패턴으로 UI 시스템을 만들 수 있습니다!
