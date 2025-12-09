#include "Rendering/ForwardRenderSystem.h"

#include <d3dcompiler.h>
// 텍스처 로더 (vcpkg의 DirectXTK 사용)
#include <DirectXTK/WICTextureLoader.h>
#include <DirectXTK/DDSTextureLoader.h>
#include <filesystem>
#include <vector>

#include <Core/ResourceManager.h>
#include <Core/Logger.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace Alice
{
    // 간단한 Lambert / Phong / Blinn-Phong 셰이더 코드
    // (D3D11 튜토리얼의 기본 조명 코드를 참고한 단순 버전)
    namespace
    {
        const char* g_PhongVertexShaderSource = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor; // per-object 머티리얼 색상

    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    float3   gPad0;
};

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float2 TexCoord : TEXCOORD0;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    float4 worldPos = mul(float4(input.Position, 1.0f), gWorld);
    float4 viewPos  = mul(worldPos, gView);
    output.Position = mul(viewPos, gProj);

    output.WorldPos = worldPos.xyz;
    output.Normal   = mul(float4(input.Normal, 0.0f), gWorld).xyz;
    output.TexCoord = input.TexCoord;

    return output;
}
)";

        // 간단한 스키닝 전용 버텍스 셰이더
        // 현재 단계에서는 "스킨 가중치"를 실제로 적용하지 않고,
        // FBX 메시에 포함된 원래 위치/노말을 그대로 사용해서
        // "정적 메쉬"처럼 그리기만 합니다.
        // (파이프라인이 정상 동작하는지 확인하기 위한 가장 단순한 형태)
        const char* g_SkinnedVertexShaderSource = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;

    float    gRoughness;
    float    gMetalness;
    float2   gPad0;
};

cbuffer CBBones : register(b2)
{
    float4x4 gBones[64];
};

struct VSInput
{
    float3 Position     : POSITION;
    float3 Normal       : NORMAL;
    uint4  BoneIndices  : BLENDINDICES;
    float4 BoneWeights  : BLENDWEIGHT;
    float2 TexCoord     : TEXCOORD0;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    // TODO(스킨 애니메이션): 나중에 BoneIndices / BoneWeights / gBones 를
    // 실제로 사용해서 스키닝을 적용합니다.
    // 지금은 단순히 원래 정점 위치/노말을 그대로 사용합니다.
    float4 worldPos = mul(float4(input.Position, 1.0f), gWorld);
    float4 viewPos  = mul(worldPos, gView);
    output.Position = mul(viewPos, gProj);

    output.WorldPos = worldPos.xyz;
    output.Normal   = normalize(mul(float4(input.Normal, 0.0f), gWorld).xyz);
    output.TexCoord = input.TexCoord;

    return output;
}
)";

        const char* g_PhongPixelShaderSource = R"(
Texture2D gDiffuseMap  : register(t0);
Texture2D gNormalMap   : register(t1);
Texture2D gSpecularMap : register(t2);
SamplerState gSampler  : register(s0);

// 섀도우 맵 (Depth 텍스처)
Texture2D<float>        gShadowMap     : register(t4);
SamplerComparisonState  gShadowSampler : register(s1);

// VS 와 동일한 CBPerObject 레이아웃 (materialColor 포함)
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;

    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    float3   gPad0;
};

cbuffer CBLighting : register(b1)
{
    // Key Light
    float3 gKeyLightDir;
    float  gKeyLightPad0;

    float3 gKeyLightColor;
    float  gKeyLightIntensity;

    // Fill Light
    float3 gFillLightDir;
    float  gFillLightPad0;

    float3 gFillLightColor;
    float  gFillLightIntensity;

    float3 gCameraPos;
    float  gPad1;

    float4 gMaterialDiffuse;   // rgb: diffuse color
    float4 gMaterialSpecular;  // rgb: specular color, a: shininess

    int    gShadingMode;       // 0: Lambert, 1: Phong, 2: Blinn-Phong, 3: Toon
    int3   gPad2;

    float4x4 gLightViewProj;   // 섀도우 맵 계산용 라이트 뷰-프로젝션
};

static const float2 gShadowTexelSize = float2(1.0f / 2048.0f, 1.0f / 2048.0f);

struct PSInput
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
};

float4 main(PSInput input) : SV_TARGET
{
    // 기본적으로 월드 공간 노멀(기하학 노멀)을 사용해서 방향성 조명이 잘 보이도록 합니다.
    // 노말맵은 필요하면 이후에 섞어서 디테일을 추가할 수 있습니다.
    float3 N = normalize(input.Normal);
    // 예시) 노말맵을 살짝 섞고 싶다면 다음과 같이 사용할 수 있습니다.
    //float3 normalTex = gNormalMap.Sample(gSampler, input.TexCoord).xyz * 2.0f - 1.0f;
    //N = normalize(N + normalTex);

    float3 V = normalize(gCameraPos - input.WorldPos);

    float3 totalDiffuse  = float3(0.0f, 0.0f, 0.0f);
    float3 totalSpecular = float3(0.0f, 0.0f, 0.0f);

    // Key Light
    {
        float3 L = normalize(-gKeyLightDir);
        float  NdotL = max(dot(N, L), 0.0f);
        float3 lightColor = gKeyLightColor * gKeyLightIntensity;

        totalDiffuse += NdotL * lightColor;

        if (gShadingMode != 0 && NdotL > 0.0f)
        {
            float specularTerm = 0.0f;
            if (gShadingMode == 2) // Blinn-Phong
            {
                float3 H = normalize(L + V);
                float NdotH = max(dot(N, H), 0.0f);
                specularTerm = pow(NdotH, gMaterialSpecular.a);
            }
            else // Phong
            {
                float3 R = reflect(-L, N);
                float RdotV = max(dot(R, V), 0.0f);
                specularTerm = pow(RdotV, gMaterialSpecular.a);
            }

            totalSpecular += specularTerm * lightColor;
        }
    }

    // Fill Light (옵션)
    {
        float3 L = normalize(-gFillLightDir);
        float  NdotL = max(dot(N, L), 0.0f);
        float3 lightColor = gFillLightColor * gFillLightIntensity;

        totalDiffuse += NdotL * lightColor;

        if (gShadingMode != 0 && NdotL > 0.0f)
        {
            float specularTerm = 0.0f;
            if (gShadingMode == 2) // Blinn-Phong
            {
                float3 H = normalize(L + V);
                float NdotH = max(dot(N, H), 0.0f);
                specularTerm = pow(NdotH, gMaterialSpecular.a);
            }
            else // Phong
            {
                float3 R = reflect(-L, N);
                float RdotV = max(dot(R, V), 0.0f);
                specularTerm = pow(RdotV, gMaterialSpecular.a);
            }

            totalSpecular += specularTerm * lightColor;
        }
    }

    // 섀도우 팩터 (PCF)
    float shadow = 1.0f;
    {
        float4 shadowPos = mul(float4(input.WorldPos, 1.0f), gLightViewProj);
        shadowPos.xyz /= shadowPos.w;

        float2 shadowTex;
        shadowTex.x = shadowPos.x * 0.5f + 0.5f;
        shadowTex.y = -shadowPos.y * 0.5f + 0.5f;
        float depth = shadowPos.z;

        // 간단한 바이어스
        const float bias = 0.001f;

        if (shadowTex.x >= 0.0f && shadowTex.x <= 1.0f &&
            shadowTex.y >= 0.0f && shadowTex.y <= 1.0f)
        {
            float sum = 0.0f;
            [unroll] for (int y = -1; y <= 1; ++y)
            {
                [unroll] for (int x = -1; x <= 1; ++x)
                {
                    float2 offset = float2(x, y) * gShadowTexelSize;
                    sum += gShadowMap.SampleCmpLevelZero(
                        gShadowSampler,
                        shadowTex + offset,
                        depth - bias);
                }
            }
            shadow = sum / 9.0f;
        }
    }

    totalDiffuse  *= shadow;
    totalSpecular *= shadow;

    // 머티리얼 베이스 컬러
    // - gUseTexture != 0 인 경우에만 디퓨즈 텍스처를 곱해주고,
    //   그렇지 않으면 순수한 머티리얼 색만 사용합니다.
    float3 albedo = gMaterialColor.rgb;
    if (gUseTexture != 0)
    {
        float3 texSample = gDiffuseMap.Sample(gSampler, input.TexCoord).rgb;
        albedo *= texSample;
    }
    float3 specColor = float3(1.0f, 1.0f, 1.0f);

    float3 ambient = 0.1f * gKeyLightColor;

    // === Toon Shading (shadingMode == 3) ===
    if (gShadingMode == 3)
    {
        float3 Lmain = normalize(-gKeyLightDir);
        float  NdotL = max(dot(N, Lmain), 0.0f);

        float level = 0.0f;
        if (NdotL > 0.95f)      level = 1.0f;
        else if (NdotL > 0.5f)  level = 0.7f;
        else if (NdotL > 0.2f)  level = 0.4f;
        else                    level = 0.1f;

        float3 toonColor = albedo * level + 0.1f * albedo;
        return float4(toonColor, 1.0f);
    }

    // === PBR 경로 (shadingMode == 4) ===
    if (gShadingMode == 4)
    {
        float roughness = saturate(gRoughness);
        float metalness = saturate(gMetalness);

        float3 Np = N;
        float3 Vp = V;
        float3 Lp = normalize(-gKeyLightDir);
        float3 Hp = normalize(Vp + Lp);

        float NdotL = max(dot(Np, Lp), 0.0f);
        float NdotV = max(dot(Np, Vp), 0.0f);
        float NdotH = max(dot(Np, Hp), 0.0f);
        float VdotH = max(dot(Vp, Hp), 0.0f);

        float3 lightColor = gKeyLightColor * gKeyLightIntensity;

        float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metalness);

        float  a      = roughness * roughness;
        float  a2     = a * a;
        float  denomD = (NdotH * NdotH) * (a2 - 1.0f) + 1.0f;
        float  D      = a2 / max(3.14159f * denomD * denomD, 1e-4f);

        float  k      = (roughness + 1.0f);
        k             = (k * k) / 8.0f;
        float  Gv     = NdotV / (NdotV * (1.0f - k) + k);
        float  Gl     = NdotL / (NdotL * (1.0f - k) + k);
        float  G      = Gv * Gl;

        float3 F      = F0 + (1.0f - F0) * pow(1.0f - VdotH, 5.0f);

        float3 numerator    = D * G * F;
        float  denomSpec    = max(4.0f * NdotV * NdotL, 1e-4f);
        float3 specularTerm = numerator / denomSpec;

        float3 kd = (1.0f - F) * (1.0f - metalness);
        float3 diffuseTerm = kd * albedo / 3.14159f;

        float3 radiance = lightColor * NdotL;

        float3 Lo = (diffuseTerm + specularTerm) * radiance;

        float3 ambientPbr = 0.03f * albedo;
        float3 colorPbr   = ambientPbr + Lo;

        return float4(colorPbr, 1.0f);
    }

    // 기본 Phong/Blinn-Phong/Lambert 경로
    float3 baseColor =
        ambient * albedo +
        totalDiffuse * albedo +
        totalSpecular * specColor;

    return float4(baseColor, 1.0f);
}
)";

        // 스카이박스 전용 셰이더 (단순 큐브 맵 샘플링)
        const char* g_SkyboxVertexShaderSource = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
};

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float2 TexCoord : TEXCOORD0;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 Direction : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    float4 worldPos = mul(float4(input.Position, 1.0f), gWorld);
    float4 viewPos  = mul(worldPos, gView);
    output.Position = mul(viewPos, gProj);

    // 방향 벡터는 위치를 그대로 사용 (정규화는 PS에서 수행)
    output.Direction = input.Position;
    return output;
}
)";

        const char* g_SkyboxPixelShaderSource = R"(
TextureCube gSkybox : register(t3);
SamplerState gSampler : register(s0);

struct PSInput
{
    float4 Position : SV_POSITION;
    float3 Direction : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    float3 dir = normalize(input.Direction);
    return gSkybox.Sample(gSampler, dir);
}
)";
    }

    ForwardRenderSystem::ForwardRenderSystem(ID3D11RenderDevice& renderDevice)
        : m_renderDevice(renderDevice)
    {
        m_device  = m_renderDevice.GetDevice();
        m_context = m_renderDevice.GetImmediateContext();
    }

    bool ForwardRenderSystem::Initialize(std::uint32_t width, std::uint32_t height)
    {
        ALICE_LOG_INFO("ForwardRenderSystem::Initialize: begin (width=%u, height=%u)", width, height);

        if (!m_device || !m_context)
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: invalid device/context.");
            return false;
        }
        if (!CreateSceneRenderTarget(width, height))
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateSceneRenderTarget failed.");
            return false;
        }
        if (!CreateShadowMapResources())
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateShadowMapResources failed.");
            return false;
        }
        if (!CreateCubeGeometry())
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateCubeGeometry failed.");
            return false;
        }
        if (!CreateShadersAndInputLayout())
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateShadersAndInputLayout failed.");
            return false;
        }
        if (!CreateSkinnedResources())
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateSkinnedResources failed.");
            return false;
        }
        if (!CreateConstantBuffers())
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateConstantBuffers failed.");
            return false;
        }
        if (!CreateTextures())
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateTextures failed.");
            return false;
        }
        if (!CreateSamplerState())
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateSamplerState failed.");
            return false;
        }
        if (!CreateRasterizerStates())
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateRasterizerStates failed.");
            return false;
        }
        if (!CreateSkyboxResources())
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateSkyboxResources failed.");
            return false;
        }

        ALICE_LOG_INFO("ForwardRenderSystem::Initialize: success.");
        return true;
    }

    void ForwardRenderSystem::Resize(std::uint32_t width, std::uint32_t height)
    {
        if (!m_device) return;
        if (width == 0 || height == 0) return;
        // 기존 리소스 해제 후 새로 생성
        m_sceneColorTex.Reset();
        m_sceneRTV.Reset();
        m_sceneSRV.Reset();
        m_sceneDepthTex.Reset();
        m_sceneDSV.Reset();

        CreateSceneRenderTarget(width, height);
    }

    bool ForwardRenderSystem::CreateSceneRenderTarget(std::uint32_t width, std::uint32_t height)
    {
        m_sceneWidth  = width;
        m_sceneHeight = height;

        if (width == 0 || height == 0) return false;

        // 색 텍스처 (RTV + SRV)
        D3D11_TEXTURE2D_DESC colorDesc = {};
        colorDesc.Width              = width;
        colorDesc.Height             = height;
        colorDesc.MipLevels          = 1;
        colorDesc.ArraySize          = 1;
        colorDesc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
        colorDesc.SampleDesc.Count   = 1;
        colorDesc.Usage              = D3D11_USAGE_DEFAULT;
        colorDesc.BindFlags          = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = m_device->CreateTexture2D(&colorDesc, nullptr, m_sceneColorTex.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        hr = m_device->CreateRenderTargetView(
            m_sceneColorTex.Get(),
            nullptr,
            m_sceneRTV.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        hr = m_device->CreateShaderResourceView(
            m_sceneColorTex.Get(),
            nullptr,
            m_sceneSRV.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        // 깊이/스텐실 텍스처 + 뷰
        D3D11_TEXTURE2D_DESC depthDesc = {};
        depthDesc.Width              = width;
        depthDesc.Height             = height;
        depthDesc.MipLevels          = 1;
        depthDesc.ArraySize          = 1;
        depthDesc.Format             = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.SampleDesc.Count   = 1;
        depthDesc.Usage              = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags          = D3D11_BIND_DEPTH_STENCIL;

        hr = m_device->CreateTexture2D(&depthDesc, nullptr, m_sceneDepthTex.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format             = depthDesc.Format;
        dsvDesc.ViewDimension      = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;

        hr = m_device->CreateDepthStencilView(
            m_sceneDepthTex.Get(),
            &dsvDesc,
            m_sceneDSV.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateShadowMapResources()
    {
        // 단일 Directional Light 용 섀도우 맵 (고정 해상도)
        const UINT shadowSize = 2048;

        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width              = shadowSize;
        texDesc.Height             = shadowSize;
        texDesc.MipLevels          = 1;
        texDesc.ArraySize          = 1;
        texDesc.Format             = DXGI_FORMAT_R32_TYPELESS;
        texDesc.SampleDesc.Count   = 1;
        texDesc.Usage              = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags          = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, m_shadowTex.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        // DSV (깊이 전용)
        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format             = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension      = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;

        hr = m_device->CreateDepthStencilView(
            m_shadowTex.Get(),
            &dsvDesc,
            m_shadowDSV.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        // SRV (쉐이더에서 깊이 값을 읽기 위한 뷰)
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = DXGI_FORMAT_R32_FLOAT;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;

        hr = m_device->CreateShaderResourceView(
            m_shadowTex.Get(),
            &srvDesc,
            m_shadowSRV.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        // 뷰포트 설정
        m_shadowViewport.TopLeftX = 0.0f;
        m_shadowViewport.TopLeftY = 0.0f;
        m_shadowViewport.Width    = static_cast<float>(shadowSize);
        m_shadowViewport.Height   = static_cast<float>(shadowSize);
        m_shadowViewport.MinDepth = 0.0f;
        m_shadowViewport.MaxDepth = 1.0f;

        // 비교 샘플러 (PCF 용)
        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter         = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
        sampDesc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
        sampDesc.MinLOD         = 0.0f;
        sampDesc.MaxLOD         = D3D11_FLOAT32_MAX;

        hr = m_device->CreateSamplerState(&sampDesc, m_shadowSampler.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateSkyboxResources()
    {
        // 스카이박스 큐브 맵 텍스처 로드 (OasisSunset.dds)
        const wchar_t* skyboxPath = L"../Resource/Skybox/OasisSunset.dds";
        HRESULT hr = DirectX::CreateDDSTextureFromFile(
            m_device.Get(),
            skyboxPath,
            nullptr,
            m_skyboxSRV.ReleaseAndGetAddressOf());

        if (FAILED(hr))
        {
            // 스카이박스는 선택 사항이므로, 로드 실패 시 비활성화만 하고 계속 진행합니다.
            m_skyboxEnabled = false;
            return true;
        }

        // 스카이박스용 셰이더 컴파일
        ComPtr<ID3DBlob> vsBlob;
        ComPtr<ID3DBlob> psBlob;
        ComPtr<ID3DBlob> errorBlob;

        hr = D3DCompile(
            g_SkyboxVertexShaderSource,
            strlen(g_SkyboxVertexShaderSource),
            nullptr,
            nullptr,
            nullptr,
            "main",
            "vs_5_0",
            0,
            0,
            vsBlob.GetAddressOf(),
            errorBlob.GetAddressOf()
        );
        if (FAILED(hr))
            return false;

        errorBlob.Reset();
        hr = D3DCompile(
            g_SkyboxPixelShaderSource,
            strlen(g_SkyboxPixelShaderSource),
            nullptr,
            nullptr,
            nullptr,
            "main",
            "ps_5_0",
            0,
            0,
            psBlob.GetAddressOf(),
            errorBlob.GetAddressOf()
        );
        if (FAILED(hr))
            return false;

        hr = m_device->CreateVertexShader(
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            nullptr,
            m_skyboxVS.ReleaseAndGetAddressOf()
        );
        if (FAILED(hr)) return false;

        hr = m_device->CreatePixelShader(
            psBlob->GetBufferPointer(),
            psBlob->GetBufferSize(),
            nullptr,
            m_skyboxPS.ReleaseAndGetAddressOf()
        );
        if (FAILED(hr)) return false;

        // 스카이박스용 DepthStencilState (깊이 테스트는 하되, depth write 는 비활성화)
        D3D11_DEPTH_STENCIL_DESC dsDesc = {};
        dsDesc.DepthEnable    = TRUE;
        dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        dsDesc.DepthFunc      = D3D11_COMPARISON_LESS_EQUAL;
        dsDesc.StencilEnable  = FALSE;

        hr = m_device->CreateDepthStencilState(&dsDesc, m_skyboxDepthState.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        // 스카이박스를 안쪽에서 보기 위해 전면 컬링을 사용
        D3D11_RASTERIZER_DESC rsDesc = {};
        rsDesc.FillMode              = D3D11_FILL_SOLID;
        rsDesc.CullMode              = D3D11_CULL_FRONT;
        rsDesc.FrontCounterClockwise = FALSE;
        rsDesc.DepthClipEnable       = TRUE;

        hr = m_device->CreateRasterizerState(&rsDesc, m_skyboxRasterizerState.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateCubeGeometry()
    {
        // 단순 단위 큐브 정점/인덱스 데이터
        // (각 면에 대한 법선과 텍스처 좌표를 명시해서 조명/텍스처링이 자연스럽도록)
        SimpleVertex vertices[] =
        {
            // Front (+Z)
            { XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f,  0.0f,  1.0f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(-1.0f,  1.0f,  1.0f), XMFLOAT3(0.0f,  0.0f,  1.0f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3( 1.0f,  1.0f,  1.0f), XMFLOAT3(0.0f,  0.0f,  1.0f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3( 1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f,  0.0f,  1.0f), XMFLOAT2(1.0f, 1.0f) },

            // Back (-Z)
            { XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f,  0.0f, -1.0f), XMFLOAT2(1.0f, 1.0f) },
            { XMFLOAT3( 1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f,  0.0f, -1.0f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3( 1.0f,  1.0f, -1.0f), XMFLOAT3(0.0f,  0.0f, -1.0f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3(-1.0f,  1.0f, -1.0f), XMFLOAT3(0.0f,  0.0f, -1.0f), XMFLOAT2(1.0f, 0.0f) },

            // Top (+Y)
            { XMFLOAT3(-1.0f,  1.0f, -1.0f), XMFLOAT3(0.0f,  1.0f,  0.0f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3( 1.0f,  1.0f, -1.0f), XMFLOAT3(0.0f,  1.0f,  0.0f), XMFLOAT2(1.0f, 1.0f) },
            { XMFLOAT3( 1.0f,  1.0f,  1.0f), XMFLOAT3(0.0f,  1.0f,  0.0f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3(-1.0f,  1.0f,  1.0f), XMFLOAT3(0.0f,  1.0f,  0.0f), XMFLOAT2(0.0f, 0.0f) },

            // Bottom (-Y)
            { XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, -1.0f,  0.0f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f, -1.0f,  0.0f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3( 1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f, -1.0f,  0.0f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3( 1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, -1.0f,  0.0f), XMFLOAT2(1.0f, 1.0f) },

            // Left (-X)
            { XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(-1.0f,  0.0f,  0.0f), XMFLOAT2(1.0f, 1.0f) },
            { XMFLOAT3(-1.0f,  1.0f, -1.0f), XMFLOAT3(-1.0f,  0.0f,  0.0f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3(-1.0f,  1.0f,  1.0f), XMFLOAT3(-1.0f,  0.0f,  0.0f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(-1.0f,  0.0f,  0.0f), XMFLOAT2(0.0f, 1.0f) },

            // Right (+X)
            { XMFLOAT3( 1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f,  0.0f,  0.0f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3( 1.0f, -1.0f,  1.0f), XMFLOAT3(1.0f,  0.0f,  0.0f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3( 1.0f,  1.0f,  1.0f), XMFLOAT3(1.0f,  0.0f,  0.0f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3( 1.0f,  1.0f, -1.0f), XMFLOAT3(1.0f,  0.0f,  0.0f), XMFLOAT2(1.0f, 1.0f) },
        };

        uint16_t indices[] =
        {
            // Front
            0, 1, 2, 0, 2, 3,
            // Back
            4, 5, 6, 4, 6, 7,
            // Top
            8, 9,10, 8,10,11,
            // Bottom
            12,13,14, 12,14,15,
            // Left
            16,17,18, 16,18,19,
            // Right
            20,21,22, 20,22,23
        };

        m_indexCount = static_cast<UINT>(std::size(indices));

        // 정점 버퍼 생성
        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.Usage = D3D11_USAGE_DEFAULT;
        vbDesc.ByteWidth = static_cast<UINT>(sizeof(vertices));
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA vbData = {};
        vbData.pSysMem = vertices;

        HRESULT hr = m_device->CreateBuffer(&vbDesc, &vbData, m_vertexBuffer.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        // 인덱스 버퍼 생성
        D3D11_BUFFER_DESC ibDesc = {};
        ibDesc.Usage = D3D11_USAGE_DEFAULT;
        ibDesc.ByteWidth = static_cast<UINT>(sizeof(indices));
        ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

        D3D11_SUBRESOURCE_DATA ibData = {};
        ibData.pSysMem = indices;

        hr = m_device->CreateBuffer(&ibDesc, &ibData, m_indexBuffer.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateShadersAndInputLayout()
    {
        ComPtr<ID3DBlob> vsBlob;
        ComPtr<ID3DBlob> psBlob;
        ComPtr<ID3DBlob> errorBlob;

        // Vertex Shader 컴파일
        HRESULT hr = D3DCompile(
            g_PhongVertexShaderSource,
            strlen(g_PhongVertexShaderSource),
            nullptr,
            nullptr,
            nullptr,
            "main",
            "vs_5_0",
            0,
            0,
            vsBlob.GetAddressOf(),
            errorBlob.GetAddressOf()
        );

        if (FAILED(hr)) return false;

        // Pixel Shader 컴파일
        errorBlob.Reset();
        hr = D3DCompile(
            g_PhongPixelShaderSource,
            strlen(g_PhongPixelShaderSource),
            nullptr,
            nullptr,
            nullptr,
            "main",
            "ps_5_0",
            0,
            0,
            psBlob.GetAddressOf(),
            errorBlob.GetAddressOf()
        );

        if (FAILED(hr)) return false;

        // 실제 셰이더 객체 생성
        hr = m_device->CreateVertexShader(
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            nullptr,
            m_vertexShader.ReleaseAndGetAddressOf()
        );
        if (FAILED(hr)) return false;

        hr = m_device->CreatePixelShader(
            psBlob->GetBufferPointer(),
            psBlob->GetBufferSize(),
            nullptr,
            m_pixelShader.ReleaseAndGetAddressOf()
        );
        if (FAILED(hr)) return false;

        // 입력 레이아웃 생성 (POSITION, NORMAL)
        D3D11_INPUT_ELEMENT_DESC layoutDesc[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,                            D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, sizeof(XMFLOAT3),             D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, sizeof(XMFLOAT3) * 2,         D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };

        hr = m_device->CreateInputLayout(
            layoutDesc,
            static_cast<UINT>(std::size(layoutDesc)),
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            m_inputLayout.ReleaseAndGetAddressOf()
        );

        if (FAILED(hr)) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateConstantBuffers()
    {
        // CBPerObject
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.Usage = D3D11_USAGE_DEFAULT;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

        cbDesc.ByteWidth = sizeof(CBPerObject);
        HRESULT hr = m_device->CreateBuffer(&cbDesc, nullptr, m_cbPerObject.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        cbDesc.ByteWidth = sizeof(CBLighting);
        hr = m_device->CreateBuffer(&cbDesc, nullptr, m_cbLighting.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateSkinnedResources()
    {
        // 스키닝 전용 VS 컴파일
        ComPtr<ID3DBlob> vsBlob;
        ComPtr<ID3DBlob> errorBlob;

        HRESULT hr = D3DCompile(
            g_SkinnedVertexShaderSource,
            std::strlen(g_SkinnedVertexShaderSource),
            nullptr,
            nullptr,
            nullptr,
            "main",
            "vs_5_0",
            0,
            0,
            vsBlob.ReleaseAndGetAddressOf(),
            errorBlob.ReleaseAndGetAddressOf());

        if (FAILED(hr))
        {
            return false;
        }

        hr = m_device->CreateVertexShader(
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            nullptr,
            m_skinnedVertexShader.ReleaseAndGetAddressOf());
        if (FAILED(hr))
            return false;

        // 스키닝 전용 입력 레이아웃 생성
        // VertexSkinnedTBN 구조체 레이아웃:
        //   pos   (float3)  : 0
        //   normal(float3)  : 12
        //   tangent(float3) : 24
        //   binormal(float3): 36
        //   color (float4)  : 48
        //   uv    (float2)  : 64
        //   boneIdx[4] (ushort4) : 72
        //   boneWeight(float4)   : 80
        //
        // HLSL VSInput 은 POSITION / NORMAL / TEXCOORD0 / BLENDINDICES / BLENDWEIGHT 만 사용하므로
        // 필요한 시맨틱만 정확한 오프셋으로 매핑합니다.
        D3D11_INPUT_ELEMENT_DESC skinnedDesc[] =
        {
            { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BLENDINDICES", 0, DXGI_FORMAT_R16G16B16A16_UINT,  0, 72, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        hr = m_device->CreateInputLayout(
            skinnedDesc,
            static_cast<UINT>(std::size(skinnedDesc)),
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            m_inputLayoutSkinned.ReleaseAndGetAddressOf());
        if (FAILED(hr))
            return false;

        // 본 행렬 상수 버퍼 생성
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth      = sizeof(CBBones);
        cbDesc.Usage          = D3D11_USAGE_DEFAULT;
        cbDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = 0;

        hr = m_device->CreateBuffer(&cbDesc, nullptr, m_cbBones.ReleaseAndGetAddressOf());
        if (FAILED(hr))
            return false;

        return true;
    }

    bool ForwardRenderSystem::CreateTextures()
    {
        // 실행 파일 기준으로 Resource/Image 폴더의 브릭 텍스처를 읽어옵니다.
        // 동시에, 존재한다면 Cooked(암호화된) 텍스처(.alice) 를 우선 사용합니다.
        const std::filesystem::path diffuseSrc  = "../Resource/Image/Bricks059_1K-JPG_Color.jpg";
        const std::filesystem::path normalSrc   = "../Resource/Image/Bricks059_1K-JPG_NormalDX.jpg";
        const std::filesystem::path specularSrc = "../Resource/Image/Bricks059_Specular.png";

        const std::filesystem::path diffuseCooked  = "../Cooked/Image/Bricks059_1K-JPG_Color.alice";
        const std::filesystem::path normalCooked   = "../Cooked/Image/Bricks059_1K-JPG_NormalDX.alice";
        const std::filesystem::path specularCooked = "../Cooked/Image/Bricks059_Specular.alice";

        auto loadTexture = [&](const std::filesystem::path& src,
                               const std::filesystem::path& cooked,
                               Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& outSrv) -> bool
        {
            HRESULT hr = E_FAIL;

            // 1) Cooked(암호화된) 파일이 있고 ResourceManager 가 있으면, 먼저 그것을 시도합니다.
            if (m_resources && std::filesystem::exists(cooked))
            {
                if (std::vector<std::uint8_t> data; m_resources->LoadBinary(cooked, data, true) && !data.empty())
                {
                    hr = DirectX::CreateWICTextureFromMemory(
                        m_device.Get(),
                        data.data(),
                        static_cast<UINT>(data.size()),
                        nullptr,
                        outSrv.ReleaseAndGetAddressOf());
                    if (SUCCEEDED(hr))
                        return true;
                }
            }

            // 2) 원본 이미지에서 직접 로드
            hr = DirectX::CreateWICTextureFromFile(
                m_device.Get(),
                src.c_str(),
                nullptr,
                outSrv.ReleaseAndGetAddressOf()
            );
            if (FAILED(hr))
            {
                // 개발용 브릭 텍스처는 필수 리소스가 아니므로 실패해도 엔진은 계속 동작하게 둡니다.
                ALICE_LOG_WARN("ForwardRenderSystem::CreateTextures: failed to load source texture \"%s\".",
                               src.string().c_str());
                return false;
            }

            // 3) ResourceManager 가 있으면, 한 번만 Cooked 파일을 생성해 둡니다.
            if (m_resources && !std::filesystem::exists(cooked))
            {
                m_resources->CookAndSave(src, cooked);
            }

            return true;
        };

        bool ok = true;
        if (!loadTexture(diffuseSrc,  diffuseCooked,  m_diffuseSRV))  ok = false;
        if (!loadTexture(normalSrc,   normalCooked,   m_normalSRV))   ok = false;
        if (!loadTexture(specularSrc, specularCooked, m_specularSRV)) ok = false;

        if (!ok)
        {
            ALICE_LOG_WARN("ForwardRenderSystem::CreateTextures: default brick textures not fully loaded; "
                           "engine will use plain gray materials instead.");
        }

        // 기본 브릭 텍스처는 필수는 아니므로, 성공 여부와 상관없이 true 를 반환합니다.
        return true;
    }

    bool ForwardRenderSystem::CreateSamplerState()
    {
        D3D11_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        samplerDesc.MinLOD = 0;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

        HRESULT hr = m_device->CreateSamplerState(&samplerDesc, m_samplerState.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateRasterizerStates()
    {
        // 기본: CCW를 앞면으로 간주, 뒷면 컬링
        D3D11_RASTERIZER_DESC desc = {};
        desc.FillMode              = D3D11_FILL_SOLID;
        desc.CullMode              = D3D11_CULL_BACK;
        desc.FrontCounterClockwise = FALSE;
        desc.DepthClipEnable       = TRUE;

        HRESULT hr = m_device->CreateRasterizerState(&desc, m_rasterizerState.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        // 음수 스케일(거울 반전)일 때는 정점의 와인딩이 뒤집히므로
        // FrontCounterClockwise 를 TRUE 로 줘서 "반대 와인딩"을 앞면으로 간주한다.
        desc.FrontCounterClockwise = TRUE;
        hr = m_device->CreateRasterizerState(&desc, m_rasterizerStateReversed.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        return true;
    }

    // 경로 문자열을 기반으로 머티리얼 전용 텍스처 SRV 를 가져오거나 생성합니다.
    // - .alice / .abtex 인 경우 ResourceManager 를 통해 복호화 후 메모리에서 로드합니다.
    // - 그 외 경우는 파일에서 직접 로드합니다.
    ID3D11ShaderResourceView* ForwardRenderSystem::GetOrCreateTexture(const std::string& path)
    {
        if (path.empty())
            return nullptr;

        auto it = m_textureCache.find(path);
        if (it != m_textureCache.end())
            return it->second.Get();

        if (!m_device)
            return nullptr;

        namespace fs = std::filesystem;
        fs::path p(path);
        if (!fs::exists(p))
        {
            char buf[256] = {};
            std::snprintf(buf, sizeof(buf),
                          "[ForwardRenderSystem] Texture file not found: \"%s\"\n",
                          path.c_str());
            OutputDebugStringA(buf);
            return nullptr;
        }

        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        HRESULT hr = E_FAIL;

        const std::string ext = p.extension().string();
        const bool isEncrypted =
            (_stricmp(ext.c_str(), ".abtex") == 0) ||
            (_stricmp(ext.c_str(), ".alice") == 0);

        if (isEncrypted && m_resources)
        {
            // 암호화된 .alice / .abtex 를 메모리로 읽어온 뒤 WIC 텍스처로 생성
            std::vector<std::uint8_t> data;
            if (m_resources->LoadBinary(p, data, true) && !data.empty())
            {
                hr = DirectX::CreateWICTextureFromMemory(
                    m_device.Get(),
                    data.data(),
                    static_cast<UINT>(data.size()),
                    nullptr,
                    srv.ReleaseAndGetAddressOf());
            }
            else
            {
                hr = E_FAIL;
            }
        }
        else
        {
            // 원본 이미지 파일에서 직접 로드 (png/jpg/tga 등)
            hr = DirectX::CreateWICTextureFromFile(
                m_device.Get(),
                p.c_str(),
                nullptr,
                srv.ReleaseAndGetAddressOf());
        }

        if (FAILED(hr) || !srv)
        {
            char buf[256] = {};
            std::snprintf(buf, sizeof(buf),
                          "[ForwardRenderSystem] Texture load FAILED: \"%s\" (isEncrypted=%d)\n",
                          path.c_str(),
                          isEncrypted ? 1 : 0);
            OutputDebugStringA(buf);
            return nullptr;
        }

        m_textureCache.emplace(path, srv);

        {
            char buf[256] = {};
            std::snprintf(buf, sizeof(buf),
                          "[ForwardRenderSystem] Texture loaded: \"%s\" (isEncrypted=%d)\n",
                          path.c_str(),
                          isEncrypted ? 1 : 0);
            OutputDebugStringA(buf);
        }

        return srv.Get();
    }

    void ForwardRenderSystem::UpdatePerObjectCB(const XMMATRIX& world,
                                                const XMMATRIX& view,
                                                const XMMATRIX& projection,
                                                const XMFLOAT4& materialColor,
                                                const float& roughness,
                                                const float& metalness,
                                                const bool& useTexture)
    {
        CBPerObject data = {};
        // HLSL에서 row-major로 사용할 수 있도록 전치 행렬 사용
        data.world         = XMMatrixTranspose(world);
        data.view          = XMMatrixTranspose(view);
        data.projection    = XMMatrixTranspose(projection);
        data.materialColor = materialColor;
        data.roughness     = roughness;
        data.metalness     = metalness;
        data.useTexture    = useTexture ? 1 : 0;

        m_context->UpdateSubresource(m_cbPerObject.Get(), 0, nullptr, &data, 0, 0);
        m_context->VSSetConstantBuffers(0, 1, m_cbPerObject.GetAddressOf());
        m_context->PSSetConstantBuffers(0, 1, m_cbPerObject.GetAddressOf());
    }

    void ForwardRenderSystem::UpdateBonesCB(const DirectX::XMFLOAT4X4* boneMatrices,
                                            std::uint32_t boneCount)
    {
        if (!m_cbBones || !boneMatrices || boneCount == 0)
            return;

        CBBones data = {};
        const std::uint32_t count = (std::min)(boneCount, MaxBones);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            XMMATRIX m = XMLoadFloat4x4(&boneMatrices[i]);
            data.bones[i] = XMMatrixTranspose(m);
        }

        m_context->UpdateSubresource(m_cbBones.Get(), 0, nullptr, &data, 0, 0);
        m_context->VSSetConstantBuffers(2, 1, m_cbBones.GetAddressOf());
    }

    void ForwardRenderSystem::UpdateLightingCB(const Camera& camera,
                                               int shadingMode,
                                               bool enableFillLight,
                                               const XMMATRIX& lightViewProj)
    {
        CBLighting data = {};

        // Key Light 방향은 파라미터에서 받아와 정규화합니다.
        {
            XMVECTOR dir = XMVectorSet(
                m_lightingParameters.keyDirection.x,
                m_lightingParameters.keyDirection.y,
                m_lightingParameters.keyDirection.z,
                0.0f);

            if (XMVector3Equal(dir, XMVectorZero()))
                dir = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);

            dir = XMVector3Normalize(dir);
            XMStoreFloat3(&data.keyLight.direction, dir);
        }
        data.keyLight.color     = m_lightingParameters.diffuseColor;
        data.keyLight.intensity = m_lightingParameters.keyIntensity;

        // Fill Light: 반대편에서 살짝 채워주는 부드러운 광원
        if (enableFillLight)
        {
            XMVECTOR dir = XMVectorSet(
                m_lightingParameters.fillDirection.x,
                m_lightingParameters.fillDirection.y,
                m_lightingParameters.fillDirection.z,
                0.0f);

            if (XMVector3Equal(dir, XMVectorZero()))
                dir = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);

            dir = XMVector3Normalize(dir);
            XMStoreFloat3(&data.fillLight.direction, dir);

            data.fillLight.color     = m_lightingParameters.diffuseColor;
            data.fillLight.intensity = m_lightingParameters.fillIntensity;
        }
        else
        {
            data.fillLight.direction = XMFLOAT3(0.0f, -1.0f, 0.0f);
            data.fillLight.color     = XMFLOAT3(0.0f, 0.0f, 0.0f);
            data.fillLight.intensity = 0.0f;
        }

        // 카메라 및 재질 정보
        data.cameraPosition = camera.GetPosition();

        data.materialDiffuse  = XMFLOAT4(
            m_lightingParameters.diffuseColor.x,
            m_lightingParameters.diffuseColor.y,
            m_lightingParameters.diffuseColor.z,
            1.0f);

        data.materialSpecular = XMFLOAT4(
            m_lightingParameters.specularColor.x,
            m_lightingParameters.specularColor.y,
            m_lightingParameters.specularColor.z,
            m_lightingParameters.shininess); // a: shininess

        data.shadingMode   = shadingMode;
        data.lightViewProj = XMMatrixTranspose(lightViewProj);

        m_context->UpdateSubresource(m_cbLighting.Get(), 0, nullptr, &data, 0, 0);
        m_context->PSSetConstantBuffers(1, 1, m_cbLighting.GetAddressOf());
    }

    void ForwardRenderSystem::RenderSkybox(const Camera& camera,
                                           const XMMATRIX& view,
                                           const XMMATRIX& projection)
    {
        if (!m_skyboxEnabled || !m_skyboxSRV || !m_skyboxVS || !m_skyboxPS)
            return;

        // 입력 어셈블러 설정은 기존 큐브 지오메트리를 재사용합니다.
        UINT stride = sizeof(SimpleVertex);
        UINT offset = 0;
        ID3D11Buffer* vb = m_vertexBuffer.Get();
        m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
        m_context->IASetInputLayout(m_inputLayout.Get());
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // 카메라를 원점에 두기 위해 View 행렬의 이동 성분을 제거합니다.
        XMMATRIX viewNoTrans = view;
        viewNoTrans.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, XMVectorGetW(view.r[3]));

        XMMATRIX world = XMMatrixIdentity();
        XMFLOAT4 whiteColor = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        // 스카이박스에서는 PBR 파라미터/텍스처를 사용하지 않습니다.
        UpdatePerObjectCB(world, viewNoTrans, projection, whiteColor, 1.0f, 0.0f, false);

        // 스카이박스 전용 상태 설정
        if (m_skyboxDepthState)
            m_context->OMSetDepthStencilState(m_skyboxDepthState.Get(), 0);
        if (m_skyboxRasterizerState)
            m_context->RSSetState(m_skyboxRasterizerState.Get());

        ID3D11ShaderResourceView* skyboxSrv = m_skyboxSRV.Get();
        m_context->PSSetShaderResources(3, 1, &skyboxSrv);

        // 샘플러는 기존 것 재사용
        ID3D11SamplerState* samplers[] = { m_samplerState.Get() };
        m_context->PSSetSamplers(0, 1, samplers);

        m_context->VSSetShader(m_skyboxVS.Get(), nullptr, 0);
        m_context->PSSetShader(m_skyboxPS.Get(), nullptr, 0);

        m_context->DrawIndexed(m_indexCount, 0, 0);

        // 기본 깊이 스텐실 상태로 복원 (nullptr = 디폴트)
        m_context->OMSetDepthStencilState(nullptr, 0);
        // 래스터라이저 상태는 Render 본문에서 다시 설정합니다.
    }

    void ForwardRenderSystem::RenderSkinnedMeshes(
        const Camera& camera,
        const std::vector<SkinnedDrawCommand>& commands)
    {
        if (commands.empty())
        {
            OutputDebugStringA("[ForwardRenderSystem] RenderSkinnedMeshes: commands empty\n");
            return;
        }

        if (!m_skinnedVertexShader || !m_pixelShader || !m_inputLayoutSkinned)
        {
            OutputDebugStringA("[ForwardRenderSystem] RenderSkinnedMeshes: missing shaders/layout\n");
            return;
        }

        {
            char buf[128] = {};
            std::snprintf(buf, sizeof(buf),
                          "[ForwardRenderSystem] RenderSkinnedMeshes: commands=%zu\n",
                          commands.size());
            OutputDebugStringA(buf);
        }

        // 카메라 행렬
        XMMATRIX view = camera.GetViewMatrix();
        XMMATRIX proj = camera.GetProjectionMatrix();

        // 스키닝 전용 셰이더/입력 어셈블러 설정
        m_context->VSSetShader(m_skinnedVertexShader.Get(), nullptr, 0);
        m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->IASetInputLayout(m_inputLayoutSkinned.Get());

        // FBX 스키닝 메시에서는 큐브용 "반전 컬링" 로직을 사용하지 않고,
        // 항상 기본 래스터라이저 상태(뒷면 컬링)를 사용합니다.
        if (m_rasterizerState)
        {
            m_context->RSSetState(m_rasterizerState.Get());
        }

        for (const auto& cmd : commands)
        {
            if (!cmd.vertexBuffer || !cmd.indexBuffer || cmd.indexCount == 0)
                continue;

            // 정점/인덱스 버퍼 설정
            UINT stride = cmd.stride;
            UINT offset = 0;
            ID3D11Buffer* vb = cmd.vertexBuffer;
            m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            m_context->IASetIndexBuffer(cmd.indexBuffer, DXGI_FORMAT_R32_UINT, 0);

            // 본 상수 버퍼 업데이트
            UpdateBonesCB(cmd.bones, cmd.boneCount);

            // 머티리얼/PBR 파라미터 포함 per-object CB 업데이트
            XMFLOAT4 matColor(cmd.color.x, cmd.color.y, cmd.color.z, 1.0f);
            // 스키닝 메시에서는 기본적으로 텍스처를 사용하는 것이 자연스럽습니다.
            UpdatePerObjectCB(cmd.world, view, proj, matColor,
                              cmd.roughness, cmd.metalness,
                              true);

            // === FBX 서브셋 + 머티리얼 SRV 기반 드로우 ===
            if (!m_skinnedRegistry || cmd.meshKey.empty())
            {
                // 레지스트리가 없으면 이전 방식대로 한 번만 그립니다.
                ID3D11ShaderResourceView* diffuseSrv = m_diffuseSRV.Get();
                if (!cmd.albedoTexturePath.empty())
                {
                    if (ID3D11ShaderResourceView* srv = GetOrCreateTexture(cmd.albedoTexturePath))
                    {
                        diffuseSrv = srv;
                    }
                }

                ID3D11ShaderResourceView* srvs[] =
                {
                    diffuseSrv,
                    m_normalSRV.Get(),
                    m_specularSRV.Get()
                };
                m_context->PSSetShaderResources(0, 3, srvs);

                m_context->DrawIndexed(cmd.indexCount, cmd.startIndex, cmd.baseVertex);
                continue;
            }

            auto mesh = m_skinnedRegistry->Find(cmd.meshKey);
            if (!mesh || mesh->subsets.empty())
            {
                // 서브셋 정보가 없으면 전체를 한 번 그립니다.
                m_context->DrawIndexed(cmd.indexCount, cmd.startIndex, cmd.baseVertex);
                continue;
            }

            for (const auto& subset : mesh->subsets)
            {
                if (subset.indexCount == 0)
                    continue;

                ID3D11ShaderResourceView* diffuseSrv = m_diffuseSRV.Get();
                if (subset.materialIndex < mesh->materialSRVs.size() &&
                    mesh->materialSRVs[subset.materialIndex])
                {
                    diffuseSrv = mesh->materialSRVs[subset.materialIndex].Get();
                }

                ID3D11ShaderResourceView* srvs[] =
                {
                    diffuseSrv,
                    m_normalSRV.Get(),
                    m_specularSRV.Get()
                };
                m_context->PSSetShaderResources(0, 3, srvs);

                m_context->DrawIndexed(subset.indexCount, subset.startIndex, cmd.baseVertex);
            }
        }
    }

    XMMATRIX ForwardRenderSystem::BuildWorldMatrix(const TransformComponent& transform) const
    {
        XMVECTOR scale = XMLoadFloat3(&transform.scale);
        XMVECTOR rotation = XMLoadFloat3(&transform.rotation);
        XMVECTOR translation = XMLoadFloat3(&transform.position);

        XMMATRIX S = XMMatrixScalingFromVector(scale);
        XMMATRIX R = XMMatrixRotationRollPitchYawFromVector(rotation);
        XMMATRIX T = XMMatrixTranslationFromVector(translation);

        return S * R * T;
    }

    void ForwardRenderSystem::Render(const World& world,
                                     const Camera& camera,
                                     EntityId /*entity*/,
                                     int shadingMode,
                                     bool enableFillLight,
                                     const std::vector<SkinnedDrawCommand>& skinnedCommands)
    {
        if (!m_vertexBuffer || !m_indexBuffer || !m_vertexShader || !m_pixelShader)
            return;

        if (!m_sceneRTV || !m_sceneDSV)
            return;

        ID3D11RenderTargetView* backBufferRTV = m_renderDevice.GetBackBufferRTV();
        ID3D11DepthStencilView* backBufferDSV = m_renderDevice.GetBackBufferDSV();

        // === 1) 섀도우 맵 패스 (깊이 전용) ===
        // 라이트 방향은 Key Light 방향을 그대로 사용합니다.
        XMVECTOR lightDir = XMVectorSet(
            m_lightingParameters.keyDirection.x,
            m_lightingParameters.keyDirection.y,
            m_lightingParameters.keyDirection.z,
            0.0f);
        if (XMVector3Equal(lightDir, XMVectorZero()))
        {
            lightDir = XMVectorSet(0.5f, -1.0f, 0.5f, 0.0f);
        }
        lightDir = XMVector3Normalize(lightDir);

        // 간단한 정방향 라이트 뷰/투영 (씬 중심(0,0,0)을 바라보는 정사영)
        const float lightDist   = 20.0f;
        const float orthoWidth  = 20.0f;
        const float orthoHeight = 20.0f;
        const float nearZ       = 1.0f;
        const float farZ        = 60.0f;

        XMVECTOR lightPos = XMVectorScale(-lightDir, lightDist);
        XMVECTOR target   = XMVectorZero();
        XMVECTOR up       = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

        XMMATRIX lightView = XMMatrixLookAtLH(lightPos, target, up);
        XMMATRIX lightProj = XMMatrixOrthographicLH(orthoWidth, orthoHeight, nearZ, farZ);
        XMMATRIX lightViewProj = lightView * lightProj;

        // 섀도우 맵 뷰포트/DSV 설정
        if (m_shadowDSV)
        {
            // 이전 프레임에서 섀도우 맵을 SRV(t4)로 사용했을 수 있으므로
            // 명시적으로 해제해서 DSV로 설정할 때 경고가 나오지 않도록 합니다.
            ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
            m_context->PSSetShaderResources(4, 1, nullSRV);

            m_context->RSSetViewports(1, &m_shadowViewport);
            m_context->OMSetRenderTargets(0, nullptr, m_shadowDSV.Get());
            m_context->ClearDepthStencilView(m_shadowDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

            // 깊이 전용 패스: 기존 VS 를 사용하고 PS 는 비활성화합니다.
            UINT stride = sizeof(SimpleVertex);
            UINT offset = 0;
            ID3D11Buffer* vb = m_vertexBuffer.Get();
            m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
            m_context->IASetInputLayout(m_inputLayout.Get());
            m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
            m_context->PSSetShader(nullptr, nullptr, 0); // 깊이만 기록

            const auto& transforms = world.GetTransforms();
            for (const auto& [id, transform] : transforms)
            {
                // 스키닝 메시가 붙은 엔티티는 여기서 큐브 지오메트리로 그리지 않습니다.
                if (world.GetSkinnedMesh(id))
                    continue;

                XMMATRIX worldM = BuildWorldMatrix(transform);
                // 머티리얼 색은 섀도우 패스에선 사용되지 않습니다.
            XMFLOAT4 dummyColor(1.0f, 1.0f, 1.0f, 1.0f);
            UpdatePerObjectCB(worldM, lightView, lightProj, dummyColor, 1.0f, 0.0f, false);
                m_context->DrawIndexed(m_indexCount, 0, 0);
            }
        }

        // === 2) 메인 컬러 패스 ===
        const float sceneClearColor[4] = { 0.1f, 0.1f, 0.3f, 1.0f };
        ID3D11RenderTargetView* rtvs[] = { m_sceneRTV.Get() };

        // 씬 렌더 타깃 뷰포트 설정
        D3D11_VIEWPORT sceneViewport {};
        sceneViewport.TopLeftX = 0.0f;
        sceneViewport.TopLeftY = 0.0f;
        sceneViewport.Width    = static_cast<float>(m_sceneWidth);
        sceneViewport.Height   = static_cast<float>(m_sceneHeight);
        sceneViewport.MinDepth = 0.0f;
        sceneViewport.MaxDepth = 1.0f;

        // 섀도우 맵 SRV(t4)가 바인딩된 상태에서 다른 DSV를 OM에 묶으면
        // D3D11 런타임이 경고를 내며 자동 해제를 시도하므로, 여기서도 명시적으로 해제합니다.
        {
            ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
            m_context->PSSetShaderResources(4, 1, nullSRV);
        }

        m_context->RSSetViewports(1, &sceneViewport);
        m_context->OMSetRenderTargets(1, rtvs, m_sceneDSV.Get());
        m_context->ClearRenderTargetView(m_sceneRTV.Get(), sceneClearColor);
        m_context->ClearDepthStencilView(m_sceneDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

        XMMATRIX viewM  = camera.GetViewMatrix();
        XMMATRIX projM  = camera.GetProjectionMatrix();
        UpdateLightingCB(camera, shadingMode, enableFillLight, lightViewProj);

        // 스카이박스 렌더링 (옵션)
        if (m_skyboxEnabled)
        {
            RenderSkybox(camera, viewM, projM);
        }

        // 이후 일반 오브젝트 렌더링을 위한 상태 설정
        UINT stride = sizeof(SimpleVertex);
        UINT offset = 0;
        ID3D11Buffer* vb = m_vertexBuffer.Get();
        m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
        m_context->IASetInputLayout(m_inputLayout.Get());
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
        m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);

        // 섀도우 맵/샘플러 바인딩 (있는 경우에만)
        if (m_shadowSRV && m_shadowSampler)
        {
            ID3D11ShaderResourceView* shadowSrv = m_shadowSRV.Get();
            m_context->PSSetShaderResources(4, 1, &shadowSrv);
            ID3D11SamplerState* shadowSampler = m_shadowSampler.Get();
            m_context->PSSetSamplers(1, 1, &shadowSampler);
        }

        ID3D11ShaderResourceView* srvs[] =
        {
            m_diffuseSRV.Get(),
            m_normalSRV.Get(),
            m_specularSRV.Get()
        };
        m_context->PSSetShaderResources(0, 3, srvs);
        ID3D11SamplerState* samplers[] = { m_samplerState.Get() };
        m_context->PSSetSamplers(0, 1, samplers);

        const auto& transforms = world.GetTransforms();
        for (const auto& [id, transform] : transforms)
        {
            // 스키닝 메시가 붙은 엔티티는 여기서 큐브 지오메트리로 그리지 않습니다.
            if (world.GetSkinnedMesh(id))
                continue;

            XMMATRIX worldM = BuildWorldMatrix(transform);

            // 기본 회색 머티리얼 (유니티 기본 큐브 느낌)
            XMFLOAT4 materialColor = XMFLOAT4(0.7f, 0.7f, 0.7f, 1.0f);
            float    roughness     = 0.5f;
            float    metalness     = 0.0f;
            bool useTexture = false;
            if (const MaterialComponent* mat = world.GetMaterial(id))
            {
                materialColor = XMFLOAT4(mat->color.x, mat->color.y, mat->color.z, 1.0f);
                roughness     = mat->roughness;
                metalness     = mat->metalness;
                useTexture    = !mat->albedoTexturePath.empty();
            }

            // 월드 행렬 determinant 가 음수면(축이 한 번 이상 반전됨) 와인딩이 뒤집힌다.
            // 이 경우 전/후면 컬링 기준을 반대로 적용해서 뒷면 컬링이 항상 올바르게 되도록 한다.
            // (현재 큐브의 인덱스/와인딩 정의에 맞추기 위해, determinant >= 0 일 때 "반전 컬링"을 사용한다)
            {
                const float det = XMVectorGetX(XMMatrixDeterminant(worldM));

                // det >= 0 : 기본 와인딩, 하지만 현재 메쉬 정의상 이때 반전 컬링 상태를 쓰는 것이 맞다.
                if (det >= 0.0f && m_rasterizerStateReversed)
                {
                    m_context->RSSetState(m_rasterizerStateReversed.Get());
                }
                else if (m_rasterizerState)
                {
                    m_context->RSSetState(m_rasterizerState.Get());
                }
            }

            UpdatePerObjectCB(worldM, viewM, projM, materialColor, roughness, metalness, useTexture);
            m_context->DrawIndexed(m_indexCount, 0, 0);
        }

        // === 3) 스키닝 메시 패스 (있다면) ===
        if (!skinnedCommands.empty())
        {
            RenderSkinnedMeshes(camera, skinnedCommands);
        }

        if (backBufferRTV)
        {
            ID3D11RenderTargetView* bbRtvs[] = { backBufferRTV };
            m_context->OMSetRenderTargets(1, bbRtvs, backBufferDSV);
        }
    }
}


