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
        /// \param world        ECS 월드 (Transform 정보 조회)
        /// \param camera       카메라 (뷰/투영 행렬 및 카메라 위치)
        /// \param entity       렌더링할 엔티티 ID (Transform 필수)
        /// \param shadingMode  0: Lambert, 1: Phong, 2: Blinn-Phong
        /// \param enableFillLight 보조광 사용 여부
        void Render(const World& world,
                    const Camera& camera,
                    EntityId entity,
                    int shadingMode,
                    bool enableFillLight);

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

        /// 단순 Directional Light 2개와 재질 파라미터를 담는 구조체입니다.
        struct LightData
        {
            DirectX::XMFLOAT3 direction;
            float             pad0;

            DirectX::XMFLOAT3 color;
            float             intensity;
        };

        struct CBLighting
        {
            LightData         keyLight;
            LightData         fillLight;

            DirectX::XMFLOAT3 cameraPosition;
            float             pad1;

            DirectX::XMFLOAT4 materialDiffuse;   // rgb: 색상, a: 사용 안 함
            DirectX::XMFLOAT4 materialSpecular;  // rgb: 색상, a: shininess

            int               shadingMode;       // 0: Lambert, 1: Phong, 2: Blinn-Phong
            int               pad2[3];           // 16바이트 정렬
        };

        /// 조명/재질 파라미터를 외부에서 쉽게 조절할 수 있도록 모아둔 구조체입니다.
        struct LightingParameters
        {
            // 재질 색상/하이라이트
            DirectX::XMFLOAT3 diffuseColor  { 0.7f, 0.7f, 0.9f };
            DirectX::XMFLOAT3 specularColor { 1.0f, 1.0f, 1.0f };
            float             shininess     { 32.0f };

            // 광원 세기
            float             keyIntensity  { 1.0f };
            float             fillIntensity { 0.5f };

            // 광원 방향 (월드 기준)
            DirectX::XMFLOAT3 keyDirection  {  0.5f, -1.0f,  0.5f };
            DirectX::XMFLOAT3 fillDirection { -0.5f, -0.5f, -0.2f };
        };

    private:
        bool CreateCubeGeometry();
        bool CreateShadersAndInputLayout();
        bool CreateConstantBuffers();

        void UpdatePerObjectCB(const DirectX::XMMATRIX& world,
                               const DirectX::XMMATRIX& view,
                               const DirectX::XMMATRIX& projection);

        void UpdateLightingCB(const Camera& camera,
                              int shadingMode,
                              bool enableFillLight);

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

        LightingParameters                              m_lightingParameters;

    public:
        /// 현재 조명 파라미터(색상, 강도, Shininess 등)를 반환합니다.
        /// ImGui 등에서 이 값을 직접 수정해도 됩니다.
        LightingParameters& GetLightingParameters() { return m_lightingParameters; }
        const LightingParameters& GetLightingParameters() const { return m_lightingParameters; }
    };
}


