#pragma once

#include <unordered_map>
#include <string>

#include <DirectXMath.h>

#include "Core/Entity.h"
#include "Core/Script.h"

namespace Alice
{
    /// 간단한 ECS 스타일의 월드(World) 구현입니다.
    /// - 엔티티 생성/삭제 책임
    /// - Transform / Script / Material 컴포넌트 관리 책임
    ///   (필요 시 다른 컴포넌트 컨테이너를 추가 확장)

    struct TransformComponent
    {
        // 위치, 회전(라디안), 스케일
        DirectX::XMFLOAT3 position { 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 rotation { 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 scale    { 1.0f, 1.0f, 1.0f };

        TransformComponent& SetPosition(float x, float y, float z)
        {
            position = DirectX::XMFLOAT3(x, y, z);
            return *this;
        }

        TransformComponent& SetScale(float x, float y, float z)
        {
            scale = DirectX::XMFLOAT3(x, y, z);
            return *this;
        }

        TransformComponent& SetRotation(float x, float y, float z)
        {
            rotation = DirectX::XMFLOAT3(x, y, z);
            return *this;
        }
    };

    /// 머티리얼 컴포넌트
    /// - 현재는 단순히 베이스 컬러만 가집니다.
    /// - 추후 메탈릭/러프니스 등 파라미터를 확장할 수 있습니다.
    struct MaterialComponent
    {
        DirectX::XMFLOAT3 color     { 0.7f, 0.7f, 0.7f };  // 베이스 색상 (albedo)
        std::string       assetPath;                      // 선택된 머티리얼 에셋 경로 (옵션)
    };

    class World
    {
    public:
        World() = default;

        /// 새로운 엔티티를 생성합니다.
        EntityId CreateEntity();

        /// 엔티티를 제거하고, 연결된 컴포넌트도 정리합니다.
        void DestroyEntity(EntityId id);

        /// Transform 컴포넌트를 추가합니다.
        TransformComponent& AddTransform(EntityId id);

        /// Transform 컴포넌트를 가져옵니다. (없으면 nullptr)
        TransformComponent* GetTransform(EntityId id);

        /// Transform 컴포넌트의 읽기 전용 포인터를 가져옵니다.
        const TransformComponent* GetTransform(EntityId id) const;

        /// 현재 등록된 Transform 컴포넌트 목록을 읽기 전용으로 반환합니다.
        /// - 에디터 하이러키 뷰에서 엔티티를 나열할 때 사용합니다.
        const std::unordered_map<EntityId, TransformComponent>& GetTransforms() const { return m_transforms; }

        // ==== Script 컴포넌트 관련 ====

        /// Script 컴포넌트를 추가합니다.
        /// \param id         대상 엔티티 ID
        /// \param scriptName ScriptFactory 에 등록된 스크립트 이름
        ScriptComponent& AddScript(EntityId id, const std::string& scriptName);

        /// Script 컴포넌트를 가져옵니다. (없으면 nullptr)
        ScriptComponent* GetScript(EntityId id);
        const ScriptComponent* GetScript(EntityId id) const;

        /// 전체 Script 컴포넌트 컨테이너 (ScriptSystem 이 사용)
        const std::unordered_map<EntityId, ScriptComponent>& GetScripts() const { return m_scripts; }

        /// Script 컴포넌트를 제거합니다.
        void RemoveScript(EntityId id);

        // ==== Material 컴포넌트 관련 ====

        /// 머티리얼 컴포넌트를 추가합니다.
        /// \param id        대상 엔티티 ID
        /// \param color     기본 베이스 컬러
        /// \param assetPath 이 머티리얼이 참조하는 에셋 경로(선택 사항)
        MaterialComponent& AddMaterial(EntityId id,
                                       const DirectX::XMFLOAT3& color,
                                       const std::string& assetPath = {});

        /// 머티리얼 컴포넌트를 가져옵니다. (없으면 nullptr)
        MaterialComponent* GetMaterial(EntityId id);
        const MaterialComponent* GetMaterial(EntityId id) const;

        /// 전체 머티리얼 컴포넌트 컨테이너 (렌더링/에디터에서 사용)
        const std::unordered_map<EntityId, MaterialComponent>& GetMaterials() const { return m_materials; }

        /// 머티리얼 컴포넌트를 제거합니다.
        void RemoveMaterial(EntityId id);

    private:
        EntityId m_nextEntityId { 1 };

        std::unordered_map<EntityId, TransformComponent> m_transforms;
        std::unordered_map<EntityId, ScriptComponent>    m_scripts;
        std::unordered_map<EntityId, MaterialComponent>  m_materials;
    };
}


