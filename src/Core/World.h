#pragma once

#include <unordered_map>
#include <vector>
#include <string>
#include <type_traits> // for std::is_same_v
#include <cstdint>

#include "Core/Entity.h"
#include "Core/IScript.h"
#include "Components/ScriptComponent.h"

// 컴포넌트 헤더들
#include "Components/ComponentStorage.h"
#include "Components/TransformComponent.h"
#include "Components/MaterialComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkinnedAnimationComponent.h"
#include "Components/CameraComponent.h"

// 물리 컴포넌트
#include "PhysX/Components/PhysicsSceneSettingsComponent.h"

class IPhysicsWorld; // 물리 인터페이스 전방선언

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

        // ==== 게임 오브젝트 생성 헬퍼 ====
        /// 빈 게임 오브젝트를 생성합니다 (Transform만 가짐)
        EntityId CreateEmpty();
        
        /// 큐브 게임 오브젝트를 생성합니다 (Transform + Material)
        EntityId CreateCube();
        
        /// 카메라 게임 오브젝트를 생성합니다 (Transform + Camera)
        EntityId CreateCamera();

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
                auto& storage = GetStorage<T>();
                if constexpr (std::is_default_constructible_v<T> && sizeof...(Args) == 0)
                {
                    // 기본 생성자만 호출
                    T defaultComp{};
                    return storage.Add(id, std::move(defaultComp));
                }
                else
                {
                    // 인자가 있는 경우 생성 후 추가
                    T newComp(std::forward<Args>(args)...);
                    return storage.Add(id, std::move(newComp));
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
                auto& storage = GetStorage<T>();
                return storage.Get(id);
            }
        }

        /// const 버전 가져오기
        template <typename T>
        const T* GetComponent(EntityId id) const
        {
            if constexpr (std::is_base_of_v<IScript, T>)
            {
                auto it = m_scripts.find(id);
                if (it == m_scripts.end()) return nullptr;

                for (const auto& scriptComp : it->second)
                {
                    if (scriptComp.instance)
                    {
                        const T* casted = dynamic_cast<const T*>(scriptComp.instance.get());
                        if (casted) return casted;
                    }
                }
                return nullptr;
            }
            else
            {
                const auto& storage = GetStorageConst<T>();
                return storage.Get(id);
            }
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
                auto& storage = GetStorage<T>();
                storage.Remove(id);
            }
        }

        // ==== 전체 컴포넌트 순회 (시스템/에디터용) ====
        // 
        // 사용 예시 (읽기 전용):
        //   for (const auto& [entityId, transform] : world.GetComponents<TransformComponent>())
        //   {
        //       // transform은 const TransformComponent&
        //       // 연속 메모리에서 효율적으로 순회됨 (캐시 친화적)
        //   }
        //
        // 사용 예시 (수정 가능):
        //   for (auto& [entityId, transform] : world.GetComponents<TransformComponent>())
        //   {
        //       // transform은 TransformComponent&
        //       transform.position.x += 1.0f; // 수정 가능
        //   }
        //
        // 성능 최적화:
        //   - 모든 TransformComponent가 연속 메모리에 저장되어 캐시 효율 극대화
        //   - 순회 시 해시맵 조회 없이 직접 접근
        //   - O(1) 삭제로 인한 순회 중 삭제 안전성 보장
        template <typename T>
        auto GetComponents() const
        {
            if constexpr (std::is_base_of_v<IScript, T>)
            {
                // 스크립트는 별도 처리 필요 (현재 구조 유지)
                static_assert(std::is_same_v<T, void>, "스크립트는 GetComponents()로 전체 순회할 수 없습니다.");
            }
            else
            {
                const auto& storage = GetStorageConst<T>();
                return storage.GetView();
            }
        }

        // 비상수 버전 (수정 가능한 순회)
        template <typename T>
        auto GetComponents()
        {
            if constexpr (std::is_base_of_v<IScript, T>)
            {
                static_assert(std::is_same_v<T, void>, "스크립트는 GetComponents()로 전체 순회할 수 없습니다.");
            }
            else
            {
                auto& storage = GetStorage<T>();
                return storage.GetView(); // const 오버로딩으로 자동 판단
            }
        }

        // ==== 스크립트 (특수 케이스) ====
        // 스크립트는 1개 엔티티에 여러 개가 붙을 수 있어 별도 관리 추천
        ScriptComponent& AddScript(EntityId id, const std::string& scriptName);

        /// 전체 Script 컨테이너 ScriptSystem에서 사용
        const std::unordered_map<EntityId, std::vector<ScriptComponent>>& GetAllScriptsInWorld() const { return m_scripts;  }
        std::unordered_map<EntityId, std::vector<ScriptComponent>>& GetAllScriptsInWorld() { return m_scripts; }

        std::vector<ScriptComponent>* GetScripts(EntityId id);
        const std::vector<ScriptComponent>* GetScripts(EntityId id) const;
        void RemoveScript(EntityId id, std::size_t index);
        void RemoveAllScript(); // Clear용

        // ==== 카메라 (특수 케이스 - 메인 카메라 등) ====
        // 필요하다면 별도 헬퍼 함수 유지
        EntityId GetMainCameraEntityId();

        // ==== 지연 파괴 시스템 ====
        /// 지연 파괴를 예약합니다. (delay 초 후에 파괴)
        void ScheduleDelayedDestruction(EntityId id, float delay);
        
        /// 지연 파괴 시스템을 업데이트합니다. (매 프레임 호출 필요)
        void UpdateDelayedDestruction(float deltaTime);

        // ==== SlotMap 기반 유효성 검사 ====
        /// 엔티티의 현재 generation을 가져옵니다. (없으면 0)
        std::uint32_t GetEntityGeneration(EntityId id) const;
        
        /// 엔티티가 유효한지 확인합니다. (generation 비교)
        bool IsEntityValid(EntityId id, std::uint32_t generation) const;


        //==============================================================
        // 물리 씬 함수
        void SetPhysicsWorld(std::shared_ptr<IPhysicsWorld> physicsWorld);
        IPhysicsWorld* GetPhysicsWorld();
        const IPhysicsWorld* GetPhysicsWorld() const;
    private:
        std::shared_ptr<IPhysicsWorld> m_physicsWorld;
        //==============================================================

    private:
        // if constexpr을 사용하여 타입에 맞는 저장소를 반환
        template <typename T>
        auto& GetStorage()
        {
            if constexpr (std::is_same_v<T, TransformComponent>) return m_transforms;
            else if constexpr (std::is_same_v<T, MaterialComponent>) return m_materials;
            else if constexpr (std::is_same_v<T, SkinnedMeshComponent>) return m_skinnedMeshes;
            else if constexpr (std::is_same_v<T, SkinnedAnimationComponent>) return m_skinnedAnimations;
            else if constexpr (std::is_same_v<T, CameraComponent>) return m_cameras;
            else if constexpr (std::is_same_v<T, PhysicsSceneSettingsComponent>) return m_physicsSettings;
            else static_assert(std::is_same_v<T, void>, "지원하지 않는 컴포넌트 타입입니다.");
        }

        // const 버전 저장소 반환
        template <typename T>
        const auto& GetStorageConst() const
        {
            if constexpr (std::is_same_v<T, TransformComponent>) return m_transforms;
            else if constexpr (std::is_same_v<T, MaterialComponent>) return m_materials;
            else if constexpr (std::is_same_v<T, SkinnedMeshComponent>) return m_skinnedMeshes;
            else if constexpr (std::is_same_v<T, SkinnedAnimationComponent>) return m_skinnedAnimations;
            else if constexpr (std::is_same_v<T, CameraComponent>) return m_cameras;
            else if constexpr (std::is_same_v<T, PhysicsSceneSettingsComponent>) return m_physicsSettings;
            else static_assert(std::is_same_v<T, void>, "지원하지 않는 컴포넌트 타입입니다.");
        }

    private:
        EntityId m_nextEntityId{ 1 };

        std::unordered_map<EntityId, std::string> m_names;

        // Sparse Set 기반 컴포넌트 저장소들 (메모리 연속성 확보)
        ComponentStorage<TransformComponent> m_transforms;
        ComponentStorage<MaterialComponent> m_materials;
        ComponentStorage<SkinnedMeshComponent> m_skinnedMeshes;
        ComponentStorage<SkinnedAnimationComponent> m_skinnedAnimations;
        ComponentStorage<CameraComponent> m_cameras;

        ComponentStorage<PhysicsSceneSettingsComponent> m_physicsSettings;

        // 스크립트는 vector를 값으로 가지므로 일반 T와 구조가 달라 따로 둠
        std::unordered_map<EntityId, std::vector<ScriptComponent>> m_scripts;

        // 지연 파괴 시스템 (EntityId -> 남은 시간)
        std::unordered_map<EntityId, float> m_delayedDestructions;

        // SlotMap 기반 유효성 검사 (EntityId -> Generation)
        // 엔티티가 생성될 때 0으로 시작하고, 파괴될 때마다 증가합니다.
        std::unordered_map<EntityId, std::uint32_t> m_entityGenerations;
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