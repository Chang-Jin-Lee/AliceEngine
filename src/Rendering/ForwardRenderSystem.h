#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdint>
#include <wrl/client.h>
#include <d3d11.h>
#include <DirectXMath.h>

#include "Core/Entity.h"
#include "Core/World.h"
#include "Rendering/Camera.h"
#include "Rendering/D3D11/ID3D11RenderDevice.h"

namespace Alice
{
    /// 간단한 Forward 렌더 시스템입니다.
    /// - 큐브 1개를 그려서 Phong / Blinn-Phong 라이트를 확인할 수 있습니다.
    /// - World의 TransformComponent를 읽어와 월드 행렬을 구성합니다.
    class ForwardRenderSystem
    {
    public:
        explicit ForwardRenderSystem(ID3D11RenderDevice& renderDevice);
        ~ForwardRenderSystem() = default;

        /// 셰이더, 버퍼 등 렌더링에 필요한 리소스를 생성합니다.
        bool Initialize();

        /// 단일 엔티티(예: 큐브)를 렌더링합니다.
        /// \param world   ECS 월드 (Transform 정보 조회)
        /// \param camera  카메라 (뷰/투영 행렬 및 카메라 위치)
        /// \param entity  렌더링할 엔티티 ID (Transform 필수)
        /// \param useBlinn true면 Blinn-Phong, false면 Phong
        void Render(const World& world,
                    const Camera& camera,
                    EntityId entity,
                    bool useBlinn);

    private:
        struct SimpleVertex
        {
            DirectX::XMFLOAT3 position;
            DirectX::XMFLOAT3 normal;
        };

        struct CBPerObject
        {
            DirectX::XMMATRIX world;
            DirectX::XMMATRIX view;
            DirectX::XMMATRIX projection;
        };

        struct CBLighting
        {
            DirectX::XMFLOAT3 lightDirection;
            float             padding0; // 16바이트 정렬

            DirectX::XMFLOAT3 lightColor;
            float             padding1;

            DirectX::XMFLOAT3 cameraPosition;
            float             specularPower;

            // x: useBlinn(1.0 = Blinn, 0.0 = Phong)
            DirectX::XMFLOAT4 options;
        };

    private:
        bool CreateCubeGeometry();
        bool CreateShadersAndInputLayout();
        bool CreateConstantBuffers();

        void UpdatePerObjectCB(const DirectX::XMMATRIX& world,
                               const DirectX::XMMATRIX& view,
                               const DirectX::XMMATRIX& projection);

        void UpdateLightingCB(const Camera& camera, bool useBlinn);

        DirectX::XMMATRIX BuildWorldMatrix(const TransformComponent& transform) const;

    private:
        ID3D11RenderDevice& m_renderDevice;

        Microsoft::WRL::ComPtr<ID3D11Device>           m_device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext>    m_context;

        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_vertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_indexBuffer;
        UINT                                           m_indexCount = 0;

        Microsoft::WRL::ComPtr<ID3D11VertexShader>     m_vertexShader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>      m_pixelShader;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>      m_inputLayout;

        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbPerObject;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbLighting;
    };
}


