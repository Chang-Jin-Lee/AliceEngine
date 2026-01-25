#include "Rendering/DeferredRenderSystem.h"

#include <d3dcompiler.h>
#include <DirectXTK/WICTextureLoader.h>
#include <DirectXTK/DDSTextureLoader.h>
#include <filesystem>
#include <vector>
#include <cmath>
#include <cfloat>
#include <algorithm>
#include <cstring>
#include <DirectXMath.h>

#include "Core/ResourceManager.h"
#include "Core/Logger.h"
#include "Core/World.h"
#include "Components/TransformComponent.h"
#include "Components/MaterialComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/TrailEffectComponent.h"
#include "Rendering/ShaderCode/CommonShaderCode.h"
#include "Rendering/ShaderCode/DeferredShader.h"
#include "Rendering/TrailEffectRenderSystem.h"
#include <fstream>
#include <sstream>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace Alice
{

    DeferredRenderSystem::DeferredRenderSystem(ID3D11RenderDevice& renderDevice)
        : m_renderDevice(renderDevice)
    {
        m_device = renderDevice.GetDevice();
        m_context = renderDevice.GetImmediateContext();
    }

    bool DeferredRenderSystem::Initialize(std::uint32_t width, std::uint32_t height)
    {
        if (!m_device || !m_context)
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: Device or Context is null.");
            return false;
        }

        // G-Buffer 생성
        if (!CreateGBuffer(width, height))
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateGBuffer failed.");
            return false;
        }

        // 셰이더 생성
        if (!CreateShaders())
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateShaders failed.");
            return false;
        }

        // Quad 지오메트리 생성
        if (!CreateQuadGeometry())
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateQuadGeometry failed.");
            return false;
        }

        // 큐브 지오메트리 생성
        if (!CreateCubeGeometry())
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateCubeGeometry failed.");
            return false;
        }

        // 상수 버퍼 생성
        if (!CreateConstantBuffers())
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateConstantBuffers failed.");
            return false;
        }

        // 샘플러 상태 생성
        if (!CreateSamplerStates())
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateSamplerStates failed.");
            return false;
        }

        // 블렌드 상태 생성
        if (!CreateBlendStates())
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateBlendStates failed.");
            return false;
        }

        // 래스터라이저 상태 생성
        if (!CreateRasterizerStates())
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateRasterizerStates failed.");
            return false;
        }

        // 깊이/스텐실 상태 생성
        if (!CreateDepthStencilStates())
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateDepthStencilStates failed.");
            return false;
        }

        // 섀도우 맵 리소스 생성 (Deferred Light에서 PCF로 사용)
        if (!CreateShadowMapResources())
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateShadowMapResources failed.");
            return false;
        }

		if (!CreateToneMappingResources(width, height))
		{
			ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateToneMappingResources failed.");
			return false;
		}

		if (!CreateBloomResources(width, height))
		{
			ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateBloomResources failed.");
			return false;
		}
        
        if (!CreatePostBloomResources(width, height))
        {
			ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreatePostBloomResources failed.");
			return false;
        }

        // IBL 리소스 생성
        if (!CreateIblResources())
        {
            ALICE_LOG_WARN("DeferredRenderSystem::Initialize: CreateIblResources failed (optional).");
        }

        // UI 리소스 생성
        if (!CreateUIResources())
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateUIResources failed.");
            return false;
        }

        ALICE_LOG_INFO("DeferredRenderSystem::Initialize: success.");
        return true;
    }

    void DeferredRenderSystem::Resize(std::uint32_t width, std::uint32_t height)
    {
        if (!m_device) return;
        if (width == 0 || height == 0) return;

        // G-Buffer 리사이즈
        CreateGBuffer(width, height);

        // 씬 렌더 타겟 리사이즈
        m_sceneColorTex.Reset();
        m_sceneRTV.Reset();
        m_sceneColorSRV.Reset();
        m_viewportTex.Reset();
        m_viewportRTV.Reset();
        m_viewportSRV.Reset();
        m_sceneDepthTex.Reset();
        m_sceneDSV.Reset();
        m_sceneDepthSRV.Reset();

        m_sceneWidth = width;
        m_sceneHeight = height;
        D3D11_TEXTURE2D_DESC cDesc = { width, height, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (FAILED(m_device->CreateTexture2D(&cDesc, nullptr, m_sceneColorTex.ReleaseAndGetAddressOf()))) return;
        if (FAILED(m_device->CreateRenderTargetView(m_sceneColorTex.Get(), nullptr, m_sceneRTV.ReleaseAndGetAddressOf()))) return;
        if (FAILED(m_device->CreateShaderResourceView(m_sceneColorTex.Get(), nullptr, m_sceneColorSRV.ReleaseAndGetAddressOf()))) return;

        D3D11_TEXTURE2D_DESC vDesc = { width, height, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (FAILED(m_device->CreateTexture2D(&vDesc, nullptr, m_viewportTex.ReleaseAndGetAddressOf()))) return;
        if (FAILED(m_device->CreateRenderTargetView(m_viewportTex.Get(), nullptr, m_viewportRTV.ReleaseAndGetAddressOf()))) return;
        if (FAILED(m_device->CreateShaderResourceView(m_viewportTex.Get(), nullptr, m_viewportSRV.ReleaseAndGetAddressOf()))) return;

        D3D11_TEXTURE2D_DESC dDesc = { width, height, 1, 1, DXGI_FORMAT_D24_UNORM_S8_UINT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL, 0, 0 };
        if (FAILED(m_device->CreateTexture2D(&dDesc, nullptr, m_sceneDepthTex.ReleaseAndGetAddressOf()))) return;
        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = { dDesc.Format, D3D11_DSV_DIMENSION_TEXTURE2D, 0 };
        if (FAILED(m_device->CreateDepthStencilView(m_sceneDepthTex.Get(), &dsvDesc, m_sceneDSV.ReleaseAndGetAddressOf()))) return;

        // Bloom 리소스 리사이즈
        for (int level = 0; level < BLOOM_LEVEL_COUNT; ++level)
        {
            for (int pingPong = 0; pingPong < 2; ++pingPong)
            {
                m_bloomLevelTex[level][pingPong].Reset();
                m_bloomLevelRTV[level][pingPong].Reset();
                m_bloomLevelSRV[level][pingPong].Reset();
            }
        }
        CreateBloomResources(width, height);
        
        // Post-Bloom 합성 리소스 리사이즈
        m_postBloomTex.Reset();
        m_postBloomRTV.Reset();
        m_postBloomSRV.Reset();
        CreatePostBloomResources(width, height);
    }

    bool DeferredRenderSystem::CreateGBuffer(std::uint32_t width, std::uint32_t height)
    {
        // 기존 G-Buffer 해제
        for (int i = 0; i < GBufferCount; ++i)
        {
            m_gBufferSRVs[i].Reset();
            m_gBufferRTVs[i].Reset();
            m_gBufferTextures[i].Reset();
        }

        // G-Buffer 포맷 정의
        // NOTE:
        // - Shadow/IBL 등에서 월드 포지션 기반 연산(특히 ShadowMap 투영)은 정밀도에 매우 민감합니다.
        // - PositionWS를 R16F(half)로 저장하면 씬 스케일/거리에서 양자화가 커져
        //   "원점으로 찢어지는" 형태의 섀도우 아티팩트가 발생할 수 있어, Position만 R32F로 올립니다.
        DXGI_FORMAT formats[GBufferCount] = {
            DXGI_FORMAT_R32G32B32A32_FLOAT,  // 0: PositionWS (정밀도 강화)
            DXGI_FORMAT_R16G16B16A16_FLOAT,  // 1: NormalWS
            DXGI_FORMAT_R8_UNORM,             // 2: Metalness
            DXGI_FORMAT_R8_UNORM,             // 3: Roughness
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,  // 4: BaseColor
        };

        // 각 G-Buffer 텍스처 생성
        for (int i = 0; i < GBufferCount; ++i)
        {
            D3D11_TEXTURE2D_DESC td = {};
            td.Width = width;
            td.Height = height;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = formats[i];
            td.SampleDesc.Count = 1;
            td.SampleDesc.Quality = 0;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            td.CPUAccessFlags = 0;
            td.MiscFlags = 0;

            if (FAILED(m_device->CreateTexture2D(&td, nullptr, m_gBufferTextures[i].ReleaseAndGetAddressOf())))
                return false;
            if (FAILED(m_device->CreateRenderTargetView(m_gBufferTextures[i].Get(), nullptr, m_gBufferRTVs[i].ReleaseAndGetAddressOf())))
                return false;
            if (FAILED(m_device->CreateShaderResourceView(m_gBufferTextures[i].Get(), nullptr, m_gBufferSRVs[i].ReleaseAndGetAddressOf())))
                return false;
        }

        return true;
    }

    bool DeferredRenderSystem::CreateShaders()
    {
        ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;

        // G-Buffer Vertex Shader
        if (FAILED(D3DCompile(DeferredShader::GBufferVS, strlen(DeferredShader::GBufferVS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), errorBlob.GetAddressOf())))
        {
            if (errorBlob)
            {
                ALICE_LOG_ERRORF("GBuffer VS compile error: %s", (char*)errorBlob->GetBufferPointer());
            }
            return false;
        }

        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_gBufferVS.ReleaseAndGetAddressOf())))
            return false;

        // G-Buffer Input Layout
        D3D11_INPUT_ELEMENT_DESC gbufferLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}
        };
        if (FAILED(m_device->CreateInputLayout(gbufferLayout, ARRAYSIZE(gbufferLayout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_gBufferInputLayout.ReleaseAndGetAddressOf())))
            return false;

        // G-Buffer Skinned Vertex Shader
        vsBlob.Reset();
        if (FAILED(D3DCompile(DeferredShader::GBufferSkinnedVS, strlen(DeferredShader::GBufferSkinnedVS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), errorBlob.GetAddressOf())))
        {
            if (errorBlob)
            {
                ALICE_LOG_ERRORF("GBuffer Skinned VS compile error: %s", (char*)errorBlob->GetBufferPointer());
            }
            return false;
        }

        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_gBufferSkinnedVS.ReleaseAndGetAddressOf())))
            return false;

        // G-Buffer Skinned Input Layout
        // - ForwardRenderSystem 과 동일한 정점 레이아웃/오프셋을 사용해야 본 인덱스/웨이트가 깨지지 않습니다.
        //   (Deferred 쪽이 COLOR를 누락하면 TEXCOORD 이후 오프셋이 밀려 애니메이션/UV가 전부 망가질 수 있음)
        D3D11_INPUT_ELEMENT_DESC skinnedLayout[] = {
            {"POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"BINORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR",        0, DXGI_FORMAT_R32G32B32A32_FLOAT,    0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,          0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"BLENDINDICES", 0, DXGI_FORMAT_R16G16B16A16_UINT,     0, 72, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT,    0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"SMOOTHNORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 96, D3D11_INPUT_PER_VERTEX_DATA, 0}
        };
        if (FAILED(m_device->CreateInputLayout(skinnedLayout, ARRAYSIZE(skinnedLayout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_gBufferSkinnedInputLayout.ReleaseAndGetAddressOf())))
            return false;

        // Quad Vertex Shader
        vsBlob.Reset();
        if (FAILED(D3DCompile(CommonShaderCode::QuadVS, strlen(CommonShaderCode::QuadVS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), errorBlob.GetAddressOf())))
        {
            if (errorBlob)
            {
                ALICE_LOG_ERRORF("Quad VS compile error: %s", (char*)errorBlob->GetBufferPointer());
            }
            return false;
        }

        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_quadVS.ReleaseAndGetAddressOf())))
            return false;

        // Quad Input Layout
        D3D11_INPUT_ELEMENT_DESC quadLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}
        };
        if (FAILED(m_device->CreateInputLayout(quadLayout, ARRAYSIZE(quadLayout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_quadInputLayout.ReleaseAndGetAddressOf())))
            return false;

        // G-Buffer Pixel Shader 컴파일
        if (FAILED(D3DCompile(DeferredShader::GBufferPS, strlen(DeferredShader::GBufferPS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
        {
            if (errorBlob)
            {
                ALICE_LOG_ERRORF("GBuffer PS compile error: %s", (char*)errorBlob->GetBufferPointer());
            }
            return false;
        }
        if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_gBufferPS.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create G-Buffer PS");
            return false;
        }

        // Deferred Light Pixel Shader 컴파일
        psBlob.Reset();
        errorBlob.Reset();
        if (FAILED(D3DCompile(DeferredShader::LightPS, strlen(DeferredShader::LightPS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
        {
            if (errorBlob)
            {
                ALICE_LOG_ERRORF("Deferred Light PS compile error: %s", (char*)errorBlob->GetBufferPointer());
            }
            return false;
        }
        if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_deferredLightPS.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Deferred Light PS");
            return false;
        }

        // ===================== Transparent Forward-Style Shaders =====================
        // Skinned Transparent VS
        vsBlob.Reset();
        errorBlob.Reset();
        if (FAILED(D3DCompile(DeferredShader::TransparentSkinnedVS,
                              strlen(DeferredShader::TransparentSkinnedVS),
                              nullptr, nullptr, nullptr,
                              "main", "vs_5_0",
                              0, 0,
                              vsBlob.GetAddressOf(),
                              errorBlob.GetAddressOf())))
        {
            if (errorBlob)
                ALICE_LOG_ERRORF("Transparent Skinned VS compile error: %s", (char*)errorBlob->GetBufferPointer());
            return false;
        }
        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(),
                                                vsBlob->GetBufferSize(),
                                                nullptr,
                                                m_transparentSkinnedVS.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Transparent Skinned VS");
            return false;
        }

        // Transparent Skinned Input Layout (Forward와 동일 오프셋)
        {
            D3D11_INPUT_ELEMENT_DESC skinnedLayoutT[] = {
                {"POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"BINORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"COLOR",        0, DXGI_FORMAT_R32G32B32A32_FLOAT,    0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,          0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"BLENDINDICES", 0, DXGI_FORMAT_R16G16B16A16_UINT,     0, 72, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT,    0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"SMOOTHNORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 96, D3D11_INPUT_PER_VERTEX_DATA, 0}
            };
            if (FAILED(m_device->CreateInputLayout(skinnedLayoutT,
                                                   ARRAYSIZE(skinnedLayoutT),
                                                   vsBlob->GetBufferPointer(),
                                                   vsBlob->GetBufferSize(),
                                                   m_transparentSkinnedInputLayout.ReleaseAndGetAddressOf())))
            {
                ALICE_LOG_ERRORF("Failed to create Transparent Skinned InputLayout");
                return false;
            }
        }

        // Transparent PS
        psBlob.Reset();
        errorBlob.Reset();
        if (FAILED(D3DCompile(DeferredShader::TransparentPS,
                              strlen(DeferredShader::TransparentPS),
                              nullptr, nullptr, nullptr,
                              "main", "ps_5_0",
                              0, 0,
                              psBlob.GetAddressOf(),
                              errorBlob.GetAddressOf())))
        {
            if (errorBlob)
                ALICE_LOG_ERRORF("Transparent PS compile error: %s", (char*)errorBlob->GetBufferPointer());
            return false;
        }
        if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(),
                                               psBlob->GetBufferSize(),
                                               nullptr,
                                               m_transparentPS.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Transparent PS");
            return false;
        }

        // Skybox Vertex Shader 컴파일
        vsBlob.Reset();
        errorBlob.Reset();
        if (FAILED(D3DCompile(CommonShaderCode::SkyboxVS, strlen(CommonShaderCode::SkyboxVS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), errorBlob.GetAddressOf())))
        {
            if (errorBlob)
            {
                ALICE_LOG_ERRORF("Skybox VS compile error: %s", (char*)errorBlob->GetBufferPointer());
            }
            return false;
        }
        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_skyboxVS.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Skybox VS");
            return false;
        }
        D3D11_INPUT_ELEMENT_DESC skyboxLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}
        };
        if (FAILED(m_device->CreateInputLayout(skyboxLayout, ARRAYSIZE(skyboxLayout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_skyboxInputLayout.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Skybox Input Layout");
            return false;
        }

        // Skybox Pixel Shader 컴파일
        psBlob.Reset();
        errorBlob.Reset();
        if (FAILED(D3DCompile(CommonShaderCode::SkyboxPS, strlen(CommonShaderCode::SkyboxPS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
        {
            if (errorBlob)
            {
                ALICE_LOG_ERRORF("Skybox PS compile error: %s", (char*)errorBlob->GetBufferPointer());
            }
            return false;
        }
        if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_skyboxPS.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Skybox PS");
            return false;
        }

        // Skybox Depth State 및 Rasterizer State 생성
        D3D11_DEPTH_STENCIL_DESC skyboxDsDesc = {};
        skyboxDsDesc.DepthEnable = TRUE;
        skyboxDsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        skyboxDsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        skyboxDsDesc.StencilEnable = FALSE;
        if (FAILED(m_device->CreateDepthStencilState(&skyboxDsDesc, m_skyboxDepthState.ReleaseAndGetAddressOf())))
            return false;

        D3D11_RASTERIZER_DESC skyboxRsDesc = {};
        skyboxRsDesc.FillMode = D3D11_FILL_SOLID;
        skyboxRsDesc.CullMode = D3D11_CULL_NONE;
        skyboxRsDesc.DepthClipEnable = TRUE;
        if (FAILED(m_device->CreateRasterizerState(&skyboxRsDesc, m_skyboxRasterizerState.ReleaseAndGetAddressOf())))
            return false;

        // Skybox Constant Buffer 생성
        D3D11_BUFFER_DESC skyboxCbDesc = {};
        skyboxCbDesc.ByteWidth = sizeof(XMMATRIX);
        skyboxCbDesc.Usage = D3D11_USAGE_DYNAMIC;
        skyboxCbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        skyboxCbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(m_device->CreateBuffer(&skyboxCbDesc, nullptr, m_cbSkybox.ReleaseAndGetAddressOf())))
            return false;

        // 톤매핑 Pixel Shader는 CreateToneMappingResources에서 HDR 지원 여부에 따라 생성합니다.

        // 톤매핑 전용 상태 객체 생성 (Blend OFF, Depth OFF, Cull OFF)
        // Depth OFF
        {
            D3D11_DEPTH_STENCIL_DESC ds = {};
            ds.DepthEnable = FALSE;
            ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            ds.DepthFunc = D3D11_COMPARISON_ALWAYS;
            ds.StencilEnable = FALSE;
            if (FAILED(m_device->CreateDepthStencilState(&ds, m_ppDepthOff.ReleaseAndGetAddressOf())))
            {
                ALICE_LOG_ERRORF("Failed to create PostProcess Depth State");
                return false;
            }
        }

        // Blend OFF (opaque)
        {
            D3D11_BLEND_DESC bd = {};
            bd.AlphaToCoverageEnable = FALSE;
            bd.IndependentBlendEnable = FALSE;
            auto& rt = bd.RenderTarget[0];
            rt.BlendEnable = FALSE;
            rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            if (FAILED(m_device->CreateBlendState(&bd, m_ppBlendOpaque.ReleaseAndGetAddressOf())))
            {
                ALICE_LOG_ERRORF("Failed to create PostProcess Blend State");
                return false;
            }
        }

        // Rasterizer: cull off, scissor off
        {
            D3D11_RASTERIZER_DESC rd = {};
            rd.FillMode = D3D11_FILL_SOLID;
            rd.CullMode = D3D11_CULL_NONE;
            rd.DepthClipEnable = TRUE;
            rd.ScissorEnable = FALSE;
            if (FAILED(m_device->CreateRasterizerState(&rd, m_ppRasterNoCull.ReleaseAndGetAddressOf())))
            {
                ALICE_LOG_ERRORF("Failed to create PostProcess Rasterizer State");
                return false;
            }
        }

        // ===================== Shadow Pass Shaders =====================
        // Static shadow VS + input layout (POSITION only)
        vsBlob.Reset();
        errorBlob.Reset();
        if (FAILED(D3DCompile(DeferredShader::ShadowVS,
                              strlen(DeferredShader::ShadowVS),
                              nullptr, nullptr, nullptr,
                              "main", "vs_5_0",
                              0, 0,
                              vsBlob.GetAddressOf(),
                              errorBlob.GetAddressOf())))
        {
            if (errorBlob)
                ALICE_LOG_ERRORF("Shadow VS compile error: %s", (char*)errorBlob->GetBufferPointer());
            return false;
        }
        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(),
                                                vsBlob->GetBufferSize(),
                                                nullptr,
                                                m_shadowVS.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Shadow VS");
            return false;
        }
        {
            D3D11_INPUT_ELEMENT_DESC il[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}
            };
            if (FAILED(m_device->CreateInputLayout(il,
                                                   ARRAYSIZE(il),
                                                   vsBlob->GetBufferPointer(),
                                                   vsBlob->GetBufferSize(),
                                                   m_shadowInputLayout.ReleaseAndGetAddressOf())))
            {
                ALICE_LOG_ERRORF("Failed to create Shadow InputLayout");
                return false;
            }
        }

        // Skinned shadow VS (input layout은 m_gBufferSkinnedInputLayout을 그대로 사용)
        vsBlob.Reset();
        errorBlob.Reset();
        if (FAILED(D3DCompile(DeferredShader::ShadowSkinnedVS,
                              strlen(DeferredShader::ShadowSkinnedVS),
                              nullptr, nullptr, nullptr,
                              "main", "vs_5_0",
                              0, 0,
                              vsBlob.GetAddressOf(),
                              errorBlob.GetAddressOf())))
        {
            if (errorBlob)
                ALICE_LOG_ERRORF("Shadow Skinned VS compile error: %s", (char*)errorBlob->GetBufferPointer());
            return false;
        }
        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(),
                                                vsBlob->GetBufferSize(),
                                                nullptr,
                                                m_shadowSkinnedVS.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Shadow Skinned VS");
            return false;
        }

        return true;
    }

    bool DeferredRenderSystem::CreateQuadGeometry()
    {
        struct QuadVertex
        {
            DirectX::XMFLOAT3 position;
            DirectX::XMFLOAT2 uv;
        };

        QuadVertex vertices[] = {
            { DirectX::XMFLOAT3(-1.0f, 1.0f, 1.0f), DirectX::XMFLOAT2(0.0f, 0.0f) },  // Left Top
            { DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f), DirectX::XMFLOAT2(1.0f, 0.0f) },   // Right Top
            { DirectX::XMFLOAT3(-1.0f, -1.0f, 1.0f), DirectX::XMFLOAT2(0.0f, 1.0f) }, // Left Bottom
            { DirectX::XMFLOAT3(1.0f, -1.0f, 1.0f), DirectX::XMFLOAT2(1.0f, 1.0f) }   // Right Bottom
        };

        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.ByteWidth = sizeof(QuadVertex) * 4;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbDesc.Usage = D3D11_USAGE_DEFAULT;
        D3D11_SUBRESOURCE_DATA vbData = {};
        vbData.pSysMem = vertices;
        if (FAILED(m_device->CreateBuffer(&vbDesc, &vbData, m_quadVB.ReleaseAndGetAddressOf())))
            return false;

        m_quadStride = sizeof(QuadVertex);
        m_quadOffset = 0;

        WORD indices[] = { 0, 1, 2, 2, 1, 3 };
        m_quadIndexCount = 6;
        D3D11_BUFFER_DESC ibDesc = {};
        ibDesc.ByteWidth = sizeof(WORD) * 6;
        ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        ibDesc.Usage = D3D11_USAGE_DEFAULT;
        D3D11_SUBRESOURCE_DATA ibData = {};
        ibData.pSysMem = indices;
        if (FAILED(m_device->CreateBuffer(&ibDesc, &ibData, m_quadIB.ReleaseAndGetAddressOf())))
            return false;

        return true;
    }

    bool DeferredRenderSystem::CreateConstantBuffers()
    {
        // PerObject CB (Forward와 동일한 구조: 행렬 + 재질 정보 + 셰이딩 모드)
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth = sizeof(CBPerObject);
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_cbPerObject.ReleaseAndGetAddressOf())))
            return false;

        // Lighting CB (Deferred Light 패스용 - ConstantBuffer register(b0))
        // HLSL의 ConstantBuffer 구조체 크기에 맞춰야 함 (대략 512바이트 이상)
        cbDesc.ByteWidth = 4096; // 충분한 크기
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_cbLighting.ReleaseAndGetAddressOf())))
            return false;

        // Directional Light CB
        cbDesc.ByteWidth = sizeof(DirectX::XMFLOAT4) * 2 + sizeof(float) * 4; // dir, color, intensity, pad
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_cbDirectionalLight.ReleaseAndGetAddressOf())))
            return false;

        // Extra Lights CB (Point/Spot/Rect)
        cbDesc.ByteWidth = (sizeof(ExtraLightsCB) + 15u) & ~15u;
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_cbExtraLights.ReleaseAndGetAddressOf())))
            return false;

        // Bones CB
        cbDesc.ByteWidth = sizeof(DirectX::XMMATRIX) * 1023 + sizeof(std::uint32_t) * 4;
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_cbBones.ReleaseAndGetAddressOf())))
            return false;

        // PostProcess CB
        cbDesc.ByteWidth = sizeof(float) * 4; // exposure, maxHDRNits, padding
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_cbPostProcess.ReleaseAndGetAddressOf())))
            return false;

        // Bloom CB
        cbDesc.ByteWidth = sizeof(BloomCB);
        cbDesc.ByteWidth = (cbDesc.ByteWidth + 15u) & ~15u; // 16-byte alignment
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_cbBloom.ReleaseAndGetAddressOf())))
            return false;

        // Transparent Forward-Style Light CB (register(b1))
        // float3 dir + float intensity + float3 color + pad + float3 camPos + pad = 48 bytes (16B 정렬)
        cbDesc.ByteWidth = sizeof(float) * 12;
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_cbTransparentLight.ReleaseAndGetAddressOf())))
            return false;

        // Shadow CB (register(b4))
        // float4x4(64) + float3(12) + int(4) + float3 pad(12) = 92 -> 96(16B align)
        {
            cbDesc.ByteWidth = sizeof(DirectX::XMMATRIX) + sizeof(float) * 3 + sizeof(int) + sizeof(float) * 3;
            cbDesc.ByteWidth = (cbDesc.ByteWidth + 15u) & ~15u;
            if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_cbShadow.ReleaseAndGetAddressOf())))
                return false;
        }


        return true;
    }

    bool DeferredRenderSystem::CreateCubeGeometry()
    {
        // ForwardRenderSystem::SimpleVertex와 동일한 구조체
        struct SimpleVertex
        {
            XMFLOAT3 Position;
            XMFLOAT3 Normal;
            XMFLOAT2 TexCoord;
        };

        SimpleVertex v[] = {
            // Front (+Z)
            { {-1,-1, 1}, { 0, 0, 1}, {0,1} }, { {-1, 1, 1}, { 0, 0, 1}, {0,0} }, { { 1, 1, 1}, { 0, 0, 1}, {1,0} }, { { 1,-1, 1}, { 0, 0, 1}, {1,1} },
            // Back (-Z)
            { {-1,-1,-1}, { 0, 0,-1}, {1,1} }, { { 1,-1,-1}, { 0, 0,-1}, {0,1} }, { { 1, 1,-1}, { 0, 0,-1}, {0,0} }, { {-1, 1,-1}, { 0, 0,-1}, {1,0} },
            // Top (+Y)
            { {-1, 1,-1}, { 0, 1, 0}, {0,1} }, { { 1, 1,-1}, { 0, 1, 0}, {1,1} }, { { 1, 1, 1}, { 0, 1, 0}, {1,0} }, { {-1, 1, 1}, { 0, 1, 0}, {0,0} },
            // Bottom (-Y)
            { {-1,-1,-1}, { 0,-1, 0}, {0,1} }, { {-1,-1, 1}, { 0,-1, 0}, {0,0} }, { { 1,-1, 1}, { 0,-1, 0}, {1,0} }, { { 1,-1,-1}, { 0,-1, 0}, {1,1} },
            // Left (-X)
            { {-1,-1,-1}, {-1, 0, 0}, {1,1} }, { {-1, 1,-1}, {-1, 0, 0}, {1,0} }, { {-1, 1, 1}, {-1, 0, 0}, {0,0} }, { {-1,-1, 1}, {-1, 0, 0}, {0,1} },
            // Right (+X)
            { { 1,-1,-1}, { 1, 0, 0}, {0,1} }, { { 1,-1, 1}, { 1, 0, 0}, {0,0} }, { { 1, 1, 1}, { 1, 0, 0}, {1,0} }, { { 1, 1,-1}, { 1, 0, 0}, {1,1} }
        };

        uint16_t i[] = {
            0,1,2, 0,2,3,     4,5,6, 4,6,7,     8,9,10, 8,10,11,
            12,13,14, 12,14,15, 16,17,18, 16,18,19, 20,21,22, 20,22,23
        };

        m_cubeIndexCount = (UINT)std::size(i);

        D3D11_BUFFER_DESC desc = { sizeof(v), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA data = { v, 0, 0 };
        if (FAILED(m_device->CreateBuffer(&desc, &data, m_cubeVB.ReleaseAndGetAddressOf()))) return false;

        desc.ByteWidth = sizeof(i);
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        data.pSysMem = i;
        if (FAILED(m_device->CreateBuffer(&desc, &data, m_cubeIB.ReleaseAndGetAddressOf()))) return false;

        return true;
    }

    bool DeferredRenderSystem::CreateSamplerStates()
    {
        D3D11_SAMPLER_DESC sDesc = {};
        sDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        sDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sDesc.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(m_device->CreateSamplerState(&sDesc, m_samplerState.ReleaseAndGetAddressOf())))
            return false;

        // Shadow Sampler
        sDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
        sDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
        sDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        if (FAILED(m_device->CreateSamplerState(&sDesc, m_shadowSampler.ReleaseAndGetAddressOf())))
            return false;

        // Linear Sampler
        sDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        if (FAILED(m_device->CreateSamplerState(&sDesc, m_samplerLinear.ReleaseAndGetAddressOf())))
            return false;

        return true;
    }

    bool DeferredRenderSystem::CreateBlendStates()
    {
        // Additive Blend State (라이트 패스용)
        D3D11_BLEND_DESC blendDesc = {};
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(m_device->CreateBlendState(&blendDesc, m_blendStateAdditive.ReleaseAndGetAddressOf())))
            return false;

        // Alpha Blend State (반투명 Forward-Style 패스용)
        D3D11_BLEND_DESC alphaDesc = {};
        alphaDesc.RenderTarget[0].BlendEnable = TRUE;
        alphaDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        alphaDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        alphaDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        alphaDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        alphaDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        alphaDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        alphaDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(m_device->CreateBlendState(&alphaDesc, m_alphaBlendState.ReleaseAndGetAddressOf())))
            return false;

        return true;
    }

    bool DeferredRenderSystem::CreateRasterizerStates()
    {
        D3D11_RASTERIZER_DESC rsDesc = {};
        rsDesc.FillMode = D3D11_FILL_SOLID;
        rsDesc.CullMode = D3D11_CULL_NONE;
        rsDesc.FrontCounterClockwise = FALSE;
        rsDesc.DepthBias = 0;
        rsDesc.DepthBiasClamp = 0.0f;
        rsDesc.SlopeScaledDepthBias = 0.0f;
        rsDesc.DepthClipEnable = TRUE;
        rsDesc.ScissorEnable = FALSE;
        rsDesc.MultisampleEnable = FALSE;
        rsDesc.AntialiasedLineEnable = FALSE;
        if (FAILED(m_device->CreateRasterizerState(&rsDesc, m_rasterizerState.ReleaseAndGetAddressOf())))
            return false;

        // Shadow pass RS (Depth Bias)
        {
            D3D11_RASTERIZER_DESC s = rsDesc;
            s.CullMode = D3D11_CULL_BACK;
            s.DepthBias = 1000;
            s.SlopeScaledDepthBias = 1.0f;
            s.FrontCounterClockwise = TRUE;
            if (FAILED(m_device->CreateRasterizerState(&s, m_shadowRasterizerState.ReleaseAndGetAddressOf())))
                return false;

            s.FrontCounterClockwise = FALSE;
            if (FAILED(m_device->CreateRasterizerState(&s, m_shadowRasterizerStateReversed.ReleaseAndGetAddressOf())))
                return false;
        }

        // 아웃라인용 Rasterizer State (Cull Front)
        // - 정점을 법선 방향으로 확장한 뒤, 뒷면(Back Face)을 그리면 원본 물체 뒤로 테두리가 나타납니다.
        D3D11_RASTERIZER_DESC outlineDesc = rsDesc;
        outlineDesc.CullMode = D3D11_CULL_FRONT;    // 앞면을 제거하고 뒷면을 그림
        if (FAILED(m_device->CreateRasterizerState(&outlineDesc, m_rsCullFront.ReleaseAndGetAddressOf())))
            return false;

        return true;
    }

    bool DeferredRenderSystem::CreateDepthStencilStates()
    {
        // 기본 Depth Stencil State
        D3D11_DEPTH_STENCIL_DESC dsDesc = {};
        dsDesc.DepthEnable = TRUE;
        dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        dsDesc.DepthFunc = D3D11_COMPARISON_LESS;
        dsDesc.StencilEnable = FALSE;
        if (FAILED(m_device->CreateDepthStencilState(&dsDesc, m_depthStencilState.ReleaseAndGetAddressOf())))
            return false;

        // Read Only Depth Stencil State (라이트 패스용)
        dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        if (FAILED(m_device->CreateDepthStencilState(&dsDesc, m_depthStencilStateReadOnly.ReleaseAndGetAddressOf())))
            return false;

        return true;
    }

    bool DeferredRenderSystem::CreateShadowMapResources()
    {
        const UINT size = (UINT)m_shadowSettings.mapSizePx;
        if (size == 0) return false;

        // 1) Shadow map texture (typeless)
        D3D11_TEXTURE2D_DESC tDesc = { size, size, 1, 1, DXGI_FORMAT_R32_TYPELESS, {1, 0},
                                       D3D11_USAGE_DEFAULT,
                                       D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE,
                                       0, 0 };
        if (FAILED(m_device->CreateTexture2D(&tDesc, nullptr, m_shadowTex.ReleaseAndGetAddressOf())))
            return false;

        // 2) DSV
        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = { DXGI_FORMAT_D32_FLOAT, D3D11_DSV_DIMENSION_TEXTURE2D, 0 };
        if (FAILED(m_device->CreateDepthStencilView(m_shadowTex.Get(), &dsvDesc, m_shadowDSV.ReleaseAndGetAddressOf())))
            return false;

        // 3) SRV
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = { DXGI_FORMAT_R32_FLOAT, D3D11_SRV_DIMENSION_TEXTURE2D, 0 };
        srvDesc.Texture2D.MipLevels = 1;
        if (FAILED(m_device->CreateShaderResourceView(m_shadowTex.Get(), &srvDesc, m_shadowSRV.ReleaseAndGetAddressOf())))
            return false;

        // 4) Viewport
        m_shadowViewport = { 0.0f, 0.0f, (float)size, (float)size, 0.0f, 1.0f };

        return true;
    }

    bool DeferredRenderSystem::CreateToneMappingResources(const std::uint32_t& width, const std::uint32_t& height)
    {
		// 씬 렌더 타겟 생성 (HDR 포맷: 톤매핑을 위해 R16G16B16A16_FLOAT 사용)
		m_sceneWidth = width;
		m_sceneHeight = height;
		D3D11_TEXTURE2D_DESC cDesc = { width, height, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
		if (FAILED(m_device->CreateTexture2D(&cDesc, nullptr, m_sceneColorTex.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(m_device->CreateRenderTargetView(m_sceneColorTex.Get(), nullptr, m_sceneRTV.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(m_device->CreateShaderResourceView(m_sceneColorTex.Get(), nullptr, m_sceneColorSRV.ReleaseAndGetAddressOf()))) return false;

		// 에디터 뷰포트 표시용 LDR 결과 텍스처 (ToneMapped)
		D3D11_TEXTURE2D_DESC vDesc = { width, height, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
		if (FAILED(m_device->CreateTexture2D(&vDesc, nullptr, m_viewportTex.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(m_device->CreateRenderTargetView(m_viewportTex.Get(), nullptr, m_viewportRTV.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(m_device->CreateShaderResourceView(m_viewportTex.Get(), nullptr, m_viewportSRV.ReleaseAndGetAddressOf()))) return false;

		// Depth Texture & View (DSV, SRV) - SRV 생성을 위해 typeless 포맷 사용
		D3D11_TEXTURE2D_DESC dDesc = { width, height, 1, 1, DXGI_FORMAT_R24G8_TYPELESS, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
		if (FAILED(m_device->CreateTexture2D(&dDesc, nullptr, m_sceneDepthTex.ReleaseAndGetAddressOf()))) return false;
		
		// DSV 설정 (D24_UNORM_S8_UINT 포맷으로 뷰 생성)
		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		dsvDesc.Texture2D.MipSlice = 0;
		if (FAILED(m_device->CreateDepthStencilView(m_sceneDepthTex.Get(), &dsvDesc, m_sceneDSV.ReleaseAndGetAddressOf()))) return false;
		
		// Depth SRV 생성 (depth test용) - 실패해도 계속 진행 (선택적)
		D3D11_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
		depthSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;  // R24G8_TYPELESS의 SRV 포맷
		depthSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		depthSrvDesc.Texture2D.MostDetailedMip = 0;
		depthSrvDesc.Texture2D.MipLevels = 1;
		HRESULT hrDepthSRV = m_device->CreateShaderResourceView(m_sceneDepthTex.Get(), &depthSrvDesc, m_sceneDepthSRV.ReleaseAndGetAddressOf());
		if (FAILED(hrDepthSRV))
		{
			ALICE_LOG_WARN("DeferredRenderSystem::CreateToneMappingResources: CreateShaderResourceView(depthSRV) failed (0x%08X) - depth test disabled", (unsigned)hrDepthSRV);
			m_sceneDepthSRV.Reset();
			// 초기화는 성공으로 계속 진행 (depth test 없이 렌더링)
		}

        // HDR 지원 여부 확인 및 적절한 톤매핑 셰이더 선택
        ComPtr<ID3DBlob> psBlob, errorBlob;
        float maxNits = 100.0f;
        bool isHDRSupported = m_renderDevice.IsHDRSupported(maxNits);
        const char* toneMappingShaderSource = isHDRSupported ? CommonShaderCode::ToneMappingPS_HDR : CommonShaderCode::ToneMappingPS_LDR;
        const char* shaderName = isHDRSupported ? "HDR" : "LDR";

        // Tone Mapping Pixel Shader 컴파일
        if (FAILED(D3DCompile(toneMappingShaderSource, strlen(toneMappingShaderSource), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
        {
            if (errorBlob)
            {
                ALICE_LOG_ERRORF("Tone Mapping PS (%s) compile error: %s", shaderName, (char*)errorBlob->GetBufferPointer());
            }
            return false;
        }
        if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_toneMappingPS.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Tone Mapping PS (%s)", shaderName);
            return false;
        }

        if (isHDRSupported)
        {
            ALICE_LOG_INFO("DeferredRenderSystem::CreateToneMappingResources: HDR 톤매핑 셰이더 사용. MaxNits: %.1f", maxNits);
        }
        else
        {
            ALICE_LOG_INFO("DeferredRenderSystem::CreateToneMappingResources: LDR 톤매핑 셰이더 사용.");
        }

        // 파티클 오버레이 Pixel Shader 생성
        {
            ComPtr<ID3DBlob> psBlob, errorBlob;
            if (FAILED(D3DCompile(CommonShaderCode::ParticleOverlayPS, strlen(CommonShaderCode::ParticleOverlayPS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
            {
                if (errorBlob)
                {
                    ALICE_LOG_ERRORF("Particle Overlay PS compile error: %s", (char*)errorBlob->GetBufferPointer());
                }
                return false;
            }
            if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_particleOverlayPS.ReleaseAndGetAddressOf())))
            {
                ALICE_LOG_ERRORF("Failed to create Particle Overlay PS");
                return false;
            }
        }

        // 파티클 오버레이용 Additive Blend State 생성
        {
            D3D11_BLEND_DESC blendDesc = {};
            blendDesc.RenderTarget[0].BlendEnable = TRUE;
            blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
            blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
            blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
            blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            if (FAILED(m_device->CreateBlendState(&blendDesc, m_ppBlendAdditive.ReleaseAndGetAddressOf())))
            {
                ALICE_LOG_ERRORF("Failed to create Particle Overlay Additive Blend State");
                return false;
            }
        }

        return true;
    }

    bool DeferredRenderSystem::CreateBloomResources(const std::uint32_t& width, const std::uint32_t& height)
    {
        // 레벨 0: viewport/downsample
        std::uint32_t level0Width = width / m_bloomSettings.downsample;
        std::uint32_t level0Height = height / m_bloomSettings.downsample;
        
        // 최소 크기 보장
        if (level0Width == 0) level0Width = 1;
        if (level0Height == 0) level0Height = 1;
        
        // 각 레벨의 해상도 계산 및 텍스처 생성
        for (int level = 0; level < BLOOM_LEVEL_COUNT; ++level)
        {
            std::uint32_t levelWidth = level0Width;
            std::uint32_t levelHeight = level0Height;
            
            // 레벨 1~4는 이전 레벨의 절반
            for (int i = 0; i < level; ++i)
            {
                levelWidth = (levelWidth > 1) ? (levelWidth / 2) : 1;
                levelHeight = (levelHeight > 1) ? (levelHeight / 2) : 1;
            }
            
            // 최소 크기 보장
            if (levelWidth == 0) levelWidth = 1;
            if (levelHeight == 0) levelHeight = 1;
            
            m_bloomLevelWidth[level] = levelWidth;
            m_bloomLevelHeight[level] = levelHeight;
            
            // 각 레벨마다 ping-pong 텍스처 2장 (A/B) 생성
            D3D11_TEXTURE2D_DESC bloomDesc = { levelWidth, levelHeight, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
            
            for (int pingPong = 0; pingPong < 2; ++pingPong)
            {
                if (FAILED(m_device->CreateTexture2D(&bloomDesc, nullptr, m_bloomLevelTex[level][pingPong].ReleaseAndGetAddressOf()))) return false;
                if (FAILED(m_device->CreateRenderTargetView(m_bloomLevelTex[level][pingPong].Get(), nullptr, m_bloomLevelRTV[level][pingPong].ReleaseAndGetAddressOf()))) return false;
                if (FAILED(m_device->CreateShaderResourceView(m_bloomLevelTex[level][pingPong].Get(), nullptr, m_bloomLevelSRV[level][pingPong].ReleaseAndGetAddressOf()))) return false;
            }


        }

        // Bloom 셰이더 컴파일 (한 번만 컴파일하므로 이미 생성되어 있으면 스킵)
        if (!m_bloomBrightPassPS)
        {
            ComPtr<ID3DBlob> psBlob, errorBlob;

            // Bright Pass PS
            if (FAILED(D3DCompile(CommonShaderCode::BloomBrightPassPS, strlen(CommonShaderCode::BloomBrightPassPS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
            {
                if (errorBlob) ALICE_LOG_ERRORF("Bloom Bright Pass PS compile error: %s", (char*)errorBlob->GetBufferPointer());
                return false;
            }
            if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_bloomBrightPassPS.ReleaseAndGetAddressOf()))) return false;

            psBlob.Reset();
            errorBlob.Reset();

            // Blur Pass PS (Horizontal)
            if (FAILED(D3DCompile(CommonShaderCode::BloomBlurPassPS_H, strlen(CommonShaderCode::BloomBlurPassPS_H), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
            {
                if (errorBlob) ALICE_LOG_ERRORF("Bloom Blur Pass PS (H) compile error: %s", (char*)errorBlob->GetBufferPointer());
                return false;
            }
            if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_bloomBlurPassPS_H.ReleaseAndGetAddressOf()))) return false;

            psBlob.Reset();
            errorBlob.Reset();

            // Blur Pass PS (Vertical)
            if (FAILED(D3DCompile(CommonShaderCode::BloomBlurPassPS_V, strlen(CommonShaderCode::BloomBlurPassPS_V), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
            {
                if (errorBlob) ALICE_LOG_ERRORF("Bloom Blur Pass PS (V) compile error: %s", (char*)errorBlob->GetBufferPointer());
                return false;
            }
            if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_bloomBlurPassPS_V.ReleaseAndGetAddressOf()))) return false;

            psBlob.Reset();
            errorBlob.Reset();

            // Downsample PS
            if (FAILED(D3DCompile(CommonShaderCode::BloomDownsamplePS, strlen(CommonShaderCode::BloomDownsamplePS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
            {
                if (errorBlob) ALICE_LOG_ERRORF("Bloom Downsample PS compile error: %s", (char*)errorBlob->GetBufferPointer());
                return false;
            }
            if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_bloomDownsamplePS.ReleaseAndGetAddressOf()))) return false;

            psBlob.Reset();
            errorBlob.Reset();

            // Upsample PS
            if (FAILED(D3DCompile(CommonShaderCode::BloomUpsamplePS, strlen(CommonShaderCode::BloomUpsamplePS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
            {
                if (errorBlob) ALICE_LOG_ERRORF("Bloom Upsample PS compile error: %s", (char*)errorBlob->GetBufferPointer());
                return false;
            }
            if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_bloomUpsamplePS.ReleaseAndGetAddressOf()))) return false;

            psBlob.Reset();
            errorBlob.Reset();

            // Composite PS
            if (FAILED(D3DCompile(CommonShaderCode::BloomCompositePS, strlen(CommonShaderCode::BloomCompositePS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
            {
                if (errorBlob) ALICE_LOG_ERRORF("Bloom Composite PS compile error: %s", (char*)errorBlob->GetBufferPointer());
                return false;
            }
            if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_bloomCompositePS.ReleaseAndGetAddressOf()))) return false;
        }

        ALICE_LOG_INFO("DeferredRenderSystem::CreateBloomResources: success. Level0: (%dx%d)", m_bloomLevelWidth[0], m_bloomLevelHeight[0]);
        return true;
    }

    bool DeferredRenderSystem::CreatePostBloomResources(const std::uint32_t& width, const std::uint32_t& height)
	{
		// Bloom 합성(HDR) -> ToneMapping 입력으로 쓸 중간 텍스처
		m_postBloomTex.Reset();
		m_postBloomRTV.Reset();
		m_postBloomSRV.Reset();

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; // HDR 유지
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		if (FAILED(m_device->CreateTexture2D(&desc, nullptr, m_postBloomTex.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(m_device->CreateRenderTargetView(m_postBloomTex.Get(), nullptr, m_postBloomRTV.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(m_device->CreateShaderResourceView(m_postBloomTex.Get(), nullptr, m_postBloomSRV.ReleaseAndGetAddressOf()))) return false;

        return true;
	}


    bool DeferredRenderSystem::CreateIblResources(const std::string& iblDir, const std::string& iblName)
    {
        if (!m_resources) return false;

        namespace fs = std::filesystem;
        // 경로 및 이름 설정 (Sample -> BakerSample, 그 외 소문자 변환)
        fs::path base = fs::path("Resource/Skybox") / iblDir;

        // Diffuse, Specular, Brdf 로드
        if (!(m_iblDiffuseSRV = m_resources->LoadData<ID3D11ShaderResourceView>(base / (iblName + "DiffuseHDR.dds"), m_device.Get())))
            ALICE_LOG_WARN("Failed IBL Diffuse: %s", (base / iblName).string().c_str());

        if (!(m_iblSpecularSRV = m_resources->LoadData<ID3D11ShaderResourceView>(base / (iblName + "SpecularHDR.dds"), m_device.Get())))
            ALICE_LOG_WARN("Failed IBL Specular %s", (base / iblName).string().c_str());

        if (!(m_iblBrdfLutSRV = m_resources->LoadData<ID3D11ShaderResourceView>(base / (iblName + "Brdf.dds"), m_device.Get())))
            ALICE_LOG_WARN("Failed IBL BRDF %s", (base / iblName).string().c_str());

        // Skybox Env 로드 및 상태 설정
        m_skyboxEnabled = (m_skyboxSRV = m_resources->LoadData<ID3D11ShaderResourceView>(base / (iblName + "EnvHDR.dds"), m_device.Get())) != nullptr;
        if (!m_skyboxEnabled) ALICE_LOG_WARN("Failed Skybox Env");

        m_currentIblSet = iblName;
        return true;
    }

    DirectX::XMMATRIX DeferredRenderSystem::RenderShadowPass(
        const World& world,
        const std::vector<SkinnedDrawCommand>& skinnedCommands,
        const std::unordered_set<EntityId>& cameraEntities,
        bool editorMode,
        bool isPlaying)
    {
        using namespace DirectX;

        if (!m_shadowDSV || !m_shadowVS || !m_shadowSkinnedVS) return XMMatrixIdentity();

       // 1) 라이트 방향: 에디터 UI에서 바뀌는 keyDirection을 그대로 반영
       auto GetSafeDir = [](const DirectX::XMFLOAT3& v) {
        DirectX::XMVECTOR vv = DirectX::XMLoadFloat3(&v);
        return DirectX::XMVector3Equal(vv, DirectX::XMVectorZero())
            ? DirectX::XMVectorSet(0, -1, 0, 0)
            : DirectX::XMVector3Normalize(vv);
        };
        XMVECTOR lightDir = GetSafeDir(m_lightingParameters.keyDirection);

        // 2) 씬 바운딩 계산 (카메라 엔티티 제외)
        XMFLOAT3 minP{ FLT_MAX, FLT_MAX, FLT_MAX };
        XMFLOAT3 maxP{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
        bool hasObjects = false;

        const auto& transforms = world.GetComponents<TransformComponent>();
        for (const auto& [id, tr] : transforms)
        {
            if (cameraEntities.contains(id)) continue;
            if (!tr.enabled) continue;
            hasObjects = true;
            minP.x = (std::min)(minP.x, tr.position.x); minP.y = (std::min)(minP.y, tr.position.y); minP.z = (std::min)(minP.z, tr.position.z);
            maxP.x = (std::max)(maxP.x, tr.position.x); maxP.y = (std::max)(maxP.y, tr.position.y); maxP.z = (std::max)(maxP.z, tr.position.z);
        }

        if (!hasObjects)
        {
            minP = { -10.0f, -10.0f, -10.0f };
            maxP = { 10.0f, 10.0f, 10.0f };
        }

        // 3) Focus/Radius
        XMVECTOR vMin = XMLoadFloat3(&minP);
        XMVECTOR vMax = XMLoadFloat3(&maxP);
        XMVECTOR focus = (vMin + vMax) * 0.5f;

        XMVECTOR diagonal = XMVector3Length(vMax - vMin);
        float sceneRadius = XMVectorGetX(diagonal) * 0.5f;

        float r = (std::max)(m_shadowSettings.orthoRadius, sceneRadius);
        r *= 1.5f;

        // 4) lightView/lightProj
        float distFromCenter = r * 3.0f;
        XMVECTOR lightPos = focus - lightDir * distFromCenter;

        XMVECTOR up = (fabsf(XMVectorGetX(XMVector3Dot(XMVectorSet(0, 1, 0, 0), lightDir))) > 0.99f)
            ? XMVectorSet(0, 0, 1, 0) : XMVectorSet(0, 1, 0, 0);

        XMMATRIX lightView = XMMatrixLookToLH(lightPos, lightDir, up);

        float nearZ = 0.01f;
        float farZ = distFromCenter + r * 2.0f;
        XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(-r, r, -r, r, nearZ, farZ);

        // 5) Texel snapping
        XMVECTOR focusLS = XMVector3TransformCoord(focus, lightView);
        float texelWorld = (2.0f * r) / static_cast<float>(m_shadowSettings.mapSizePx);
        float snapX = floorf(XMVectorGetX(focusLS) / texelWorld) * texelWorld;
        float snapY = floorf(XMVectorGetY(focusLS) / texelWorld) * texelWorld;
        lightView = XMMatrixTranslation(snapX - XMVectorGetX(focusLS), snapY - XMVectorGetY(focusLS), 0.0f) * lightView;

        XMMATRIX lightViewProj = lightView * lightProj;

        // --- Render Shadow Depth ---
        // SRV(t8) 바인딩 해제 (DSV 충돌 방지)
        ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
        m_context->PSSetShaderResources(8, 1, nullSRV);

        m_context->RSSetViewports(1, &m_shadowViewport);
        m_context->OMSetRenderTargets(0, nullptr, m_shadowDSV.Get());
        m_context->ClearDepthStencilView(m_shadowDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);

        // Depth-only: PS none
        m_context->PSSetShader(nullptr, nullptr, 0);

        // 1) Static meshes (cube)
        if (m_cubeVB && m_cubeIB && m_shadowInputLayout && m_shadowVS && m_cubeIndexCount > 0)
        {
            UINT stride = sizeof(DirectX::XMFLOAT3) * 2 + sizeof(DirectX::XMFLOAT2); // SimpleVertex(Position,Normal,Tex)
            UINT offset = 0;
            ID3D11Buffer* vb = m_cubeVB.Get();
            m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            m_context->IASetIndexBuffer(m_cubeIB.Get(), DXGI_FORMAT_R16_UINT, 0);
            m_context->IASetInputLayout(m_shadowInputLayout.Get());
            m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            m_context->VSSetShader(m_shadowVS.Get(), nullptr, 0);

            for (const auto& [id, tr] : transforms)
            {
                if (cameraEntities.contains(id)) continue;
                if (world.GetComponent<SkinnedMeshComponent>(id)) continue;
                if (!tr.enabled) continue;

                XMMATRIX worldM = BuildWorldMatrix(world, id, tr);

                const bool flipped = XMVectorGetX(XMMatrixDeterminant(worldM)) < 0.0f;
                if (flipped && m_shadowRasterizerStateReversed) m_context->RSSetState(m_shadowRasterizerStateReversed.Get());
                else if (m_shadowRasterizerState) m_context->RSSetState(m_shadowRasterizerState.Get());

                UpdatePerObjectCB(worldM, lightView, lightProj, XMFLOAT4(1, 1, 1, 1), 1.0f, 0.0f, false, false, 0, 1.0f);
                m_context->DrawIndexed(m_cubeIndexCount, 0, 0);
            }
        }

        // 2) Skinned meshes
        if (!skinnedCommands.empty() && m_gBufferSkinnedInputLayout && m_shadowSkinnedVS)
        {
            UINT offset = 0;
            m_context->IASetInputLayout(m_gBufferSkinnedInputLayout.Get());
            m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            m_context->VSSetShader(m_shadowSkinnedVS.Get(), nullptr, 0);

            for (const auto& cmd : skinnedCommands)
            {
                if (!cmd.vertexBuffer || !cmd.indexBuffer || cmd.indexCount == 0) continue;

                UINT sStride = cmd.stride;
                m_context->IASetVertexBuffers(0, 1, &cmd.vertexBuffer, &sStride, &offset);
                m_context->IASetIndexBuffer(cmd.indexBuffer, DXGI_FORMAT_R32_UINT, 0);

                const bool flipped = XMVectorGetX(XMMatrixDeterminant(cmd.world)) < 0.0f;
                if (flipped && m_shadowRasterizerStateReversed) m_context->RSSetState(m_shadowRasterizerStateReversed.Get());
                else if (m_shadowRasterizerState) m_context->RSSetState(m_shadowRasterizerState.Get());

                UpdateBonesCB(cmd.bones, cmd.boneCount);
                UpdatePerObjectCB(cmd.world, lightView, lightProj, XMFLOAT4(1, 1, 1, 1), 1.0f, 0.0f, false, false, 0, 1.0f);
                m_context->DrawIndexed(cmd.indexCount, cmd.startIndex, cmd.baseVertex);
            }
        }

        return lightViewProj;
    }

    void DeferredRenderSystem::Render(const World& world,
                                      const Camera& camera,
                                      EntityId entity,
                                      const std::unordered_set<EntityId>& cameraEntities,
                                      int shadingMode,
                                      bool enableFillLight,
                                      const std::vector<SkinnedDrawCommand>& skinnedCommands,
                                      UIWorldManager& uiWorld,
                                      bool editorMode,
                                      bool isPlaying)
    {
        if (!m_device || !m_context) return;

        // 실제로 사용한 카메라 정보 저장 (ComputeEffect용)
        m_lastViewProj = camera.GetViewProjectionMatrix();
        m_lastCameraPos = camera.GetPosition();
        m_lastNearPlane = camera.GetNearPlane();
        m_lastFarPlane = camera.GetFarPlane();

        // Viewport 설정
        D3D11_VIEWPORT vp{};
        vp.Width = (float)m_sceneWidth; vp.Height = (float)m_sceneHeight; vp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &vp);

        // Shadow pass 먼저 렌더링 (lightViewProj 계산 + shadow depth 생성)
        const DirectX::XMMATRIX lightViewProj = RenderShadowPass(world, skinnedCommands, cameraEntities, editorMode, isPlaying);

        // ShadowPass에서 viewport가 섀도우맵 해상도로 바뀌므로, 씬 뷰포트를 다시 설정
        m_context->RSSetViewports(1, &vp);

        // G-Buffer 패스
        PassGBuffer(world, camera, skinnedCommands, cameraEntities, shadingMode, editorMode, isPlaying);

        // Deferred Light 패스 (IBL 포함)
        PassDeferredLight(world, camera, shadingMode, enableFillLight, lightViewProj);

        // TrailEffectRenderSystem 렌더링 (IBL 패스 이후)
        if (m_trailRenderSystem)
        {
            m_trailRenderSystem->Render(world, camera);
        }

        // 스카이박스 렌더링
        if (m_skyboxEnabled)
        {
            RenderSkybox(camera);
        }

        // 반투명(알파 블렌딩) 오브젝트는 라이트 패스 이후 Forward-Style로 합성
        PassTransparentForward(camera, skinnedCommands, shadingMode);

        // 에디터 뷰포트 표시용 LDR 텍스처로 Bloom + 톤매핑 (ImGui::Image에서 사용)
        if (m_viewportRTV)
        {
            D3D11_VIEWPORT viewport = {};
            viewport.Width = static_cast<float>(m_sceneWidth);
            viewport.Height = static_cast<float>(m_sceneHeight);
            viewport.MaxDepth = 1.0f;
            
            // Bloom 패스 (enabled일 때만)
            if (m_bloomSettings.enabled)
            {
                RenderBloomPass(m_sceneColorSRV.Get(), m_viewportRTV.Get(), viewport);
            }
            else
            {
                // Bloom이 꺼져있으면 씬 컬러를 바로 톤매핑
                RenderToneMapping(m_sceneColorSRV.Get(), m_viewportRTV.Get(), viewport);
            }

            // UI 렌더링 (Post-processing 이후, 최상단에 렌더링)
            uiWorld.Render();  // D2D → UI 텍스처 렌더링
            RenderUI(uiWorld, m_viewportRTV.Get(), viewport);
        }

        // 최종 백버퍼 복귀 (ImGui 등 UI 렌더링을 위해)
        RestoreBackBuffer();
    }

    void DeferredRenderSystem::PassGBuffer(const World& world,
                                           const Camera& camera,
                                           const std::vector<SkinnedDrawCommand>& skinnedCommands,
                                           const std::unordered_set<EntityId>& cameraEntities,
                                           int shadingMode,
                                           bool editorMode,
                                           bool isPlaying)
    {
        // ShadowPass 등에서 viewport가 변경될 수 있으므로,
        // GBuffer 패스 시작 시 항상 씬 해상도 뷰포트를 재설정합니다.
        D3D11_VIEWPORT vp{};
        vp.Width = (float)m_sceneWidth;
        vp.Height = (float)m_sceneHeight;
        vp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &vp);

        // G-Buffer 클리어
        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        // Normal 클리어 값: 평평한 노말 (0,0,1)을 [0,1] 인코딩하면 (0.5, 0.5, 1.0)
        float clearNormal[4] = { 0.5f, 0.5f, 1.0f, 1.0f };

        m_context->ClearRenderTargetView(m_gBufferRTVs[0].Get(), clearColor); // Position
        m_context->ClearRenderTargetView(m_gBufferRTVs[1].Get(), clearNormal); // Normal
        m_context->ClearRenderTargetView(m_gBufferRTVs[2].Get(), clearColor); // Metalness
        m_context->ClearRenderTargetView(m_gBufferRTVs[3].Get(), clearColor); // Roughness
        m_context->ClearRenderTargetView(m_gBufferRTVs[4].Get(), clearColor); // BaseColor
        m_context->ClearDepthStencilView(m_sceneDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

        // G-Buffer 렌더 타겟 설정
        ID3D11RenderTargetView* rtvs[GBufferCount] = {
            m_gBufferRTVs[0].Get(), m_gBufferRTVs[1].Get(),
            m_gBufferRTVs[2].Get(), m_gBufferRTVs[3].Get(),
            m_gBufferRTVs[4].Get()
        };
        m_context->OMSetRenderTargets(GBufferCount, rtvs, m_sceneDSV.Get());
        m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);
        m_context->RSSetState(m_rasterizerState.Get());

        // 파이프라인 설정
        m_context->VSSetShader(m_gBufferVS.Get(), nullptr, 0);
        m_context->PSSetShader(m_gBufferPS.Get(), nullptr, 0);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->IASetInputLayout(m_gBufferInputLayout.Get());
        
        // PS sampler 바인딩 (normal map 샘플링을 위해 필요)
        ID3D11SamplerState* samplers[] = { m_samplerState.Get() };
        m_context->PSSetSamplers(0, 1, samplers);

        // 상수 버퍼 업데이트
        XMMATRIX view = camera.GetViewMatrix();
        XMMATRIX proj = camera.GetProjectionMatrix();

        // 1. 정적 메시 (큐브) 렌더링
        // ForwardRenderSystem::SimpleVertex와 동일한 구조체 (private이므로 로컬 정의)
        UINT stride = sizeof(SimpleVertex);
        UINT offset = 0;
        ID3D11Buffer* vb = m_cubeVB.Get();
        m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        m_context->IASetIndexBuffer(m_cubeIB.Get(), DXGI_FORMAT_R16_UINT, 0);

        const auto& transforms = world.GetComponents<TransformComponent>();
        for (const auto& [id, transform] : transforms)
        {
            if (cameraEntities.contains(id)) continue;
            if (world.GetComponent<SkinnedMeshComponent>(id)) continue;
            if (!transform.enabled) continue;

            XMMATRIX worldM = BuildWorldMatrix(world, id, transform);
            
            // 재질 정보 가져오기
            XMFLOAT4 color = { 1, 1, 1, 1 };
            float rough = 0.5f, metal = 0.0f;
            bool useTex = false;
            ID3D11ShaderResourceView* texSRV = nullptr;
            
            // MaterialComponent가 있으면 값 적용
            XMFLOAT3 outlineColor = {0,0,0};
            float outlineWidth = 0.0f;
            float normalStrength = 1.0f;
            int objectShadingMode = shadingMode;
            
            const MaterialComponent* mat = world.GetComponent<MaterialComponent>(id);
            if (mat) {
                color = { mat->color.x, mat->color.y, mat->color.z, 1.0f };
                rough = mat->roughness; 
                metal = mat->metalness;
                normalStrength = mat->normalStrength;
                outlineColor = mat->outlineColor;
                outlineWidth = mat->outlineWidth;
                if (mat->shadingMode >= 0) objectShadingMode = mat->shadingMode;
                if (!mat->albedoTexturePath.empty()) {
                    texSRV = GetOrCreateTexture(mat->albedoTexturePath);
                    useTex = (texSRV != nullptr);
                }
            }

            // 텍스처 바인딩 (t0: Diffuse, t1: Normal)
            ID3D11ShaderResourceView* srvs[] = { texSRV, nullptr }; // 정적 메시는 노말맵 현재 null
            m_context->PSSetShaderResources(0, 2, srvs);

            // Pass 1. 원본 물체 그리기 (아웃라인 두께 0으로 강제)
            UpdatePerObjectCB(worldM, view, proj, color, rough, metal, useTex, false, 
                              objectShadingMode, normalStrength, outlineColor, 0.0f); // width = 0
            m_context->DrawIndexed(m_cubeIndexCount, 0, 0);

            // Pass 2. 아웃라인 그리기 (설정된 경우만)
            if (outlineWidth > 0.0f)
            {
                m_context->RSSetState(m_rsCullFront.Get()); // 뒷면 그리기
                
                // 아웃라인 값 적용
                UpdatePerObjectCB(worldM, view, proj, color, rough, metal, useTex, false, 
                                  objectShadingMode, normalStrength, outlineColor, outlineWidth);
                m_context->DrawIndexed(m_cubeIndexCount, 0, 0);
                
                m_context->RSSetState(m_rasterizerState.Get()); // 상태 복구
            }
        }
        
        // 2. 스키닝 메시 렌더링
        // - ForwardRenderSystem과 동일하게, Registry의 서브셋 머티리얼 SRV를 우선 사용합니다.
        // - (cmd.albedoTexturePath는 에디터에서 오버라이드한 경우에만 사용)
        if (!skinnedCommands.empty() && m_gBufferSkinnedVS && m_gBufferPS)
        {
            m_context->VSSetShader(m_gBufferSkinnedVS.Get(), nullptr, 0);
            m_context->IASetInputLayout(m_gBufferSkinnedInputLayout.Get());

            for (const auto& cmd : skinnedCommands)
            {
                if (!cmd.vertexBuffer || !cmd.indexBuffer || cmd.indexCount == 0) continue;

                UINT sStride = cmd.stride;
                m_context->IASetVertexBuffers(0, 1, &cmd.vertexBuffer, &sStride, &offset);
                m_context->IASetIndexBuffer(cmd.indexBuffer, DXGI_FORMAT_R32_UINT, 0);

                UpdateBonesCB(cmd.bones, cmd.boneCount);

                const XMFLOAT4 color(cmd.color.x, cmd.color.y, cmd.color.z, 1.0f);

				std::shared_ptr<SkinnedMeshGPU> mesh =
					(m_skinnedRegistry && !cmd.meshKey.empty()) ? m_skinnedRegistry->Find(cmd.meshKey) : nullptr;

                if (mesh && !mesh->subsets.empty())
                {
                    for (const auto& sub : mesh->subsets)
                    {
                        if (sub.indexCount == 0) continue;

                        ID3D11ShaderResourceView* diff =
                            (sub.materialIndex < mesh->materialSRVs.size()) ? mesh->materialSRVs[sub.materialIndex].Get() : nullptr;
                        ID3D11ShaderResourceView* norm =
                            (sub.materialIndex < mesh->normalSRVs.size()) ? mesh->normalSRVs[sub.materialIndex].Get() : nullptr;

                        ID3D11ShaderResourceView* srvs[] = { diff, norm };
                        m_context->PSSetShaderResources(0, 2, srvs);

                        const int objectShadingMode = (cmd.shadingMode >= 0) ? cmd.shadingMode : shadingMode;
                        
                        // Pass 1. 원본
                        UpdatePerObjectCB(cmd.world, view, proj, color, cmd.roughness, cmd.metalness,
                                          (diff != nullptr), (norm != nullptr), objectShadingMode, 
                                          cmd.normalStrength, cmd.outlineColor, 0.0f); // width 0
                        m_context->DrawIndexed(sub.indexCount, sub.startIndex, cmd.baseVertex);

                        // Pass 2. 아웃라인
                        if (cmd.outlineWidth > 0.0f)
                        {
                            m_context->RSSetState(m_rsCullFront.Get());
                            UpdatePerObjectCB(cmd.world, view, proj, color, cmd.roughness, cmd.metalness,
                                              (diff != nullptr), (norm != nullptr), objectShadingMode, 
                                              cmd.normalStrength, cmd.outlineColor, cmd.outlineWidth);
                            m_context->DrawIndexed(sub.indexCount, sub.startIndex, cmd.baseVertex);
                            m_context->RSSetState(m_rasterizerState.Get());
                        }
                    }
                }
                else
                {
                    // 오버라이드 텍스처 (또는 단일 텍스처)만 있는 경우
                    ID3D11ShaderResourceView* diff = GetOrCreateTexture(cmd.albedoTexturePath);
                    ID3D11ShaderResourceView* srvs[] = { diff, nullptr };
                    m_context->PSSetShaderResources(0, 2, srvs);

                    const int objectShadingMode = (cmd.shadingMode >= 0) ? cmd.shadingMode : shadingMode;
                    
                    // Pass 1. 원본
                    UpdatePerObjectCB(cmd.world, view, proj, color, cmd.roughness, cmd.metalness,
                                      (diff != nullptr), false, objectShadingMode, 
                                      cmd.normalStrength, cmd.outlineColor, 0.0f);
                    m_context->DrawIndexed(cmd.indexCount, cmd.startIndex, cmd.baseVertex);

                    // Pass 2. 아웃라인
                    if (cmd.outlineWidth > 0.0f)
                    {
                        m_context->RSSetState(m_rsCullFront.Get());
                        UpdatePerObjectCB(cmd.world, view, proj, color, cmd.roughness, cmd.metalness,
                                          (diff != nullptr), false, objectShadingMode, 
                                          cmd.normalStrength, cmd.outlineColor, cmd.outlineWidth);
                        m_context->DrawIndexed(cmd.indexCount, cmd.startIndex, cmd.baseVertex);
                        m_context->RSSetState(m_rasterizerState.Get());
                    }
                }
            }
        }

        // ============================== 카메라(큐브) 렌더링 ==================================
        // 3. 카메라를 큐브로 렌더링 (에디터 모드이고 Play 중이 아닐 때만)
        if (editorMode && !isPlaying)
        {
            // 정적 메시용 셰이더로 복귀
            m_context->VSSetShader(m_gBufferVS.Get(), nullptr, 0);
            m_context->PSSetShader(m_gBufferPS.Get(), nullptr, 0);
            m_context->IASetInputLayout(m_gBufferInputLayout.Get());
            m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            UINT stride = sizeof(SimpleVertex);
            UINT offset = 0;
            ID3D11Buffer* vb = m_cubeVB.Get();
            m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            m_context->IASetIndexBuffer(m_cubeIB.Get(), DXGI_FORMAT_R16_UINT, 0);

            const auto& cameraComponents = world.GetComponents<CameraComponent>();
            for (const auto& [camId, _] : cameraComponents)
            {
                const auto* camTr = world.GetComponent<TransformComponent>(camId);
                if (!camTr) continue;

                using namespace DirectX;

                // 카메라 위치에 스케일 0.5, 0.5, 0.5인 큐브 렌더링
                TransformComponent cameraCubeTr = *camTr;
                cameraCubeTr.scale = { 0.5f, 0.5f, 0.5f };
                XMMATRIX cameraCubeWorld = BuildWorldMatrix(cameraCubeTr);

                // 카메라 큐브 재질 (흰색)
                XMFLOAT4 cameraCubeColor(1.0f, 1.0f, 1.0f, 1.0f);
                UpdatePerObjectCB(cameraCubeWorld, view, proj, cameraCubeColor, 0.5f, 0.0f, false, false, shadingMode);

                ID3D11ShaderResourceView* srvs[] = { nullptr, nullptr };
                m_context->PSSetShaderResources(0, 2, srvs);

                m_context->DrawIndexed(m_cubeIndexCount, 0, 0);

                // 카메라의 forward 방향을 보여주는 하늘색 큐브
                // forward 벡터 계산 (rotation에서)
                XMVECTOR rotVec = XMLoadFloat3(&camTr->rotation);
                // 행렬 대신 쿼터니언 생성
                XMVECTOR rotQuat = XMQuaternionRotationRollPitchYawFromVector(rotVec);

                // 쿼터니언으로 회전
                XMVECTOR forwardVec = XMVector3Rotate(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotQuat);
                XMFLOAT3 forward;
                XMStoreFloat3(&forward, forwardVec);

                // forward 방향으로 약간 앞에 큐브 배치 (하늘색)
                const float forwardDistance = 0.75f;
                TransformComponent directionCubeTr;
                directionCubeTr.position = {
                    camTr->position.x + forward.x * forwardDistance,
                    camTr->position.y + forward.y * forwardDistance,
                    camTr->position.z + forward.z * forwardDistance
                };
                directionCubeTr.rotation = camTr->rotation;
                directionCubeTr.scale = { 0.3f, 0.3f, 0.2f };

                XMMATRIX directionCubeWorld = BuildWorldMatrix(directionCubeTr);

                // 하늘색 (0.5, 0.8, 1.0)
                XMFLOAT4 skyBlueColor(0.5f, 0.8f, 1.0f, 1.0f);
                UpdatePerObjectCB(directionCubeWorld, view, proj, skyBlueColor, 0.5f, 0.0f, false, false, shadingMode);

                m_context->DrawIndexed(m_cubeIndexCount, 0, 0);
            }
        }

        // RTV 해제
        ID3D11RenderTargetView* nullRTVs[GBufferCount] = { nullptr };
        m_context->OMSetRenderTargets(GBufferCount, nullRTVs, nullptr);
    }

    void DeferredRenderSystem::PassDeferredLight(const World& world, const Camera& camera, int shadingMode, bool enableFillLight, DirectX::CXMMATRIX lightViewProj)
    {
        // 뷰포트 설정 (ForwardRenderSystem과 동일)
        D3D11_VIEWPORT vp{};
        vp.Width = (float)m_sceneWidth; vp.Height = (float)m_sceneHeight; vp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &vp);

        // 씬 타겟 설정
        m_context->OMSetRenderTargets(1, m_sceneRTV.GetAddressOf(), nullptr);
        // FullScreen Quad 패스는 DSV를 사용하지 않으므로 Depth Test를 반드시 꺼야 합니다.
        float blendFactor[4] = { 0, 0, 0, 0 };
        m_context->OMSetBlendState(m_ppBlendOpaque.Get(), blendFactor, 0xFFFFFFFF);
        m_context->OMSetDepthStencilState(m_ppDepthOff.Get(), 0);
        m_context->RSSetState(m_ppRasterNoCull.Get());

        // 클리어 (배경색)
        float clearColor[4] = { m_backgroundColor.x, m_backgroundColor.y, m_backgroundColor.z, m_backgroundColor.w };
        m_context->ClearRenderTargetView(m_sceneRTV.Get(), clearColor);

        // G-Buffer 텍스처 바인딩
        std::vector<ID3D11ShaderResourceView*> srvs = {
            m_gBufferSRVs[0].Get(), // Position
            m_gBufferSRVs[1].Get(), // Normal
            m_gBufferSRVs[2].Get(), // Metalness
            m_gBufferSRVs[3].Get(), // Roughness
            m_gBufferSRVs[4].Get(), // BaseColor
            m_iblDiffuseSRV.Get(),   // IBL Diffuse
            m_iblSpecularSRV.Get(),  // IBL Specular
            m_iblBrdfLutSRV.Get(),   // IBL BRDF LUT
            m_shadowSRV.Get()        // Shadow Map
        };
        m_context->PSSetShaderResources(0, static_cast<UINT>(srvs.size()), srvs.data());

        // 샘플러 설정
        ID3D11SamplerState* samplers[] = { m_samplerState.Get(), m_shadowSampler.Get(), m_samplerLinear.Get() };
        m_context->PSSetSamplers(0, 3, samplers);

        // 상수 버퍼 업데이트 (섀도우 파라미터 포함)
        UpdateLightingCB(camera, shadingMode, enableFillLight, lightViewProj);
        UpdateExtraLightsCB(world);

         // ShadowCB(b4) 업데이트 (패킹 안전)
        // - Shadow 행렬/파라미터는 ShadowCB에서만 읽도록(셰이더) 변경했습니다.
        if (m_cbShadow)
        {
            struct ShadowCBData
            {
                DirectX::XMMATRIX lightViewProjT;
                float bias;
                float mapSize;
                float pcfRadius;
                int   enabled;
                float pad[3];
            };

            ShadowCBData scb{};
            scb.lightViewProjT = DirectX::XMMatrixTranspose(lightViewProj);
            scb.bias = m_shadowSettings.bias;
            scb.mapSize = (float)m_shadowSettings.mapSizePx;
            scb.pcfRadius = m_shadowSettings.pcfRadius;
            scb.enabled = m_shadowSettings.enabled ? 1 : 0;

            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(m_context->Map(m_cbShadow.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                std::memcpy(mapped.pData, &scb, sizeof(scb));
                m_context->Unmap(m_cbShadow.Get(), 0);
            }

            ID3D11Buffer* cb = m_cbShadow.Get();
            m_context->PSSetConstantBuffers(4, 1, &cb); // b4
        }


        // FullScreen Quad 그리기
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->IASetInputLayout(m_quadInputLayout.Get());
        m_context->IASetVertexBuffers(0, 1, m_quadVB.GetAddressOf(), &m_quadStride, &m_quadOffset);
        m_context->IASetIndexBuffer(m_quadIB.Get(), DXGI_FORMAT_R16_UINT, 0);

        m_context->VSSetShader(m_quadVS.Get(), nullptr, 0);
        m_context->PSSetShader(m_deferredLightPS.Get(), nullptr, 0);
        m_context->DrawIndexed(m_quadIndexCount, 0, 0);

        // 리소스 해제
        ID3D11ShaderResourceView* nullSRVs[9] = { nullptr };
        m_context->PSSetShaderResources(0, 9, nullSRVs);
    }

    void DeferredRenderSystem::PassTransparentForward(
        const Camera& camera,
        const std::vector<SkinnedDrawCommand>& skinnedCommands,
        int shadingMode)
    {
        if (!m_device || !m_context) return;
        if (!m_sceneRTV || !m_sceneDSV) return;
        if (!m_alphaBlendState || !m_depthStencilStateReadOnly) return;
        if (!m_transparentSkinnedVS || !m_transparentPS || !m_transparentSkinnedInputLayout) return;
        if (!m_cbTransparentLight) return;

        // 현재는 "반투명 문제가 주로 FBX(스키닝) 쪽"에서 발생하므로 스키닝 커맨드만 처리합니다.
        if (skinnedCommands.empty()) return;

        // 렌더 타깃: HDR 씬 컬러 + (GBuffer에서 채운) 깊이 버퍼
        m_context->OMSetRenderTargets(1, m_sceneRTV.GetAddressOf(), m_sceneDSV.Get());

        // 블렌딩 ON, 깊이 테스트 ON(읽기 전용)
        float blendFactor[4] = { 0, 0, 0, 0 };
        m_context->OMSetBlendState(m_alphaBlendState.Get(), blendFactor, 0xFFFFFFFF);
        m_context->OMSetDepthStencilState(m_depthStencilStateReadOnly.Get(), 0);
        m_context->RSSetState(m_rasterizerState.Get());

        // 파이프라인
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->IASetInputLayout(m_transparentSkinnedInputLayout.Get());
        m_context->VSSetShader(m_transparentSkinnedVS.Get(), nullptr, 0);
        m_context->PSSetShader(m_transparentPS.Get(), nullptr, 0);

        // 샘플러
        ID3D11SamplerState* samplers[] = { m_samplerState.Get() };
        m_context->PSSetSamplers(0, 1, samplers);

        // Transparent Light CB 업데이트 (register b1)
        struct TransparentLightCB
        {
            DirectX::XMFLOAT3 lightDir;
            float             intensity;
            DirectX::XMFLOAT3 lightColor;
            float             pad0;
            DirectX::XMFLOAT3 cameraPos;
            float             pad1;
        };

        TransparentLightCB tl{};
        tl.lightDir = m_lightingParameters.keyDirection;
        tl.intensity = m_lightingParameters.keyIntensity;
        tl.lightColor = m_lightingParameters.diffuseColor;
        tl.cameraPos = camera.GetPosition();

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(m_context->Map(m_cbTransparentLight.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            std::memcpy(mapped.pData, &tl, sizeof(tl));
            m_context->Unmap(m_cbTransparentLight.Get(), 0);
        }
        ID3D11Buffer* tlCB = m_cbTransparentLight.Get();
        m_context->PSSetConstantBuffers(1, 1, &tlCB);

        // IBL 리소스 바인딩 (t5~t7)
        ID3D11ShaderResourceView* iblDiffuse = m_iblDiffuseSRV.Get();
        ID3D11ShaderResourceView* iblSpec = m_iblSpecularSRV.Get();
        ID3D11ShaderResourceView* iblBrdf = m_iblBrdfLutSRV.Get();
        ID3D11ShaderResourceView* iblSrvs[] = { iblDiffuse, iblSpec, iblBrdf };
        m_context->PSSetShaderResources(5, 3, iblSrvs);

        // 공통 행렬
        DirectX::XMMATRIX view = camera.GetViewMatrix();
        DirectX::XMMATRIX proj = camera.GetProjectionMatrix();

        for (const auto& cmd : skinnedCommands)
        {
            if (!cmd.vertexBuffer || !cmd.indexBuffer || cmd.indexCount == 0) continue;

            UINT stride = cmd.stride;
            UINT offset = 0;
            ID3D11Buffer* vb = cmd.vertexBuffer;
            m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            m_context->IASetIndexBuffer(cmd.indexBuffer, DXGI_FORMAT_R32_UINT, 0);

            // Bones
            UpdateBonesCB(cmd.bones, cmd.boneCount);

            // PerObject CB
            const DirectX::XMFLOAT4 color(cmd.color.x, cmd.color.y, cmd.color.z, 1.0f);
            const int objectShadingMode = (cmd.shadingMode >= 0) ? cmd.shadingMode : shadingMode;
            XMFLOAT3 outlineColor = cmd.outlineColor;
            float outlineWidth = cmd.outlineWidth;

            // FBX 서브셋 머티리얼이 있으면 그걸 우선 사용 (Forward와 동일)
            std::shared_ptr<SkinnedMeshGPU> mesh =
                (m_skinnedRegistry && !cmd.meshKey.empty()) ? m_skinnedRegistry->Find(cmd.meshKey) : nullptr;

            if (mesh && !mesh->subsets.empty())
            {
                for (const auto& sub : mesh->subsets)
                {
                    if (sub.indexCount == 0) continue;

                    ID3D11ShaderResourceView* diff =
                        (sub.materialIndex < mesh->materialSRVs.size()) ? mesh->materialSRVs[sub.materialIndex].Get() : nullptr;
                    ID3D11ShaderResourceView* norm =
                        (sub.materialIndex < mesh->normalSRVs.size()) ? mesh->normalSRVs[sub.materialIndex].Get() : nullptr;

                    // t0: diffuse, t1: normal
                    ID3D11ShaderResourceView* srvs01[2] = { diff, norm };
                    m_context->PSSetShaderResources(0, 2, srvs01);

                    // enableNormalMap은 "노말 SRV가 존재할 때만" 켜는게 안정적입니다.
                    const int objectShadingMode = (cmd.shadingMode >= 0) ? cmd.shadingMode : shadingMode;
                    
                    // Pass 1. 원본
                    UpdatePerObjectCB(cmd.world, view, proj, color, cmd.roughness, cmd.metalness, (diff != nullptr), (norm != nullptr), objectShadingMode, cmd.normalStrength, outlineColor, 0.0f);
                    m_context->DrawIndexed(sub.indexCount, sub.startIndex, cmd.baseVertex);
                    
                    // Pass 2. 아웃라인
                    if (outlineWidth > 0.0f)
                    {
                        m_context->RSSetState(m_rsCullFront.Get());
                        UpdatePerObjectCB(cmd.world, view, proj, color, cmd.roughness, cmd.metalness, (diff != nullptr), (norm != nullptr), objectShadingMode, cmd.normalStrength, outlineColor, outlineWidth);
                        m_context->DrawIndexed(sub.indexCount, sub.startIndex, cmd.baseVertex);
                        m_context->RSSetState(m_rasterizerState.Get());
                    }
                }
            }
            else
            {
                // 머티리얼 오버라이드(에디터) 경로가 있으면 그걸 사용
                ID3D11ShaderResourceView* diff = GetOrCreateTexture(cmd.albedoTexturePath);
                ID3D11ShaderResourceView* srvs01[2] = { diff, nullptr };
                m_context->PSSetShaderResources(0, 2, srvs01);
                const int objectShadingMode = (cmd.shadingMode >= 0) ? cmd.shadingMode : shadingMode;
                
                // [Pass 1] 원본
                UpdatePerObjectCB(cmd.world, view, proj, color, cmd.roughness, cmd.metalness, (diff != nullptr), false, objectShadingMode, cmd.normalStrength, outlineColor, 0.0f);
                m_context->DrawIndexed(cmd.indexCount, cmd.startIndex, cmd.baseVertex);
                
                // [Pass 2] 아웃라인
                if (outlineWidth > 0.0f)
                {
                    m_context->RSSetState(m_rsCullFront.Get());
                    UpdatePerObjectCB(cmd.world, view, proj, color, cmd.roughness, cmd.metalness, (diff != nullptr), false, objectShadingMode, cmd.normalStrength, outlineColor, outlineWidth);
                    m_context->DrawIndexed(cmd.indexCount, cmd.startIndex, cmd.baseVertex);
                    m_context->RSSetState(m_rasterizerState.Get());
                }
            }
        }

        // SRV 정리 (D3D11 hazard 방지)
        ID3D11ShaderResourceView* nulls[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
        m_context->PSSetShaderResources(0, 8, nulls);
    }

    void DeferredRenderSystem::RenderSkybox(const Camera& camera)
    {
        // 유효성 체크 (ForwardRenderSystem과 동일)
        if (!m_skyboxEnabled || !m_skyboxSRV || !m_skyboxVS || !m_skyboxPS || !m_cbSkybox) return;

        // 씬 타겟에 렌더링 (깊이 버퍼 사용)
        m_context->OMSetRenderTargets(1, m_sceneRTV.GetAddressOf(), m_sceneDSV.Get());
        m_context->OMSetDepthStencilState(m_skyboxDepthState.Get(), 0);
        m_context->RSSetState(m_skyboxRasterizerState.Get());

        // 큐브 지오메트리 설정 (ForwardRenderSystem의 큐브 사용 - 필요시 별도 생성)
        // 현재는 간단히 하기 위해 인라인 큐브 데이터 사용
        struct SkyboxVertex { XMFLOAT3 Position; };
        SkyboxVertex vertices[] = {
            {{-1,-1, 1}}, {{-1, 1, 1}}, {{ 1, 1, 1}}, {{ 1,-1, 1}},
            {{-1,-1,-1}}, {{ 1,-1,-1}}, {{ 1, 1,-1}}, {{-1, 1,-1}},
            {{-1, 1,-1}}, {{ 1, 1,-1}}, {{ 1, 1, 1}}, {{-1, 1, 1}},
            {{-1,-1,-1}}, {{-1,-1, 1}}, {{ 1,-1, 1}}, {{ 1,-1,-1}},
            {{-1,-1,-1}}, {{-1, 1,-1}}, {{-1, 1, 1}}, {{-1,-1, 1}},
            {{ 1,-1,-1}}, {{ 1,-1, 1}}, {{ 1, 1, 1}}, {{ 1, 1,-1}}
        };
        uint16_t indices[] = {
            0,1,2, 0,2,3,     4,5,6, 4,6,7,     8,9,10, 8,10,11,
            12,13,14, 12,14,15, 16,17,18, 16,18,19, 20,21,22, 20,22,23
        };

        // 임시 버퍼 생성 (최적화: 초기화 시 생성하는 것이 좋음)
        ComPtr<ID3D11Buffer> skyboxVB, skyboxIB;
        D3D11_BUFFER_DESC vbDesc = { sizeof(vertices), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA vbData = { vertices, 0, 0 };
        if (FAILED(m_device->CreateBuffer(&vbDesc, &vbData, skyboxVB.GetAddressOf()))) return;

        D3D11_BUFFER_DESC ibDesc = { sizeof(indices), D3D11_USAGE_DEFAULT, D3D11_BIND_INDEX_BUFFER, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA ibData = { indices, 0, 0 };
        if (FAILED(m_device->CreateBuffer(&ibDesc, &ibData, skyboxIB.GetAddressOf()))) return;

        // IA 설정
        UINT stride = sizeof(SkyboxVertex), offset = 0;
        ID3D11Buffer* vb = skyboxVB.Get();
        m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        m_context->IASetIndexBuffer(skyboxIB.Get(), DXGI_FORMAT_R16_UINT, 0);
        m_context->IASetInputLayout(m_skyboxInputLayout.Get());
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        m_context->VSSetShader(m_skyboxVS.Get(), nullptr, 0);
        m_context->PSSetShader(m_skyboxPS.Get(), nullptr, 0);

        // 행렬 계산 (Translation 제거)
        XMMATRIX view = camera.GetViewMatrix();
        view.r[3] = XMVectorSet(0.f, 0.f, 0.f, 1.f);
        XMMATRIX wvpT = XMMatrixTranspose(view * camera.GetProjectionMatrix());

        D3D11_MAPPED_SUBRESOURCE map;
        if (SUCCEEDED(m_context->Map(m_cbSkybox.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map)))
        {
            memcpy(map.pData, &wvpT, sizeof(XMMATRIX));
            m_context->Unmap(m_cbSkybox.Get(), 0);
        }

        // 리소스 바인딩
        ID3D11Buffer* cb = m_cbSkybox.Get();
        ID3D11ShaderResourceView* srv = m_skyboxSRV.Get();
        ID3D11SamplerState* sam = m_samplerState.Get();

        m_context->VSSetConstantBuffers(0, 1, &cb);
        m_context->PSSetShaderResources(0, 1, &srv);
        m_context->PSSetSamplers(0, 1, &sam);

        m_context->DrawIndexed(36, 0, 0);
    }

    void DeferredRenderSystem::UpdatePerObjectCB(const DirectX::XMMATRIX& world,
                                                  const DirectX::XMMATRIX& view,
                                                  const DirectX::XMMATRIX& projection)
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(m_context->Map(m_cbPerObject.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            XMMATRIX* data = (XMMATRIX*)mapped.pData;
            data[0] = XMMatrixTranspose(world);
            data[1] = XMMatrixTranspose(view);
            data[2] = XMMatrixTranspose(projection);
            data[3] = XMMatrixTranspose(XMMatrixInverse(nullptr, world));
            m_context->Unmap(m_cbPerObject.Get(), 0);
        }

        m_context->VSSetConstantBuffers(0, 1, m_cbPerObject.GetAddressOf());
    }

    void DeferredRenderSystem::UpdatePerObjectCB(const DirectX::XMMATRIX& world,
                                                 const DirectX::XMMATRIX& view,
                                                 const DirectX::XMMATRIX& projection,
                                                 const DirectX::XMFLOAT4& color,
                                                 float roughness,
                                                 float metalness,
                                                 bool useTexture,
                                                 bool enableNormalMap,
                                                 int shadingMode,
                                                 float normalStrength,
                                                 const XMFLOAT3& outlineColor,
                                                 float outlineWidth)
    {
        struct CBPerObjectData
        {
            XMMATRIX gWorld;
            XMMATRIX gView;
            XMMATRIX gProj;
            XMFLOAT4 gMaterialColor;
            float    gRoughness;
            float    gMetalness;
            int      gUseTexture;
            int      gEnableNormalMap;
            int      gShadingMode;
            int      gPad0;
            // [Fixed] HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
            float    gPad1[2];        // Offset: 232 -> 240
            // 노말맵 강도 조절
            float    gNormalStrength; // Offset: 240 -> 244
            float    gPad2;           // Offset: 244 -> 248
            // [중요] HLSL에서 float3는 16바이트 경계를 걸칠 수 없음.
            // 현재 248번지이므로, 12바이트짜리 outlineColor가 들어갈 수 없어 256번지로 밀림.
            float    gPadAlign[2];    // Offset: 248 -> 256 (8바이트 패딩)
            XMFLOAT3 gOutlineColor;   // Offset: 256 -> 268
            float    gOutlineWidth;   // Offset: 268 -> 272
        };

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(m_context->Map(m_cbPerObject.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            CBPerObjectData* data = (CBPerObjectData*)mapped.pData;
            data->gWorld = XMMatrixTranspose(world);
            data->gView  = XMMatrixTranspose(view);
            data->gProj  = XMMatrixTranspose(projection);
            data->gMaterialColor = color;
            data->gRoughness = roughness;
            data->gMetalness = metalness;
            data->gUseTexture = useTexture ? 1 : 0;
            data->gEnableNormalMap = enableNormalMap ? 1 : 0;
            data->gShadingMode = shadingMode;
            data->gPad0 = 0;
            // 패딩 초기화 (안전하게 0으로)
            data->gPad1[0] = 0.0f;
            data->gPad1[1] = 0.0f;
            data->gNormalStrength = normalStrength;
            data->gPad2 = 0.0f;
            // 패딩 초기화 (안전하게 0으로)
            data->gPadAlign[0] = 0.0f;
            data->gPadAlign[1] = 0.0f;
            data->gOutlineColor = outlineColor;
            data->gOutlineWidth = outlineWidth;
            m_context->Unmap(m_cbPerObject.Get(), 0);
        }

        m_context->VSSetConstantBuffers(0, 1, m_cbPerObject.GetAddressOf());
        // PS에서도 재질 정보를 사용하므로 반드시 바인딩
        m_context->PSSetConstantBuffers(0, 1, m_cbPerObject.GetAddressOf());
    }

    void DeferredRenderSystem::UpdateLightingCB(const Camera& camera, int shadingMode, bool /*enableFillLight*/, DirectX::CXMMATRIX lightViewProj)
    {
        // IMPORTANT:
        // - ConstantBufferData는 매우 큰 구조체이므로 "부분만 채우고 memcpy" 하면
        //   나머지 필드가 쓰레기 값이 되어 라이팅/파라미터가 랜덤하게 깨질 수 있습니다.
        // - 반드시 0 초기화 후 필요한 값을 모두 안정적으로 세팅합니다.
        ConstantBufferData cbData = {};

        // (1) 행렬: Deferred Light PS에서는 주로 g_EyePosW / PBR 파라미터 등을 사용하지만,
        //     구조체에 행렬 필드가 있으므로 안전하게 채웁니다.
        cbData.g_World = XMMatrixIdentity();
        cbData.g_View = XMMatrixIdentity();
        cbData.g_Proj = XMMatrixIdentity();
        cbData.g_WorldInvTranspose = XMMatrixIdentity();
        cbData.g_LightViewProj = XMMatrixTranspose(lightViewProj); // (b0에도 보관: 디버그/호환용)

        // (2) 카메라
        cbData.g_EyePosW = camera.GetPosition();

        // (3) 셰이딩 모드
        cbData.g_ShadingMode = shadingMode;

        // (4) PBR/재질 파라미터 (Deferred PS가 참조하는 값 포함)
        cbData.g_PBRBaseColor = XMFLOAT4(m_lightingParameters.baseColor.x,
                                         m_lightingParameters.baseColor.y,
                                         m_lightingParameters.baseColor.z,
                                         1.0f);
        cbData.g_PBRMetalness = m_lightingParameters.metalness;
        cbData.g_PBRRoughness = m_lightingParameters.roughness;
        cbData.g_PBRAmbientOcclusion = m_lightingParameters.ambientOcclusion;
        cbData.g_UseTextureColor = 1;

        // (5) 섀도우 (PCF) - b4(ShadowCB)가 실제로 사용되지만, b0에도 안정적으로 채워둡니다.
        cbData.g_ShadowBias = m_shadowSettings.bias;
        cbData.g_ShadowMapSize = (float)m_shadowSettings.mapSizePx;
        cbData.g_ShadowPCFRadius = m_shadowSettings.pcfRadius;
        cbData.g_ShadowEnabled = m_shadowSettings.enabled ? 1 : 0;

        // (6) Directional light (b0에 있는 레거시 필드도 일관되게 세팅)
        cbData.g_DirLight_direction = m_lightingParameters.keyDirection;
        cbData.g_DirLight_intensity = m_lightingParameters.keyIntensity;
        cbData.g_DirLight_diffuse = XMFLOAT4(m_lightingParameters.diffuseColor.x,
                                             m_lightingParameters.diffuseColor.y,
                                             m_lightingParameters.diffuseColor.z,
                                             1.0f);

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(m_context->Map(m_cbLighting.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            memcpy(mapped.pData, &cbData, sizeof(ConstantBufferData));
            m_context->Unmap(m_cbLighting.Get(), 0);
        }

        // ConstantBuffer 바인딩 (register b0)
        m_context->PSSetConstantBuffers(0, 1, m_cbLighting.GetAddressOf());

        
        // DirectionalLightBuffer(b3)는 Deferred Light PS에서 직접 사용합니다.
        // ShadowPass의 라이트 방향/강도와 반드시 동일해야 섀도우 방향/세기가 일치합니다.
        DirectionalLightData lightData = {};
        {
            XMVECTOR dir = XMLoadFloat3(&m_lightingParameters.keyDirection);
            if (XMVector3Equal(dir, XMVectorZero()))
                dir = XMVectorSet(0, -1, 0, 0);
            dir = XMVector3Normalize(dir);

            XMFLOAT3 dirN{};
            XMStoreFloat3(&dirN, dir);
            lightData.direction = XMFLOAT4(dirN.x, dirN.y, dirN.z, 0.0f);
        }
        lightData.color = XMFLOAT4(m_lightingParameters.diffuseColor.x,
                                   m_lightingParameters.diffuseColor.y,
                                   m_lightingParameters.diffuseColor.z,
                                   0.0f);
        lightData.intensity = m_lightingParameters.keyIntensity;

        if (SUCCEEDED(m_context->Map(m_cbDirectionalLight.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            memcpy(mapped.pData, &lightData, sizeof(DirectionalLightData));
            m_context->Unmap(m_cbDirectionalLight.Get(), 0);
        }

        m_context->PSSetConstantBuffers(3, 1, m_cbDirectionalLight.GetAddressOf());
    }

    void DeferredRenderSystem::UpdateExtraLightsCB(const World& world)
    {
        if (!m_cbExtraLights) return;

        ExtraLightsCB data = {};

        // Point lights
        for (const auto& [id, light] : world.GetComponents<PointLightComponent>())
        {
            if (!light.enabled) continue;
            if (data.pointCount >= MaxPointLights) break;
            const auto* tr = world.GetComponent<TransformComponent>(id);
            if (!tr || !tr->enabled) continue;

            auto& dst = data.pointLights[data.pointCount++];
            dst.position = tr->position;
            dst.range = (std::max)(light.range, 0.01f);
            dst.color = light.color;
            dst.intensity = light.intensity;
        }

        // Spot lights
        for (const auto& [id, light] : world.GetComponents<SpotLightComponent>())
        {
            if (!light.enabled) continue;
            if (data.spotCount >= MaxSpotLights) break;
            const auto* tr = world.GetComponent<TransformComponent>(id);
            if (!tr || !tr->enabled) continue;

            XMVECTOR forward = XMVectorSet(0, 0, 1, 0);
            XMMATRIX rot = XMMatrixRotationRollPitchYawFromVector(XMLoadFloat3(&tr->rotation));
            XMVECTOR dirW = XMVector3Normalize(XMVector3TransformNormal(forward, rot));
            XMFLOAT3 dir{};
            XMStoreFloat3(&dir, dirW);

            float innerRad = DirectX::XMConvertToRadians((std::max)(0.0f, light.innerAngleDeg));
            float outerRad = DirectX::XMConvertToRadians((std::max)(light.innerAngleDeg, light.outerAngleDeg));

            auto& dst = data.spotLights[data.spotCount++];
            dst.position = tr->position;
            dst.range = (std::max)(light.range, 0.01f);
            dst.direction = dir;
            dst.innerCos = std::cosf(innerRad);
            dst.outerCos = std::cosf(outerRad);
            dst.color = light.color;
            dst.intensity = light.intensity;
        }

        // Rect lights
        for (const auto& [id, light] : world.GetComponents<RectLightComponent>())
        {
            if (!light.enabled) continue;
            if (data.rectCount >= MaxRectLights) break;
            const auto* tr = world.GetComponent<TransformComponent>(id);
            if (!tr || !tr->enabled) continue;

            XMVECTOR forward = XMVectorSet(0, 0, 1, 0);
            XMMATRIX rot = XMMatrixRotationRollPitchYawFromVector(XMLoadFloat3(&tr->rotation));
            XMVECTOR dirW = XMVector3Normalize(XMVector3TransformNormal(forward, rot));
            XMFLOAT3 dir{};
            XMStoreFloat3(&dir, dirW);

            auto& dst = data.rectLights[data.rectCount++];
            dst.position = tr->position;
            dst.range = (std::max)(light.range, 0.01f);
            dst.direction = dir;
            dst.width = (std::max)(light.width, 0.01f);
            dst.height = (std::max)(light.height, 0.01f);
            dst.color = light.color;
            dst.intensity = light.intensity;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(m_context->Map(m_cbExtraLights.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            std::memcpy(mapped.pData, &data, sizeof(ExtraLightsCB));
            m_context->Unmap(m_cbExtraLights.Get(), 0);
        }

        m_context->PSSetConstantBuffers(5, 1, m_cbExtraLights.GetAddressOf());
    }

    void DeferredRenderSystem::UpdateBonesCB(const DirectX::XMFLOAT4X4* boneMatrices, std::uint32_t boneCount)
    {
        // ForwardRenderSystem과 동일한 구현
        if (!m_cbBones || !boneMatrices || boneCount == 0) return;

        static constexpr std::uint32_t MaxBones = 1023;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(m_context->Map(m_cbBones.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;


        auto* cb = reinterpret_cast<CBBones*>(mapped.pData);
        cb->boneCount = (std::min)(boneCount, MaxBones);

        // 유효한 본은 Transpose해서 넣고, 나머지는 Identity로 채움
        for (std::uint32_t i = 0; i < MaxBones; ++i)
        {
            if (i < cb->boneCount) cb->bones[i] = XMMatrixTranspose(XMLoadFloat4x4(&boneMatrices[i]));
            else cb->bones[i] = XMMatrixIdentity();
        }

        m_context->Unmap(m_cbBones.Get(), 0);
        m_context->VSSetConstantBuffers(2, 1, m_cbBones.GetAddressOf());
    }

    DirectX::XMMATRIX DeferredRenderSystem::BuildWorldMatrix(const TransformComponent& transform) const
    {
        XMVECTOR scale = XMLoadFloat3(&transform.scale);
        XMVECTOR rotation = XMLoadFloat3(&transform.rotation);
        XMVECTOR translation = XMLoadFloat3(&transform.position);
        
        XMMATRIX S = XMMatrixScalingFromVector(scale);
        XMMATRIX R = XMMatrixRotationRollPitchYawFromVector(rotation);
        XMMATRIX T = XMMatrixTranslationFromVector(translation);
        
        // DirectXMath 행벡터 컨벤션: S * R * T
        return S * R * T;
    }

    DirectX::XMMATRIX DeferredRenderSystem::BuildWorldMatrix(const World& world, EntityId entityId, const TransformComponent& transform) const
    {
        // c.txt 참조: 부모부터 루트까지 로컬 행렬을 스택에 쌓고, 루트에서 자식으로 내려가면서 행렬 곱하기
        std::vector<XMMATRIX> matrixStack;
        EntityId currentId = entityId;
        
        // 부모부터 루트까지 로컬 행렬을 스택에 쌓음
        while (currentId != InvalidEntityId)
        {
            const TransformComponent* t = world.GetComponent<TransformComponent>(currentId);
            if (t)
            {
                XMVECTOR scale = XMLoadFloat3(&t->scale);
                XMVECTOR rotation = XMLoadFloat3(&t->rotation);
                XMVECTOR translation = XMLoadFloat3(&t->position);
                
                // 로컬 행렬: S * R * T 순서 (DirectXMath 행벡터 컨벤션)
                XMMATRIX localMatrix = XMMatrixScalingFromVector(scale) *
                    XMMatrixRotationRollPitchYawFromVector(rotation) *
                    XMMatrixTranslationFromVector(translation);
                
                matrixStack.push_back(localMatrix);
                currentId = t->parent;
            }
            else
            {
                break;
            }
        }
        
        // 행벡터 컨벤션: child * parent * ... * root 형태로 곱하기 (정순)
        XMMATRIX worldMatrix = XMMatrixIdentity();
        for (const auto& m : matrixStack)  // child -> parent -> root 순서
        {
            worldMatrix = worldMatrix * m;  // I * child * parent * ... * root
        }
        
        return worldMatrix;
    }

    ID3D11ShaderResourceView* DeferredRenderSystem::GetOrCreateTexture(const std::string& path)
    {
        // ForwardRenderSystem과 동일한 구현
        if (path.empty()) return nullptr;

        auto it = m_textureCache.find(path);
        if (it != m_textureCache.end()) return it->second.Get();

        if (!m_device || !m_resources) return nullptr;

        auto srv = m_resources->LoadData<ID3D11ShaderResourceView>(std::filesystem::path(path), m_device.Get());

        if (!srv)
        {
            ALICE_LOG_WARN("[DeferredRenderSystem] Texture load FAILED: \"%s\"", path.c_str());
            return nullptr;
        }

        m_textureCache.emplace(path, srv);
        ALICE_LOG_INFO("[DeferredRenderSystem] Texture loaded: \"%s\"", path.c_str());

        return srv.Get();
    }

    void DeferredRenderSystem::GetPostProcessParams(float& outExposure, float& outMaxHDRNits) const
    {
        outExposure = m_postProcessParams.exposure;
        
        // RenderDevice에서 HDR 지원 여부 및 최대 밝기 가져오기
        float maxNits = 100.0f;
        m_renderDevice.IsHDRSupported(maxNits);
        // 사용자가 설정한 값이 있으면 사용, 없으면 모니터 최대 밝기 사용
        outMaxHDRNits = (m_postProcessParams.maxHDRNits > 0.0f) ? m_postProcessParams.maxHDRNits : maxNits;
    }

    void DeferredRenderSystem::SetPostProcessParams(float exposure, float maxHDRNits)
    {
        m_postProcessParams.exposure = exposure;
        m_postProcessParams.maxHDRNits = maxHDRNits;
    }

    void DeferredRenderSystem::SetBloomSettings(const BloomSettings& settings)
    {
        bool downsampleChanged = (m_bloomSettings.downsample != settings.downsample);
        m_bloomSettings = settings;
        
        // 다운샘플링 변경 시 리소스 재생성
        if (downsampleChanged && m_sceneWidth > 0 && m_sceneHeight > 0)
        {
            CreateBloomResources(m_sceneWidth, m_sceneHeight);
        }
    }

    bool DeferredRenderSystem::SetIblSet(const std::string& iblDir, const std::string& iblName)
    {
        return CreateIblResources(iblDir, iblName);
    }

    void DeferredRenderSystem::SetSkyboxEnabled(bool enabled)
    {
        m_skyboxEnabled = enabled;
    }

    void DeferredRenderSystem::RestoreBackBuffer()
    {
        // ForwardRenderSystem과 동일한 구현
        ID3D11RenderTargetView* backBufferRTV = m_renderDevice.GetBackBufferRTV();
        ID3D11DepthStencilView* backBufferDSV = m_renderDevice.GetBackBufferDSV();

        if (backBufferRTV)
        {
            ID3D11RenderTargetView* rtvs[] = { backBufferRTV };
            m_context->OMSetRenderTargets(1, rtvs, backBufferDSV);
        }
    }

    void DeferredRenderSystem::RenderToneMapping(ID3D11ShaderResourceView* inputSRV, ID3D11RenderTargetView* targetRTV, const D3D11_VIEWPORT& viewport)
    {
        if (!m_toneMappingPS || !m_quadVS || !inputSRV || !targetRTV) return;

        // 뷰포트 설정
        m_context->RSSetViewports(1, &viewport);

        // 렌더 타겟 설정
        m_context->OMSetRenderTargets(1, &targetRTV, nullptr);

        // 상태 정리 (이전 패스의 상태가 남아있으면 후처리가 이상해짐 조심하셈)
        float blendFactor[4] = { 0, 0, 0, 0 };
        m_context->OMSetBlendState(m_ppBlendOpaque.Get(), blendFactor, 0xFFFFFFFF);
        m_context->OMSetDepthStencilState(m_ppDepthOff.Get(), 0);
        m_context->RSSetState(m_ppRasterNoCull.Get());

        // 상수 버퍼 업데이트 (실제 노출값 적용)
        PostProcessCB cbData = {};
        GetPostProcessParams(cbData.exposure, cbData.maxHDRNits);

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(m_context->Map(m_cbPostProcess.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            std::memcpy(mapped.pData, &cbData, sizeof(PostProcessCB));
            m_context->Unmap(m_cbPostProcess.Get(), 0);
        }

        // 리소스 바인딩 (입력 텍스처 사용)
        ID3D11SamplerState* sampler = m_samplerLinear.Get();
        ID3D11Buffer* cb = m_cbPostProcess.Get();

        m_context->PSSetShaderResources(0, 1, &inputSRV);
        m_context->PSSetSamplers(0, 1, &sampler);
        m_context->PSSetConstantBuffers(2, 1, &cb); // register(b2)에 맞춰 슬롯 2 사용

        // Quad 그리기
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->IASetInputLayout(m_quadInputLayout.Get());
        m_context->IASetVertexBuffers(0, 1, m_quadVB.GetAddressOf(), &m_quadStride, &m_quadOffset);
        m_context->IASetIndexBuffer(m_quadIB.Get(), DXGI_FORMAT_R16_UINT, 0);

        m_context->VSSetShader(m_quadVS.Get(), nullptr, 0);
        m_context->PSSetShader(m_toneMappingPS.Get(), nullptr, 0);
        m_context->DrawIndexed(m_quadIndexCount, 0, 0);

        // 리소스 해제
        ID3D11ShaderResourceView* nullSRV = nullptr;
        m_context->PSSetShaderResources(0, 1, &nullSRV);
    }

    void DeferredRenderSystem::RenderParticleOverlayToViewport(ID3D11ShaderResourceView* particleSRV)
    {
        // 씬 전환 중 리소스가 유효하지 않을 수 있으므로 모든 리소스 확인
        if (!particleSRV) return;
        ID3D11RenderTargetView* viewportRTV = m_viewportRTV.Get();
        if (!viewportRTV) return;
        if (m_sceneWidth == 0 || m_sceneHeight == 0) return;
        if (!m_particleOverlayPS || !m_quadVS || !m_quadVB || !m_quadIB || !m_quadInputLayout) return;
        
        // UAV와 SRV 동시 바인딩 충돌 방지: Compute Shader에서 사용한 UAV/SRV를 명시적으로 unbind
        // DirectX11에서는 같은 리소스를 UAV와 SRV로 동시에 바인딩할 수 없음
        // Compute Shader는 CS stage에서 UAV를 사용하므로, CS stage의 UAV/SRV만 unbind하면 충분
        ID3D11UnorderedAccessView* nullUAVs[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
        UINT uavInitialCounts[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        m_context->CSSetUnorderedAccessViews(0, 8, nullUAVs, uavInitialCounts);
        
        // CS에서 SRV로도 바인딩되어 있을 수 있으므로 CS SRV도 unbind
        ID3D11ShaderResourceView* nullCSsrvs[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
        m_context->CSSetShaderResources(0, 8, nullCSsrvs);
        
        // PS의 SRV도 먼저 unbind한 후에 다시 바인딩 (안전을 위해)
        ID3D11ShaderResourceView* nullSRVs[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
        m_context->PSSetShaderResources(0, 8, nullSRVs);
        
        D3D11_VIEWPORT viewport = {};
        viewport.Width = static_cast<float>(m_sceneWidth);
        viewport.Height = static_cast<float>(m_sceneHeight);
        viewport.MaxDepth = 1.0f;
        
        // 뷰포트 설정
        m_context->RSSetViewports(1, &viewport);
        
        // 렌더 타겟 설정
        m_context->OMSetRenderTargets(1, m_viewportRTV.GetAddressOf(), nullptr);
        
        // Additive blending 활성화
        float blendFactor[4] = { 0, 0, 0, 0 };
        m_context->OMSetBlendState(m_ppBlendAdditive.Get(), blendFactor, 0xFFFFFFFF);
        m_context->OMSetDepthStencilState(m_ppDepthOff.Get(), 0);
        m_context->RSSetState(m_ppRasterNoCull.Get());
        
        // 리소스 바인딩 (UAV unbind 후에 SRV 바인딩)
        ID3D11ShaderResourceView* srv = particleSRV;
        ID3D11SamplerState* sampler = m_samplerLinear.Get();
        
        m_context->PSSetShaderResources(0, 1, &srv);
        m_context->PSSetSamplers(0, 1, &sampler);
        
        // Quad 그리기
        UINT stride = m_quadStride, offset = m_quadOffset;
        ID3D11Buffer* vb = m_quadVB.Get();
        
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->IASetInputLayout(m_quadInputLayout.Get());
        m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        m_context->IASetIndexBuffer(m_quadIB.Get(), DXGI_FORMAT_R16_UINT, 0);
        
        m_context->VSSetShader(m_quadVS.Get(), nullptr, 0);
        m_context->PSSetShader(m_particleOverlayPS.Get(), nullptr, 0);
        m_context->DrawIndexed(m_quadIndexCount, 0, 0);
        
        // 리소스 해제
        ID3D11ShaderResourceView* nullSRV = nullptr;
        m_context->PSSetShaderResources(0, 1, &nullSRV);

        // 파이프라인 상태 복원 (다음 렌더링을 위해)
        m_context->OMSetBlendState(m_ppBlendOpaque.Get(), blendFactor, 0xFFFFFFFF);
        m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);
        m_context->RSSetState(m_rasterizerState.Get());
        
        // 뷰포트 RTV를 SRV로 읽을 수 있도록 BackBuffer로 복귀 (ImGui::Image가 viewportSRV를 읽기 위해 필수)
        // DirectX11에서는 같은 리소스를 RTV와 SRV로 동시에 바인딩할 수 없음
        RestoreBackBuffer();
    }

	void DeferredRenderSystem::RenderBloomPass(ID3D11ShaderResourceView* sourceSRV, ID3D11RenderTargetView* targetRTV, const D3D11_VIEWPORT& viewport)
	{
		if (!m_bloomSettings.enabled || !sourceSRV || !targetRTV) return;
		if (!m_bloomBrightPassPS || !m_bloomDownsamplePS || !m_bloomBlurPassPS_H || !m_bloomBlurPassPS_V || !m_bloomUpsamplePS || !m_bloomCompositePS) return;

		// 상태 설정
		float blendFactor[4] = { 0, 0, 0, 0 };
		m_context->OMSetBlendState(m_ppBlendOpaque.Get(), blendFactor, 0xFFFFFFFF);
		m_context->OMSetDepthStencilState(m_ppDepthOff.Get(), 0);
		m_context->RSSetState(m_ppRasterNoCull.Get());

		// Quad 설정
		m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_context->IASetInputLayout(m_quadInputLayout.Get());
		m_context->IASetVertexBuffers(0, 1, m_quadVB.GetAddressOf(), &m_quadStride, &m_quadOffset);
		m_context->IASetIndexBuffer(m_quadIB.Get(), DXGI_FORMAT_R16_UINT, 0);
		m_context->VSSetShader(m_quadVS.Get(), nullptr, 0);

		ID3D11Buffer* cbBloom = m_cbBloom.Get();
		ID3D11SamplerState* sampler = m_samplerLinear.Get();
		ID3D11ShaderResourceView* nullSRVs[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

		// 블러 반복 횟수 (기본값: 1회, 필요시 BloomSettings에 추가 가능)
		const int blurIterations = 1;

		// ========== 1. Bright Pass: sourceSRV → level0 A ==========
		{
			std::uint32_t level0Width = m_bloomLevelWidth[0];
			std::uint32_t level0Height = m_bloomLevelHeight[0];
			D3D11_VIEWPORT level0Viewport = { 0.0f, 0.0f, (float)level0Width, (float)level0Height, 0.0f, 1.0f };

			float texelSizeX = (level0Width > 0) ? (1.0f / level0Width) : 1.0f;
			float texelSizeY = (level0Height > 0) ? (1.0f / level0Height) : 1.0f;

			BloomCB bloomCB = {};
			bloomCB.threshold = m_bloomSettings.threshold;
			bloomCB.knee = m_bloomSettings.knee;
			bloomCB.intensity = m_bloomSettings.intensity;
			bloomCB.radius = m_bloomSettings.radius;
			bloomCB.texelSize = DirectX::XMFLOAT2(texelSizeX, texelSizeY);
			bloomCB.downsample = m_bloomSettings.downsample;

			D3D11_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(m_context->Map(m_cbBloom.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				memcpy(mapped.pData, &bloomCB, sizeof(BloomCB));
				m_context->Unmap(m_cbBloom.Get(), 0);
			}

			m_context->RSSetViewports(1, &level0Viewport);
			m_context->OMSetRenderTargets(1, m_bloomLevelRTV[0][0].GetAddressOf(), nullptr); // level0 A
			m_context->PSSetShaderResources(0, 1, &sourceSRV);
			m_context->PSSetSamplers(0, 1, &sampler);
			m_context->PSSetConstantBuffers(3, 1, &cbBloom);
			m_context->PSSetShader(m_bloomBrightPassPS.Get(), nullptr, 0);
			m_context->DrawIndexed(m_quadIndexCount, 0, 0);

			m_context->PSSetShaderResources(0, 8, nullSRVs);
		}

		// ========== 2. Downsample Chain: level(i-1) A → level(i) A (i=1..4) ==========
		for (int level = 1; level < BLOOM_LEVEL_COUNT; ++level)
		{
			std::uint32_t prevWidth = m_bloomLevelWidth[level - 1];
			std::uint32_t prevHeight = m_bloomLevelHeight[level - 1];
			std::uint32_t currWidth = m_bloomLevelWidth[level];
			std::uint32_t currHeight = m_bloomLevelHeight[level];

			// 입력 텍스처의 텍셀 크기
			float inputTexelSizeX = (prevWidth > 0) ? (1.0f / prevWidth) : 1.0f;
			float inputTexelSizeY = (prevHeight > 0) ? (1.0f / prevHeight) : 1.0f;

			BloomCB bloomCB = {};
			bloomCB.threshold = m_bloomSettings.threshold;
			bloomCB.knee = m_bloomSettings.knee;
			bloomCB.intensity = m_bloomSettings.intensity;
			bloomCB.radius = m_bloomSettings.radius;
			bloomCB.texelSize = DirectX::XMFLOAT2(inputTexelSizeX, inputTexelSizeY);
			bloomCB.downsample = m_bloomSettings.downsample;

			D3D11_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(m_context->Map(m_cbBloom.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				memcpy(mapped.pData, &bloomCB, sizeof(BloomCB));
				m_context->Unmap(m_cbBloom.Get(), 0);
			}

			D3D11_VIEWPORT currViewport = { 0.0f, 0.0f, (float)currWidth, (float)currHeight, 0.0f, 1.0f };
			m_context->RSSetViewports(1, &currViewport);

			// level(i-1) A → level(i) A
			ID3D11ShaderResourceView* inputSRV = m_bloomLevelSRV[level - 1][0].Get(); // 이전 레벨 A
			m_context->OMSetRenderTargets(1, m_bloomLevelRTV[level][0].GetAddressOf(), nullptr); // 현재 레벨 A

			m_context->PSSetShaderResources(0, 1, &inputSRV);
			m_context->PSSetConstantBuffers(3, 1, &cbBloom);
			m_context->PSSetShader(m_bloomDownsamplePS.Get(), nullptr, 0);
			m_context->DrawIndexed(m_quadIndexCount, 0, 0);

			m_context->PSSetShaderResources(0, 8, nullSRVs);
		}

		// ========== 3. Blur per Level: level i에서 A↔B로 (H then V) * blurIterations ==========
		for (int level = 0; level < BLOOM_LEVEL_COUNT; ++level)
		{
			std::uint32_t levelWidth = m_bloomLevelWidth[level];
			std::uint32_t levelHeight = m_bloomLevelHeight[level];

			float texelSizeX = (levelWidth > 0) ? (1.0f / levelWidth) : 1.0f;
			float texelSizeY = (levelHeight > 0) ? (1.0f / levelHeight) : 1.0f;

			BloomCB bloomCB = {};
			bloomCB.threshold = m_bloomSettings.threshold;
			bloomCB.knee = m_bloomSettings.knee;
			bloomCB.intensity = m_bloomSettings.intensity;
			bloomCB.radius = m_bloomSettings.radius;
			bloomCB.texelSize = DirectX::XMFLOAT2(texelSizeX, texelSizeY);
			bloomCB.downsample = m_bloomSettings.downsample;

			D3D11_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(m_context->Map(m_cbBloom.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				memcpy(mapped.pData, &bloomCB, sizeof(BloomCB));
				m_context->Unmap(m_cbBloom.Get(), 0);
			}

			D3D11_VIEWPORT levelViewport = { 0.0f, 0.0f, (float)levelWidth, (float)levelHeight, 0.0f, 1.0f };
			m_context->RSSetViewports(1, &levelViewport);

			int pingPongIndex = 0; // 0=A, 1=B

			for (int iteration = 0; iteration < blurIterations; ++iteration)
			{
				// Horizontal Blur: A → B
				ID3D11ShaderResourceView* inputSRV = m_bloomLevelSRV[level][pingPongIndex].Get();
				int outputPingPong = 1 - pingPongIndex;

				m_context->OMSetRenderTargets(1, m_bloomLevelRTV[level][outputPingPong].GetAddressOf(), nullptr);
				m_context->PSSetShaderResources(0, 1, &inputSRV);
				m_context->PSSetConstantBuffers(3, 1, &cbBloom);
				m_context->PSSetShader(m_bloomBlurPassPS_H.Get(), nullptr, 0);
				m_context->DrawIndexed(m_quadIndexCount, 0, 0);

				m_context->PSSetShaderResources(0, 8, nullSRVs);

				// Vertical Blur: B → A
				pingPongIndex = outputPingPong;
				inputSRV = m_bloomLevelSRV[level][pingPongIndex].Get();
				outputPingPong = 1 - pingPongIndex;

				m_context->OMSetRenderTargets(1, m_bloomLevelRTV[level][outputPingPong].GetAddressOf(), nullptr);
				m_context->PSSetShaderResources(0, 1, &inputSRV);
				m_context->PSSetConstantBuffers(3, 1, &cbBloom);
				m_context->PSSetShader(m_bloomBlurPassPS_V.Get(), nullptr, 0);
				m_context->DrawIndexed(m_quadIndexCount, 0, 0);

				m_context->PSSetShaderResources(0, 8, nullSRVs);

				pingPongIndex = outputPingPong;
			}
		}

		// ========== 4. Upsample+Add: level4 → level3 → ... → level0 (합성) ==========
		// Additive Blend State 사용 (기존 m_blendStateAdditive 사용)
		if (!m_blendStateAdditive)
		{
			ALICE_LOG_ERRORF("Additive blend state not available for bloom upsample");
			return;
		}

		// 레벨 4부터 레벨 0까지 역순으로 업샘플링+합성
		for (int level = BLOOM_LEVEL_COUNT - 1; level > 0; --level)
		{
			std::uint32_t lowResWidth = m_bloomLevelWidth[level];
			std::uint32_t lowResHeight = m_bloomLevelHeight[level];
			std::uint32_t highResWidth = m_bloomLevelWidth[level - 1];
			std::uint32_t highResHeight = m_bloomLevelHeight[level - 1];

			// 저해상도 텍스처의 텍셀 크기 (업샘플링용)
			float texelSizeX = (lowResWidth > 0) ? (1.0f / lowResWidth) : 1.0f;
			float texelSizeY = (lowResHeight > 0) ? (1.0f / lowResHeight) : 1.0f;

			BloomCB bloomCB = {};
			bloomCB.threshold = m_bloomSettings.threshold;
			bloomCB.knee = m_bloomSettings.knee;
			bloomCB.intensity = m_bloomSettings.intensity;
			bloomCB.radius = m_bloomSettings.radius;
			bloomCB.texelSize = DirectX::XMFLOAT2(texelSizeX, texelSizeY);
			bloomCB.downsample = m_bloomSettings.downsample;

			D3D11_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(m_context->Map(m_cbBloom.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				memcpy(mapped.pData, &bloomCB, sizeof(BloomCB));
				m_context->Unmap(m_cbBloom.Get(), 0);
			}

			D3D11_VIEWPORT highResViewport = { 0.0f, 0.0f, (float)highResWidth, (float)highResHeight, 0.0f, 1.0f };
			m_context->RSSetViewports(1, &highResViewport);

			// Additive Blending 활성화 (고해상도 텍스처에 저해상도를 더하기)
			m_context->OMSetBlendState(m_blendStateAdditive.Get(), blendFactor, 0xFFFFFFFF);

			// 저해상도 텍스처만 바인딩 (업샘플링할 소스)
			// 고해상도 텍스처는 이미 RTV에 바인딩되어 있으므로 Additive Blending으로 자동 합성됨
			ID3D11ShaderResourceView* lowResSRV = m_bloomLevelSRV[level][0].Get(); // 현재 레벨의 블러 결과 (A)
			//ID3D11RenderTargetView* renderpassRTV = 


			// 이전 레벨의 RTV에 업샘플링 결과를 렌더링 (Additive Blending으로 기존 값에 더하기)
			m_context->OMSetRenderTargets(1, m_bloomLevelRTV[level - 1][0].GetAddressOf(), nullptr);
			m_context->PSSetShaderResources(0, 1, &lowResSRV);
			m_context->PSSetConstantBuffers(3, 1, &cbBloom);
			m_context->PSSetShader(m_bloomUpsamplePS.Get(), nullptr, 0);
			m_context->DrawIndexed(m_quadIndexCount, 0, 0);

			m_context->PSSetShaderResources(0, 8, nullSRVs);
		}

		// Additive Blending 비활성화
		m_context->OMSetBlendState(m_ppBlendOpaque.Get(), blendFactor, 0xFFFFFFFF);

		// ========== 5. Composite: Scene + Bloom → PostBloomTex (HDR) ==========
		// 톤매핑과 합성을 분리합니다. 여기서는 HDR 상태로 합치기만 합니다.
		{
			// 뷰포트를 전체 씬 크기로 설정
			D3D11_VIEWPORT sceneViewport = { 0.0f, 0.0f, (float)m_sceneWidth, (float)m_sceneHeight, 0.0f, 1.0f };
			m_context->RSSetViewports(1, &sceneViewport);
			
			// 타겟을 Viewport가 아닌 중간 HDR 버퍼(m_postBloomRTV)로 변경
			m_context->OMSetRenderTargets(1, m_postBloomRTV.GetAddressOf(), nullptr);

			ID3D11ShaderResourceView* sceneSRV = sourceSRV;
			ID3D11ShaderResourceView* bloomSRV = m_bloomLevelSRV[0][0].Get(); // level0의 최종 bloom 결과
			ID3D11ShaderResourceView* compositeSRVs[2] = { sceneSRV, bloomSRV };

			m_context->PSSetShaderResources(0, 2, compositeSRVs);

			// [중요] 합성 단계에서는 Exposure를 1.0으로 강제하여 색상 변형 방지
			// 실제 노출 보정은 다음 단계인 RenderToneMapping에서 수행함
			PostProcessCB postProcessCB = {};
			float tempExposure;
			GetPostProcessParams(tempExposure, postProcessCB.maxHDRNits);
			postProcessCB.exposure = 1.0f; // 합성 단계에서는 노출 적용 안 함 (중립값)

			D3D11_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(m_context->Map(m_cbPostProcess.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				std::memcpy(mapped.pData, &postProcessCB, sizeof(PostProcessCB));
				m_context->Unmap(m_cbPostProcess.Get(), 0);
			}

			// Bloom CB 업데이트 (최종 합성용)
			std::uint32_t level0Width = m_bloomLevelWidth[0];
			std::uint32_t level0Height = m_bloomLevelHeight[0];
			float texelSizeX = (level0Width > 0) ? (1.0f / level0Width) : 1.0f;
			float texelSizeY = (level0Height > 0) ? (1.0f / level0Height) : 1.0f;

			BloomCB bloomCB = {};
			bloomCB.threshold = m_bloomSettings.threshold;
			bloomCB.knee = m_bloomSettings.knee;
			bloomCB.intensity = m_bloomSettings.intensity;
			bloomCB.radius = m_bloomSettings.radius;
			bloomCB.texelSize = DirectX::XMFLOAT2(texelSizeX, texelSizeY);
			bloomCB.downsample = m_bloomSettings.downsample;

			if (SUCCEEDED(m_context->Map(m_cbBloom.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				std::memcpy(mapped.pData, &bloomCB, sizeof(BloomCB));
				m_context->Unmap(m_cbBloom.Get(), 0);
			}

			// 셰이더 및 버퍼 바인딩
			ID3D11Buffer* cbPostProcess = m_cbPostProcess.Get();
			ID3D11Buffer* cbBloom = m_cbBloom.Get();
			m_context->PSSetConstantBuffers(2, 1, &cbPostProcess);
			m_context->PSSetConstantBuffers(3, 1, &cbBloom);
			m_context->PSSetShader(m_bloomCompositePS.Get(), nullptr, 0);
			m_context->DrawIndexed(m_quadIndexCount, 0, 0);

			// SRV 해제 (ToneMapping에서 입력으로 쓰기 위해 필수)
			ID3D11ShaderResourceView* nullSRVs2[2] = { nullptr, nullptr };
			m_context->PSSetShaderResources(0, 2, nullSRVs2);
		}

		// ========== 6. Final Tone Mapping: PostBloomTex(HDR) → TargetRTV(LDR) ==========
		// 합성된 HDR 텍스처를 입력으로 받아 실제 Exposure를 적용하고 LDR로 변환
		RenderToneMapping(m_postBloomSRV.Get(), targetRTV, viewport);
	}


    bool DeferredRenderSystem::CreateUIResources()
    {
        ALICE_LOG_INFO("DeferredRenderSystem::CreateUIResources: begin");

        // UI Quad Vertex Shader 컴파일 (DeferredShader::UIRenderVS 사용)
        ComPtr<ID3DBlob> vsBlob;
        ComPtr<ID3DBlob> errorBlob;
        HRESULT hr = D3DCompile(
            DeferredShader::UIRenderVS, strlen(DeferredShader::UIRenderVS),
            "UIRenderVS", nullptr, nullptr, "main", "vs_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS, 0, &vsBlob, &errorBlob);
        if (FAILED(hr))
        {
            if (errorBlob)
                ALICE_LOG_ERRORF("DeferredRenderSystem::CreateUIResources: VS compile failed: %s",
                    static_cast<const char*>(errorBlob->GetBufferPointer()));
            return false;
        }

        hr = m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
            nullptr, m_uiQuadVS.GetAddressOf());
        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::CreateUIResources: CreateVertexShader failed.");
            return false;
        }

        // UI Composite Pixel Shader 컴파일 (DeferredShader::UIRenderPS 사용)
        ComPtr<ID3DBlob> psBlob;
        hr = D3DCompile(
            DeferredShader::UIRenderPS, strlen(DeferredShader::UIRenderPS),
            "UIRenderPS", nullptr, nullptr, "main", "ps_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS, 0, &psBlob, &errorBlob);
        if (FAILED(hr))
        {
            if (errorBlob)
                ALICE_LOG_ERRORF("DeferredRenderSystem::CreateUIResources: PS compile failed: %s",
                    static_cast<const char*>(errorBlob->GetBufferPointer()));
            return false;
        }

        hr = m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
            nullptr, m_uiCompositePS.GetAddressOf());
        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::CreateUIResources: CreatePixelShader failed.");
            return false;
        }

        ALICE_LOG_INFO("DeferredRenderSystem::CreateUIResources: success");
        return true;
    }

    void DeferredRenderSystem::RenderUI(UIWorldManager& uiWorld, ID3D11RenderTargetView* targetRTV, const D3D11_VIEWPORT& viewport)
    {

        ID3D11ShaderResourceView* uiSrv = uiWorld.GetUISRV();
        if (!uiSrv)
        {
            return; // UI 텍스처가 없으면 건너뜀
        }

        // 1) 렌더 타겟 설정 (톤매핑 후 백버퍼 또는 에디터 뷰포트)
        m_context->OMSetRenderTargets(1, &targetRTV, nullptr);
        m_context->RSSetViewports(1, &viewport);

        // 2) Depth OFF
        m_context->OMSetDepthStencilState(nullptr, 0);

        // 3) 알파 블렌딩 ON
        float blendFactor[4] = { 0,0,0,0 };
        m_context->OMSetBlendState(m_alphaBlendState.Get(), blendFactor, 0xffffffff);

        // 4) IA 비우기 (SV_VertexID 방식이라 VB/IL 필요 없음)
        m_context->IASetInputLayout(nullptr);
        ID3D11Buffer* nullVB = nullptr;
        UINT stride = 0, offset = 0;
        m_context->IASetVertexBuffers(0, 1, &nullVB, &stride, &offset);
        m_context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // 5) UI 텍스처 SRV 바인딩 (t101 슬롯 - DeferredShader::UIRenderPS에서 사용)
        m_context->PSSetShaderResources(101, 1, &uiSrv);

        // 6) 샘플러 바인딩
        ID3D11SamplerState* sampler = m_samplerLinear.Get();
        m_context->PSSetSamplers(0, 1, &sampler);

        // 7) 셰이더 바인딩
        m_context->VSSetShader(m_uiQuadVS.Get(), nullptr, 0);
        m_context->PSSetShader(m_uiCompositePS.Get(), nullptr, 0);

        // 8) Draw (6 vertices = fullscreen quad)
        m_context->Draw(6, 0);

        // 9) 정리 (다음 패스에 영향 방지)
        ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
        m_context->PSSetShaderResources(101, 1, nullSRV);
        m_context->OMSetBlendState(nullptr, blendFactor, 0xffffffff);
    }
}