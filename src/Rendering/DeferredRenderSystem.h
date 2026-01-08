#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <wrl/client.h>
#include <d3d11.h>
#include <DirectXMath.h>

#include "Core/Entity.h"
#include "Core/World.h"
#include "Rendering/Camera.h"
#include "Rendering/D3D11/ID3D11RenderDevice.h"
#include "Rendering/SkinnedMeshRegistry.h"
#include "Rendering/ForwardRenderSystem.h" // SkinnedDrawCommand 사용

namespace Alice
{
    class ResourceManager;
    
    /// 디퍼드 렌더링 시스템입니다.
    /// - G-Buffer 패스: 지오메트리 정보를 G-Buffer에 렌더링
    /// - Deferred Light 패스: G-Buffer를 읽어서 조명 계산
    /// - Post Process: Tone Mapping 등 포스트 프로세스 효과 적용
    class DeferredRenderSystem
    {
    public:
        explicit DeferredRenderSystem(ID3D11RenderDevice& renderDevice);
        ~DeferredRenderSystem() = default;

        /// 셰이더, G-Buffer 등 렌더링에 필요한 리소스를 생성합니다.
        bool Initialize(std::uint32_t width, std::uint32_t height);

        /// 뷰포트 크기가 변경되면 G-Buffer 텍스처도 함께 리사이즈합니다.
        void Resize(std::uint32_t width, std::uint32_t height);

        /// 리소스 매니저를 주입합니다.
        void SetResourceManager(ResourceManager* resources) { m_resources = resources; }

        /// 스키닝 메시 레지스트리를 주입합니다.
        void SetSkinnedMeshRegistry(SkinnedMeshRegistry* registry) { m_skinnedRegistry = registry; }

        /// 디퍼드 렌더링을 수행합니다.
        /// @param world ECS 월드
        /// @param camera 카메라
        /// @param entity (현재는 사용하지 않지만, 향후 특정 엔티티만 선택 렌더링용으로 예약)
        /// @param cameraEntities 카메라 엔티티 집합
        /// @param shadingMode 셰이딩 모드
        /// @param enableFillLight 보조광 사용 여부
        /// @param skinnedCommands 스키닝 메시 드로우 커맨드 목록
        void Render(const World& world,
                    const Camera& camera,
                    EntityId entity,
                    const std::unordered_set<EntityId>& cameraEntities,
                    int shadingMode,
                    bool enableFillLight,
                    const std::vector<ForwardRenderSystem::SkinnedDrawCommand>& skinnedCommands);

        /// 씬 컬러 텍스처 SRV를 반환합니다 (에디터에서 사용).
        ID3D11ShaderResourceView* GetSceneColorSRV() const { return m_sceneColorSRV.Get(); }
        std::uint32_t GetSceneWidth()  const { return m_sceneWidth; }
        std::uint32_t GetSceneHeight() const { return m_sceneHeight; }

        /// 에디터 뷰포트 표시용(톤매핑 완료) SRV
        ID3D11ShaderResourceView* GetViewportSRV() const { return m_viewportSRV.Get(); }

        /// IBL 세트를 변경합니다.
        bool SetIblSet(const std::string& iblDir = "Bridge", const std::string& iblName = "bridge");

        /// 스카이박스 활성화/비활성화를 설정합니다.
        void SetSkyboxEnabled(bool enabled);

        /// 배경색을 설정합니다 (스카이박스가 Off일 때 사용).
        void SetBackgroundColor(const DirectX::XMFLOAT4& color) { m_backgroundColor = color; }
        const DirectX::XMFLOAT4& GetBackgroundColor() const { return m_backgroundColor; }

        /// 톤매핑을 적용하여 HDR 씬 텍스처를 백버퍼에 렌더링합니다.
        /// @param targetRTV 백버퍼 RTV
        /// @param viewport 뷰포트 영역
        void RenderToneMapping(ID3D11RenderTargetView* targetRTV, const D3D11_VIEWPORT& viewport);

        /// 포스트 프로세스 파라미터 가져오기
        void GetPostProcessParams(float& outExposure, float& outMaxHDRNits) const;
        
        /// 포스트 프로세스 파라미터 설정하기
        void SetPostProcessParams(float exposure, float maxHDRNits);

    private:
        /// 백버퍼로 렌더 타겟을 복귀시킵니다 (ImGui 등 후처리를 위해).
        void RestoreBackBuffer();
        // G-Buffer 개수 (Position, Normal, Metalness, Roughness, BaseColor)
        static constexpr int GBufferCount = 5;

        // G-Buffer 생성
        bool CreateGBuffer(std::uint32_t width, std::uint32_t height);
        
        // 셰이더 및 리소스 생성
        bool CreateShaders();
        bool CreateQuadGeometry();
        bool CreateConstantBuffers();
        bool CreateSamplerStates();
        bool CreateBlendStates();
        bool CreateRasterizerStates();
        bool CreateDepthStencilStates();
        bool CreateIblResources(const std::string& iblDir = "Bridge", const std::string& iblName = "bridge");
        
        // 렌더링 패스
        void PassGBuffer(const World& world, 
                        const Camera& camera,
                        const std::vector<ForwardRenderSystem::SkinnedDrawCommand>& skinnedCommands,
                        const std::unordered_set<EntityId>& cameraEntities);
        void PassDeferredLight(const Camera& camera, int shadingMode, bool enableFillLight);
        void RenderSkybox(const Camera& camera);
        
        // 상수 버퍼 업데이트
        void UpdatePerObjectCB(const DirectX::XMMATRIX& world,
                               const DirectX::XMMATRIX& view,
                               const DirectX::XMMATRIX& projection);
        void UpdateLightingCB(const Camera& camera, int shadingMode, bool enableFillLight);
        void UpdateBonesCB(const DirectX::XMFLOAT4X4* boneMatrices, std::uint32_t boneCount);
        
        // 월드 행렬 구성
        DirectX::XMMATRIX BuildWorldMatrix(const TransformComponent& transform) const;
        
        // 텍스처 로딩
        ID3D11ShaderResourceView* GetOrCreateTexture(const std::string& path);
        
        // 포스트 프로세스 파라미터 구조체
        struct PostProcessParams
        {
            float exposure = 0.0f;        // Exposure 값 (기본값: 0 = 1.0배)
            float maxHDRNits = 1000.0f;   // HDR 모니터 최대 밝기 (nits)
        };

    private:
        ID3D11RenderDevice& m_renderDevice;
        ResourceManager*     m_resources { nullptr };
        SkinnedMeshRegistry* m_skinnedRegistry { nullptr };

        Microsoft::WRL::ComPtr<ID3D11Device>           m_device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext>    m_context;

        // ==== G-Buffer 리소스 ====
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_gBufferTextures[GBufferCount];
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>  m_gBufferRTVs[GBufferCount];
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_gBufferSRVs[GBufferCount];

        // ==== G-Buffer 셰이더 ====
        Microsoft::WRL::ComPtr<ID3D11VertexShader>      m_gBufferVS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>      m_gBufferPS;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>      m_gBufferInputLayout;

        // ==== 스키닝용 G-Buffer 셰이더 ====
        Microsoft::WRL::ComPtr<ID3D11VertexShader>      m_gBufferSkinnedVS;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>      m_gBufferSkinnedInputLayout;

        // ==== Deferred Light 패스 셰이더 ====
        Microsoft::WRL::ComPtr<ID3D11PixelShader>      m_deferredLightPS;

        // ==== Tone Mapping 셰이더 ====
        Microsoft::WRL::ComPtr<ID3D11PixelShader>      m_toneMappingPS;

        // ==== Quad (FullScreen) 리소스 ====
        Microsoft::WRL::ComPtr<ID3D11VertexShader>      m_quadVS;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>      m_quadInputLayout;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_quadVB;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_quadIB;
        UINT                                           m_quadIndexCount = 0;
        UINT                                           m_quadStride = 0;
        UINT                                           m_quadOffset = 0;

        // ==== 상수 버퍼 ====
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbPerObject;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbLighting;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbDirectionalLight;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbBones;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbPostProcess;

        // ==== 씬 렌더 타겟 (최종 결과) ====
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_sceneColorTex;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>  m_sceneRTV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_sceneColorSRV;
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_sceneDepthTex;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView>  m_sceneDSV;

        // ==== 에디터 뷰포트 표시용 LDR 결과 텍스처 (ToneMapped) ====
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_viewportTex;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>  m_viewportRTV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_viewportSRV;

        std::uint32_t                                   m_sceneWidth  = 0;
        std::uint32_t                                   m_sceneHeight = 0;

        // ==== 샘플러 상태 ====
        Microsoft::WRL::ComPtr<ID3D11SamplerState>      m_samplerState;
        Microsoft::WRL::ComPtr<ID3D11SamplerState>      m_shadowSampler;
        Microsoft::WRL::ComPtr<ID3D11SamplerState>      m_samplerLinear;

        // ==== 블렌드 상태 ====
        Microsoft::WRL::ComPtr<ID3D11BlendState>        m_blendStateAdditive; // 라이트 패스용

        // ==== 래스터라이저 상태 ====
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>   m_rasterizerState;

        // ==== 깊이/스텐실 상태 ====
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthStencilState;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthStencilStateReadOnly; // 라이트 패스용
        
        // ==== 톤매핑 전용 상태 객체 (Blend OFF, Depth OFF, Cull OFF) ====
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_ppDepthOff;
        Microsoft::WRL::ComPtr<ID3D11BlendState>        m_ppBlendOpaque;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>   m_ppRasterNoCull;

        // ==== IBL 리소스 ====
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_iblDiffuseSRV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_iblSpecularSRV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_iblBrdfLutSRV;
        std::string                                      m_currentIblSet;

        // ==== 스카이박스 리소스 ====
        bool                                             m_skyboxEnabled { true };
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_skyboxSRV;
        Microsoft::WRL::ComPtr<ID3D11VertexShader>       m_skyboxVS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>        m_skyboxPS;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>       m_skyboxInputLayout;
        Microsoft::WRL::ComPtr<ID3D11Buffer>            m_cbSkybox;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState>  m_skyboxDepthState;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>    m_skyboxRasterizerState;
        DirectX::XMFLOAT4                                m_backgroundColor { 0.1f, 0.1f, 0.1f, 1.0f };

        // ==== 섀도우 맵 리소스 (ForwardRenderSystem과 공유 가능하도록 설계) ====
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_shadowTex;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView>  m_shadowDSV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_shadowSRV;
        D3D11_VIEWPORT                                  m_shadowViewport {};
        
        // ==== 텍스처 캐시 ====
        std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> m_textureCache;

        // ==== 기본 재질 ====
        struct DefaultMaterial
        {
            DirectX::XMFLOAT4 baseColor { 1.0f, 1.0f, 1.0f, 1.0f };
            float metalness = 0.0f;
            float roughness = 0.5f;
            float ambientOcclusion = 1.0f;
        } m_defaultMaterial;

        // ==== 포스트 프로세스 파라미터 ====
        PostProcessParams m_postProcessParams;
    };
}

