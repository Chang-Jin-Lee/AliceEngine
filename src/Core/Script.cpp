#include "Core/Script.h"

#include "Core/World.h"
#include "Core/GameObject.h"
#include "Core/InputSystem.h"
#include "Core/Scene.h"
#include "Core/SceneFile.h"
#include "Rendering/SkinnedMeshRegistry.h"
#include "Logger.h"

namespace Alice
{
    // === IScript 기본 헬퍼 구현 ===

    TransformComponent* IScript::GetTransform()
    {
        if (!m_world || m_entity == InvalidEntityId)
            return nullptr;

        return m_world->GetTransform(m_entity);
    }

    GameObject IScript::gameObject() const
    {
        return GameObject(m_world, m_entity, m_services);
    }

    namespace
    {
        // 전역 스크립트 레지스트리 (간단한 이름 → 생성 함수 매핑)
        std::unordered_map<std::string, ScriptCreateFunc>& GetScriptRegistry()
        {
            static std::unordered_map<std::string, ScriptCreateFunc> s_registry;
            return s_registry;
        }

        // 동적 스크립트 DLL (라이브 코딩)에서 제공하는 함수 포인터들
        DynamicScriptCreateFunc   g_DynCreate  = nullptr;
        DynamicScriptCountFunc    g_DynCount   = nullptr;
        DynamicScriptGetNameFunc  g_DynGetName = nullptr;
    }

    // === ScriptFactory 구현 및 동적 스크립트 함수 ===

    void SetDynamicScriptFunctions(DynamicScriptCreateFunc   createFn,
                                   DynamicScriptCountFunc    countFn,
                                   DynamicScriptGetNameFunc  getNameFn)
    {
        g_DynCreate  = createFn;
        g_DynCount   = countFn;
        g_DynGetName = getNameFn;
    }

    void ScriptFactory::Register(const char* name, ScriptCreateFunc func)
    {
        if (!name || !func)
            return;

        auto& registry = GetScriptRegistry();
        registry[name] = func;
    }

    std::unique_ptr<IScript> ScriptFactory::Create(const char* name)
    {
        if (!name)
            return nullptr;

        // 1) 정적(내장) 스크립트 레지스트리에서 먼저 찾습니다.
        auto& registry = GetScriptRegistry();
        auto  it       = registry.find(name);
        if (it != registry.end())
        {
            IScript* raw = it->second();
            return std::unique_ptr<IScript>(raw);
        }

        // 2) 동적 스크립트 DLL 이 있다면, 그쪽에서 생성 시도
        if (g_DynCreate)
        {
            IScript* raw = g_DynCreate(name);
            if (raw)
            {
                return std::unique_ptr<IScript>(raw);
            }
        }

        return nullptr;
    }

    std::vector<std::string> ScriptFactory::GetRegisteredScriptNames()
    {
        std::vector<std::string> result;

        // 1) 정적(내장) 스크립트들
        auto& registry = GetScriptRegistry();
        result.reserve(registry.size());

        for (const auto& [name, _] : registry)
        {
            result.push_back(name);
        }

        // 2) 동적 스크립트 DLL 이 제공하는 스크립트들
        if (g_DynCount && g_DynGetName)
        {
            const int count = g_DynCount();
            for (int i = 0; i < count; ++i)
            {
                char buffer[128] = {};
                if (g_DynGetName(i, buffer, static_cast<int>(sizeof(buffer))))
                {
                    result.emplace_back(buffer);
                }
            }
        }

        return result;
    }

    // === ScriptSystem 구현 ===

    void ScriptSystem::SetServices(InputSystem* input,
                                   SceneManager* scenes,
                                   ResourceManager* resources,
                                   SkinnedMeshRegistry* skinnedRegistry)
    {
        m_input = input;
        m_scenes = scenes;
        m_resources = resources;
        m_skinnedRegistry = skinnedRegistry;

        m_services.input = this;
        m_services.scene = this;
        m_services.skinnedRegistry = m_skinnedRegistry;
        m_services.resources = m_resources;
    }

    void ScriptSystem::BeginInputFrame()
    {
        m_prevKeys = m_currKeys;
        m_currKeys = {};

        auto toDx = [](KeyCode k) -> DirectX::Keyboard::Keys
        {
            using K = DirectX::Keyboard::Keys;
            switch (k)
            {
            case KeyCode::Alpha1: return K::D1;
            case KeyCode::Alpha2: return K::D2;
            case KeyCode::Alpha3: return K::D3;
            case KeyCode::F1:     return K::F1;
            case KeyCode::F2:     return K::F2;
            case KeyCode::Space:  return K::Space;
            default:              return K::None;
            }
        };

        auto snap = [&](KeyCode k, bool& out)
        {
            if (!m_input) { out = false; return; }
            const auto dx = toDx(k);
            out = (dx != DirectX::Keyboard::Keys::None) ? m_input->IsKeyDown(dx) : false;
        };

        snap(KeyCode::Alpha1, m_currKeys.k1);
        snap(KeyCode::Alpha2, m_currKeys.k2);
        snap(KeyCode::Alpha3, m_currKeys.k3);
        snap(KeyCode::F1,     m_currKeys.f1);
        snap(KeyCode::F2,     m_currKeys.f2);
        snap(KeyCode::Space,  m_currKeys.sp);
    }

    bool ScriptSystem::GetKeyInternal(KeyCode key) const
    {
        switch (key)
        {
        case KeyCode::Alpha1: return m_currKeys.k1;
        case KeyCode::Alpha2: return m_currKeys.k2;
        case KeyCode::Alpha3: return m_currKeys.k3;
        case KeyCode::F1:     return m_currKeys.f1;
        case KeyCode::F2:     return m_currKeys.f2;
        case KeyCode::Space:  return m_currKeys.sp;
        default:              return false;
        }
    }

    bool ScriptSystem::GetKey(KeyCode key) const { return GetKeyInternal(key); }
    bool ScriptSystem::GetKeyDown(KeyCode key) const
    {
        const bool now = GetKeyInternal(key);
        bool prev = false;
        switch (key)
        {
        case KeyCode::Alpha1: prev = m_prevKeys.k1; break;
        case KeyCode::Alpha2: prev = m_prevKeys.k2; break;
        case KeyCode::Alpha3: prev = m_prevKeys.k3; break;
        case KeyCode::F1:     prev = m_prevKeys.f1; break;
        case KeyCode::F2:     prev = m_prevKeys.f2; break;
        case KeyCode::Space:  prev = m_prevKeys.sp; break;
        default:              prev = false; break;
        }
        return now && !prev;
    }
    bool ScriptSystem::GetKeyUp(KeyCode key) const
    {
        const bool now = GetKeyInternal(key);
        bool prev = false;
        switch (key)
        {
        case KeyCode::Alpha1: prev = m_prevKeys.k1; break;
        case KeyCode::Alpha2: prev = m_prevKeys.k2; break;
        case KeyCode::Alpha3: prev = m_prevKeys.k3; break;
        case KeyCode::F1:     prev = m_prevKeys.f1; break;
        case KeyCode::F2:     prev = m_prevKeys.f2; break;
        case KeyCode::Space:  prev = m_prevKeys.sp; break;
        default:              prev = false; break;
        }
        return !now && prev;
    }

    std::string ScriptSystem::GetResolvedPath(const char* filename) const
    {
        if (!filename || !filename[0])
            return "";

        std::string path = filename;

        // 만약 입력값에 이미 경로나 슬래시가 포함되어 있다면 그대로 쓸 수도 있겠지만,
        // 여기서는 요청하신 대로 "파일명만 들어온다"고 가정하고 무조건 경로를 붙입니다.
        if (m_editorMode)
        {
            // 에디터 실행 중: 실행 파일 위치 기준 한 단계 상위의 원본 소스 폴더 참조
            // 예: "../Assets/Scenes/Stage1.scene"
            return "../Assets/Scenes/" + path;
        }
        else
        {
            // 빌드된 게임 실행 중: 실행 파일 옆의 배포된 폴더 참조
            // 예: "Assets/Scenes/Stage1.scene"
            return "Assets/Scenes/" + path;
        }
    }

    void ScriptSystem::SwitchTo(const char* sceneName)
    {
        m_pendingSwitch = GetResolvedPath(sceneName);
    }

    void ScriptSystem::LoadSceneFile(const char* scenePathUtf8)
    {
        m_pendingSceneFile = GetResolvedPath(scenePathUtf8);
    }

    void ScriptSystem::EnsureServicesBound(World& world)
    {
        for (auto& [entityId, comp] : world.GetScripts())
        {
            if (!comp.instance) continue;
            comp.instance->SetContext(&world, entityId);
            comp.instance->SetServices(&m_services);
        }
    }

    void ScriptSystem::CallFixedUpdate(World& world, float fixedDt)
    {
        for (auto& [entityId, comp] : world.GetScripts())
        {
            if (!comp.instance || !comp.enabled) continue;
            comp.instance->FixedUpdate(fixedDt);
        }
    }

    void ScriptSystem::CallLateUpdate(World& world, float deltaTime)
    {
        for (auto& [entityId, comp] : world.GetScripts())
        {
            if (!comp.instance || !comp.enabled) continue;
            comp.instance->LateUpdate(deltaTime);
        }
    }

    void ScriptSystem::ProcessSceneRequests(World& world)
    {
        if (m_pendingSwitch.empty() && m_pendingSceneFile.empty())
            return;

        // 1) 코드 씬 전환
        if (m_scenes)
        {
            if (!m_pendingSwitch.empty())
            {
                const std::string name = std::exchange(m_pendingSwitch, {});
                if (!m_scenes->SwitchTo(name.c_str()))
                    ALICE_LOG_WARN("ScriptSystem: SceneManager::SwitchTo failed. name=\"%s\"", name.c_str());
            }
        }

        // 2) .scene 파일 로드
        {
            if (!m_pendingSceneFile.empty())
            {
                const std::string path = std::exchange(m_pendingSceneFile, {});
                const bool ok = SceneFile::Load(world, std::filesystem::path(path));
                if (!ok)
                    ALICE_LOG_ERRORF("ScriptSystem: SceneFile::Load failed. path=\"%s\"", path.c_str());
                else if (m_afterSceneLoaded)
                    m_afterSceneLoaded();
            }
        }
    }

    void ScriptSystem::Tick(World& world, float deltaTime)
    {
        BeginInputFrame();
        EnsureServicesBound(world);

        // Awake/OnEnable/Start/Update
        for (auto& [entityId, comp] : world.GetScripts())
        {
            if (!comp.instance)
                continue;

            comp.instance->SetContext(&world, entityId);
            comp.instance->SetServices(&m_services);

            if (!comp.awoken)
            {
                comp.awoken = true;
                comp.wasEnabled = comp.enabled;

                comp.instance->Awake();
                comp.instance->OnCreate(world, entityId); // 구 버전 호환

                if (comp.enabled)
                    comp.instance->OnEnable();
            }

            if (comp.enabled != comp.wasEnabled)
            {
                if (comp.enabled) comp.instance->OnEnable();
                else              comp.instance->OnDisable();
                comp.wasEnabled = comp.enabled;
            }

            if (!comp.enabled)
                continue;

            if (!comp.started)
            {
                comp.started = true;
                comp.instance->Start();
            }

            comp.instance->Update(deltaTime);
            comp.instance->OnUpdate(world, entityId, deltaTime); // 구 버전 호환
        }

        // FixedUpdate
        m_fixedAcc += deltaTime;
        while (m_fixedAcc >= m_fixedDt)
        {
            CallFixedUpdate(world, m_fixedDt);
            m_fixedAcc -= m_fixedDt;
        }

        // LateUpdate
        CallLateUpdate(world, deltaTime);

        // 씬 요청은 프레임 끝에 반영
        ProcessSceneRequests(world);
    }

    void ScriptSystem::OnApplicationQuit(World& world)
    {
        EnsureServicesBound(world);
        for (auto& [entityId, comp] : world.GetScripts())
        {
            if (!comp.instance) continue;
            comp.instance->OnApplicationQuit();
        }
    }
}



