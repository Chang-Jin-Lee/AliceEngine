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

        /// 파티클 출력 텍스처의 SRV를 반환합니다.
        ID3D11ShaderResourceView* GetOutputSRV() const { return m_outputSRV.Get(); }

        /// 파티클 개수를 설정합니다.
        void SetParticleCount(std::uint32_t particleCount);

        // (선택) 파티클 발사 위치/색상/크기 튜닝용
        void SetEmitterNormalized(float x, float y, float radius);
        void SetParticleColor(float r, float g, float b);
        void SetParticleSizePx(float sizePx);

    private:
        bool CreateComputeShader();
        bool CreateComputeShaders();
        bool CreateConstantBuffer();
        bool CreateUnorderedAccessViews(std::uint32_t width, std::uint32_t height);
        bool CreateParticleBuffers(std::uint32_t particleCount);

        void UpdateConstantBuffer();
        void DispatchClear();
        void DispatchParticlesUpdate();
        void DispatchParticlesDraw();
        void UnbindCS();

    private:
        ID3D11RenderDevice& m_renderDevice;

        Microsoft::WRL::ComPtr<ID3D11Device>        m_device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;

        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_computeShader;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_clearShader;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_particleUpdateShader;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_particleDrawShader;
        Microsoft::WRL::ComPtr<ID3D11Buffer>        m_constantBuffer;

        // 컴퓨트 셰이더 출력용 UAV
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_outputTexture;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_outputUAV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_outputSRV;

        // 파티클 버퍼
        Microsoft::WRL::ComPtr<ID3D11Buffer>              m_particleBuffer;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_particleUAV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_particleSRV;
        std::uint32_t m_particleCount = 65536;

        // 파티클 파라미터
        DirectX::XMFLOAT4 m_params0{ 0.5f, 0.25f, 0.08f, 0.25f }; // emitterX,Y,radius,jitter
        DirectX::XMFLOAT4 m_params1{ 1.0f, 0.8f, 0.2f, 3.0f };   // color rgb, size(px)

        // 시간 관리
        LARGE_INTEGER m_qpcFreq{};
        LARGE_INTEGER m_qpcPrev{};
        float m_timeSec = 0.0f;
        float m_dtSec   = 0.0f;

        std::uint32_t m_width  = 0;
        std::uint32_t m_height = 0;
    };
}
