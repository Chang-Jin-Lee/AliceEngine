# AliceRenderer 엔진 아키텍처 구조도

## 📐 전체 시스템 계층 구조

```
┌─────────────────────────────────────────────────────────────────┐
│                         Engine (최상위)                         │
│  - 윈도우 생성 및 메시지 루프 관리                               │
│  - 전체 시스템 조율 및 업데이트/렌더링                            │
│  - 에디터 모드 / 게임 모드 전환                                  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ├──────────────────────────────────┐
                              │                                  │
                              ▼                                  ▼
        ┌──────────────────────────────┐      ┌──────────────────────────────┐
        │      Core Systems            │      │    Rendering Systems         │
        │                              │      │                              │
        │  ┌──────────────────────┐   │      │  ┌──────────────────────┐   │
        │  │      World           │   │      │  │  D3D11RenderDevice   │   │
        │  │  (ECS 시스템)        │   │      │  │  (DirectX 11 래퍼)   │   │
        │  └──────────────────────┘   │      │  └──────────────────────┘   │
        │                              │      │                              │
        │  ┌──────────────────────┐   │      │  ┌──────────────────────┐   │
        │  │   ScriptSystem       │   │      │  │ ForwardRenderSystem │   │
        │  │  (스크립트 업데이트)  │   │      │  │  (Forward 렌더링)    │   │
        │  └──────────────────────┘   │      │  └──────────────────────┘   │
        │                              │      │                              │
        │  ┌──────────────────────┐   │      │  ┌──────────────────────┐   │
        │  │   SceneManager       │   │      │  │ DeferredRenderSystem │   │
        │  │  (씬 전환/관리)       │   │      │  │  (Deferred 렌더링)   │   │
        │  └──────────────────────┘   │      │  └──────────────────────┘   │
        │                              │      │                              │
        │  ┌──────────────────────┐   │      │  ┌──────────────────────┐   │
        │  │   InputSystem        │   │      │  │  DebugDrawSystem     │   │
        │  │  (입력 처리)          │   │      │  │  (디버그 라인 그리기) │   │
        │  └──────────────────────┘   │      │  └──────────────────────┘   │
        │                              │      │                              │
        │  ┌──────────────────────┐   │      │                              │
        │  │  ResourceManager     │   │      │                              │
        │  │  (리소스 로드)        │   │      │                              │
        │  └──────────────────────┘   │      │                              │
        │                              │      │                              │
        │  ┌──────────────────────┐   │      │                              │
        │  │  EditorCore          │   │      │                              │
        │  │  (ImGui 에디터 UI)    │   │      │                              │
        │  └──────────────────────┘   │      │                              │
        └──────────────────────────────┘      └──────────────────────────────┘
```

---

## 🎮 ECS (Entity Component System) 구조

```
┌─────────────────────────────────────────────────────────────────┐
│                            World                                │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  EntityId 관리                                            │  │
│  │  - CreateEntity() → EntityId 생성                        │  │
│  │  - DestroyEntity() → 엔티티 파괴                          │  │
│  │  - SlotMap 기반 Generation 관리 (안전한 참조)              │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                 │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Component Storage (Sparse Set 기반)                      │  │
│  │  ┌──────────────────┐  ┌──────────────────┐            │  │
│  │  │ TransformComponent│  │MaterialComponent │            │  │
│  │  └──────────────────┘  └──────────────────┘            │  │
│  │  ┌──────────────────┐  ┌──────────────────┐            │  │
│  │  │SkinnedMeshComp    │  │CameraComponent   │            │  │
│  │  └──────────────────┘  └──────────────────┘            │  │
│  │  ┌──────────────────┐                                   │  │
│  │  │SkinnedAnimComp   │                                   │  │
│  │  └──────────────────┘                                   │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                 │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Script Components (IScript 상속)                         │  │
│  │  - std::unordered_map<EntityId, vector<ScriptComponent>> │  │
│  │  - 여러 스크립트를 하나의 엔티티에 부착 가능                │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                 │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  유틸리티 함수                                            │  │
│  │  - CreateEmpty() → 빈 GameObject 생성                    │  │
│  │  - CreateCube() → 큐브 GameObject 생성                   │  │
│  │  - CreateCamera() → 카메라 GameObject 생성                │  │
│  │  - FindGameObject(name) → 이름으로 검색                    │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ 래핑
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                        GameObject                               │
│  (Unity 스타일 엔티티 래퍼)                                     │
│                                                                 │
│  - World* + EntityId + Generation 저장                         │
│  - GetComponent<T>() → 컴포넌트 가져오기                       │
│  - AddComponent<T>() → 컴포넌트 추가                           │
│  - RemoveComponent<T>() → 컴포넌트 제거                        │
│  - destroy() → 엔티티 파괴                                     │
│  - GetAnimator() → 애니메이션 제어                             │
└─────────────────────────────────────────────────────────────────┘
```

---

## 🔄 게임 루프 흐름

```
Engine::Run()
    │
    ├─► [메시지 루프]
    │   └─► PeekMessage() → TranslateMessage() → DispatchMessage()
    │
    ├─► [Update 단계]
    │   │
    │   ├─► GameTimer.Tick() → DeltaTime 계산
    │   │
    │   ├─► InputSystem.Update() → 키보드/마우스 입력 갱신
    │   │
    │   ├─► [카메라 업데이트]
    │   │   ├─► 에디터 모드: 프리 카메라 (WASD + 마우스)
    │   │   └─► 게임 모드: 씬의 CameraComponent 동기화
    │   │
    │   ├─► SceneManager.Update() → 현재 씬 로직 실행
    │   │
    │   ├─► ScriptSystem.Tick() → 모든 스크립트 업데이트
    │   │   ├─► Awake() → 최초 1회
    │   │   ├─► OnEnable() → 활성화 시
    │   │   ├─► Start() → 최초 1회
    │   │   ├─► Update(deltaTime) → 매 프레임
    │   │   ├─► LateUpdate(deltaTime) → 매 프레임 (Update 후)
    │   │   └─► FixedUpdate(fixedDt) → 고정 시간 간격
    │   │
    │   ├─► SkinnedAnimationSystem.Update() → 스키닝 애니메이션 갱신
    │   │
    │   └─► World.UpdateDelayedDestruction() → 지연 파괴 처리
    │
    └─► [Render 단계]
        │
        ├─► SkinnedMeshSystem.BuildDrawList() → 드로우 커맨드 생성
        │
        ├─► [렌더링 시스템 선택]
        │   ├─► ForwardRenderSystem.Render() (Forward 모드)
        │   │   ├─► Shadow Pass (섀도우 맵 생성)
        │   │   ├─► Main Pass (정적 메시 + 라이팅)
        │   │   ├─► Skinned Mesh Pass (스키닝 메시)
        │   │   ├─► Skybox Pass
        │   │   └─► Tone Mapping
        │   │
        │   └─► DeferredRenderSystem.Render() (Deferred 모드)
        │       ├─► G-Buffer Pass
        │       ├─► Lighting Pass
        │       └─► Tone Mapping
        │
        ├─► DebugDrawSystem.Render() → 디버그 라인 그리기
        │
        ├─► EditorCore.RenderDrawData() → ImGui UI 렌더링 (에디터 모드)
        │
        └─► RenderDevice.EndFrame() → Present
```

---

## 📦 컴포넌트 시스템 상세

### Core Components

```
┌─────────────────────────────────────────────────────────────┐
│                    TransformComponent                        │
│  - position: XMFLOAT3 (위치)                                 │
│  - rotation: XMFLOAT3 (회전 - Roll/Pitch/Yaw)               │
│  - scale: XMFLOAT3 (스케일)                                  │
│  - SetPosition(), SetRotation(), SetScale()                 │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                    MaterialComponent                         │
│  - color: XMFLOAT3 (기본 색상)                               │
│  - texture paths (알베도, 노말, 스페큘러 등)                  │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                  SkinnedMeshComponent                        │
│  - meshAssetPath: string (FBX 에셋 경로)                     │
│  - instanceAssetPath: string                                 │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│              SkinnedAnimationComponent                       │
│  - clipIndex: int (재생할 애니메이션 인덱스)                  │
│  - timeSec: double (현재 재생 시간)                          │
│  - playing: bool (재생 중 여부)                              │
│  - speed: float (재생 속도)                                  │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                    CameraComponent                           │
│  - fovYRad: float (시야각)                                   │
│  - nearPlane, farPlane: float                               │
│  - primary: bool (메인 카메라 여부)                          │
└─────────────────────────────────────────────────────────────┘
```

### Script Components

```
┌─────────────────────────────────────────────────────────────┐
│                        IScript                               │
│  (모든 스크립트의 베이스 클래스)                              │
│                                                              │
│  Lifecycle:                                                  │
│    Awake() → OnEnable() → Start()                            │
│    ↓                                                          │
│    Update(deltaTime) / LateUpdate(deltaTime)                 │
│    FixedUpdate(fixedDt)                                      │
│    ↓                                                          │
│    OnDisable() → OnDestroy()                                 │
│                                                              │
│  API:                                                        │
│    - GetComponent<T>() → 컴포넌트 가져오기                   │
│    - AddComponent<T>() → 컴포넌트 추가                       │
│    - gameObject() → GameObject 래퍼 얻기                      │
│    - input, scene, resources (ScriptServices)                │
└─────────────────────────────────────────────────────────────┘
```

---

## 🎬 스크립트 시스템 상세

```
┌─────────────────────────────────────────────────────────────┐
│                      ScriptSystem                            │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  ScriptServices (스크립트에 제공되는 서비스)         │  │
│  │  - InputSystem* → 입력 처리                          │  │
│  │  - SceneManager* → 씬 전환                          │  │
│  │  - ResourceManager* → 리소스 로드                    │  │
│  │  - SkinnedMeshRegistry* → 메시 레지스트리            │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  입력 스냅샷 시스템                                   │  │
│  │  - m_prevKeys / m_currKeys → GetKeyDown/Up 감지      │  │
│  │  - m_prevMouseButtons / m_currMouseButtons           │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  델리게이트 시스템                                    │  │
│  │  - onAfterSceneLoaded → 씬 로드 후 콜백              │  │
│  │  - onTrimVideoMemory → 메모리 정리 콜백               │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                              │
│  Tick(World&, float):                                        │
│    1. BeginInputFrame() → 입력 스냅샷 갱신                  │
│    2. EnsureServicesBound() → 스크립트에 서비스 바인딩     │
│    3. CallUpdate() → 모든 스크립트의 Update() 호출         │
│    4. CallLateUpdate() → 모든 스크립트의 LateUpdate() 호출  │
│    5. CallFixedUpdate() → 고정 시간 간격 업데이트           │
│    6. ProcessSceneRequests() → 씬 전환 요청 처리           │
└─────────────────────────────────────────────────────────────┘
```

---

## 🎨 렌더링 파이프라인

```
┌─────────────────────────────────────────────────────────────┐
│                    ForwardRenderSystem                       │
│                                                              │
│  Render() 순서:                                              │
│    1. Shadow Pass                                            │
│       └─► 섀도우 맵 생성 (Directional Light)                │
│                                                              │
│    2. Main Pass                                              │
│       ├─► RTV 클리어                                         │
│       ├─► 공통 리소스 바인딩 (Light, Camera 등)            │
│       └─► 정적 메시 렌더링 (MaterialComponent 기반)         │
│                                                              │
│    3. Skinned Mesh Pass                                      │
│       └─► 스키닝 메시 렌더링 (SkinnedMeshComponent)         │
│                                                              │
│    4. Skybox Pass                                            │
│       └─► 스카이박스 렌더링                                 │
│                                                              │
│    5. Tone Mapping                                           │
│       └─► HDR → LDR 변환 (ImGui 표시용)                     │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                   DeferredRenderSystem                       │
│                                                              │
│  Render() 순서:                                              │
│    1. G-Buffer Pass                                          │
│       └─► Position, Normal, Albedo, Metallic 등을 G-Buffer에 │
│                                                              │
│    2. Lighting Pass                                          │
│       └─► G-Buffer를 읽어서 라이팅 계산                      │
│                                                              │
│    3. Tone Mapping                                           │
│       └─► HDR → LDR 변환                                    │
└─────────────────────────────────────────────────────────────┘
```

---

## 📁 파일 구조

```
src/
├── Core/                    # 핵심 시스템
│   ├── World.h/cpp          # ECS 시스템 (엔티티/컴포넌트 관리)
│   ├── GameObject.h         # Unity 스타일 엔티티 래퍼
│   ├── IScript.h/cpp        # 스크립트 베이스 클래스
│   ├── ScriptSystem.h/cpp   # 스크립트 업데이트 시스템
│   ├── Scene.h/cpp          # 씬 관리 시스템
│   ├── InputSystem.h/cpp    # 입력 처리
│   ├── ResourceManager.h/cpp # 리소스 로드
│   └── ...
│
├── Components/              # 컴포넌트 정의
│   ├── TransformComponent.h
│   ├── MaterialComponent.h
│   ├── SkinnedMeshComponent.h
│   ├── SkinnedAnimationComponent.h
│   ├── CameraComponent.h
│   └── ComponentStorage.h   # Sparse Set 기반 저장소
│
├── Engine/                  # 엔진 최상위
│   ├── Engine.h/cpp         # 메인 엔진 클래스
│   └── ...
│
├── Rendering/               # 렌더링 시스템
│   ├── ForwardRenderSystem.h/cpp
│   ├── DeferredRenderSystem.h/cpp
│   ├── D3D11RenderDevice.h/cpp
│   └── ...
│
├── Editor/                  # 에디터 시스템
│   ├── EditorCore.h/cpp     # ImGui 에디터 UI
│   └── ViewportPicker.h/cpp
│
├── Game/                    # 게임 로직
│   ├── SkinnedMeshSystem.h/cpp
│   ├── SkinnedAnimationSystem.h/cpp
│   └── ...
│
└── Scripts/                  # 사용자 스크립트
    └── ...
```

---

## 🔗 주요 의존성 관계

```
Engine
  ├─► World (소유)
  │     ├─► ComponentStorage<T> (컴포넌트 저장)
  │     └─► ScriptComponent (스크립트 저장)
  │
  ├─► ScriptSystem (소유)
  │     ├─► InputSystem* (참조)
  │     ├─► SceneManager* (참조)
  │     ├─► ResourceManager* (참조)
  │     └─► SkinnedMeshRegistry* (참조)
  │
  ├─► SceneManager (소유)
  │     └─► IScene* (현재 씬)
  │
  ├─► ForwardRenderSystem (소유)
  │     ├─► D3D11RenderDevice* (참조)
  │     ├─► ResourceManager* (참조)
  │     └─► SkinnedMeshRegistry* (참조)
  │
  └─► EditorCore (소유, 에디터 모드에서만)
        ├─► ResourceManager* (참조)
        ├─► SkinnedMeshRegistry* (참조)
        └─► InputSystem* (참조)
```

---

## 💡 주요 설계 패턴

1. **ECS (Entity Component System)**
   - World가 모든 엔티티와 컴포넌트 관리
   - Sparse Set 기반 컴포넌트 저장 (캐시 효율성)

2. **PIMPL (Pointer to Implementation)**
   - Engine 클래스가 Impl 구조체로 구현 세부사항 숨김

3. **Factory Pattern**
   - ScriptFactory: 스크립트 동적 생성
   - SceneFactory: 씬 동적 생성

4. **Delegate Pattern**
   - ScriptSystem의 onAfterSceneLoaded, onTrimVideoMemory

5. **SlotMap Pattern**
   - EntityId + Generation으로 안전한 참조 관리

---

## 🎯 UIObject 추가 시 고려사항

현재 구조에서 UIObject를 추가하는 방법:

### 옵션 1: Engine에 별도 UI 시스템 추가 (권장)
```
Engine::Impl
  └─► std::unique_ptr<UISystem> m_uiSystem
      └─► UIObject들을 별도로 관리
          (게임 오브젝트와 분리된 렌더링 파이프라인)
```

### 옵션 2: World에 UI 컴포넌트 추가
```
World
  └─► ComponentStorage<UIComponent> m_uiComponents
      (게임 오브젝트와 동일한 방식으로 관리)
```

옵션 1이 권장되는 이유:
- UI는 보통 게임 오브젝트와 다른 렌더링 파이프라인 사용
- Screen Space 좌표계 사용
- ImGui와 통합 용이
- 게임 오브젝트와 독립적인 생명주기 관리
