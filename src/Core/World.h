#pragma once

#include <unordered_map>
#include <vector>
#include <string>
#include <type_traits> // for std::is_same_v

#include "Core/Entity.h"
#include "Core/Script.h"

// 컴포넌트 헤더들
#include "Components/TransformComponent.h"
#include "Components/MaterialComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkinnedAnimationComponent.h"
#include "Components/CameraComponent.h"

namespace Alice
{
    class GameObject;

    class World
    {
    public:
        World() = default;

        void Clear();
        EntityId CreateEntity();
        void DestroyEntity(EntityId id);

        // ==== 유틸리티 ====
        GameObject FindGameObject(const std::string& name);
        void SetEntityName(EntityId id, const std::string& name);
        std::string GetEntityName(EntityId id) const;

        // ==== 제네릭 컴포넌트 관리 시스템 ====
        // 컴포넌트 타입 T에 따라 올바른 Map을 자동으로 찾아줍니다.

        /// 컴포넌트 추가 (기존 데이터가 있으면 덮어쓰거나 반환)
        /// 사용법: world.AddComponent<TransformComponent>(id).SetPosition(0,0,0);
        template <typename T, typename... Args>
        T& AddComponent(EntityId id, Args&&... args)
        {
            // 1. 유저 스크립트인 경우 (IScript 상속 여부 확인)
            if constexpr (std::is_base_of_v<IScript, T>)
            {
                // unique_ptr 생성
                auto instance = std::make_unique<T>(std::forward<Args>(args)...);

                // 반환값 저장을 위해 Raw Pointer 확보 (move 후에는 instance가 null이 됨)
                T* rawPtr = instance.get();

                // 컨테이너 생성 및 데이터 채우기
                ScriptComponent newScriptComp{};
                newScriptComp.scriptName = typeid(T).name();
                newScriptComp.instance = std::move(instance); // 소유권 이전

                // 초기화 루틴
                newScriptComp.instance->SetContext(this, id);

                // 월드 데이터에 등록 (Move)
                m_scripts[id].push_back(std::move(newScriptComp));

                // 저장해둔 포인터 반환
                return *rawPtr;
            }
            else
            {
                auto& map = GetMap<T>();
                // emplace는 키가 이미 있으면 삽입하지 않고 iterator를 반환함
                // or_insert_assign 등의 로직이 필요하면 [] 연산자 사용
                // 여기서는 깔끔하게 []로 접근하여 생성 또는 갱신
                if constexpr (std::is_default_constructible_v<T> && sizeof...(Args) == 0)
                {
                    return map[id];
                }
                else
                {
                    // 인자가 있는 경우 덮어쓰기
                    T newComp(std::forward<Args>(args)...);
                    map[id] = std::move(newComp);
                    return map[id];
                }
            }
        }

        /// 컴포넌트 가져오기 (없으면 nullptr)
        /// 사용법: auto* tr = world.GetComponent<TransformComponent>(id);
        template <typename T>
        T* GetComponent(EntityId id)
        {
            // T가 유저 스크립트인 경우. IScript를 상속받았으면 유저가 만든 스크립트임
            if constexpr (std::is_base_of_v<IScript, T>)
            {
                auto it = m_scripts.find(id);
                if (it == m_scripts.end()) return nullptr;

                // 해당 엔티티에 붙은 모든 스크립트를 순회하며 타입 검사
                for (auto& scriptComp : it->second)
                {
                    // IScript* -> MyCustomScript* 로 변환 시도
                    // dynamic_cast는 실패 시 nullptr를 반환함
                    if (scriptComp.instance)
                    {
                        T* casted = dynamic_cast<T*>(scriptComp.instance.get());
                        if (casted) return casted;
                    }
                }
                return nullptr;
            }
            else
            {
                auto& map = GetMap<T>();
                auto it = map.find(id);
                if (it == map.end()) return nullptr;
                return &it->second;
            }
        }

        /// const 버전 가져오기
        template <typename T>
        const T* GetComponent(EntityId id) const
        {
            // const_cast를 피해 const 맵을 가져오는 헬퍼 필요하지만, 
            // 간단하게 const_cast로 처리하거나 별도 GetMapConst 구현.
            // 여기선 코드 단축을 위해 const_cast 활용 (안전함)
            return const_cast<World*>(this)->GetComponent<T>(id);
        }

        /// 사용법: std::vector<MonsterScript*> list = world.GetComponents<MonsterScript>(id);
        template <typename T>
        std::vector<T*> GetComponents(EntityId id)
        {
            std::vector<T*> results;

            // 스크립트인 경우: 벡터를 순회하며 dynamic_cast 성공하는 모든 객체 수집
            if constexpr (std::is_base_of_v<IScript, T>)
            {
                auto it = m_scripts.find(id);
                if (it != m_scripts.end())
                {
                    for (auto& scriptComp : it->second)
                    {
                        if (scriptComp.instance)
                        {
                            // 부모 타입으로 요청해도 자식들을 다 찾아줍니다.
                            T* casted = dynamic_cast<T*>(scriptComp.instance.get());
                            if (casted) results.push_back(casted);
                        }
                    }
                }
            }
            // 2. 일반 엔진 컴포넌트인 경우: 1개만 있으므로 있으면 담아서 리턴
            else
            {
                T* comp = GetComponent<T>(id);
                if (comp) results.push_back(comp);
            }

            return results;
        }

        /// const 버전 GetComponents
        template <typename T>
        std::vector<const T*> GetComponents(EntityId id) const
        {
            std::vector<const T*> results;

            if constexpr (std::is_base_of_v<IScript, T>)
            {
                auto it = m_scripts.find(id);
                if (it != m_scripts.end())
                {
                    for (const auto& scriptComp : it->second)
                    {
                        if (scriptComp.instance)
                        {
                            const T* casted = dynamic_cast<const T*>(scriptComp.instance.get());
                            if (casted) results.push_back(casted);
                        }
                    }
                }
            }
            else
            {
                const T* comp = GetComponent<T>(id);
                if (comp) results.push_back(comp);
            }
            return results;
        }

        /// 컴포넌트 제거
        template <typename T>
        void RemoveComponent(EntityId id)
        {
            if constexpr (std::is_base_of_v<IScript, T>)
            {
                auto it = m_scripts.find(id);
                if (it == m_scripts.end()) return;

                auto& vec = it->second;
                for (auto iter = vec.begin(); iter != vec.end(); ++iter)
                {
                    // 타입 일치 확인
                    if (iter->instance && dynamic_cast<T*>(iter->instance.get()))
                    {
                        iter->instance->OnDisable();
                        iter->instance->OnDestroy();

                        vec.erase(iter); // 벡터에서 해당 요소 하나만 제거

                        // 비었으면 맵에서도 엔티티 키 제거
                        if (vec.empty()) m_scripts.erase(it);
                        return;
                    }
                }
            }
            else
            {
                GetMap<T>().erase(id);
            }
        }

        // 전체 맵 접근 (시스템/에디터용)
        template <typename T>
        const auto& GetComponents() const { return GetMapConst<T>(); }

        // ==== 스크립트 (특수 케이스) ====
        // 스크립트는 1개 엔티티에 여러 개가 붙을 수 있어 별도 관리 추천
        ScriptComponent& AddScript(EntityId id, const std::string& scriptName);

        /// 전체 Script 컨테이너 ScriptSystem에서 사용
        const std::unordered_map<EntityId, std::vector<ScriptComponent>>& GetAllScripts() const { return m_scripts;  }
        std::unordered_map<EntityId, std::vector<ScriptComponent>>& GetAllScripts() { return m_scripts; }

        std::vector<ScriptComponent>* GetScripts(EntityId id);
        const std::vector<ScriptComponent>* GetScripts(EntityId id) const;
        void RemoveScript(EntityId id, std::size_t index);
        void RemoveAllScript(); // Clear용

        // ==== 카메라 (특수 케이스 - 메인 카메라 등) ====
        // 필요하다면 별도 헬퍼 함수 유지
        EntityId GetMainCameraEntityId();

    private:
        // if constexpr을 사용하여 타입에 맞는 맵을 반환
        template <typename T>
        auto& GetMap()
        {
            if constexpr (std::is_same_v<T, TransformComponent>) return m_transforms;
            else if constexpr (std::is_same_v<T, MaterialComponent>) return m_materials;
            else if constexpr (std::is_same_v<T, SkinnedMeshComponent>) return m_skinnedMeshes;
            else if constexpr (std::is_same_v<T, SkinnedAnimationComponent>) return m_skinnedAnimations;
            else if constexpr (std::is_same_v<T, CameraComponent>) return m_cameras;
            else static_assert(std::is_same_v<T, void>, "지원하지 않는 컴포넌트 타입입니다.");
        }

        // const 버전 맵 반환
        template <typename T>
        const auto& GetMapConst() const
        {
            if constexpr (std::is_same_v<T, TransformComponent>) return m_transforms;
            else if constexpr (std::is_same_v<T, MaterialComponent>) return m_materials;
            else if constexpr (std::is_same_v<T, SkinnedMeshComponent>) return m_skinnedMeshes;
            else if constexpr (std::is_same_v<T, SkinnedAnimationComponent>) return m_skinnedAnimations;
            else if constexpr (std::is_same_v<T, CameraComponent>) return m_cameras;
            else static_assert(std::is_same_v<T, void>, "지원하지 않는 컴포넌트 타입입니다.");
        }

    private:
        EntityId m_nextEntityId{ 1 };

        std::unordered_map<EntityId, std::string> m_names;

        // 데이터 컨테이너들 (메모리 연속성을 위해 map<id, struct> 유지)
        std::unordered_map<EntityId, TransformComponent> m_transforms;
        std::unordered_map<EntityId, MaterialComponent> m_materials;
        std::unordered_map<EntityId, SkinnedMeshComponent> m_skinnedMeshes;
        std::unordered_map<EntityId, SkinnedAnimationComponent> m_skinnedAnimations;
        std::unordered_map<EntityId, CameraComponent> m_cameras;

        // 스크립트는 vector를 값으로 가지므로 일반 T와 구조가 달라 따로 둠
        std::unordered_map<EntityId, std::vector<ScriptComponent>> m_scripts;
    };

    template <typename T>
    T* IScript::GetComponent()
    {
        if (!m_world || m_entity == InvalidEntityId) return nullptr;
        return m_world->GetComponent<T>(m_entity);
    }

    template <typename T>
    const T* IScript::GetComponent() const
    {
        if (!m_world || m_entity == InvalidEntityId) return nullptr;
        return m_world->GetComponent<T>(m_entity);
    }

    template <typename T>
    std::vector<T*> IScript::GetComponents()
    {
        if (!m_world || m_entity == InvalidEntityId) return {};
        return m_world->GetComponents<T>(m_entity);
    }

    template <typename T, typename... Args>
    T& IScript::AddComponent(Args&&... args)
    {
        // World가 없으면 크래시가 나겠지만, 스크립트가 실행 중이라면 World는 반드시 존재해야 합니다.
        return m_world->AddComponent<T>(m_entity, std::forward<Args>(args)...);
    }

    template <typename T>
    void IScript::RemoveComponent()
    {
        if (m_world && m_entity != InvalidEntityId)
            m_world->RemoveComponent<T>(m_entity);
    }
}