#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdint>
#include <wrl/client.h>
#include <d3d11.h>
#include <DirectXMath.h>

#include "Core/World.h"
#include "Rendering/D3D11/ID3D11RenderDevice.h"

namespace Alice
{
    /// 컴퓨트 셰이더 이펙트 시스템
    /// - ComputeEffectComponent를 가진 엔티티들에 컴퓨트 셰이더 이펙트를 적용합니다.
    class ComputeEffectSystem
    {
    public:
        explicit ComputeEffectSystem(ID3D11RenderDevice& renderDevice);
        ~ComputeEffectSystem();

        /// 셰이더, 버퍼 등 렌더링에 필요한 리소스를 생성합니다.
        bool Initialize(std::uint32_t width, std::uint32_t height);

        /// 뷰포트 크기가 변경되면 리소스도 함께 리사이즈합니다.
        void Resize(std::uint32_t width, std::uint32_t height);

        /// 컴퓨트 셰이더 이펙트를 실행합니다.
        /// \param world ECS 월드 (ComputeEffectComponent 조회)
        void Execute(const World& world);

    private:
        bool CreateComputeShader();
        bool CreateConstantBuffer();
        bool CreateUnorderedAccessViews(std::uint32_t width, std::uint32_t height);

    private:
        ID3D11RenderDevice& m_renderDevice;

        Microsoft::WRL::ComPtr<ID3D11Device>        m_device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;

        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_computeShader;
        Microsoft::WRL::ComPtr<ID3D11Buffer>        m_constantBuffer;

        // 컴퓨트 셰이더 출력용 UAV
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_outputTexture;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_outputUAV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_outputSRV;

        std::uint32_t m_width  = 0;
        std::uint32_t m_height = 0;
    };
}
