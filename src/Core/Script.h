#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <array>

#include "Delegate.h"
#include "Core/Entity.h"
#include "Core/ScriptAPI.h"
#include "Logger.h"
#include <directXTK/Keyboard.h>

namespace Alice
{
    class World;
    struct TransformComponent;
    struct MaterialComponent;
    struct SkinnedMeshComponent;
    struct SkinnedAnimationComponent;
    class SceneManager;
    class ResourceManager;
    class SkinnedMeshRegistry;
    class InputSystem;
    class GameObject;

    ALICE_DECLARE_DELEGATE(FOnTrimVideoMemory);
    ALICE_DECLARE_DELEGATE(FOnAfterSceneLoaded);

    /// 모든 스크립트가 상속해야 하는 기본 베이스 클래스입니다.
    /// - Unity 의 MonoBehaviour 와 비슷한 개념
    /// - World / Entity 에 접근해서 간단한 게임 로직을 작성할 수 있습니다.
    class IScript
    {
    public:
        virtual ~IScript() = default;

        /// 스크립트 이름 (디버깅용, 필요 시 오버라이드)
        virtual const char* GetName() const { return "IScript"; }

        // ==== Component lifecycle ====
        virtual void Awake() {}
        virtual void OnEnable() {}
        virtual void Start() {}
        virtual void Update(float /*deltaTime*/) {}
        virtual void LateUpdate(float /*deltaTime*/) {}
        virtual void FixedUpdate(float /*fixedDeltaTime*/) {}
        virtual void OnDisable() {}
        virtual void OnDestroy() {}
        virtual void OnApplicationQuit() {}

        // (구 버전 호환) 기존 스크립트가 OnCreate/OnUpdate를 오버라이드해도 동작하게 둡니다.
        virtual void OnCreate(World& /*world*/, EntityId /*entity*/) {}
        virtual void OnUpdate(World& /*world*/, EntityId /*entity*/, float /*deltaTime*/) {}

        /// World / Entity 컨텍스트를 내부에 저장합니다.
        /// - World::AddScript 에서 자동으로 호출됩니다.
        void SetContext(World* world, EntityId entity) { m_world = world; m_entity = entity; }

        /// ScriptSystem 이 제공하는 서비스(입력/씬/레지스트리 등)
        void SetServices(ScriptServices* services) { m_services = services; }

    protected:
        /// 이 스크립트를 소유한 월드에 접근합니다.
        World* GetWorld() const { return m_world; }

        /// 소유 엔티티 ID (Unity 의 gameObject / this.Entity 느낌)
        EntityId GetOwner() const { return m_entity; }

        /// 소유 엔티티의 Transform 컴포넌트를 가져옵니다. (없으면 nullptr)
        TransformComponent* GetTransform();

        /// Unity 스타일 짧은 별칭 (transform)
        TransformComponent* transform() { return GetTransform(); }

        /// Unity 느낌의 게임오브젝트 핸들
        GameObject gameObject() const;

        /// 입력/씬 서비스
        IScriptInput* Input() const { return m_services ? m_services->input : nullptr; }
        IScriptScene* Scenes() const { return m_services ? m_services->scene : nullptr; }

    private:
        World*   m_world  = nullptr;
        EntityId m_entity = InvalidEntityId;
        ScriptServices* m_services = nullptr;
    };

    // === 간단한 리플렉션/팩토리 ===

    using ScriptCreateFunc = IScript* (*)();

    // 동적 스크립트 DLL(라이브 코딩용)에서 사용할 함수 포인터 타입들입니다.
    using DynamicScriptCreateFunc   = IScript* (*)(const char* name);
    using DynamicScriptCountFunc    = int (*)(void);
    using DynamicScriptGetNameFunc  = bool (*)(int index, char* outName, int maxLen);

    /// 문자열 이름으로 스크립트를 생성하는 간단한 팩토리입니다.
    /// - SceneFactory 와 동일한 패턴을 사용합니다.
    class ScriptFactory
    {
    public:
        static void Register(const char* name, ScriptCreateFunc func);

        /// 이름으로 새 스크립트 인스턴스를 생성합니다. (없으면 nullptr)
        static std::unique_ptr<IScript> Create(const char* name);

        /// 현재 등록된 스크립트 이름 목록을 반환합니다.
        static std::vector<std::string> GetRegisteredScriptNames();
    };

    /// 동적 스크립트 DLL 쪽에서 가져온 함수 포인터를 등록합니다.
    /// - createFn: 이름으로 스크립트를 생성
    /// - countFn : 등록된 스크립트 개수
    /// - getNameFn: 인덱스로 스크립트 이름 얻기
    void SetDynamicScriptFunctions(DynamicScriptCreateFunc   createFn,
                                   DynamicScriptCountFunc    countFn,
                                   DynamicScriptGetNameFunc  getNameFn);

    /// 템플릿을 이용해 간단하게 스크립트를 등록할 수 있게 합니다.
    template <typename TScript>
    class ScriptRegistrar
    {
    public:
        explicit ScriptRegistrar(const char* name)
        {
            ScriptFactory::Register(name, []() -> IScript*
            {
                return new TScript();
            });
        }
    };

    /// 한 엔티티에 붙는 단일 스크립트 컴포넌트입니다.
    /// - scriptName 은 팩토리/리플렉션용 이름입니다.
    /// - instance 는 실제 실행되는 스크립트 객체입니다.
    struct ScriptComponent
    {
        std::string                 scriptName;
        std::unique_ptr<IScript>   instance;
        bool enabled { true };
        bool awoken  { false };
        bool started { false };
        bool wasEnabled { true };
    };

    /// 모든 ScriptComponent 를 매 프레임 업데이트하는 간단한 시스템입니다.
    class ScriptSystem : public IScriptInput, public IScriptScene
    {
    public:
        void SetServices(InputSystem* input,
                         SceneManager* scenes,
                         ResourceManager* resources,
                         SkinnedMeshRegistry* skinnedRegistry);

        // Unity-style tick
        void Tick(World& world, float deltaTime);

        // 종료 시 호출
        void OnApplicationQuit(World& world);

        // === IScriptInput ===
        bool GetKey(KeyCode key) const override;
        bool GetKeyDown(KeyCode key) const override;
        bool GetKeyUp(KeyCode key) const override;

        std::string GetResolvedPath(const char* originalPath) const;

        // === IScriptScene ===
        void SwitchTo(const char* sceneName) override;
        void LoadSceneFile(const char* scenePathUtf8) override;

        // === editormode ===
        void SetEditorMode(const bool& isEditor) { m_editorMode = isEditor; }

    private:
        static DirectX::Keyboard::Keys ToDxKey(KeyCode k);

        void BeginInputFrame();
        void EnsureServicesBound(World& world);
        void CallLateUpdate(World& world, float deltaTime);
        void CallFixedUpdate(World& world, float fixedDt);
        void ProcessSceneRequests(World& world);
        bool GetKeyInternal(KeyCode key) const;

        float m_fixedDt = 0.02f;
        float m_fixedAcc = 0.0f;

        bool m_editorMode = true;

        InputSystem* m_input = nullptr;
        SceneManager* m_scenes = nullptr;
        ResourceManager* m_resources = nullptr;
        SkinnedMeshRegistry* m_skinnedRegistry = nullptr;

        ScriptServices m_services{};

        // input snapshot (KeyCode 전체)
        std::array<bool, static_cast<std::size_t>(KeyCode::Count)> m_prevKeys{};
        std::array<bool, static_cast<std::size_t>(KeyCode::Count)> m_currKeys{};

        // scene requests
        std::string m_pendingSwitch;
        std::string m_pendingSceneFile;
        
    public:
        // 씬 로드 직후 엔진 쪽에서 추가 작업
        FOnAfterSceneLoaded onAfterSceneLoaded;
        FOnTrimVideoMemory onTrimVideoMemory;
    };

    // 매크로로 간단하게 스크립트를 등록합니다.
    // 사용 예:
    //
    //   class Rotator : public IScript { ... };
    //   REGISTER_SCRIPT(Rotator);
    //
    #define REGISTER_SCRIPT(ScriptType) \
        static Alice::ScriptRegistrar<ScriptType> s_script_registrar_##ScriptType(#ScriptType);
}



