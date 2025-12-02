#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "Core/Entity.h"
#include "Core/World.h"

namespace Alice
{
    class ResourceManager;

    /// 모든 씬이 공통으로 구현해야 하는 최소 인터페이스입니다.
    class IScene
    {
    public:
        virtual ~IScene() = default;

        /// 이 씬의 이름(디버깅/리플렉션용) 입니다.
        virtual const char* GetName() const = 0;

        /// 씬이 활성화될 때 한 번 호출됩니다.
        virtual void OnEnter(World& world, ResourceManager& resources) { (void)world; (void)resources; }

        /// 씬이 비활성화되기 직전에 한 번 호출됩니다.
        virtual void OnExit(World& world, ResourceManager& resources) { (void)world; (void)resources; }

        /// 매 프레임 씬 로직을 갱신합니다.
        virtual void Update(World& world, ResourceManager& resources, float deltaTime) = 0;

        /// Forward 렌더링에 사용할 대표 엔티티 ID를 돌려줍니다.
        /// (필요 없으면 InvalidEntityId 반환)
        virtual EntityId GetPrimaryRenderableEntity() const { return InvalidEntityId; }
    };

    // ==== 씬 리플렉션/팩토리 ====

    using SceneCreateFunc = IScene* (*)();

    class SceneFactory
    {
    public:
        static void Register(const char* name, SceneCreateFunc func);
        static std::unique_ptr<IScene> Create(const char* name);
    };

    template <typename TScene>
    class SceneRegistrar
    {
    public:
        explicit SceneRegistrar(const char* name)
        {
            SceneFactory::Register(name, []() -> IScene*
            {
                return new TScene();
            });
        }
    };

    /// 현재 활성 씬 한 개를 관리하는 간단한 매니저입니다.
    class SceneManager
    {
    public:
        SceneManager(World& world, ResourceManager& resources);

        /// 이름으로 씬을 생성/전환합니다.
        bool SwitchTo(const char* sceneName);

        /// 현재 씬 업데이트
        void Update(float deltaTime);

        /// 현재 씬의 대표 렌더링 엔티티 ID
        EntityId GetPrimaryRenderableEntity() const;

    private:
        World&          m_world;
        ResourceManager& m_resources;
        std::unique_ptr<IScene> m_currentScene;
    };

    // 매크로로 간단하게 씬 등록을 할 수 있게 합니다.
    #define REGISTER_SCENE(SceneType) \
        static Alice::SceneRegistrar<SceneType> s_scene_registrar_##SceneType(#SceneType);
}


