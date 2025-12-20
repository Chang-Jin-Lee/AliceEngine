#include "Rendering/ForwardRenderSystem.h"

#include <d3dcompiler.h>
// 텍스처 로더 (vcpkg의 DirectXTK 사용)
#include <DirectXTK/WICTextureLoader.h>
#include <DirectXTK/DDSTextureLoader.h>
#include <filesystem>
#include <vector>
#include <cmath>
#include <cfloat>
#include <algorithm>

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
    int      gEnableNormalMap;
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
    float3 TangentW : TEXCOORD3;
    float3 BitanW   : TEXCOORD4;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    float4 worldPos = mul(float4(input.Position, 1.0f), gWorld);
    float4 viewPos  = mul(worldPos, gView);
    output.Position = mul(viewPos, gProj);

    output.WorldPos = worldPos.xyz;
    float3 N = normalize(mul(float4(input.Normal, 0.0f), gWorld).xyz);
    output.Normal = N;
    // 정적 지오메트리(큐브 등)는 탄젠트/바이탄젠트가 없으므로
    // 노말에서 임의의 직교 기저를 만들어 노말맵(TBN) 계산이 가능하게 합니다.
    float3 up = (abs(N.y) > 0.999f) ? float3(1,0,0) : float3(0,1,0);
    float3 T = normalize(cross(up, N));
    float3 B = normalize(cross(N, T));
    output.TangentW = T;
    output.BitanW = B;
    output.TexCoord = input.TexCoord;

    return output;
}
)";

        // 스키닝 전용 버텍스 셰이더
        // - BLENDINDICES / BLENDWEIGHT / gBones 를 사용하여 4본 스키닝을 적용합니다.
        // - gBones 는 CPU에서 매 프레임 갱신되며, 엔티티별 애니메이션 재생 상태를 반영합니다.
        const char* g_SkinnedVertexShaderSource = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;

    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    int      gEnableNormalMap;
};

cbuffer CBBones : register(b2)
{
    float4x4 gBones[1023];
    uint     gBoneCount;
    float3   _padBones;
};

struct VSInput
{
    float3 Position     : POSITION;
    float3 Normal       : NORMAL;
    float3 Tangent      : TANGENT;
    float3 Binormal     : BINORMAL;
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
    float3 TangentW : TEXCOORD3;
    float3 BitanW   : TEXCOORD4;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    // D3D11-AliceTutorial/31_IBL 방식으로 스키닝
    // - CPU에서 전치 업로드된 본 팔레트에 대해 row-vector 곱(mul(v, M))을 사용합니다.
    uint4 bi = input.BoneIndices;
    float4 bw = input.BoneWeights;

    // DirectX11(행벡터) 기준: v' = v * (Σ w_i * M_i)
    matrix M = bw.x * gBones[bi.x]
             + bw.y * gBones[bi.y]
             + bw.z * gBones[bi.z]
             + bw.w * gBones[bi.w];

    float4 posL = float4(input.Position, 1.0f);
    float3 nL = input.Normal;
    float3 tL = input.Tangent;
    float3 bL = input.Binormal;

    float4 skinnedPos = mul(posL, M);
    float3x3 M3 = (float3x3)M;
    float3 skinnedN = normalize(mul(nL, M3));
    float3 skinnedT = normalize(mul(tL, M3));
    float3 skinnedB = normalize(mul(bL, M3));

    float4 worldPos = mul(skinnedPos, gWorld);
    float4 viewPos  = mul(worldPos, gView);
    output.Position = mul(viewPos, gProj);

    output.WorldPos = worldPos.xyz;
    output.Normal   = normalize(mul(float4(skinnedN, 0.0f), gWorld).xyz);
    output.TangentW = normalize(mul(float4(skinnedT, 0.0f), gWorld).xyz);
    output.BitanW   = normalize(mul(float4(skinnedB, 0.0f), gWorld).xyz);
    output.TexCoord = input.TexCoord;

    return output;
}
)";

        const char* g_PhongPixelShaderSource = R"(
Texture2D gDiffuseMap  : register(t0);
Texture2D gNormalMap   : register(t1);
Texture2D gSpecularMap : register(t2);
TextureCube gSkybox    : register(t3);
SamplerState gSampler  : register(s0);

// 섀도우 맵 (Depth 텍스처)
Texture2D<float>        gShadowMap     : register(t4);
SamplerComparisonState  gShadowSampler : register(s1);

// IBL (Image-Based Lighting) 텍스처들
// - gIBL_Diffuse  : Diffuse IBL (Irradiance map, N 방향 샘플)
// - gIBL_Specular : Specular IBL (Prefiltered env map, R 방향 + roughness)
// - gIBL_BRDF_LUT : BRDF LUT (RG = A,B, NdotV/Roughness → 평균 F,G 계수)
TextureCube gIBL_Diffuse  : register(t5);
TextureCube gIBL_Specular : register(t6);
Texture2D   gIBL_BRDF_LUT : register(t7);

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
    int      gEnableNormalMap;
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

    // Shadow params (34_ToneMapping 방식)
    float  gShadowBias;
    float  gShadowMapSize;
    float  gShadowPCFRadius;
    int    gShadowEnabled;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
    float3 TangentW : TEXCOORD3;
    float3 BitanW   : TEXCOORD4;
};

float4 main(PSInput input) : SV_TARGET
{
    float3 N = normalize(input.Normal);
    if (gEnableNormalMap != 0)
    {
        // D3D11-AliceTutorial/31_IBL/31_BasicPS.hlsl 의 방식으로 TBN 기반 노말맵 적용
        float3 T = normalize(input.TangentW);
        float3 B = normalize(input.BitanW);
        float handed = dot(cross(T, B), N);
        if (handed < 0.0f) B = -B;
        float3x3 TBN = float3x3(T, B, N);
        float3 N_ts = gNormalMap.Sample(gSampler, input.TexCoord).xyz * 2.0f - 1.0f;
        N_ts.y = -N_ts.y; // 그린 채널 반전 보정
        N_ts = normalize(N_ts);
        N = normalize(mul(N_ts, TBN));
    }

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
        if (gShadowEnabled != 0)
        {
        float4 shadowPos = mul(float4(input.WorldPos, 1.0f), gLightViewProj);
        shadowPos.xyz /= shadowPos.w;

        float2 shadowTex;
        shadowTex.x = shadowPos.x * 0.5f + 0.5f;
        shadowTex.y = -shadowPos.y * 0.5f + 0.5f;
        float depth = shadowPos.z;

        // Shadow map texel 크기 및 PCF 반경(텍셀 단위)
        const float2 texelSize = float2(1.0f, 1.0f) / max(gShadowMapSize, 1.0f);
        const float2 pcfStep = max(gShadowPCFRadius, 0.0f) * texelSize;

        if (shadowTex.x >= 0.0f && shadowTex.x <= 1.0f &&
            shadowTex.y >= 0.0f && shadowTex.y <= 1.0f)
        {
            float sum = 0.0f;
            [unroll] for (int y = -1; y <= 1; ++y)
            {
                [unroll] for (int x = -1; x <= 1; ++x)
                {
                    float2 offset = float2(x, y) * pcfStep;
                    sum += gShadowMap.SampleCmpLevelZero(
                        gShadowSampler,
                        shadowTex + offset,
                        depth - gShadowBias);
                }
            }
            shadow = sum / 9.0f;
        }
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

        float3 Lo = (diffuseTerm + specularTerm) * radiance * shadow;

        // === IBL (Image-Based Lighting) 계산 ===
        // Diffuse IBL: Irradiance map을 법선 방향으로 샘플링
        float3 diffuseIBL = kd * gIBL_Diffuse.Sample(gSampler, Np).rgb * albedo;

        // Specular IBL: Prefiltered env map + BRDF LUT (split-sum 근사)
        float3 Renv = reflect(-Vp, Np);
        const float kMaxSpecularMip = 8.0f;
        float3 prefilteredColor = gIBL_Specular.SampleLevel(gSampler, Renv, roughness * kMaxSpecularMip).rgb;
        float2 specBRDF = gIBL_BRDF_LUT.Sample(gSampler, float2(NdotV, roughness)).rg;
        float3 specularIBL = prefilteredColor * (F0 * specBRDF.x + specBRDF.y);

        // 최종 색상 = 직접광 + 간접광(IBL)
        float3 colorPbr = Lo + (diffuseIBL + specularIBL);

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

        // 스카이박스 전용 셰이더
        // - 불필요한 역행렬 연산 제거, input.Position을 그대로 Direction으로 사용
        // - z/w = 1.0으로 고정하여 항상 배경으로 렌더링
        const char* g_SkyboxVertexShaderSource = R"(
cbuffer CBSkybox : register(b0)
{
    float4x4 gWorldViewProj; // View(회전only) * Proj
};

struct VSInput
{
    float3 Position : POSITION;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 Direction : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput o;

    // 방향 벡터: 큐브의 로컬 위치가 곧 월드 상의 방향입니다.
    o.Direction = input.Position;

    // 위치 변환: 이동 성분이 제거된 ViewProj 행렬을 곱합니다.
    float4 posH = mul(float4(input.Position, 1.0f), gWorldViewProj);
    
    // Z-Fighting 방지 및 배경 처리 최적화
    // z를 w로 치환하면, Perspective Divide(z/w) 후 깊이 값이 항상 1.0(Far Plane)이 됩니다.
    o.Position = posH.xyww;
    
    return o;
}
)";

        const char* g_SkyboxPixelShaderSource = R"(
TextureCube g_TexCube : register(t0);
SamplerState g_Sam : register(s0);

struct PSInput
{
    float4 Position : SV_POSITION;
    float3 Direction : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    // 방향 벡터로 큐브맵 샘플링 (참고 프로젝트와 동일)
    // Sample 함수가 자동으로 정규화하므로 명시적 정규화 불필요
    return g_TexCube.Sample(g_Sam, input.Direction);
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
        if (!CreateIblResources("Sample"))
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateIblResources failed.");
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
        const UINT shadowSize = static_cast<UINT>(m_shadowSettings.mapSizePx);

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
        // 스카이박스 텍스처는 CreateIblResources()에서 IBL 환경맵과 함께 로드됩니다.
        // 여기서는 쉐이더와 렌더 상태만 생성합니다.

        // 스카이박스용 셰이더 컴파일
        ComPtr<ID3DBlob> vsBlob;
        ComPtr<ID3DBlob> psBlob;
        ComPtr<ID3DBlob> errorBlob;

        HRESULT hr = D3DCompile(
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

        // 스카이박스는 컬링 없이 렌더링 (참고 프로젝트와 동일)
        D3D11_RASTERIZER_DESC rsDesc = {};
        rsDesc.FillMode              = D3D11_FILL_SOLID;
        rsDesc.CullMode              = D3D11_CULL_NONE;  // 참고 프로젝트와 동일
        rsDesc.FrontCounterClockwise = FALSE;
        rsDesc.DepthClipEnable       = TRUE;

        hr = m_device->CreateRasterizerState(&rsDesc, m_skyboxRasterizerState.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        ALICE_LOG_INFO("ForwardRenderSystem::CreateSkyboxResources: shaders and states created successfully");
        return true;
    }

    bool ForwardRenderSystem::CreateIblResources(const std::string& iblSetName)
    {
        // IBL 리소스 경로 구성 (Resource/Skybox/{SetName}/)
        // 파일명은 소문자로 시작: bridge, indoor, BakerSample
        namespace fs = std::filesystem;
        fs::path basePath = fs::path("Resource/Skybox") / iblSetName;

        // 파일명 접두사 결정 (Sample -> BakerSample, 나머지는 소문자)
        std::string prefix = iblSetName;
        if (iblSetName == "Sample")
        {
            prefix = "BakerSample";
        }
        else
        {
            // Bridge -> bridge, Indoor -> indoor
            prefix[0] = static_cast<char>(std::tolower(prefix[0]));
        }

        if (!m_resources)
        {
            ALICE_LOG_WARN("ForwardRenderSystem::CreateIblResources: ResourceManager is null.");
            return false;
        }

        // Diffuse IBL (Irradiance map)
        fs::path diffusePath = basePath / (prefix + "DiffuseHDR.dds");
        m_iblDiffuseSRV = m_resources->LoadData<ID3D11ShaderResourceView>(diffusePath, m_device.Get());
        if (!m_iblDiffuseSRV)
        {
            ALICE_LOG_WARN("ForwardRenderSystem::CreateIblResources: failed to load Diffuse IBL \"%s\"",
                diffusePath.string().c_str());
        }

        // Specular IBL (Prefiltered env map)
        fs::path specularPath = basePath / (prefix + "SpecularHDR.dds");
        m_iblSpecularSRV = m_resources->LoadData<ID3D11ShaderResourceView>(specularPath, m_device.Get());
        if (!m_iblSpecularSRV)
        {
            ALICE_LOG_WARN("ForwardRenderSystem::CreateIblResources: failed to load Specular IBL \"%s\"",
                specularPath.string().c_str());
        }

        // BRDF LUT
        fs::path brdfPath = basePath / (prefix + "Brdf.dds");
        m_iblBrdfLutSRV = m_resources->LoadData<ID3D11ShaderResourceView>(brdfPath, m_device.Get());
        if (!m_iblBrdfLutSRV)
        {
            ALICE_LOG_WARN("ForwardRenderSystem::CreateIblResources: failed to load BRDF LUT \"%s\"",
                brdfPath.string().c_str());
        }

        // 스카이박스 환경맵도 함께 로드 (같은 세트 사용)
        fs::path envPath = basePath / (prefix + "EnvHDR.dds");
        m_skyboxSRV = m_resources->LoadData<ID3D11ShaderResourceView>(envPath, m_device.Get());
        if (m_skyboxSRV)
        {
            m_skyboxEnabled = true;
            ALICE_LOG_INFO("ForwardRenderSystem::CreateIblResources: Skybox loaded from \"%s\"",
                envPath.string().c_str());
        }
        else
        {
            m_skyboxEnabled = false;
            ALICE_LOG_WARN("ForwardRenderSystem::CreateIblResources: failed to load Skybox \"%s\"",
                envPath.string().c_str());
        }

        m_currentIblSet = iblSetName;
        ALICE_LOG_INFO("ForwardRenderSystem::CreateIblResources: IBL set \"%s\" loaded", iblSetName.c_str());
        return true;
    }

    bool ForwardRenderSystem::SetIblSet(const std::string& iblSetName)
    {
        // 기존 리소스 해제
        m_iblDiffuseSRV.Reset();
        m_iblSpecularSRV.Reset();
        m_iblBrdfLutSRV.Reset();
        m_skyboxSRV.Reset();

        // 새 IBL 세트 로드
        return CreateIblResources(iblSetName);
    }

    void ForwardRenderSystem::SetSkyboxEnabled(bool enabled)
    {
        m_skyboxEnabled = enabled;
        
        // 스카이박스를 끄면 IBL도 함께 끕니다
        if (!enabled)
        {
            m_iblDiffuseSRV.Reset();
            m_iblSpecularSRV.Reset();
            m_iblBrdfLutSRV.Reset();
            m_skyboxSRV.Reset();
        }
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

        // 스카이박스 전용 CB (DYNAMIC, Map 가능)
        D3D11_BUFFER_DESC skyboxDesc = {};
        skyboxDesc.ByteWidth = sizeof(XMMATRIX); // ViewProj만 (64 bytes)
        skyboxDesc.Usage = D3D11_USAGE_DYNAMIC;
        skyboxDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        skyboxDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = m_device->CreateBuffer(&skyboxDesc, nullptr, m_cbSkybox.ReleaseAndGetAddressOf());
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
        // 노말맵(TBN)을 위해 TANGENT/BINORMAL 시맨틱도 매핑합니다.
        D3D11_INPUT_ELEMENT_DESC skinnedDesc[] =
        {
            { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BINORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
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

        // 본 행렬 상수 버퍼 생성 (31_IBL 방식)
        // - D3D11_USAGE_DYNAMIC + Map(WRITE_DISCARD)로 매 프레임 갱신
        // - 1023개 전체를 Identity로 초기화한 뒤 필요한 본만 덮어써서
        //   인덱스/카운트가 어긋나더라도 메시가 '폭발'하지 않도록 합니다.
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth      = sizeof(CBBones);
        cbDesc.Usage          = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        hr = m_device->CreateBuffer(&cbDesc, nullptr, m_cbBones.ReleaseAndGetAddressOf());
        if (FAILED(hr))
            return false;

        return true;
    }

    bool ForwardRenderSystem::CreateTextures()
    {
        // 디폴트 브릭 텍스처는 "논리 경로"로만 취급합니다.
        // - editorMode: Resource/Image/... 원본을 로드 (필요 시 Cooked 생성 가능)
        // - gameMode  : Cooked/Resource/... 암호화 바이너리를 우선 복호화 로드
        const std::filesystem::path diffuseLogical  = "Resource/Image/Bricks059_1K-JPG_Color.jpg";
        const std::filesystem::path normalLogical   = "Resource/Image/Bricks059_1K-JPG_NormalDX.jpg";
        const std::filesystem::path specularLogical = "Resource/Image/Bricks059_Specular.png";

        if (!m_resources)
        {
            ALICE_LOG_WARN("ForwardRenderSystem::CreateTextures: ResourceManager is null.");
            return false;
        }

        // LoadData<T> 한 줄로 끝! (경로 처리, 암호화 해독, 리소스 생성이 모두 내부에서 처리됨)
        m_diffuseSRV  = m_resources->LoadData<ID3D11ShaderResourceView>(diffuseLogical, m_device.Get());
        m_normalSRV   = m_resources->LoadData<ID3D11ShaderResourceView>(normalLogical, m_device.Get());
        m_specularSRV = m_resources->LoadData<ID3D11ShaderResourceView>(specularLogical, m_device.Get());

        bool ok = true;
        if (!m_diffuseSRV)  ok = false;
        if (!m_normalSRV)   ok = false;
        if (!m_specularSRV) ok = false;

        if (!ok)
        {
            ALICE_LOG_WARN("ForwardRenderSystem::CreateTextures: default brick textures not fully loaded; "
                           "engine will use plain gray materials instead.");
        }

        // 노말맵 기본값(Flat normal)을 생성합니다.
        // - 노말맵이 없는 머티리얼이 "벽돌 노말"을 공유해버리는 문제를 막기 위함입니다.
        // - (0.5, 0.5, 1.0, 1.0) = (128,128,255,255)
        {
            D3D11_TEXTURE2D_DESC td{};
            td.Width = 1;
            td.Height = 1;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_IMMUTABLE;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            const std::uint8_t rgba[4] = { 128, 128, 255, 255 };
            D3D11_SUBRESOURCE_DATA sd{};
            sd.pSysMem = rgba;
            sd.SysMemPitch = 4;

            ComPtr<ID3D11Texture2D> tex;
            if (SUCCEEDED(m_device->CreateTexture2D(&td, &sd, tex.GetAddressOf())))
            {
                D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
                srvd.Format = td.Format;
                srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srvd.Texture2D.MipLevels = 1;
                srvd.Texture2D.MostDetailedMip = 0;
                m_device->CreateShaderResourceView(tex.Get(), &srvd, m_flatNormalSRV.ReleaseAndGetAddressOf());
            }
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
        // "CCW = Front" 로 통일합니다.
        D3D11_RASTERIZER_DESC desc = {};
        desc.FillMode              = D3D11_FILL_SOLID;
        desc.CullMode              = D3D11_CULL_BACK;
        desc.FrontCounterClockwise = TRUE;
        desc.DepthClipEnable       = TRUE;

        HRESULT hr = m_device->CreateRasterizerState(&desc, m_rasterizerState.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        // 음수 스케일(거울 반전)일 때는 정점의 와인딩이 뒤집히므로
        // FrontCounterClockwise 를 반대로 줘서 "반대 와인딩"을 앞면으로 간주한다.
        desc.FrontCounterClockwise = FALSE;
        hr = m_device->CreateRasterizerState(&desc, m_rasterizerStateReversed.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        // Shadow pass 전용 RS (DepthBias)
        // - 34_ToneMapping과 동일: DepthBias=1000, SlopeScaled=1.0
        D3D11_RASTERIZER_DESC shadowDesc = desc;
        shadowDesc.CullMode = D3D11_CULL_BACK;
        shadowDesc.DepthBias = 1000;
        shadowDesc.SlopeScaledDepthBias = 1.0f;
        shadowDesc.DepthBiasClamp = 0.0f;

        // 현재 desc는 "Reversed(FrontCounterClockwise = FALSE)" 상태이므로,
        // 먼저 기본(FrontCounterClockwise = TRUE) 섀도우 RS를 만든 뒤,
        // 다음으로 reversed 섀도우 RS를 만듭니다.
        shadowDesc.FrontCounterClockwise = TRUE;
        hr = m_device->CreateRasterizerState(&shadowDesc, m_shadowRasterizerState.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        shadowDesc.FrontCounterClockwise = FALSE;
        hr = m_device->CreateRasterizerState(&shadowDesc, m_shadowRasterizerStateReversed.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        return true;
    }

    // 경로 문자열을 기반으로 머티리얼 전용 텍스처 SRV 를 가져오거나 생성합니다.
    // - LoadData를 통해 경로 처리, 암호화 해독, 리소스 생성이 모두 내부에서 처리됩니다.
    ID3D11ShaderResourceView* ForwardRenderSystem::GetOrCreateTexture(const std::string& path)
    {
        if (path.empty())
            return nullptr;

        auto it = m_textureCache.find(path);
        if (it != m_textureCache.end())
            return it->second.Get();

        if (!m_device || !m_resources)
            return nullptr;

        // LoadData<T> 한 줄로 끝!
        auto srv = m_resources->LoadData<ID3D11ShaderResourceView>(std::filesystem::path(path), m_device.Get());

        if (!srv)
        {
            ALICE_LOG_WARN("[ForwardRenderSystem] Texture load FAILED: \"%s\"", path.c_str());
            return nullptr;
        }

        m_textureCache.emplace(path, srv);
        ALICE_LOG_INFO("[ForwardRenderSystem] Texture loaded: \"%s\"", path.c_str());

        return srv.Get();
    }

    void ForwardRenderSystem::UpdatePerObjectCB(const XMMATRIX& world,
                                                const XMMATRIX& view,
                                                const XMMATRIX& projection,
                                                const XMFLOAT4& materialColor,
                                                const float& roughness,
                                                const float& metalness,
                                                const bool& useTexture,
                                                const bool& enableNormalMap)
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
        data.enableNormalMap = enableNormalMap ? 1 : 0;

        m_context->UpdateSubresource(m_cbPerObject.Get(), 0, nullptr, &data, 0, 0);
        m_context->VSSetConstantBuffers(0, 1, m_cbPerObject.GetAddressOf());
        m_context->PSSetConstantBuffers(0, 1, m_cbPerObject.GetAddressOf());
    }

    void ForwardRenderSystem::UpdateBonesCB(const DirectX::XMFLOAT4X4* boneMatrices,
                                            std::uint32_t boneCount)
    {
        if (!m_cbBones || !boneMatrices || boneCount == 0)
            return;

        // 31_IBL과 동일하게: 전체 팔레트를 Identity로 초기화 후 필요한 범위만 덮어쓰기
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(m_context->Map(m_cbBones.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            return;

        auto* cb = reinterpret_cast<CBBones*>(mapped.pData);
        if (!cb)
        {
            m_context->Unmap(m_cbBones.Get(), 0);
            return;
        }

        const std::uint32_t count = (std::min)(boneCount, MaxBones);
        cb->boneCount = count;

        const XMMATRIX I = XMMatrixIdentity();
        for (std::uint32_t i = 0; i < MaxBones; ++i)
        {
            cb->bones[i] = XMMatrixTranspose(I);
        }

        for (std::uint32_t i = 0; i < count; ++i)
        {
            XMMATRIX m = XMLoadFloat4x4(&boneMatrices[i]);
            cb->bones[i] = XMMatrixTranspose(m);
        }

        m_context->Unmap(m_cbBones.Get(), 0);
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
        data.shadowBias      = m_shadowSettings.bias;
        data.shadowMapSize   = static_cast<float>(m_shadowSettings.mapSizePx);
        data.shadowPcfRadius = m_shadowSettings.pcfRadius;
        data.shadowEnabled   = m_shadowSettings.enabled ? 1 : 0;

        m_context->UpdateSubresource(m_cbLighting.Get(), 0, nullptr, &data, 0, 0);
        // b1은 VS/PS 모두에서 사용합니다. (VS에서 섀도우 좌표 계산할 수 있게)
        m_context->VSSetConstantBuffers(1, 1, m_cbLighting.GetAddressOf());
        m_context->PSSetConstantBuffers(1, 1, m_cbLighting.GetAddressOf());
    }

    void ForwardRenderSystem::RenderSkybox(const Camera& camera,
                                           const XMMATRIX& view,
                                           const XMMATRIX& projection)
    {
        if (!m_skyboxEnabled || !m_skyboxSRV || !m_skyboxVS || !m_skyboxPS || !m_cbSkybox)
        {
            ALICE_LOG_WARN("ForwardRenderSystem::RenderSkybox: skipped (enabled=%d, srv=%p, vs=%p, ps=%p, cb=%p)",
                m_skyboxEnabled ? 1 : 0,
                m_skyboxSRV.Get(),
                m_skyboxVS.Get(),
                m_skyboxPS.Get(),
                m_cbSkybox.Get());
            return;
        }

        // 이전 상태 저장
        ID3D11RasterizerState* prevRS = nullptr;
        ID3D11DepthStencilState* prevDS = nullptr;
        UINT prevStencilRef = 0;
        ID3D11ShaderResourceView* prevSRV0 = nullptr;
        m_context->RSGetState(&prevRS);
        m_context->OMGetDepthStencilState(&prevDS, &prevStencilRef);
        m_context->PSGetShaderResources(0, 1, &prevSRV0);

        // 스카이박스 전용 상태 설정
        if (m_skyboxDepthState)
            m_context->OMSetDepthStencilState(m_skyboxDepthState.Get(), 0);
        if (m_skyboxRasterizerState)
            m_context->RSSetState(m_skyboxRasterizerState.Get());

        // 입력 어셈블러 설정
        UINT stride = sizeof(SimpleVertex);
        UINT offset = 0;
        ID3D11Buffer* vb = m_vertexBuffer.Get();
        m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
        m_context->IASetInputLayout(m_inputLayout.Get());
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // [핵심 수정] View 행렬에서 이동(Translation) 성분을 제거
        // 스카이박스는 카메라를 따라다녀야 하므로 회전값만 적용
        XMMATRIX viewNoTrans = view;
        viewNoTrans.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);

        // row-vector 규칙: v * (View * Proj)
        XMMATRIX wvp = viewNoTrans * projection;

        // 상수 버퍼 데이터 준비 (ViewProj만)
        XMMATRIX wvpT = XMMatrixTranspose(wvp);

        // DYNAMIC 버퍼에 Map하여 업로드
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(m_context->Map(m_cbSkybox.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            std::memcpy(mapped.pData, &wvpT, sizeof(XMMATRIX));
            m_context->Unmap(m_cbSkybox.Get(), 0);
        }

        // 셰이더 설정
        m_context->VSSetShader(m_skyboxVS.Get(), nullptr, 0);
        m_context->PSSetShader(m_skyboxPS.Get(), nullptr, 0);

        // 스카이박스 전용 CB 바인딩
        ID3D11Buffer* cbs[] = { m_cbSkybox.Get() };
        m_context->VSSetConstantBuffers(0, 1, cbs);

        // 스카이박스 텍스처 바인딩 (t0)
        ID3D11ShaderResourceView* skyboxSrv = m_skyboxSRV.Get();
        m_context->PSSetShaderResources(0, 1, &skyboxSrv);

        // 샘플러 바인딩
        ID3D11SamplerState* sam = m_samplerState.Get();
        m_context->PSSetSamplers(0, 1, &sam);

        m_context->DrawIndexed(m_indexCount, 0, 0);

        // 이전 상태 복원
        m_context->OMSetDepthStencilState(prevDS, prevStencilRef);
        m_context->RSSetState(prevRS);
        m_context->PSSetShaderResources(0, 1, &prevSRV0);
        if (prevSRV0) prevSRV0->Release();
        if (prevDS) prevDS->Release();
        if (prevRS) prevRS->Release();
    }

    void ForwardRenderSystem::RenderSkinnedMeshes(
        const Camera& camera,
        const std::vector<SkinnedDrawCommand>& commands)
    {
        if (commands.empty())
        {
            return;
        }

        if (!m_skinnedVertexShader || !m_pixelShader || !m_inputLayoutSkinned)
        {
            ALICE_LOG_ERRORF("[ForwardRenderSystem] RenderSkinnedMeshes: missing shaders/layout");
            return;
        }

        // 카메라 행렬
        XMMATRIX view = camera.GetViewMatrix();
        XMMATRIX proj = camera.GetProjectionMatrix();

        // 스키닝 전용 셰이더/입력 어셈블러 설정
        m_context->VSSetShader(m_skinnedVertexShader.Get(), nullptr, 0);
        m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->IASetInputLayout(m_inputLayoutSkinned.Get());

        for (const auto& cmd : commands)
        {
            if (!cmd.vertexBuffer || !cmd.indexBuffer || cmd.indexCount == 0)
                continue;

            // [FBX 컬링 수정]
            // 많은 FBX(특히 DCC/임포터 설정)에선 기본 와인딩이 CW(=FrontCounterClockwise = FALSE)로 들어오는 케이스가 있습니다.
            // 엔진 기본은 CCW(FrontCounterClockwise = TRUE)이므로, 스키닝 메시에서는 기본/반전을 서로 바꿔 적용합니다.
            // - det < 0 이면 월드 변환에서 와인딩이 한 번 뒤집히므로, 그에 맞춰 RS도 함께 뒤집어 줍니다.
            {
                const float det = XMVectorGetX(XMMatrixDeterminant(cmd.world));
                const bool flipped = (det < 0.0f);

                // skinned mesh base winding: CW front
                const bool useCWFront = !flipped;
                if (useCWFront && m_rasterizerStateReversed)
                {
                    m_context->RSSetState(m_rasterizerStateReversed.Get()); // FrontCounterClockwise = FALSE
                }
                else if (m_rasterizerState)
                {
                    m_context->RSSetState(m_rasterizerState.Get()); // FrontCounterClockwise = TRUE
                }
            }

            // 정점/인덱스 버퍼 설정
            UINT stride = cmd.stride;
            UINT offset = 0;
            ID3D11Buffer* vb = cmd.vertexBuffer;
            m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            m_context->IASetIndexBuffer(cmd.indexBuffer, DXGI_FORMAT_R32_UINT, 0);

            // 본 상수 버퍼 업데이트
            UpdateBonesCB(cmd.bones, cmd.boneCount);

            // 머티리얼/PBR 파라미터 포함 per-object CB 업데이트
            // cmd에 값이 있으면 사용하고, 없으면 LightingParameters의 기본값 사용
            XMFLOAT4 matColor(cmd.color.x, cmd.color.y, cmd.color.z, 1.0f);
            float roughness = (cmd.roughness > 0.0f || cmd.roughness < 0.0f) 
                ? cmd.roughness 
                : m_lightingParameters.roughness;
            float metalness = (cmd.metalness > 0.0f || cmd.metalness < 0.0f) 
                ? cmd.metalness 
                : m_lightingParameters.metalness;
            
            // 스키닝 메시에서는 기본적으로 텍스처를 사용하는 것이 자연스럽습니다.
            // 노말맵은 SRV(t1)가 유효할 때만 활성화합니다.
            // (스키닝 메시의 경우, 머티리얼별 normalSRV가 없으면 flatNormal을 사용합니다)
            const bool enableNormalMap = (m_flatNormalSRV != nullptr);
            UpdatePerObjectCB(cmd.world, view, proj, matColor,
                              roughness, metalness,
                              true,
                              enableNormalMap);

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

                // 스키닝 메시 렌더링 시에도 IBL/Shadow 텍스처를 바인딩
                ID3D11ShaderResourceView* srvs[8] = {};
                srvs[0] = diffuseSrv;
                srvs[1] = m_flatNormalSRV ? m_flatNormalSRV.Get() : m_normalSRV.Get();
                srvs[2] = m_specularSRV.Get();
                srvs[3] = m_skyboxSRV.Get();
                srvs[4] = m_shadowSRV.Get(); // 섀도우 맵 (t4)
                srvs[5] = m_iblDiffuseSRV.Get();
                srvs[6] = m_iblSpecularSRV.Get();
                srvs[7] = m_iblBrdfLutSRV.Get();
                m_context->PSSetShaderResources(0, 8, srvs);

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

                ID3D11ShaderResourceView* normalSrv = m_flatNormalSRV ? m_flatNormalSRV.Get() : m_normalSRV.Get();
                if (subset.materialIndex < mesh->normalSRVs.size() &&
                    mesh->normalSRVs[subset.materialIndex])
                {
                    normalSrv = mesh->normalSRVs[subset.materialIndex].Get();
                }

                // 서브셋별 렌더링 시에도 IBL/Shadow 텍스처를 바인딩
                ID3D11ShaderResourceView* srvs[8] = {};
                srvs[0] = diffuseSrv;
                srvs[1] = normalSrv;
                srvs[2] = m_specularSRV.Get();
                srvs[3] = m_skyboxSRV.Get();
                srvs[4] = m_shadowSRV.Get(); // 섀도우 맵 (t4)
                srvs[5] = m_iblDiffuseSRV.Get();
                srvs[6] = m_iblSpecularSRV.Get();
                srvs[7] = m_iblBrdfLutSRV.Get();
                m_context->PSSetShaderResources(0, 8, srvs);

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

        // - Directional + Ortho
        // - 씬 포커스(대략적인 중심)를 기준으로 near/far 를 동적으로 잡고
        // - 텍셀 스냅으로 shimmering 을 줄입니다.
        const auto& transforms = world.GetTransforms();
        XMFLOAT3 focusF{ 0.0f, 0.0f, 0.0f };
        XMFLOAT3 minP{  FLT_MAX,  FLT_MAX,  FLT_MAX };
        XMFLOAT3 maxP{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
        int focusCount = 0;
        for (const auto& [id, tr] : transforms)
        {
            (void)id;
            focusF.x += tr.position.x;
            focusF.y += tr.position.y;
            focusF.z += tr.position.z;
            minP.x = (tr.position.x < minP.x) ? tr.position.x : minP.x;
            minP.y = (tr.position.y < minP.y) ? tr.position.y : minP.y;
            minP.z = (tr.position.z < minP.z) ? tr.position.z : minP.z;
            maxP.x = (tr.position.x > maxP.x) ? tr.position.x : maxP.x;
            maxP.y = (tr.position.y > maxP.y) ? tr.position.y : maxP.y;
            maxP.z = (tr.position.z > maxP.z) ? tr.position.z : maxP.z;
            ++focusCount;
        }
        if (focusCount > 0)
        {
            const float inv = 1.0f / static_cast<float>(focusCount);
            focusF.x *= inv;
            focusF.y *= inv;
            focusF.z *= inv;
        }

        // Ortho radius는 기본값을 유지하되, 현재 씬의 대략적인 크기(Transform 위치 범위)에 맞춰 자동 확장합니다.
        float r = m_shadowSettings.orthoRadius;
        if (focusCount > 0)
        {
            const float ex = maxP.x - minP.x;
            const float ey = maxP.y - minP.y;
            const float ez = maxP.z - minP.z;
            const float maxExtent = (std::max)((std::max)(ex, ey), ez);
            r = (std::max)(r, maxExtent * 0.5f + 5.0f);
        }
        const float backDist = r;
        XMVECTOR focus = XMLoadFloat3(&focusF);
        XMVECTOR fwd = lightDir; // keyDirection 자체를 '광선 방향'으로 사용 (튜토리얼과 동일)
        XMVECTOR lightPos = XMVectorSubtract(focus, XMVectorScale(fwd, backDist));
        XMVECTOR up = XMVectorSet(0, 1, 0, 0);
        if (fabsf(XMVectorGetX(XMVector3Dot(up, fwd))) > 0.99f)
        {
            up = XMVectorSet(0, 0, 1, 0);
        }

        XMMATRIX lightView = XMMatrixLookToLH(lightPos, fwd, up);

        // focus 주변 AABB(±r)를 light space 로 변환하여 near/far 를 계산합니다.
        float minZ = 1e9f, maxZ = -1e9f;
        for (int sx = -1; sx <= 1; sx += 2)
        {
            for (int sy = -1; sy <= 1; sy += 2)
            {
                for (int sz = -1; sz <= 1; sz += 2)
                {
                    XMVECTOR cornerWS = XMVectorSet(
                        focusF.x + static_cast<float>(sx) * r,
                        focusF.y + static_cast<float>(sy) * r,
                        focusF.z + static_cast<float>(sz) * r,
                        1.0f);
                    XMVECTOR cornerLS = XMVector3TransformCoord(cornerWS, lightView);
                    const float z = XMVectorGetZ(cornerLS);
                    minZ = (z < minZ) ? z : minZ;
                    maxZ = (z > maxZ) ? z : maxZ;
                }
            }
        }
        const float zPad = r * 0.05f;
        float nearZ = (minZ - zPad);
        if (nearZ < 0.01f) nearZ = 0.01f;
        float farZ = (maxZ + zPad);
        if (farZ <= nearZ) farZ = nearZ + 0.01f;

        XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(-r, r, -r, r, nearZ, farZ);

        // 텍셀 스냅(라이트 뷰 공간 XY를 섀도우맵 텍셀 그리드에 정렬)
        XMVECTOR focusLS = XMVector3TransformCoord(focus, lightView);
        const float texelWorld = (2.0f * r) / static_cast<float>(m_shadowSettings.mapSizePx);
        const float fx = XMVectorGetX(focusLS);
        const float fy = XMVectorGetY(focusLS);
        const float snapX = floorf(fx / texelWorld) * texelWorld;
        const float snapY = floorf(fy / texelWorld) * texelWorld;
        const XMMATRIX snap = XMMatrixTranslation(snapX - fx, snapY - fy, 0.0f);
        lightView = snap * lightView;

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

            // Shadow Depth Bias + 컬링 상태 적용
            if (m_shadowRasterizerState)
            {
                m_context->RSSetState(m_shadowRasterizerState.Get());
            }

            for (const auto& [id, transform] : transforms)
            {
                // 스키닝 메시가 붙은 엔티티는 여기서 큐브 지오메트리로 그리지 않습니다.
                if (world.GetSkinnedMesh(id))
                    continue;

                XMMATRIX worldM = BuildWorldMatrix(transform);

                // 음수 스케일 처리(섀도우 RS)
                {
                    const float det = XMVectorGetX(XMMatrixDeterminant(worldM));
                    const bool flipped = (det < 0.0f);
                    if (flipped && m_shadowRasterizerStateReversed)
                        m_context->RSSetState(m_shadowRasterizerStateReversed.Get());
                    else if (m_shadowRasterizerState)
                        m_context->RSSetState(m_shadowRasterizerState.Get());
                }

                // 머티리얼 색은 섀도우 패스에선 사용되지 않습니다.
                XMFLOAT4 dummyColor(1.0f, 1.0f, 1.0f, 1.0f);
                UpdatePerObjectCB(worldM, lightView, lightProj, dummyColor, 1.0f, 0.0f, false, false);
                m_context->DrawIndexed(m_indexCount, 0, 0);
            }

            // 스키닝 메시도 섀도우를 캐스팅해야 합니다.
            if (!skinnedCommands.empty() && m_skinnedVertexShader && m_inputLayoutSkinned)
            {
                m_context->IASetInputLayout(m_inputLayoutSkinned.Get());
                m_context->VSSetShader(m_skinnedVertexShader.Get(), nullptr, 0);
                m_context->PSSetShader(nullptr, nullptr, 0);

                for (const auto& cmd : skinnedCommands)
                {
                    if (!cmd.vertexBuffer || !cmd.indexBuffer || cmd.indexCount == 0)
                        continue;

                    // [FBX 컬링 수정 - Shadow]
                    // 메인 패스와 동일하게 스키닝 메시 기본 와인딩(CW)을 기준으로 섀도우 RS도 선택합니다.
                    {
                        const float det = XMVectorGetX(XMMatrixDeterminant(cmd.world));
                        const bool flipped = (det < 0.0f);

                        const bool useCWFront = !flipped;
                        if (useCWFront && m_shadowRasterizerStateReversed)
                            m_context->RSSetState(m_shadowRasterizerStateReversed.Get()); // CW front + bias
                        else if (m_shadowRasterizerState)
                            m_context->RSSetState(m_shadowRasterizerState.Get()); // CCW front + bias
                    }

                    UINT stride = cmd.stride;
                    UINT offset = 0;
                    ID3D11Buffer* vb = cmd.vertexBuffer;
                    m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
                    m_context->IASetIndexBuffer(cmd.indexBuffer, DXGI_FORMAT_R32_UINT, 0);

                    UpdateBonesCB(cmd.bones, cmd.boneCount);
                    const XMFLOAT4 dummyColor(1.0f, 1.0f, 1.0f, 1.0f);
                    UpdatePerObjectCB(cmd.world, lightView, lightProj, dummyColor, 1.0f, 0.0f, false, false);

                    m_context->DrawIndexed(cmd.indexCount, cmd.startIndex, cmd.baseVertex);
                }

                // 입력 레이아웃 원복(메인 패스에서 큐브를 그리므로)
                m_context->IASetInputLayout(m_inputLayout.Get());
            }
        }

        // === 2) 메인 컬러 패스 ===
        // 배경색 사용 (스카이박스가 Off일 때 이 색상이 보임)
        float sceneClearColor[4] = {
            m_backgroundColor.x,
            m_backgroundColor.y,
            m_backgroundColor.z,
            m_backgroundColor.w
        };
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

        // 일반 오브젝트 렌더링을 위한 상태 설정
        UINT stride = sizeof(SimpleVertex);
        UINT offset = 0;
        ID3D11Buffer* vb = m_vertexBuffer.Get();
        m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
        m_context->IASetInputLayout(m_inputLayout.Get());
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
        m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);

        // 텍스처 SRV 바인딩 (t0~t7)
        // - 주의: t4(섀도우 맵)는 별도로 바인딩해두고 PSSetShaderResources(0, 8, ...)를 호출하면
        //   srvs[4]가 nullptr인 순간 실수로 언바인드됩니다.
        // - 따라서 한 번에 같이 바인딩합니다.
        ID3D11ShaderResourceView* srvs[8] = {};
        srvs[0] = m_diffuseSRV.Get();
        srvs[1] = m_normalSRV.Get();
        srvs[2] = m_specularSRV.Get();
        srvs[3] = m_skyboxSRV.Get(); // 스카이박스 (t3) - 스카이박스 렌더링 후에도 유지
        srvs[4] = m_shadowSRV.Get(); // 섀도우 맵 (t4)
        srvs[5] = m_iblDiffuseSRV.Get();  // IBL Diffuse (t5)
        srvs[6] = m_iblSpecularSRV.Get(); // IBL Specular (t6)
        srvs[7] = m_iblBrdfLutSRV.Get();  // IBL BRDF LUT (t7)
        m_context->PSSetShaderResources(0, 8, srvs);
        // 샘플러 바인딩: s0(일반), s1(섀도우)
        {
            ID3D11SamplerState* samplers[] = { m_samplerState.Get() };
            m_context->PSSetSamplers(0, 1, samplers);

            ID3D11SamplerState* shadowSampler = m_shadowSampler.Get();
            m_context->PSSetSamplers(1, 1, &shadowSampler);
        }

        for (const auto& [id, transform] : transforms)
        {
            // 스키닝 메시가 붙은 엔티티는 여기서 큐브 지오메트리로 그리지 않습니다.
            if (world.GetSkinnedMesh(id))
                continue;

            XMMATRIX worldM = BuildWorldMatrix(transform);

            // 기본 머티리얼 값 (MaterialComponent가 없으면 LightingParameters의 PBR 값 사용)
            XMFLOAT4 materialColor = XMFLOAT4(
                m_lightingParameters.baseColor.x,
                m_lightingParameters.baseColor.y,
                m_lightingParameters.baseColor.z,
                1.0f);
            float    roughness     = m_lightingParameters.roughness;
            float    metalness     = m_lightingParameters.metalness;
            bool useTexture = false;
            if (const MaterialComponent* mat = world.GetMaterial(id))
            {
                materialColor = XMFLOAT4(mat->color.x, mat->color.y, mat->color.z, 1.0f);
                roughness     = mat->roughness;
                metalness     = mat->metalness;
                useTexture    = !mat->albedoTexturePath.empty();
            }

            // 월드 행렬 determinant 가 음수면(축이 한 번 이상 반전됨) 와인딩이 뒤집힌다.
            // 이 경우 FrontCounterClockwise 를 반대로 한 RS 를 적용해 뒷면 컬링이 안정적으로 동작하게 합니다.
            {
                const float det = XMVectorGetX(XMMatrixDeterminant(worldM));
                const bool flipped = (det < 0.0f);

                if (flipped && m_rasterizerStateReversed)
                {
                    m_context->RSSetState(m_rasterizerStateReversed.Get());
                }
                else if (m_rasterizerState)
                {
                    m_context->RSSetState(m_rasterizerState.Get());
                }
            }

            const bool enableNormalMap = (m_normalSRV != nullptr) && useTexture;
            UpdatePerObjectCB(worldM, viewM, projM, materialColor, roughness, metalness, useTexture, enableNormalMap);
            m_context->DrawIndexed(m_indexCount, 0, 0);
        }

        // === 3) 스키닝 메시 패스 (있다면) === 
        if (!skinnedCommands.empty())
        {
            RenderSkinnedMeshes(camera, skinnedCommands);
        }

        // === 4) 스카이박스 렌더링 (참고 프로젝트: 메인 렌더링 후에 그리기) ===
        // 주의: 스카이박스는 깊이 테스트는 하되 깊이 쓰기는 하지 않으므로,
        // 다른 오브젝트 뒤에 그려도 깊이 테스트를 통과하여 배경으로 보입니다.
        if (m_skyboxEnabled && m_skyboxSRV && m_skyboxVS && m_skyboxPS)
        {
            RenderSkybox(camera, viewM, projM);
        }

		if (backBufferRTV)
        {
            ID3D11RenderTargetView* bbRtvs[] = { backBufferRTV };
            m_context->OMSetRenderTargets(1, bbRtvs, backBufferDSV);
        }
    }
}


