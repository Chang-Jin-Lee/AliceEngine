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
#include "UI/UIWorldManager.h"
#include "Rendering/Camera.h"
#include "Rendering/D3D11/ID3D11RenderDevice.h"
#include "Rendering/SkinnedMeshRegistry.h"
#include "Rendering/RenderTypes.h"



namespace Alice
{
    class ResourceManager;
    class TrailEffectRenderSystem;
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
        /// @param uiWorld UI 월드 매니저 (2D UI 렌더링용)
        /// @param editorMode 에디터 모드 여부
        /// @param isPlaying 재생 중 여부
        void Render(const World& world,
                    const Camera& camera,
                    EntityId entity,
                    const std::unordered_set<EntityId>& cameraEntities,
                    int shadingMode,
                    bool enableFillLight,
                    const std::vector<SkinnedDrawCommand>& skinnedCommands,
                    UIWorldManager& uiWorld,
                    bool editorMode = false,
                    bool isPlaying = false);

        /// 씬 컬러 텍스처 SRV를 반환합니다 (에디터에서 사용).
        ID3D11ShaderResourceView* GetSceneColorSRV() const { return m_sceneColorSRV.Get(); }
        std::uint32_t GetSceneWidth()  const { return m_sceneWidth; }
        std::uint32_t GetSceneHeight() const { return m_sceneHeight; }

        /// 에디터 뷰포트 표시용(톤매핑 완료) SRV
        ID3D11ShaderResourceView* GetViewportSRV() const { return m_viewportSRV.Get(); }
        
        /// Scene Depth SRV를 반환합니다 (depth test용)
        ID3D11ShaderResourceView* GetSceneDepthSRV() const { return m_sceneDepthSRV.Get(); }

        /// 이번 프레임에 실제로 사용한 카메라 View-Projection 행렬을 반환합니다 (ComputeEffect용)
        const DirectX::XMMATRIX& GetLastViewProj() const { return m_lastViewProj; }
        /// 이번 프레임에 실제로 사용한 카메라 월드 위치를 반환합니다 (ComputeEffect용)
        const DirectX::XMFLOAT3& GetLastCameraPos() const { return m_lastCameraPos; }
        float GetLastNearPlane() const { return m_lastNearPlane; }
        float GetLastFarPlane() const { return m_lastFarPlane; }

        /// IBL 세트를 변경합니다.
        bool SetIblSet(const std::string& iblDir = "Bridge", const std::string& iblName = "bridge");

        /// 스카이박스 활성화/비활성화를 설정합니다.
        void SetSkyboxEnabled(bool enabled);

        /// 배경색을 설정합니다 (스카이박스가 Off일 때 사용).
        void SetBackgroundColor(const DirectX::XMFLOAT4& color) { m_backgroundColor = color; }
        const DirectX::XMFLOAT4& GetBackgroundColor() const { return m_backgroundColor; }

        /// 톤매핑을 적용하여 HDR 씬 텍스처를 백버퍼에 렌더링합니다.
        /// @param inputSRV 입력 HDR 텍스처 SRV (씬 컬러 또는 Bloom 합성 결과)
        /// @param targetRTV 백버퍼 RTV
        /// @param viewport 뷰포트 영역
        void RenderToneMapping(ID3D11ShaderResourceView* inputSRV, ID3D11RenderTargetView* targetRTV, const D3D11_VIEWPORT& viewport);

        /// Bloom 패스를 렌더링합니다.
        /// @param sourceSRV 입력 씬 텍스처 SRV
        /// @param viewport 뷰포트 영역
        /// @note 결과는 m_postBloomSRV에 저장됩니다.
        void RenderBloomPass(ID3D11ShaderResourceView* sourceSRV, ID3D11RenderTargetView* targetRTV, const D3D11_VIEWPORT& viewport);
                
        /// 뷰포트 렌더 타겟에 파티클 오버레이 합성 (에디터 모드용)
        void RenderParticleOverlayToViewport(ID3D11ShaderResourceView* particleSRV);

        /// 포스트 프로세스 파라미터 가져오기
        void GetPostProcessParams(float& outExposure, float& outMaxHDRNits) const;
        
        /// 포스트 프로세스 파라미터 설정하기
        void SetPostProcessParams(float exposure, float maxHDRNits);

        /// Bloom 설정 가져오기
        const BloomSettings& GetBloomSettings() const { return m_bloomSettings; }
        
        /// Bloom 설정 설정하기
        void SetBloomSettings(const BloomSettings& settings);

        LightingParameters& GetLightingParameters() { return m_lightingParameters; }
        const LightingParameters& GetLightingParameters() const { return m_lightingParameters; }


		void SetSwordRenderSystem(TrailEffectRenderSystem* pSwordRenderSystem) { m_trailRenderSystem = pSwordRenderSystem; }

        /// UI 텍스처를 최종 렌더 타겟에 합성합니다.
        /// @param uiWorld UIWorldManager 참조 (UI SRV 획득용)
        /// @param targetRTV 최종 렌더 타겟 (백버퍼 또는 에디터 뷰포트)
        /// @param viewport 뷰포트 영역
        void RenderUI(UIWorldManager& uiWorld, ID3D11RenderTargetView* targetRTV, const D3D11_VIEWPORT& viewport);

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
        bool CreateCubeGeometry();
        bool CreateConstantBuffers();
        bool CreateSamplerStates();
        bool CreateBlendStates();
        bool CreateRasterizerStates();
        bool CreateDepthStencilStates();
        bool CreateIblResources(const std::string& iblDir = "Bridge", const std::string& iblName = "bridge");
        bool CreateShadowMapResources();
        bool CreateToneMappingResources(const std::uint32_t& width, const std::uint32_t& height);

        bool CreateBloomResources(const std::uint32_t& width, const std::uint32_t& height);
        bool CreatePostBloomResources(const std::uint32_t& width, const std::uint32_t& height);
        
        // 렌더링 패스
        DirectX::XMMATRIX RenderShadowPass(const World& world,
                                           const std::vector<SkinnedDrawCommand>& skinnedCommands,
                                           const std::unordered_set<EntityId>& cameraEntities,
                                           bool editorMode = false,
                                           bool isPlaying = false);
        void PassGBuffer(const World& world, 
                        const Camera& camera,
                        const std::vector<SkinnedDrawCommand>& skinnedCommands,
                        const std::unordered_set<EntityId>& cameraEntities,
                        int shadingMode,
                        bool editorMode = false,
                        bool isPlaying = false);
        void PassDeferredLight(const World& world,
                               const Camera& camera,
                               int shadingMode,
                               bool enableFillLight,
                               DirectX::CXMMATRIX lightViewProj);
        void RenderSkybox(const Camera& camera);
        // 반투명(알파 블렌딩) 오브젝트는 Deferred(GBuffer)로 정확히 합성하기 어렵기 때문에
        // 라이트 패스 이후 Forward-Style 패스로 별도 렌더링합니다.
        void PassTransparentForward(const Camera& camera,
                                    const std::vector<SkinnedDrawCommand>& skinnedCommands,
                                    int shadingMode);
        
        // 상수 버퍼 업데이트
        void UpdatePerObjectCB(const DirectX::XMMATRIX& world,
                               const DirectX::XMMATRIX& view,
                               const DirectX::XMMATRIX& projection);
        void UpdatePerObjectCB(const DirectX::XMMATRIX& world,
                               const DirectX::XMMATRIX& view,
                               const DirectX::XMMATRIX& projection,
                               const DirectX::XMFLOAT4& color,
                               float roughness,
                               float metalness,
                               bool useTexture,
                               bool enableNormalMap,
                               int shadingMode,
                               float normalStrength = 1.0f,
                               const DirectX::XMFLOAT3& outlineColor = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
                               float outlineWidth = 0.00f);
        void UpdateLightingCB(const Camera& camera,
                              int shadingMode,
                              bool enableFillLight,
                              DirectX::CXMMATRIX lightViewProj);
        void UpdateExtraLightsCB(const World& world);
        void UpdateBonesCB(const DirectX::XMFLOAT4X4* boneMatrices, std::uint32_t boneCount);
        
        // 월드 행렬 구성
        DirectX::XMMATRIX BuildWorldMatrix(const TransformComponent& transform) const;
        DirectX::XMMATRIX BuildWorldMatrix(const World& world, EntityId entityId, const TransformComponent& transform) const;
        
        // 텍스처 로딩
        ID3D11ShaderResourceView* GetOrCreateTexture(const std::string& path);
        
    private:
        ID3D11RenderDevice& m_renderDevice;
        ResourceManager*     m_resources { nullptr };
        SkinnedMeshRegistry* m_skinnedRegistry { nullptr };
        class TrailEffectRenderSystem* m_trailRenderSystem { nullptr };

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

        // ==== Transparent Forward-Style 패스 셰이더 ====
        Microsoft::WRL::ComPtr<ID3D11VertexShader>     m_transparentVS;
        Microsoft::WRL::ComPtr<ID3D11VertexShader>     m_transparentSkinnedVS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>      m_transparentPS;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>      m_transparentInputLayout;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>      m_transparentSkinnedInputLayout;

        // ==== Tone Mapping 셰이더 ====
        Microsoft::WRL::ComPtr<ID3D11PixelShader>      m_toneMappingPS;

        // ==== Bloom 셰이더 ====
        // 5단계 레벨 (0~4), 각 레벨마다 ping-pong 텍스처 2장 (A/B)
        static constexpr int BLOOM_LEVEL_COUNT = 5;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_bloomLevelSRV[BLOOM_LEVEL_COUNT][2]; // [level][A=0/B=1]
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_bloomLevelRTV[BLOOM_LEVEL_COUNT][2];
        Microsoft::WRL::ComPtr<ID3D11Texture2D>        m_bloomLevelTex[BLOOM_LEVEL_COUNT][2];
        std::uint32_t m_bloomLevelWidth[BLOOM_LEVEL_COUNT];
        std::uint32_t m_bloomLevelHeight[BLOOM_LEVEL_COUNT];

		Microsoft::WRL::ComPtr<ID3D11Texture2D>        m_postBloomTex;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_postBloomRTV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_postBloomSRV;
       
        Microsoft::WRL::ComPtr<ID3D11PixelShader>      m_bloomBrightPassPS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>      m_bloomDownsamplePS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>      m_bloomBlurPassPS_H;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>      m_bloomBlurPassPS_V;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>      m_bloomUpsamplePS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>      m_bloomCompositePS;

        // ==== Shadow pass shaders ====
        Microsoft::WRL::ComPtr<ID3D11VertexShader>     m_shadowVS;
        Microsoft::WRL::ComPtr<ID3D11VertexShader>     m_shadowSkinnedVS;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>      m_shadowInputLayout; // POSITION only

        // ==== Quad (FullScreen) 리소스 ====
        Microsoft::WRL::ComPtr<ID3D11VertexShader>     m_quadVS;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>      m_quadInputLayout;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_quadVB;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_quadIB;
        UINT                                           m_quadIndexCount = 0;
        UINT                                           m_quadStride = 0;
        UINT                                           m_quadOffset = 0;

        // ==== 큐브 지오메트리 (정적 메시) ====
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cubeVB;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cubeIB;
        UINT                                           m_cubeIndexCount = 0;

        // ==== 상수 버퍼 ====
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbPerObject;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbLighting;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbDirectionalLight;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbExtraLights;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbBones;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbPostProcess;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbBloom;
        // Transparent Forward-Style 패스용 최소 조명 CB
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbTransparentLight;
        // Shadow 전용 CB (정확한 패킹/행렬 전달용)
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbShadow;

        // ==== 씬 렌더 타겟 (최종 결과) ====
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_sceneColorTex;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>  m_sceneRTV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_sceneColorSRV;
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_sceneDepthTex;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView>  m_sceneDSV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_sceneDepthSRV;

        // ==== 에디터 뷰포트 표시용 LDR 결과 텍스처 (ToneMapped) ====
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_viewportTex;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>  m_viewportRTV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_viewportSRV;

        std::uint32_t                                   m_sceneWidth  = 0;
        std::uint32_t                                   m_sceneHeight = 0;

        // 이번 프레임에 실제로 사용한 카메라 정보 (ComputeEffect용)
        DirectX::XMMATRIX                               m_lastViewProj = DirectX::XMMatrixIdentity();
        DirectX::XMFLOAT3                                m_lastCameraPos{0, 0, 0};
        float                                           m_lastNearPlane = 0.1f;
        float                                           m_lastFarPlane = 1000.0f;

        // ==== 샘플러 상태 ====
        Microsoft::WRL::ComPtr<ID3D11SamplerState>      m_samplerState;
        Microsoft::WRL::ComPtr<ID3D11SamplerState>      m_shadowSampler;
        
        // 파티클 오버레이용
        Microsoft::WRL::ComPtr<ID3D11PixelShader>       m_particleOverlayPS;
        Microsoft::WRL::ComPtr<ID3D11BlendState>        m_ppBlendAdditive;
        Microsoft::WRL::ComPtr<ID3D11SamplerState>      m_samplerLinear;

        // ==== 블렌드 상태 ====
        Microsoft::WRL::ComPtr<ID3D11BlendState>        m_blendStateAdditive; // 라이트 패스용
        Microsoft::WRL::ComPtr<ID3D11BlendState>        m_alphaBlendState;    // 반투명 Forward 패스용

        // ==== 래스터라이저 상태 ====
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>   m_rasterizerState;
        // Shadow depth bias RS
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>   m_shadowRasterizerState;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>   m_shadowRasterizerStateReversed;
        // 아웃라인용 (Cull Front) 래스터라이저
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>   m_rsCullFront;

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
        ShadowSettings                                  m_shadowSettings {};

        // Forward와 동일한 조명/재질 파라미터 (에디터 UI 공유)
        LightingParameters                              m_lightingParameters {};
        
        // ==== 텍스처 캐시 ====
        std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> m_textureCache;

        // ==== 포스트 프로세스 파라미터 ====
        PostProcessParams m_postProcessParams;
        
        // ==== Bloom 파라미터 ====
        BloomSettings m_bloomSettings;

        // ==== UI 합성 리소스 ====
        Microsoft::WRL::ComPtr<ID3D11VertexShader>     m_uiQuadVS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>      m_uiCompositePS;
        
        bool CreateUIResources();
    };
}
