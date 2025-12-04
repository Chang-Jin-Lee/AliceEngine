#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

#include "Core/Entity.h"

namespace Alice
{
    class World;
    struct TransformComponent;

    /// 모든 스크립트가 상속해야 하는 기본 베이스 클래스입니다.
    /// - Unity 의 MonoBehaviour 와 비슷한 개념
    /// - World / Entity 에 접근해서 간단한 게임 로직을 작성할 수 있습니다.
    class IScript
    {
    public:
        virtual ~IScript() = default;

        /// 스크립트 이름 (디버깅용, 필요 시 오버라이드)
        virtual const char* GetName() const { return "IScript"; }

        /// 엔티티에 스크립트가 처음 붙을 때 한 번 호출됩니다.
        virtual void OnCreate(World& world, EntityId entity)
        {
            (void)world;
            (void)entity;
        }

        /// 매 프레임 호출되는 업데이트 함수입니다.
        virtual void OnUpdate(World& world, EntityId entity, float deltaTime)
        {
            (void)world;
            (void)entity;
            (void)deltaTime;
        }

        /// World / Entity 컨텍스트를 내부에 저장합니다.
        /// - World::AddScript 에서 자동으로 호출됩니다.
        void SetContext(World* world, EntityId entity) { m_world = world; m_entity = entity; }

    protected:
        /// 이 스크립트를 소유한 월드에 접근합니다.
        World* GetWorld() const { return m_world; }

        /// 소유 엔티티 ID (Unity 의 gameObject / this.Entity 느낌)
        EntityId GetOwner() const { return m_entity; }

        /// 소유 엔티티의 Transform 컴포넌트를 가져옵니다. (없으면 nullptr)
        TransformComponent* GetTransform();

        /// Unity 스타일 짧은 별칭 (transform)
        TransformComponent* transform() { return GetTransform(); }

    private:
        World*   m_world  = nullptr;
        EntityId m_entity = InvalidEntityId;
    };

    // === 간단한 리플렉션/팩토리 ===

    using ScriptCreateFunc = IScript* (*)();

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
    };

    /// 모든 ScriptComponent 를 매 프레임 업데이트하는 간단한 시스템입니다.
    class ScriptSystem
    {
    public:
        void Update(World& world, float deltaTime);
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



