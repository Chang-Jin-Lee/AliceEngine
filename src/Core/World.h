#pragma once

#include <unordered_map>

#include <DirectXMath.h>

#include "Core/Entity.h"

namespace Alice
{
    /// 간단한 ECS 스타일의 월드(World) 구현입니다.
    /// - 엔티티 생성/삭제 책임
    /// - Transform 컴포넌트 관리 책임
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

    private:
        EntityId m_nextEntityId { 1 };

        std::unordered_map<EntityId, TransformComponent> m_transforms;
    };
}


