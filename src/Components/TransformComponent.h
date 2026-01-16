#pragma once

#include <DirectXMath.h>

// 트랜스폼
namespace Alice {
    struct TransformComponent 
    {
        // 위치, 회전(라디안), 스케일
        DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 rotation{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };

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