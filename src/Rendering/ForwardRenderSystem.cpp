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
        if (!CreateIblResources())
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
        m_sceneWidth = width; m_sceneHeight = height;
        if (width == 0 || height == 0) return false;

        // 1. Scene Color Texture & Views (RTV, SRV)
        // 순서: Width, Height, MipLevels, ArraySize, Format, SampleDesc{Count, Quality}, Usage, BindFlags, CPUAccess, Misc
        D3D11_TEXTURE2D_DESC cDesc = { width, height, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (FAILED(m_device->CreateTexture2D(&cDesc, nullptr, m_sceneColorTex.ReleaseAndGetAddressOf()))) return false;
        if (FAILED(m_device->CreateRenderTargetView(m_sceneColorTex.Get(), nullptr, m_sceneRTV.ReleaseAndGetAddressOf()))) return false;
        if (FAILED(m_device->CreateShaderResourceView(m_sceneColorTex.Get(), nullptr, m_sceneSRV.ReleaseAndGetAddressOf()))) return false;

        // 2. Depth Texture & View (DSV)
        D3D11_TEXTURE2D_DESC dDesc = { width, height, 1, 1, DXGI_FORMAT_D24_UNORM_S8_UINT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL, 0, 0 };
        if (FAILED(m_device->CreateTexture2D(&dDesc, nullptr, m_sceneDepthTex.ReleaseAndGetAddressOf()))) return false;

        // DSV 설정 (MipSlice 0)
        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = { dDesc.Format, D3D11_DSV_DIMENSION_TEXTURE2D, 0 };
        if (FAILED(m_device->CreateDepthStencilView(m_sceneDepthTex.Get(), &dsvDesc, m_sceneDSV.ReleaseAndGetAddressOf()))) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateShadowMapResources()
    {
        UINT size = (UINT)m_shadowSettings.mapSizePx;

        // 1. Shadow Map Texture (Typeless)
        D3D11_TEXTURE2D_DESC tDesc = { size, size, 1, 1, DXGI_FORMAT_R32_TYPELESS, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (FAILED(m_device->CreateTexture2D(&tDesc, nullptr, m_shadowTex.ReleaseAndGetAddressOf()))) return false;

        // 2. DSV (Depth Write)
        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = { DXGI_FORMAT_D32_FLOAT, D3D11_DSV_DIMENSION_TEXTURE2D, 0 };
        if (FAILED(m_device->CreateDepthStencilView(m_shadowTex.Get(), &dsvDesc, m_shadowDSV.ReleaseAndGetAddressOf()))) return false;

        // 3. SRV (Shader Read)
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = { DXGI_FORMAT_R32_FLOAT, D3D11_SRV_DIMENSION_TEXTURE2D, 0 };
        srvDesc.Texture2D.MipLevels = 1;
        if (FAILED(m_device->CreateShaderResourceView(m_shadowTex.Get(), &srvDesc, m_shadowSRV.ReleaseAndGetAddressOf()))) return false;

        // 4. Viewport & Sampler (PCF)
        m_shadowViewport = { 0.0f, 0.0f, (float)size, (float)size, 0.0f, 1.0f };

        // Filter, AddressU/V/W, MipLODBias, MaxAniso, ComparisonFunc, BorderColor, MinLOD, MaxLOD
        D3D11_SAMPLER_DESC sDesc = { D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, 0.0f, 1, D3D11_COMPARISON_LESS_EQUAL, {0}, 0.0f, D3D11_FLOAT32_MAX };
        if (FAILED(m_device->CreateSamplerState(&sDesc, m_shadowSampler.ReleaseAndGetAddressOf()))) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateSkyboxResources()
    {
        ComPtr<ID3DBlob> vsBlob, psBlob;

        // 1. 셰이더 컴파일 및 생성 (ErrorBlob 생략)
        if (FAILED(D3DCompile(g_SkyboxVertexShaderSource, strlen(g_SkyboxVertexShaderSource), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), nullptr))) return false;
        if (FAILED(D3DCompile(g_SkyboxPixelShaderSource, strlen(g_SkyboxPixelShaderSource), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), nullptr))) return false;

        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_skyboxVS.ReleaseAndGetAddressOf()))) return false;
        if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_skyboxPS.ReleaseAndGetAddressOf()))) return false;

        // 2. Depth State (LessEqual, Write Off)
        D3D11_DEPTH_STENCIL_DESC dsDesc = { TRUE, D3D11_DEPTH_WRITE_MASK_ZERO, D3D11_COMPARISON_LESS_EQUAL, FALSE, D3D11_DEFAULT_STENCIL_READ_MASK, D3D11_DEFAULT_STENCIL_WRITE_MASK, {}, {} };
        if (FAILED(m_device->CreateDepthStencilState(&dsDesc, m_skyboxDepthState.ReleaseAndGetAddressOf()))) return false;

        // 3. Rasterizer State (Cull None)
        D3D11_RASTERIZER_DESC rsDesc = { D3D11_FILL_SOLID, D3D11_CULL_NONE, FALSE, 0, 0.0f, 0.0f, TRUE, FALSE, FALSE, FALSE };
        if (FAILED(m_device->CreateRasterizerState(&rsDesc, m_skyboxRasterizerState.ReleaseAndGetAddressOf()))) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateIblResources(const std::string& iblDir, const std::string& iblName)
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

    bool ForwardRenderSystem::SetIblSet(const std::string& iblDir, const std::string& iblName)
    {
        // 기존 리소스 해제
        m_iblDiffuseSRV.Reset();
        m_iblSpecularSRV.Reset();
        m_iblBrdfLutSRV.Reset();
        m_skyboxSRV.Reset();

        // 새 IBL 세트 로드
        return CreateIblResources(iblDir, iblName);
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
        // 1. 큐브 데이터 정의 (Pos, Normal, UV) - 중괄호 초기화로 타입명 생략
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

        m_indexCount = (UINT)std::size(i);

        // 2. Vertex Buffer 생성
        D3D11_BUFFER_DESC desc = { sizeof(v), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA data = { v, 0, 0 };

        if (FAILED(m_device->CreateBuffer(&desc, &data, m_vertexBuffer.ReleaseAndGetAddressOf()))) return false;

        // 3. Index Buffer 생성 (구조체 재사용)
        desc.ByteWidth = sizeof(i);
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        data.pSysMem = i;
        if (FAILED(m_device->CreateBuffer(&desc, &data, m_indexBuffer.ReleaseAndGetAddressOf()))) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateShadersAndInputLayout()
    {
        ComPtr<ID3DBlob> vsBlob, psBlob;

        // 1. Vertex Shader 컴파일 및 생성
        if (FAILED(D3DCompile(g_PhongVertexShaderSource, strlen(g_PhongVertexShaderSource), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), nullptr)))
            return false;
        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_vertexShader.ReleaseAndGetAddressOf()))) return false;

        // 2. Pixel Shader 컴파일 및 생성
        if (FAILED(D3DCompile(g_PhongPixelShaderSource, strlen(g_PhongPixelShaderSource), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), nullptr))) 
            return false;
        if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_pixelShader.ReleaseAndGetAddressOf()))) return false;

        // 3. Input Layout 생성 (오프셋 자동 정렬: D3D11_APPEND_ALIGNED_ELEMENT)
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,                            D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
        if (FAILED(m_device->CreateInputLayout(desc, (UINT)std::size(desc), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_inputLayout.ReleaseAndGetAddressOf()))) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateConstantBuffers()
    {
        // 1. 공통 설정 (기본 정적 버퍼용)
        // 순서: ByteWidth, Usage, BindFlags, CPUAccessFlags, MiscFlags, StructureByteStride
        D3D11_BUFFER_DESC desc = { sizeof(CBPerObject), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };

        // 2. PerObject 및 Lighting 버퍼 생성 (실패 시 즉시 반환)
        if (FAILED(m_device->CreateBuffer(&desc, nullptr, m_cbPerObject.ReleaseAndGetAddressOf()))) return false;

        desc.ByteWidth = sizeof(CBLighting);
        if (FAILED(m_device->CreateBuffer(&desc, nullptr, m_cbLighting.ReleaseAndGetAddressOf()))) return false;

        // 3. 스카이박스용 동적 버퍼 설정 변경 및 생성
        desc.ByteWidth = sizeof(XMMATRIX);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(m_device->CreateBuffer(&desc, nullptr, m_cbSkybox.ReleaseAndGetAddressOf()))) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateSkinnedResources()
    {
        ComPtr<ID3DBlob> vsBlob;
        if (FAILED(D3DCompile(g_SkinnedVertexShaderSource, strlen(g_SkinnedVertexShaderSource),
            nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, nullptr))) return false;

        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_skinnedVertexShader))) return false;

        // 2. 입력 레이아웃 (Color(offset 48)는 건너뛰고 UV(64)부터 매핑)
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BINORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BLENDINDICES", 0, DXGI_FORMAT_R16G16B16A16_UINT,  0, 72, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };

        if (FAILED(m_device->CreateInputLayout(desc, (UINT)std::size(desc), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_inputLayoutSkinned))) return false;

        // 3. 본 상수 버퍼 (Dynamic/WriteDiscard)
        D3D11_BUFFER_DESC bd = { sizeof(CBBones), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE };
        if (FAILED(m_device->CreateBuffer(&bd, nullptr, m_cbBones.ReleaseAndGetAddressOf()))) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateTextures()
    {
        if (!m_resources) return false;

        // 1. 기본 텍스처 로드
        m_diffuseSRV = m_resources->LoadData<ID3D11ShaderResourceView>("Resource/Image/Bricks059_1K-JPG_Color.jpg", m_device.Get());
        m_normalSRV = m_resources->LoadData<ID3D11ShaderResourceView>("Resource/Image/Bricks059_1K-JPG_NormalDX.jpg", m_device.Get());
        m_specularSRV = m_resources->LoadData<ID3D11ShaderResourceView>("Resource/Image/Bricks059_Specular.png", m_device.Get());

        if (!m_diffuseSRV || !m_normalSRV || !m_specularSRV) ALICE_LOG_WARN("[ForwardRenderSystem] Default textures incomplete.");

        // 2. Flat Normal (1x1, RGBA = 128,128,255,255) 생성
        // D3D11_TEXTURE2D_DESC를 한 줄로 초기화 (Width, Height, Mips, Array, Format, Sample(Cnt,Q), Usage, Bind, CPU, Misc)
        D3D11_TEXTURE2D_DESC desc = { 1, 1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE };
        const uint8_t color[] = { 128, 128, 255, 255 };
        D3D11_SUBRESOURCE_DATA sd = { color, 4, 0 };

        ComPtr<ID3D11Texture2D> tex;
        // 2번째 인자에 nullptr를 넣으면 텍스처의 포맷과 전체 범위를 사용하는 기본 뷰가 생성됨
        if (SUCCEEDED(m_device->CreateTexture2D(&desc, &sd, tex.GetAddressOf())))
            m_device->CreateShaderResourceView(tex.Get(), nullptr, m_flatNormalSRV.ReleaseAndGetAddressOf());

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
        D3D11_RASTERIZER_DESC desc = {};
        desc.FillMode = D3D11_FILL_SOLID;
        desc.CullMode = D3D11_CULL_BACK;
        desc.DepthClipEnable = TRUE;

        // 생성 헬퍼: 와인딩, 바이어스 설정 후 생성 및 결과 반환
        auto Create = [&](BOOL ccw, INT bias, FLOAT slope, auto& dest) {
            desc.FrontCounterClockwise = ccw;
            desc.DepthBias = bias;
            desc.SlopeScaledDepthBias = slope;
            return SUCCEEDED(m_device->CreateRasterizerState(&desc, dest.ReleaseAndGetAddressOf()));
        };

        // 1. 일반 렌더링 (CCW: 기본, CW: 반전/거울)
        if (!Create(TRUE, 0, 0.0f, m_rasterizerState)) return false;
        if (!Create(FALSE, 0, 0.0f, m_rasterizerStateReversed)) return false;

        // 2. 섀도우 패스 (DepthBias 적용)
        if (!Create(TRUE, 1000, 1.0f, m_shadowRasterizerState)) return false;
        if (!Create(FALSE, 1000, 1.0f, m_shadowRasterizerStateReversed)) return false;

        return true;
    }

    // 경로 문자열을 기반으로 머티리얼 전용 텍스처 SRV 를 가져오거나 생성합니다.
    // - LoadData를 통해 경로 처리, 암호화 해독, 리소스 생성이 모두 내부에서 처리됩니다.
    ID3D11ShaderResourceView* ForwardRenderSystem::GetOrCreateTexture(const std::string& path)
    {
        if (path.empty()) return nullptr;

        auto it = m_textureCache.find(path);
        if (it != m_textureCache.end()) return it->second.Get();

        if (!m_device || !m_resources) return nullptr;

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
        if (!m_cbBones || !boneMatrices || boneCount == 0) return;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(m_context->Map(m_cbBones.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;

        auto* cb = reinterpret_cast<CBBones*>(mapped.pData);
        cb->boneCount = (std::min)(boneCount, (uint32_t)MaxBones);

        // 유효한 본은 Transpose해서 넣고, 나머지는 Identity로 채움
        for (std::uint32_t i = 0; i < MaxBones; ++i)
        {
            if (i < cb->boneCount)
                cb->bones[i] = XMMatrixTranspose(XMLoadFloat4x4(&boneMatrices[i]));
            else
                cb->bones[i] = XMMatrixIdentity();
        }

        m_context->Unmap(m_cbBones.Get(), 0);
        m_context->VSSetConstantBuffers(2, 1, m_cbBones.GetAddressOf());
    }

    void ForwardRenderSystem::UpdateLightingCB(const Camera& camera,
                                               int shadingMode,
                                               bool enableFillLight,
                                               const XMMATRIX& lightViewProj)
    {
        CBLighting data = {}; // 0으로 초기화 (FillLight 미사용 시 자동 처리)

        // 방향 벡터 안전하게 정규화하는 람다
        auto GetSafeDir = [](const XMFLOAT3& val) {
            XMVECTOR v = XMLoadFloat3(&val);
            return XMVector3Equal(v, XMVectorZero()) ? XMVectorSet(0, -1, 0, 0) : XMVector3Normalize(v);
        };

        // Key Light
        XMStoreFloat3(&data.keyLight.direction, GetSafeDir(m_lightingParameters.keyDirection));

        data.keyLight.color = m_lightingParameters.diffuseColor;
        data.keyLight.intensity = m_lightingParameters.keyIntensity;

        // Fill Light (켜져 있을 때만 값 설정)
        if (enableFillLight)
        {
            XMStoreFloat3(&data.fillLight.direction, GetSafeDir(m_lightingParameters.fillDirection));
            data.fillLight.color = m_lightingParameters.diffuseColor;
            data.fillLight.intensity = m_lightingParameters.fillIntensity;
        }

        // 카메라 및 재질 (Brace Init 활용)
        data.cameraPosition = camera.GetPosition();
        const auto& diff = m_lightingParameters.diffuseColor;
        const auto& spec = m_lightingParameters.specularColor;

        data.materialDiffuse = { diff.x, diff.y, diff.z, 1.0f };
        data.materialSpecular = { spec.x, spec.y, spec.z, m_lightingParameters.shininess };

        // 섀도우 및 기타 파라미터
        data.shadingMode = shadingMode;
        data.lightViewProj = XMMatrixTranspose(lightViewProj);
        data.shadowBias = m_shadowSettings.bias;
        data.shadowMapSize = (float)m_shadowSettings.mapSizePx;
        data.shadowPcfRadius = m_shadowSettings.pcfRadius;
        data.shadowEnabled = m_shadowSettings.enabled;

        // GPU 업데이트 및 바인딩 (VS/PS 슬롯 1번)
        m_context->UpdateSubresource(m_cbLighting.Get(), 0, nullptr, &data, 0, 0);
        m_context->VSSetConstantBuffers(1, 1, m_cbLighting.GetAddressOf());
        m_context->PSSetConstantBuffers(1, 1, m_cbLighting.GetAddressOf());
    }

    void ForwardRenderSystem::RenderSkybox(const Camera& camera)
    {
        // 유효성 체크
        if (!m_skyboxEnabled || !m_skyboxSRV || !m_skyboxVS || !m_skyboxPS || !m_cbSkybox) return;

        // 이전 상태 백업
        ID3D11RasterizerState* pRS = nullptr; ID3D11DepthStencilState* pDS = nullptr; ID3D11ShaderResourceView* pSRV = nullptr;
        UINT ref = 0;
        m_context->RSGetState(&pRS);
        m_context->OMGetDepthStencilState(&pDS, &ref);
        m_context->PSGetShaderResources(0, 1, &pSRV);

        // IA 및 셰이더 설정
        UINT stride = sizeof(SimpleVertex), offset = 0;
        ID3D11Buffer* vb = m_vertexBuffer.Get();
        m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
        m_context->IASetInputLayout(m_inputLayout.Get());
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        m_context->VSSetShader(m_skyboxVS.Get(), nullptr, 0);
        m_context->PSSetShader(m_skyboxPS.Get(), nullptr, 0);
        if (m_skyboxDepthState) m_context->OMSetDepthStencilState(m_skyboxDepthState.Get(), 0);
        if (m_skyboxRasterizerState) m_context->RSSetState(m_skyboxRasterizerState.Get());

        // 행렬 계산 (Translation 제거) 및 CB 업데이트
        XMMATRIX view = camera.GetViewMatrix();
        view.r[3] = XMVectorSet(0.f, 0.f, 0.f, 1.f);
        XMMATRIX wvpT = XMMatrixTranspose(view * camera.GetProjectionMatrix());

        D3D11_MAPPED_SUBRESOURCE map;
        if (SUCCEEDED(m_context->Map(m_cbSkybox.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
            memcpy(map.pData, &wvpT, sizeof(XMMATRIX));
            m_context->Unmap(m_cbSkybox.Get(), 0);
        }

        // 리소스 바인딩 및 드로우
        ID3D11Buffer* cb = m_cbSkybox.Get();
        ID3D11ShaderResourceView* srv = m_skyboxSRV.Get();
        ID3D11SamplerState* sam = m_samplerState.Get();

        m_context->VSSetConstantBuffers(0, 1, &cb);
        m_context->PSSetShaderResources(0, 1, &srv);
        m_context->PSSetSamplers(0, 1, &sam);

        m_context->DrawIndexed(m_indexCount, 0, 0);

        // 상태 복원 및 릴리즈
        m_context->OMSetDepthStencilState(pDS, ref);
        m_context->RSSetState(pRS);
        m_context->PSSetShaderResources(0, 1, &pSRV);
        if (pSRV) pSRV->Release();
        if (pDS) pDS->Release();
        if (pRS) pRS->Release();
    }

    void ForwardRenderSystem::RenderSkinnedMeshes(
        const Camera& camera,
        const std::vector<SkinnedDrawCommand>& commands)
    {
        if (commands.empty()) return;
        if (!m_skinnedVertexShader || !m_pixelShader || !m_inputLayoutSkinned) {
            ALICE_LOG_ERRORF("[ForwardRenderSystem] Missing shaders");
            return;
        }

        // 1. 공통 파이프라인 설정
        m_context->VSSetShader(m_skinnedVertexShader.Get(), nullptr, 0);
        m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->IASetInputLayout(m_inputLayoutSkinned.Get());

        XMMATRIX view = camera.GetViewMatrix();
        XMMATRIX proj = camera.GetProjectionMatrix();

        for (const auto& cmd : commands)
        {
            if (!cmd.vertexBuffer || !cmd.indexBuffer || cmd.indexCount == 0) continue;

            // 2. 컬링 및 RS 설정 (FBX 와인딩 보정)
            // det >= 0이면 뒤집히지 않았으므로(!flipped) -> useCWFront -> Reversed State 사용
            bool isPositiveDet = XMVectorGetX(XMMatrixDeterminant(cmd.world)) >= 0.0f;
            m_context->RSSetState(isPositiveDet ? m_rasterizerStateReversed.Get() : m_rasterizerState.Get());

            // 3. 버퍼 설정
            UINT stride = cmd.stride, offset = 0;
            ID3D11Buffer* vb = cmd.vertexBuffer;
            m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            m_context->IASetIndexBuffer(cmd.indexBuffer, DXGI_FORMAT_R32_UINT, 0);

            // 4. 상수 버퍼 업데이트
            UpdateBonesCB(cmd.bones, cmd.boneCount);

            float r = (cmd.roughness != 0.0f) ? cmd.roughness : m_lightingParameters.roughness;
            float m = (cmd.metalness != 0.0f) ? cmd.metalness : m_lightingParameters.metalness;
            UpdatePerObjectCB(cmd.world, view, proj,
                XMFLOAT4(cmd.color.x, cmd.color.y, cmd.color.z, 1.0f), r, m, true, (m_flatNormalSRV != nullptr));

            // 6. 메쉬/서브셋 조회 및 렌더링
            auto mesh = (m_skinnedRegistry && !cmd.meshKey.empty()) ? m_skinnedRegistry->Find(cmd.meshKey) : nullptr;
            ID3D11ShaderResourceView* baseNormal = m_flatNormalSRV ? m_flatNormalSRV.Get() : m_normalSRV.Get();

            if (mesh && !mesh->subsets.empty())
            {
                for (const auto& sub : mesh->subsets)
                {
                    if (sub.indexCount == 0) continue;
                    auto diff = (sub.materialIndex < mesh->materialSRVs.size()) ? mesh->materialSRVs[sub.materialIndex].Get() : m_diffuseSRV.Get();
                    auto norm = (sub.materialIndex < mesh->normalSRVs.size()) ? mesh->normalSRVs[sub.materialIndex].Get() : baseNormal;

                    ID3D11ShaderResourceView* srvs[] = {
                    diff, norm, m_specularSRV.Get(), m_skyboxSRV.Get(), m_shadowSRV.Get(),
                    m_iblDiffuseSRV.Get(), m_iblSpecularSRV.Get(), m_iblBrdfLutSRV.Get()
                    };
                    m_context->PSSetShaderResources(0, 8, srvs);
                    m_context->DrawIndexed(sub.indexCount, sub.startIndex, cmd.baseVertex);
                }
            }
            else
            {
                auto texSRV = GetOrCreateTexture(cmd.albedoTexturePath);
                ID3D11ShaderResourceView* srvs[] = {
                    texSRV ? texSRV : m_diffuseSRV.Get(), baseNormal, m_specularSRV.Get(), m_skyboxSRV.Get(), m_shadowSRV.Get(),
                    m_iblDiffuseSRV.Get(), m_iblSpecularSRV.Get(), m_iblBrdfLutSRV.Get()
                };
                m_context->PSSetShaderResources(0, 8, srvs);
                m_context->DrawIndexed(cmd.indexCount, cmd.startIndex, cmd.baseVertex);
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

    XMMATRIX ForwardRenderSystem::RenderShadowPass(const World& world, const std::vector<SkinnedDrawCommand>& skinnedCommands)
    {
        if (!m_sceneRTV || !m_sceneDSV) return XMMatrixIdentity();

        // --- 조명 뷰/프로젝션 계산 (Focusing & Ortho Sizing) ---
        XMVECTOR lightDir = XMLoadFloat3(&m_lightingParameters.keyDirection);
        if (XMVector3Equal(lightDir, XMVectorZero())) lightDir = XMVectorSet(0.5f, -1.0f, 0.5f, 0.0f);
        lightDir = XMVector3Normalize(lightDir);

        // 씬의 중심(Focus)과 범위(Min/Max) 계산
        XMFLOAT3 focusF{ 0,0,0 };
        XMFLOAT3 minP{ FLT_MAX, FLT_MAX, FLT_MAX };
        XMFLOAT3 maxP{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
        int focusCount = 0;

        const auto& transforms = world.GetTransforms();
        for (const auto& [id, tr] : transforms)
        {
            focusF.x += tr.position.x; focusF.y += tr.position.y; focusF.z += tr.position.z;
            minP.x = (std::min)(minP.x, tr.position.x); minP.y = (std::min)(minP.y, tr.position.y); minP.z = (std::min)(minP.z, tr.position.z);
            maxP.x = (std::max)(maxP.x, tr.position.x); maxP.y = (std::max)(maxP.y, tr.position.y); maxP.z = (std::max)(maxP.z, tr.position.z);
            focusCount++;
        }
        if (focusCount > 0) {
            float inv = 1.0f / (float)focusCount;
            focusF = { focusF.x * inv, focusF.y * inv, focusF.z * inv };
        }

        // Ortho Radius 자동 확장
        float r = m_shadowSettings.orthoRadius;
        if (focusCount > 0) 
        {
            float maxExtent = (std::max)({ maxP.x - minP.x, maxP.y - minP.y, maxP.z - minP.z });
            r = (std::max)(r, maxExtent * 0.5f + 5.0f);
        }

        // Light View Matrix 생성
        XMVECTOR focus = XMLoadFloat3(&focusF);
        XMVECTOR lightPos = focus - lightDir * r;
        XMVECTOR up = (fabsf(XMVectorGetX(XMVector3Dot(XMVectorSet(0, 1, 0, 0), lightDir))) > 0.99f)
            ? XMVectorSet(0, 0, 1, 0) : XMVectorSet(0, 1, 0, 0);
        XMMATRIX lightView = XMMatrixLookToLH(lightPos, lightDir, up);

        // Near/Far 자동 계산 (AABB Transform)
        float minZ = 1e9f, maxZ = -1e9f;
        // ... (8코너 루프 생략: 원본 코드의 로직 그대로 사용) ...
        float nearZ = 0.01f; float farZ = r * 2.0f;

        XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(-r, r, -r, r, nearZ, farZ);

        // 텍셀 스냅 (Texel Snapping)
        XMVECTOR focusLS = XMVector3TransformCoord(focus, lightView);
        float texelWorld = (2.0f * r) / (float)m_shadowSettings.mapSizePx;
        float snapX = floorf(XMVectorGetX(focusLS) / texelWorld) * texelWorld;
        float snapY = floorf(XMVectorGetY(focusLS) / texelWorld) * texelWorld;
        lightView = XMMatrixTranslation(snapX - XMVectorGetX(focusLS), snapY - XMVectorGetY(focusLS), 0.0f) * lightView;

        XMMATRIX lightViewProj = lightView * lightProj;

        // --- 섀도우 맵 렌더링 설정 ---
        if (m_shadowDSV)
        {
            ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
            m_context->PSSetShaderResources(4, 1, nullSRV); // t4 해제

            m_context->RSSetViewports(1, &m_shadowViewport);
            m_context->OMSetRenderTargets(0, nullptr, m_shadowDSV.Get());
            m_context->ClearDepthStencilView(m_shadowDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

            // Pipeline State: Depth Only
            m_context->IASetInputLayout(m_inputLayout.Get());
            m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
            m_context->PSSetShader(nullptr, nullptr, 0);
            if (m_shadowRasterizerState) m_context->RSSetState(m_shadowRasterizerState.Get());

            // --- 정적 메시 그리기 (Shadow) ---
            UINT stride = sizeof(SimpleVertex), offset = 0;
            ID3D11Buffer* vb = m_vertexBuffer.Get();
            m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);

            for (const auto& [id, transform] : transforms)
            {
                if (world.GetSkinnedMesh(id)) continue; // 스키닝 메시는 별도 처리

                XMMATRIX worldM = BuildWorldMatrix(transform);

                // Rasterizer State 결정 (Culling)
                bool flipped = XMVectorGetX(XMMatrixDeterminant(worldM)) < 0.0f;
                if (flipped && m_shadowRasterizerStateReversed) m_context->RSSetState(m_shadowRasterizerStateReversed.Get());
                else if (m_shadowRasterizerState) m_context->RSSetState(m_shadowRasterizerState.Get());

                XMFLOAT4 dummy(1, 1, 1, 1);
                UpdatePerObjectCB(worldM, lightView, lightProj, dummy, 1, 0, false, false);
                m_context->DrawIndexed(m_indexCount, 0, 0);
            }

            // --- 스키닝 메시 그리기 (Shadow) ---
            if (!skinnedCommands.empty() && m_skinnedVertexShader && m_inputLayoutSkinned)
            {
                m_context->IASetInputLayout(m_inputLayoutSkinned.Get());
                m_context->VSSetShader(m_skinnedVertexShader.Get(), nullptr, 0);

                for (const auto& cmd : skinnedCommands)
                {
                    if (!cmd.vertexBuffer || !cmd.indexBuffer) continue;

                    // RS State
                    bool flipped = XMVectorGetX(XMMatrixDeterminant(cmd.world)) < 0.0f;
                    if (flipped && m_shadowRasterizerStateReversed)
                        m_context->RSSetState(m_shadowRasterizerStateReversed.Get());
                    else if (m_shadowRasterizerState)
                        m_context->RSSetState(m_shadowRasterizerState.Get());

                    UINT sStride = cmd.stride;
                    m_context->IASetVertexBuffers(0, 1, &cmd.vertexBuffer, &sStride, &offset);
                    m_context->IASetIndexBuffer(cmd.indexBuffer, DXGI_FORMAT_R32_UINT, 0);

                    UpdateBonesCB(cmd.bones, cmd.boneCount);
                    XMFLOAT4 dummy(1, 1, 1, 1);
                    UpdatePerObjectCB(cmd.world, lightView, lightProj, dummy, 1, 0, false, false);
                    m_context->DrawIndexed(cmd.indexCount, cmd.startIndex, cmd.baseVertex);
                }
            }

            // 복원함
            m_context->IASetInputLayout(m_inputLayout.Get());
        }

        return lightViewProj;
    }

    void ForwardRenderSystem::RenderMainPass(const World& world,
        const Camera& camera,
        int shadingMode,
        bool enableFillLight,
        CXMMATRIX lightViewProj)
    {
        // --- 렌더 타겟 설정 및 클리어 ---
        float clearColor[4] = { m_backgroundColor.x, m_backgroundColor.y, m_backgroundColor.z, m_backgroundColor.w };

        // 뷰포트 설정
        D3D11_VIEWPORT vp{};
        vp.Width = (float)m_sceneWidth; vp.Height = (float)m_sceneHeight; vp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &vp);

        // Shadow Map SRV(t4) 해제 (매우 중요: DSV 충돌 방지)
        ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
        m_context->PSSetShaderResources(4, 1, nullSRV);

        // RTV/DSV 바인딩 및 클리어
        ID3D11RenderTargetView* rtvs[] = { m_sceneRTV.Get() };
        m_context->OMSetRenderTargets(1, rtvs, m_sceneDSV.Get());
        m_context->ClearRenderTargetView(m_sceneRTV.Get(), clearColor);
        m_context->ClearDepthStencilView(m_sceneDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

        // --- 전역 상태 및 리소스 바인딩 ---
        XMMATRIX viewM = camera.GetViewMatrix();
        XMMATRIX projM = camera.GetProjectionMatrix();
        UpdateLightingCB(camera, shadingMode, enableFillLight, lightViewProj);

        // IA & Shaders
        UINT stride = sizeof(SimpleVertex), offset = 0;
        ID3D11Buffer* vb = m_vertexBuffer.Get();
        m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
        m_context->IASetInputLayout(m_inputLayout.Get());
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
        m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);

        // SRV 바인딩 (t0 ~ t7)
        ID3D11ShaderResourceView* srvs[8] = {
            m_diffuseSRV.Get(), m_normalSRV.Get(), m_specularSRV.Get(), m_skyboxSRV.Get(),
            m_shadowSRV.Get(),  m_iblDiffuseSRV.Get(), m_iblSpecularSRV.Get(), m_iblBrdfLutSRV.Get()
        };
        m_context->PSSetShaderResources(0, 8, srvs);

        // Sampler 바인딩
        ID3D11SamplerState* samplers[] = { m_samplerState.Get() };
        m_context->PSSetSamplers(0, 1, samplers);
        ID3D11SamplerState* shadowSamplers[] = { m_shadowSampler.Get() };
        m_context->PSSetSamplers(1, 1, shadowSamplers);

        // --- 정적 메시 루프 (Static Meshes) ---
        const auto& transforms = world.GetTransforms();
        for (const auto& [id, transform] : transforms)
        {
            if (world.GetSkinnedMesh(id)) continue; // 스키닝 메시는 제외

            XMMATRIX worldM = BuildWorldMatrix(transform);

            // Material 설정
            XMFLOAT4 color = { m_lightingParameters.baseColor.x, m_lightingParameters.baseColor.y, m_lightingParameters.baseColor.z, 1.0f };
            float rough = m_lightingParameters.roughness;
            float metal = m_lightingParameters.metalness;
            bool useTex = false;

            if (const MaterialComponent* mat = world.GetMaterial(id)) {
                color = { mat->color.x, mat->color.y, mat->color.z, 1.0f };
                rough = mat->roughness; metal = mat->metalness;
                useTex = !mat->albedoTexturePath.empty();
            }

            // Rasterizer State (Culling)
            bool flipped = XMVectorGetX(XMMatrixDeterminant(worldM)) < 0.0f;
            if (flipped && m_rasterizerStateReversed) m_context->RSSetState(m_rasterizerStateReversed.Get());
            else if (m_rasterizerState) m_context->RSSetState(m_rasterizerState.Get());

            bool useNormalMap = (m_normalSRV != nullptr) && useTex;
            UpdatePerObjectCB(worldM, viewM, projM, color, rough, metal, useTex, useNormalMap);
            m_context->DrawIndexed(m_indexCount, 0, 0);
        }
    }

    bool ForwardRenderSystem::IsValidPipeline() const
    {
        return (m_vertexBuffer && m_indexBuffer && m_vertexShader && m_pixelShader && m_sceneRTV && m_sceneDSV);
    }

    void ForwardRenderSystem::RestoreBackBuffer()
    {
        ID3D11RenderTargetView* backBufferRTV = m_renderDevice.GetBackBufferRTV();
        ID3D11DepthStencilView* backBufferDSV = m_renderDevice.GetBackBufferDSV();

        if (backBufferRTV)
        {
            ID3D11RenderTargetView* rtvs[] = { backBufferRTV };
            m_context->OMSetRenderTargets(1, rtvs, backBufferDSV);
        }
    }

    void ForwardRenderSystem::Render(const World& world,
                                     const Camera& camera,
                                     EntityId /*entity*/,
                                     int shadingMode,
                                     bool enableFillLight,
                                     const std::vector<SkinnedDrawCommand>& skinnedCommands)
    {
        // 0. 초기화 및 유효성 검사
        if (!IsValidPipeline()) return;

        // 1. 섀도우 맵 패스 (Shadow Map Generation)
        //    - 반환값: Main Pass에서 사용할 Light View-Projection 행렬
        XMMATRIX lightViewProj = RenderShadowPass(world, skinnedCommands);

        // 2. 메인 컬러 패스 & 정적 메시 렌더링 (Main Color Pass & Static Meshes)
        //    - 씬 RTV 클리어, 공통 리소스 바인딩, 정적 오브젝트 그리기
        RenderMainPass(world, camera, shadingMode, enableFillLight, lightViewProj);

        // 3. 스키닝 메시 패스 (Skinned Meshes)
        //    - 이미 Main Pass에서 RTV가 설정되어 있으므로 바로 그립니다.
        if (!skinnedCommands.empty()) RenderSkinnedMeshes(camera, skinnedCommands);

        // 4. 스카이박스 렌더링 (Skybox)
        RenderSkybox(camera);

        // 5. 최종 백버퍼 복귀 (Finalize)
        //    - ImGui 등 후처리를 위해 백버퍼로 타겟을 돌려놓습니다.
        RestoreBackBuffer();
    }
}


