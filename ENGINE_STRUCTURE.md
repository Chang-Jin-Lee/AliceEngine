# AliceRenderer 엔진 구조 가이드

## 📋 목차
1. [전체 엔진 구조](#전체-엔진-구조)
2. [핵심 시스템](#핵심-시스템)
3. [ScriptsBuild 시스템](#scriptsbuild-시스템)
4. [빌드 프로세스](#빌드-프로세스)

---

## 전체 엔진 구조

### 🏗️ 계층 구조

```
AliceRenderer
├── Engine (정적 라이브러리)
│   ├── Core (ECS 기반)
│   │   ├── World (게임 오브젝트 관리)
│   │   ├── UIWorld (UI 오브젝트 관리)
│   │   ├── ScriptSystem (스크립트 라이프사이클)
│   │   ├── PhysicsSystem (물리 ECS 브릿지)
│   │   └── SceneManager (씬 로드/전환)
│   ├── Rendering
│   │   ├── ForwardRenderSystem
│   │   ├── DeferredRenderSystem
│   │   └── D3D11RenderDevice
│   ├── PhysX (물리 엔진)
│   └── UI (UI 렌더링)
│       ├── UIWorldManager (D2D 리소스 관리)
│       └── UISceneManager (씬별 UI 관리)
├── Launch (에디터 실행 파일)
├── AlicePlayer (게임 실행 파일)
└── AliceScripts.dll (동적 스크립트 라이브러리)
```

---

## 핵심 시스템

### 1. **Engine (엔진 메인 루프)**

**위치**: `src/Engine/Engine.cpp`

**역할**:
- 윈도우 생성 및 메시지 처리
- 메인 게임 루프 (`Update()` / `Render()`)
- 모든 시스템의 초기화 및 생명주기 관리

**주요 멤버** (`Engine::Impl`):
```cpp
World          m_world;              // 게임 오브젝트 ECS
UIWorldManager m_uiWorld;            // UI 오브젝트 관리
ScriptSystem   m_scriptSystem;       // 스크립트 시스템
PhysicsSystem  m_physicsSystem;      // 물리 시스템
SceneManager   m_sceneManager;       // 씬 관리자
InputSystem    m_inputSystem;        // 입력 시스템
ResourceManager m_resourceManager;   // 리소스 관리
```

**초기화 순서** (`Engine::Initialize()`):
1. PhysX 컨텍스트 초기화
2. 윈도우 생성
3. 입력 시스템 초기화
4. 렌더 디바이스 생성 (D3D11)
5. 에디터 코어 초기화 (에디터 모드일 경우)
6. 렌더 시스템 초기화 (Forward/Deferred)
7. 디버그 드로우 시스템 초기화
8. **스크립트 DLL 로드** (`ScriptHotReload_Load()`)
9. 씬 매니저 생성 및 초기 씬 로드
10. 물리 시스템 생성
11. **UIWorldManager 초기화**

**업데이트 루프** (`Engine::Update()`):
```
1. 타이머/입력 갱신
2. 씬 매니저 업데이트
3. ScriptSystem Tick (Awake/Start/Update/LateUpdate/FixedUpdate)
4. [SAFE POINT] 씬 변경 커밋 (물리 업데이트 전에!)
5. 물리 시스템 업데이트 (Game → Physics 동기화)
6. 물리 시뮬레이션 (고정 시간 스텝)
7. 물리 이벤트 처리
8. 카메라 시스템 업데이트
9. 최종 카메라 동기화
```

**렌더 루프** (`Engine::Render()`):
```
1. 에디터 UI 렌더링 (에디터 모드)
2. Forward/Deferred 렌더링
3. 스키닝 메시 렌더링
4. 디버그 드로우
5. UI 렌더링 (UIWorldManager::Render())
6. ImGui 렌더링
7. 프레임 버퍼 스왑
```

---

### 2. **World (게임 오브젝트 ECS)**

**위치**: `src/Core/World.h`, `src/Core/World.cpp`

**역할**:
- Entity-Component-System 아키텍처의 핵심
- 게임 오브젝트(`GameObject`) 및 컴포넌트 관리
- 컴포넌트 저장소 (`ComponentStorage<T>`)

**주요 기능**:
- `CreateEntity()` / `DestroyEntity()` - 엔티티 생성/삭제
- `AddComponent<T>()` / `RemoveComponent<T>()` - 컴포넌트 관리
- `GetComponent<T>()` / `GetComponents<T>()` - 컴포넌트 조회
- 물리 월드 통합 (`GetPhysicsWorld()`)

**컴포넌트 예시**:
- `TransformComponent` - 위치/회전/스케일
- `CameraComponent` - 카메라 설정
- `ScriptComponent` - 스크립트 인스턴스
- `SkinnedMeshComponent` - 스키닝 메시
- `Phy_RigidBodyComponent` - 물리 리지드바디

---

### 3. **UIWorld (UI 오브젝트 ECS)**

**위치**: `src/UI/UISceneManager.h`

**역할**:
- UI 전용 ECS 시스템
- `UIBase` (게임 오브젝트의 `GameObject`와 유사) 관리
- UI 컴포넌트 관리 (`IUIComponent` 파생)

**주요 구조**:
- `UIWorld` - UI 엔티티 및 컴포넌트 저장소
- `UISceneManager` - 씬별 UI 관리
- `UIWorldManager` - D2D 리소스 및 씬 매니저 관리

**컴포넌트 예시**:
- `UITransform` - UI 위치/회전/스케일 (필수)
- `UI_ImageComponent` - 이미지 렌더링

**Delegate 기반 컴포넌트 관리**:
- `UIBase`는 `World`의 컴포넌트 관리 기능을 Delegate로 받음
- `UIWorld::Initialize()`에서 Delegate 바인딩
- 컴포넌트 생성 시 자동으로 `Owner` 및 `OwnerID` 설정

---

### 4. **ScriptSystem (스크립트 라이프사이클)**

**위치**: `src/Core/ScriptSystem.h`, `src/Core/ScriptSystem.cpp`

**역할**:
- 모든 `ScriptComponent`의 라이프사이클 관리
- `IScript` 인터페이스 기반 스크립트 실행

**라이프사이클 순서**:
```
1. Awake()      - 컴포넌트 생성 직후 (한 번만)
2. Start()      - 첫 Update 전 (한 번만)
3. Update()     - 매 프레임
4. LateUpdate() - 모든 Update 후
5. FixedUpdate() - 고정 시간 스텝 (물리와 동기화)
6. OnDestroy()  - 컴포넌트 삭제 시
```

**서비스 제공**:
- `IScriptInput` - 입력 접근
- `IScriptScene` - 씬 전환 요청
- `ResourceManager` - 리소스 접근
- `SkinnedMeshRegistry` - 스키닝 메시 접근

**씬 변경 처리**:
- 스크립트에서 `SceneManager::SwitchTo()` 호출 시
- `ScriptSystem::HasPendingSceneRequests()`로 확인
- `Engine::Update()`의 SAFE POINT에서 커밋

---

### 5. **PhysicsSystem (물리 ECS 브릿지)**

**위치**: `src/PhysX/PhysicsSystem.h`

**역할**:
- ECS 컴포넌트와 PhysX 액터 간 동기화
- Game → Physics: Transform 변경 시 물리 액터 업데이트
- Physics → Game: 물리 시뮬레이션 결과를 Transform에 반영

**주요 기능**:
- `Update()` - Game → Physics 동기화
- `SyncPhysicsToGame()` - Physics → Game 동기화
- 이벤트 처리 (충돌, 트리거 등)

**물리 컴포넌트**:
- `Phy_SettingsComponent` - 물리 월드 설정
- `Phy_RigidBodyComponent` - 리지드바디
- `Phy_ColliderComponent` - 콜라이더
- `Phy_TerrainHeightFieldComponent` - 지형

---

### 6. **SceneManager (씬 관리)**

**위치**: `src/Core/Scene.h`, `src/Core/Scene.cpp`

**역할**:
- 씬 파일 로드/저장 (`.scene` JSON)
- 씬 전환 관리
- 프리팹 인스턴스화

**씬 파일 구조**:
- JSON 기반
- 엔티티, 컴포넌트, 프리팹 참조 저장
- RTTR을 통한 리플렉션 기반 직렬화

---

## ScriptsBuild 시스템

### 📦 개요

**ScriptsBuild**는 사용자 스크립트(`Assets/Scripts/*.cpp`)를 별도의 DLL(`AliceScripts.dll`)로 빌드하는 독립적인 CMake 프로젝트입니다.

**목적**:
- **핫 리로드**: 런타임에 스크립트를 다시 빌드하고 로드하여 게임을 재시작하지 않고도 코드 변경 반영
- **빌드 분리**: 엔진과 스크립트를 독립적으로 빌드하여 개발 속도 향상
- **RTTR 통합**: 스크립트의 RTTR 등록이 엔진에서 인식되도록 공유 DLL 사용

---

### 🗂️ 파일 구조

```
AliceRenderer/
├── ScriptsBuild/
│   └── CMakeLists.txt          # 스크립트 전용 빌드 설정
├── Assets/
│   └── Scripts/                # 사용자 스크립트 소스
│       ├── CameraController.cpp
│       ├── CameraController.h
│       ├── ScriptExports.cpp   # DLL Export 함수 정의
│       └── ...
└── build/
    └── bin/
        └── Debug/
            ├── Launch.exe
            ├── AliceScripts.dll  # 빌드된 스크립트 DLL
            └── rttr_core.dll     # RTTR 공유 DLL
```

---

### 🔧 ScriptsBuild/CMakeLists.txt 구조

#### 1. **프로젝트 설정**
```cmake
project(AliceUserScripts LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
```

#### 2. **VCPKG 경로 감지**
- 환경변수 `VCPKG_ROOT` 또는 D:/vcpkg, C:/vcpkg 자동 탐색
- 메인 프로젝트와 동일한 triplet 사용 (`x64-windows-static-md`, `x64-windows`)

#### 3. **RTTR 라이브러리 설정**
```cmake
set(BUILD_STATIC OFF CACHE BOOL "" FORCE)
set(BUILD_RTTR_DYNAMIC ON CACHE BOOL "" FORCE)
```
- **중요**: RTTR은 **shared DLL**로 빌드되어야 함
- 이유: `Launch.exe`와 `AliceScripts.dll`이 같은 RTTR registry를 공유해야 스크립트의 RTTR 등록이 엔진에서 보임

#### 4. **소스 파일 수집**
```cmake
file(GLOB_RECURSE USER_SCRIPT_SOURCES CONFIGURE_DEPENDS
    "${ALICE_ROOT}/Assets/Scripts/*.cpp"
    "${ALICE_ROOT}/Assets/Scripts/*.h"
    "${ALICE_ROOT}/Assets/Scripts/*.hpp"
)
```

#### 5. **SHARED 라이브러리 생성**
```cmake
add_library(AliceScripts SHARED
    ${USER_SCRIPT_SOURCES}
)
```

#### 6. **엔진 라이브러리 링크**
```cmake
target_link_libraries(AliceScripts
    PRIVATE
        "${ALICE_ROOT}/build/$<CONFIG>/Engine.lib"  # 정적 라이브러리
        RTTR::Core                                   # RTTR 공유 DLL
        d3d11, dxgi, ...                             # DirectX 라이브러리
)
```

#### 7. **핫 리로드 최적화**
```cmake
target_link_options(AliceScripts PRIVATE "/DEBUG:NONE")
```
- PDB 파일 잠금 방지 (DLL 언로드 시 충돌 방지)

---

### 🔄 핫 리로드 프로세스

#### 1. **에디터에서 "Reload Scripts" 버튼 클릭**

**위치**: `src/Editor/EditorCore.cpp::ReloadScripts_FromButton()`

#### 2. **CMake Configure 실행**
```cpp
std::wstring cmdConfig = L"cmd /C \"cmake -S \"";
cmdConfig += scriptsRoot.wstring();      // ScriptsBuild/
cmdConfig += L"\" -B \"";
cmdConfig += scriptsBuildDir.wstring(); // ScriptsBuild/build
cmdConfig += L"\" || pause\"";
```

#### 3. **CMake Build 실행**
```cpp
std::wstring cmdBuild = L"cmd /C \"cmake --build \"";
cmdBuild += scriptsBuildDir.wstring();
cmdBuild += L"\" --config ";
cmdBuild += kConfig;  // Debug 또는 Release
cmdBuild += L" --target AliceScripts || pause\"";
```

#### 4. **빌드된 DLL 복사**
```cpp
path builtDll = scriptsBuildDir / path(kConfig) / "AliceScripts.dll";
path targetDll = exeDir / "AliceScripts.dll";
copy_file(builtDll, targetDll, copy_options::overwrite_existing);
```

#### 5. **RTTR DLL 복사**
```cpp
path builtRttr = scriptsBuildDir / path(kConfig) / "rttr_core.dll";
copy_file(builtRttr, exeDir / "rttr_core.dll", ...);
```

#### 6. **기존 스크립트 스냅샷 및 삭제**
```cpp
std::vector<EntityReloadSnap> snaps;
SnapshotAndDestroyScripts(world, snaps);
```
- 현재 월드의 모든 `ScriptComponent`의 값(프로퍼티)을 스냅샷
- 기존 스크립트 인스턴스 삭제

#### 7. **기존 DLL 언로드**
```cpp
ScriptHotReload_Unload();
```
- `FreeLibrary()` 호출로 DLL 언로드

#### 8. **새 DLL 로드**
```cpp
ScriptHotReload_Reload();
```
- `LoadLibrary()` 호출
- `GetProcAddress()`로 Export 함수 찾기:
  - `Alice_GetDynamicScriptCount`
  - `Alice_GetDynamicScriptName`
  - `Alice_CreateDynamicScript`
- `ScriptFactory::SetDynamicScriptFunctions()`로 함수 포인터 등록

#### 9. **스크립트 인스턴스 복원**
```cpp
RestoreScripts(world, snaps);
```
- 스냅샷된 값으로 새 스크립트 인스턴스 생성
- `Awake()` / `Start()` 재호출

---

### 📤 DLL Export 함수

**위치**: `Assets/Scripts/ScriptExports.cpp`

```cpp
extern "C"
{
    // 등록된 스크립트 개수 반환
    __declspec(dllexport) int Alice_GetDynamicScriptCount()
    {
        return ScriptFactory::GetRegisteredScriptNames().size();
    }

    // 인덱스로 스크립트 이름 가져오기
    __declspec(dllexport) bool Alice_GetDynamicScriptName(int index, char* outName, int maxLen)
    {
        // ...
    }

    // 이름으로 스크립트 인스턴스 생성
    __declspec(dllexport) IScript* Alice_CreateDynamicScript(const char* name)
    {
        return ScriptFactory::Create(name).release();
    }
}
```

**역할**:
- 엔진이 DLL을 로드한 후 `GetProcAddress()`로 이 함수들을 찾음
- `ScriptFactory`의 동적 스크립트 생성 기능과 연결

---

### 🎯 스크립트 등록

**방법 1: 매크로 사용 (권장)**
```cpp
// CameraController.h
class CameraController : public IScript
{
    // ...
};

// CameraController.cpp
#include "CameraController.h"
REGISTER_SCRIPT(CameraController);  // 자동 등록
```

**방법 2: 수동 등록**
```cpp
static Alice::ScriptRegistrar<CameraController> s_registrar("CameraController");
```

**RTTR 등록** (선택):
```cpp
RTTR_REGISTRATION
{
    rttr::registration::class_<CameraController>("CameraController")
        .property("speed", &CameraController::speed)
        .property("sensitivity", &CameraController::sensitivity);
}
```

---

### ⚠️ 주의사항

1. **RTTR DLL 공유**:
   - `rttr_core.dll`은 반드시 **shared**로 빌드되어야 함
   - 실행 파일 폴더에 하나만 존재해야 함 (같은 registry 공유)

2. **핫 리로드 시 주의**:
   - 가상 함수 테이블이 변경되면 기존 인스턴스는 크래시 가능
   - 따라서 스냅샷 → 삭제 → 새 DLL 로드 → 복원 순서 필수

3. **빌드 순서**:
   - 메인 프로젝트를 먼저 빌드하여 `Engine.lib` 생성
   - 그 다음 `ScriptsBuild` 빌드

4. **PDB 잠금**:
   - `/DEBUG:NONE` 옵션으로 PDB 생성 비활성화 (핫 리로드 시 파일 잠금 방지)

---

## 빌드 프로세스

### 📝 초기 빌드 (build_msvc.cmd)

```batch
1. git submodule update --init --recursive  # 서브모듈 업데이트
2. cmake -S . -B build -G "Visual Studio 17 2022"  # 메인 프로젝트 생성
3. cmake -S ScriptsBuild -B ScriptsBuild/build -G "Visual Studio 17 2022"  # 스크립트 프로젝트 생성
```

### 🔨 개발 중 빌드

1. **메인 프로젝트 빌드**:
   - Visual Studio에서 `Engine`, `Launch` 빌드
   - 또는 `cmake --build build --config Debug`

2. **스크립트 빌드**:
   - Visual Studio에서 `AliceScripts` 프로젝트 빌드
   - 또는 `cmake --build ScriptsBuild/build --config Debug`

3. **핫 리로드**:
   - 에디터에서 "Reload Scripts" 버튼 클릭
   - 자동으로 Configure → Build → 복사 → 로드 수행

---

## 요약

### 엔진 구조
- **ECS 기반**: `World` (게임), `UIWorld` (UI)
- **시스템 분리**: ScriptSystem, PhysicsSystem, RenderSystem
- **씬 관리**: SceneManager를 통한 씬 로드/전환

### ScriptsBuild 시스템
- **독립 빌드**: 스크립트를 별도 DLL로 빌드
- **핫 리로드**: 런타임에 스크립트 재빌드 및 로드
- **RTTR 통합**: 공유 DLL을 통한 리플렉션 지원

### 핵심 흐름
1. 엔진 초기화 → 스크립트 DLL 로드
2. 씬 로드 → 스크립트 인스턴스 생성
3. 매 프레임: ScriptSystem Tick → 물리 업데이트 → 렌더링
4. 핫 리로드: 스냅샷 → DLL 언로드 → 새 DLL 로드 → 복원
