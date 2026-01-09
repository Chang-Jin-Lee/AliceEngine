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

namespace Alice
{
    class ResourceManager;
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

        /// 리소스 매니저를 주입합니다.
        /// - 텍스처 등의 로딩/쿠킹에 사용됩니다.
        void SetResourceManager(ResourceManager* resources) { m_resources = resources; }

        /// 스키닝 메시 메타데이터(서브셋/스켈레톤)를 조회하기 위한 레지스트리를 주입합니다.
        void SetSkinnedMeshRegistry(SkinnedMeshRegistry* registry) { m_skinnedRegistry = registry; }

    private:
        struct ShadowSettings
        {
            // 튜토리얼(34_ToneMapping)과 동일한 기본값 스케일
            std::uint32_t mapSizePx  = 2048;   // 섀도우맵 해상도(한 변)
            float         bias       = 0.0015f;
            float         pcfRadius  = 1.0f;   // texel 단위(0~3 권장)
            float         orthoRadius = 20.0f; // 월드 단위(씬 크기에 맞게 조절)
            bool          enabled    = true;
        };

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
            DirectX::XMFLOAT4 materialColor; // per-object 베이스 컬러

            float             roughness;     // 0~1
            float             metalness;     // 0~1
            int               useTexture;   // 0: 색만, 1: 디퓨즈 텍스처 사용
            int               enableNormalMap; // 0/1: 노말맵 사용
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

            int               shadingMode;       // 0: Lambert, 1: Phong, 2: Blinn-Phong, 3: Toon
            int               pad2[3];           // 16바이트 정렬

            DirectX::XMMATRIX lightViewProj;     // 섀도우 맵 계산용 라이트 뷰-프로젝션

            // Shadow params (34_ToneMapping 방식)
            float             shadowBias;        // 깊이 바이어스(0~)
            float             shadowMapSize;     // 섀도우맵 한 변(px)
            float             shadowPcfRadius;   // PCF 반경(texel)
            int               shadowEnabled;     // 0/1
        };

        // 스키닝용 본 행렬 상수 버퍼
        // - D3D11 상수버퍼 최대 크기(64KB)에 맞춰 1023개(= 1023 * 64B = 65472B)를 사용합니다.
        // - D3D11-AliceTutorial/31_IBL 과 동일한 스케일.
        static constexpr std::uint32_t MaxBones = 1023;
        struct CBBones
        {
            DirectX::XMMATRIX bones[MaxBones];
            std::uint32_t     boneCount { 0 };
            std::uint32_t     pad[3] { 0, 0, 0 }; // 16바이트 정렬
        };

    public:
        /// 스키닝 메시를 그리기 위한 드로우 커맨드입니다.
        struct SkinnedDrawCommand
        {
            ID3D11Buffer*     vertexBuffer { nullptr };
            ID3D11Buffer*     indexBuffer  { nullptr };
            UINT              stride       { 0 };
            UINT              indexCount   { 0 };
            UINT              startIndex   { 0 };
            INT               baseVertex   { 0 };

            DirectX::XMMATRIX world       { DirectX::XMMatrixIdentity() };
            const DirectX::XMFLOAT4X4* bones { nullptr };
            std::uint32_t     boneCount   { 0 };

            DirectX::XMFLOAT3 color       { 0.7f, 0.7f, 0.7f };
            float             roughness   { 0.5f };
            float             metalness   { 0.0f };

            // 선택적인 알베도 텍스처 경로 (.alice 단일 포맷 또는 원본 이미지 경로)
            std::string       albedoTexturePath;
            // 어떤 스키닝 메시(레지스트리 키)를 사용할지 나타내는 논리 키
            std::string       meshKey;
        };

        /// 단일 엔티티(예: 큐브)와 스키닝 메시들을 함께 렌더링합니다.
        /// \param world        ECS 월드 (Transform 정보 조회)
        /// \param camera       카메라 (뷰/투영 행렬 및 카메라 위치)
        /// \param entity       (현재는 사용하지 않지만, 향후 특정 엔티티만 선택 렌더링용으로 예약)
        /// \param shadingMode  0: Lambert, 1: Phong, 2: Blinn-Phong
        /// \param enableFillLight 보조광 사용 여부
        /// \param skinnedCommands 스키닝 메시 드로우 커맨드 목록
        void Render(const World& world,
                    const Camera& camera,
                    EntityId entity,
                    const std::unordered_set<EntityId>& cameraEntities,
                    int shadingMode,
                    bool enableFillLight,
                    const std::vector<SkinnedDrawCommand>& skinnedCommands);
    private:

        /// 조명/재질 파라미터를 외부에서 쉽게 조절할 수 있도록 모아둔 구조체입니다.
        struct LightingParameters
        {
            // 재질 색상/하이라이트 (레거시 쉐이더용)
            DirectX::XMFLOAT3 diffuseColor  { 0.7f, 0.7f, 0.9f };
            DirectX::XMFLOAT3 specularColor { 1.0f, 1.0f, 1.0f };
            float             shininess     { 32.0f };

            // PBR 재질 파라미터
            DirectX::XMFLOAT3 baseColor     { 0.3f, 0.3f, 0.3f };  // PBR Base Color (Albedo)
            float             metalness    { 0.0f };                // 0.0 = 비금속, 1.0 = 금속
            float             roughness    { 0.5f };                // 0.0 = 거울, 1.0 = 거친 표면
            float             ambientOcclusion { 1.0f };            // AO (0.0 ~ 1.0)

            // 광원 세기
            float             keyIntensity  { 1.0f };
            float             fillIntensity { 0.0f };

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
		bool CreateBlendStates();
        bool CreateRasterizerStates();

        bool CreateSkyboxResources();
        bool CreateIblResources(const std::string& iblDir = "Bridge",  const std::string& iblName = "bridge");
        bool CreateSkinnedResources();
        bool CreateToneMappingResources();

        void RenderSkybox(const Camera& camera);

        void UpdatePerObjectCB(const DirectX::XMMATRIX& world,
                               const DirectX::XMMATRIX& view,
                               const DirectX::XMMATRIX& projection,
                               const DirectX::XMFLOAT4& materialColor,
                               const float& roughness,
                               const float& metalness,
                               const bool& useTexture,
                               const bool& enableNormalMap);

        void UpdateLightingCB(const Camera& camera,
                              int shadingMode,
                              bool enableFillLight,
                              const DirectX::XMMATRIX& lightViewProj);

        void UpdateBonesCB(const DirectX::XMFLOAT4X4* boneMatrices,
                           std::uint32_t boneCount);

        DirectX::XMMATRIX BuildWorldMatrix(const TransformComponent& transform) const;

        //void GetSceneBounds(const World& world, DirectX::XMVECTOR& outFocus, float& outRadius);
       // void SetCullState(DirectX::CXMMATRIX worldM, bool isShadowPass);

        DirectX::XMMATRIX RenderShadowPass(const World& world, const std::vector<SkinnedDrawCommand>& skinnedCommands, const std::unordered_set<EntityId>& cameraEntities);
        void RenderMainPass(const World& world, const Camera& camera, int shadingMode, bool enableFillLight, DirectX::CXMMATRIX lightViewProj);

        bool IsValidPipeline() const;
        void RestoreBackBuffer();

        ID3D11ShaderResourceView* GetOrCreateTexture(const std::string& path);

    private:
        ID3D11RenderDevice& m_renderDevice;
        ResourceManager*     m_resources { nullptr };
        SkinnedMeshRegistry* m_skinnedRegistry { nullptr };

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
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbSkybox; // 스카이박스 전용 CB (DYNAMIC)
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbPostProcess; // 톤매핑용 PostProcess CB

        // 텍스처 / 샘플러
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_diffuseSRV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_normalSRV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_flatNormalSRV; // (0.5,0.5,1) 기본 노말맵
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_specularSRV;
        Microsoft::WRL::ComPtr<ID3D11SamplerState>       m_samplerState;
        Microsoft::WRL::ComPtr<ID3D11SamplerState>       m_samplerLinear; // 톤매핑용 Linear Sampler

        // 알파 블렌드용 State
        Microsoft::WRL::ComPtr<ID3D11BlendState> m_alphaBlendState;

        // 음수 스케일(반전 스케일)을 위한 컬링 모드 제어용 래스터라이저 상태
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>    m_rasterizerState;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>    m_rasterizerStateReversed;
        // 섀도우 맵 깊이 바이어스 전용 RS
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>    m_shadowRasterizerState;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>    m_shadowRasterizerStateReversed;

        // 머티리얼 전용 텍스처 캐시 (경로 -> SRV)
        std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> m_textureCache;

        LightingParameters                              m_lightingParameters;

        // ==== 스카이박스 리소스 ====
        bool                                            m_skyboxEnabled { true };
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_skyboxSRV;
        Microsoft::WRL::ComPtr<ID3D11VertexShader>       m_skyboxVS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>        m_skyboxPS;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState>  m_skyboxDepthState;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>    m_skyboxRasterizerState;
        
        // 배경색 (스카이박스가 Off일 때 사용)
        DirectX::XMFLOAT4                                m_backgroundColor { 0.1f, 0.1f, 0.1f, 1.0f };

        // ==== 포스트 프로세스 파라미터 ====
        struct PostProcessParams
        {
            float exposure = 0.0f;        // Exposure 값 (기본값: 0 = 1.0배)
            float maxHDRNits = 1000.0f;   // HDR 모니터 최대 밝기 (nits)
        } m_postProcessParams;

        // ==== IBL (Image-Based Lighting) 리소스 ====
        // - Diffuse IBL: Irradiance map (간접 난반사)
        // - Specular IBL: Prefiltered env map (거칠기별 반사)
        // - BRDF LUT: (NdotV, Roughness)에 대한 F,G 적분 평균값
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_iblDiffuseSRV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_iblSpecularSRV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_iblBrdfLutSRV;
        std::string                                      m_currentIblSet; // 현재 IBL 세트 이름 (Bridge/Indoor/Sample)

        // ==== 게임 뷰포트 렌더 타깃 (Scene Color) ====
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_sceneColorTex;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>  m_sceneRTV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_sceneSRV;

        // ==== 에디터 뷰포트 표시용 LDR 결과 텍스처 (ToneMapped) ====
        // - ImGui::Image는 HDR/톤매핑/감마를 처리하지 않으므로, 표시 전용 텍스처를 별도로 생성합니다.
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_viewportTex;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>  m_viewportRTV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_viewportSRV;

        // ==== 게임 뷰포트용 깊이/스텐실 ====
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_sceneDepthTex;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView>  m_sceneDSV;

        std::uint32_t                                   m_sceneWidth  = 0;
        std::uint32_t                                   m_sceneHeight = 0;

        bool CreateSceneRenderTarget(std::uint32_t width, std::uint32_t height);

        // ==== 섀도우 맵 리소스 (단일 Directional Light) ====
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_shadowTex;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView>  m_shadowDSV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_shadowSRV;
        Microsoft::WRL::ComPtr<ID3D11SamplerState>      m_shadowSampler;
        D3D11_VIEWPORT                                  m_shadowViewport {};
        ShadowSettings                                  m_shadowSettings {};

        bool CreateShadowMapResources();

        // ==== 스키닝 전용 리소스 ====
        Microsoft::WRL::ComPtr<ID3D11VertexShader>      m_skinnedVertexShader;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>       m_inputLayoutSkinned;
        Microsoft::WRL::ComPtr<ID3D11Buffer>            m_cbBones;

        // ==== 톤매핑 리소스 ====
        Microsoft::WRL::ComPtr<ID3D11VertexShader>      m_quadVS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>       m_toneMappingPS;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>      m_quadInputLayout;
        Microsoft::WRL::ComPtr<ID3D11Buffer>            m_quadVB;
        Microsoft::WRL::ComPtr<ID3D11Buffer>            m_quadIB;
        UINT                                           m_quadIndexCount = 0;
        UINT                                           m_quadStride = 0;
        UINT                                           m_quadOffset = 0;
        // 톤매핑 전용 상태 객체 (Blend OFF, Depth OFF, Cull OFF)
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_ppDepthOff;
        Microsoft::WRL::ComPtr<ID3D11BlendState>        m_ppBlendOpaque;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>   m_ppRasterNoCull;

    public:
        /// 스키닝 메시를 렌더링합니다.
        /// - AliceGame 의 SkinnedMeshSystem 이 만들어 준 DrawCommand 리스트를 사용합니다.
        void RenderSkinnedMeshes(const Camera& camera, const std::vector<SkinnedDrawCommand>& commands);

        /// 현재 조명 파라미터(색상, 강도, Shininess 등)를 반환합니다.
        /// ImGui 등에서 이 값을 직접 수정해도 됩니다.
        LightingParameters& GetLightingParameters() { return m_lightingParameters; }
        const LightingParameters& GetLightingParameters() const { return m_lightingParameters; }

        /// Game 창에서 사용할 씬 컬러 텍스처 SRV
        ID3D11ShaderResourceView* GetSceneColorSRV() const { return m_sceneSRV.Get(); }
        std::uint32_t GetSceneWidth()  const { return m_sceneWidth; }
        std::uint32_t GetSceneHeight() const { return m_sceneHeight; }

		ID3D11ShaderResourceView* GetSceneSRV() const { return m_sceneSRV.Get(); }

        /// 에디터 뷰포트 표시용(톤매핑 완료) SRV
        ID3D11ShaderResourceView* GetViewportSRV() const { return m_viewportSRV.Get(); }

        /// IBL 세트를 변경합니다 (Bridge/Indoor/Sample)
        /// - 씬 전환 시 호출하여 환경에 맞는 IBL을 로드합니다.
        bool SetIblSet(const std::string& iblDir = "Bridge", const std::string& iblName = "bridge");

        /// 스카이박스 활성화/비활성화를 설정합니다.
        /// - enabled가 false이면 IBL도 함께 비활성화됩니다.
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
    };
}


