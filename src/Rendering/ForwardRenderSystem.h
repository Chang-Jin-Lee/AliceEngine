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
        bool Initialize(std::uint32_t width, std::uint32_t height);

        /// 뷰포트 크기가 변경되면 렌더 타깃 텍스처도 함께 리사이즈합니다.
        void Resize(std::uint32_t width, std::uint32_t height);

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
            DirectX::XMFLOAT2 texcoord;
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
            float             fillIntensity { 1.0f };

            // 광원 방향 (월드 기준)
            DirectX::XMFLOAT3 keyDirection  {  0.5f, -1.0f,  0.5f };
            DirectX::XMFLOAT3 fillDirection { -0.5f, -0.5f, -0.2f };
        };

    private:
        bool CreateCubeGeometry();
        bool CreateShadersAndInputLayout();
        bool CreateConstantBuffers();
        bool CreateTextures();
        bool CreateSamplerState();
        bool CreateRasterizerStates();

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

        // 텍스처 / 샘플러
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_diffuseSRV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_normalSRV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_specularSRV;
        Microsoft::WRL::ComPtr<ID3D11SamplerState>       m_samplerState;

        // 음수 스케일(반전 스케일)을 위한 컬링 모드 제어용 래스터라이저 상태
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>    m_rasterizerState;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>    m_rasterizerStateReversed;

        LightingParameters                              m_lightingParameters;

        // ==== 게임 뷰포트 렌더 타깃 (Scene Color) ====
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_sceneColorTex;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>  m_sceneRTV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_sceneSRV;

        // ==== 게임 뷰포트용 깊이/스텐실 ====
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_sceneDepthTex;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView>  m_sceneDSV;

        std::uint32_t                                   m_sceneWidth  = 0;
        std::uint32_t                                   m_sceneHeight = 0;

        bool CreateSceneRenderTarget(std::uint32_t width, std::uint32_t height);

    public:
        /// 현재 조명 파라미터(색상, 강도, Shininess 등)를 반환합니다.
        /// ImGui 등에서 이 값을 직접 수정해도 됩니다.
        LightingParameters& GetLightingParameters() { return m_lightingParameters; }
        const LightingParameters& GetLightingParameters() const { return m_lightingParameters; }

        /// Game 창에서 사용할 씬 컬러 텍스처 SRV
        ID3D11ShaderResourceView* GetSceneColorSRV() const { return m_sceneSRV.Get(); }
        std::uint32_t GetSceneWidth()  const { return m_sceneWidth; }
        std::uint32_t GetSceneHeight() const { return m_sceneHeight; }
    };
}


