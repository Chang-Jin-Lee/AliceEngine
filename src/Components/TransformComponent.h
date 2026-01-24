#pragma once

#include <DirectXMath.h>
#include "Core/Entity.h"

// 트랜스폼
namespace Alice {
    struct TransformComponent 
    {
        // 위치, 회전(라디안), 스케일
        DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 rotation{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };
        bool enabled = true;
        
        // 부모 엔티티 ID (InvalidEntityId면 부모 없음)
        EntityId parent = InvalidEntityId;

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
            rotation = DirectX::XMFLOAT3(DirectX::XMConvertToRadians(x),
                DirectX::XMConvertToRadians(y),
                DirectX::XMConvertToRadians(z));
            return *this;
        }
    };
}