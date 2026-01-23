#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdint>
#include <unordered_map>
#include <string>
#include <wrl/client.h>
#include <d3d11.h>
#include <DirectXMath.h>

#include "Core/World.h"
#include "Components/TransformComponent.h"
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
        /// \param viewProj View * Projection 행렬
        /// \param cameraPos 카메라 월드 위치
        /// \param sceneDepthSRV Scene Depth SRV (depth test용, nullptr 가능)
        /// \param dtSec 델타 타임 (초)
        void Execute(const World& world, const DirectX::XMMATRIX& viewProj, const DirectX::XMFLOAT3& cameraPos, ID3D11ShaderResourceView* sceneDepthSRV, float dtSec);

        /// 파티클 출력 텍스처의 SRV를 반환합니다.
        ID3D11ShaderResourceView* GetOutputSRV() const { return m_outputSRV.Get(); }
        
        /// 이번 프레임에 활성화된 이펙트가 있었는지 반환합니다.
        bool HasActiveEffect() const;

        /// 파티클 개수를 설정합니다.
        void SetParticleCount(std::uint32_t particleCount);

        // (선택) 파티클 발사 위치/색상/크기 튜닝용
        void SetEmitterNormalized(float x, float y, float radius);
        void SetParticleColor(float r, float g, float b);
        void SetParticleSizePx(float sizePx);
        
        /// 등록된 파티클 셰이더 이름 목록을 반환합니다 (인스펙터 UI용)
        std::vector<std::string> GetRegisteredShaderNames() const;

    private:
        bool CreateComputeShader();
        bool CreateComputeShaders();
        bool CreateConstantBuffer();
        bool CreateUnorderedAccessViews(std::uint32_t width, std::uint32_t height);
        bool CreateParticleBuffers(std::uint32_t particleCount);
        bool CreateLinearSampler();
        bool CreateDummyDepthTexture();

        void UpdateConstantBuffer();
        void DispatchClear(ID3D11ComputeShader* clearShader);
        void DispatchParticlesUpdate(ID3D11ComputeShader* updateShader);
        void DispatchParticlesDraw(ID3D11ComputeShader* drawShader, ID3D11ShaderResourceView* sceneDepthSRV);
        void UnbindCS();
        
        // 파티클 셰이더 세트 등록 (타입별)
        bool RegisterParticleShaderSet(const std::string& name, 
                                       const char* clearCS, 
                                       const char* updateCS, 
                                       const char* drawCS);

    private:
        ID3D11RenderDevice& m_renderDevice;

        Microsoft::WRL::ComPtr<ID3D11Device>        m_device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;

        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_computeShader;
        
        // 파티클 셰이더 세트 (타입별로 관리)
        struct ParticleShaderSet
        {
            Microsoft::WRL::ComPtr<ID3D11ComputeShader> clearShader;
            Microsoft::WRL::ComPtr<ID3D11ComputeShader> updateShader;
            Microsoft::WRL::ComPtr<ID3D11ComputeShader> drawShader;
        };
        std::unordered_map<std::string, ParticleShaderSet> m_particleShaderSets;
        Microsoft::WRL::ComPtr<ID3D11Buffer>        m_constantBuffer;

        // 컴퓨트 셰이더 출력용 UAV
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_outputTexture;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_outputUAV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_outputSRV;
        
        // Linear Sampler State (실제로는 Point 샘플러 - depth는 Point 샘플링이 정확함)
        Microsoft::WRL::ComPtr<ID3D11SamplerState> m_linearSampler;

        // 더미 depth 텍스처 (depthSRV가 nullptr일 때 사용)
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_dummyDepthTexture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_dummyDepthSRV;

        // 파티클 버퍼
        Microsoft::WRL::ComPtr<ID3D11Buffer>              m_particleBuffer;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_particleUAV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_particleSRV;
        std::uint32_t m_particleCount = 65536;

        // 파티클 파라미터
        DirectX::XMFLOAT4 m_params0{ 0.0f, 0.0f, 0.0f, 0.1f };  // emitterX,Y,Z,radius (월드 좌표)
        DirectX::XMFLOAT4 m_params1{ 1.0f, 1.0f, 0.0f, 5.0f };   // color rgb (밝은 노란색), size(px) (더 큰 크기)
        DirectX::XMMATRIX m_viewProj{};  // View * Projection 행렬
        DirectX::XMFLOAT3 m_cameraPos{};  // 카메라 월드 위치

        // 이펙트 활성화 상태 플래그
        bool m_hasActiveEffect = false;

        // 시간 관리
        float m_timeSec = 0.0f;
        float m_dtSec = 0.0f;

        std::uint32_t m_width  = 0;
        std::uint32_t m_height = 0;
    };
}
