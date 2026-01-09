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

#include "Core/ResourceManager.h"
#include "Core/Logger.h"
#include "Components/TransformComponent.h"
#include "Components/MaterialComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include <fstream>
#include <sstream>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace Alice
{
    // G-Buffer Vertex Shader (인라인 코드)
    namespace
    {
        const char* g_GBufferVertexShaderSource = R"(
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
    
    float4 posW = mul(float4(input.Position, 1.0f), gWorld);
    output.Position = mul(mul(posW, gView), gProj);
    output.WorldPos = posW.xyz;
    
    float3 N = normalize(mul(float4(input.Normal, 0.0f), gWorld).xyz);
    output.Normal = N;
    
    float3 up = (abs(N.y) > 0.999f) ? float3(1,0,0) : float3(0,1,0);
    float3 T = normalize(cross(up, N));
    float3 B = normalize(cross(N, T));
    
    output.TangentW = T;
    output.BitanW = B;
    output.TexCoord = input.TexCoord;
    
    return output;
}
)";

        // 스키닝용 G-Buffer Vertex Shader
        const char* g_GBufferSkinnedVertexShaderSource = R"(
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
    float4 Color        : COLOR;
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
    
    uint4 bi = input.BoneIndices;
    float4 bw = input.BoneWeights;
    matrix M = bw.x * gBones[bi.x]
             + bw.y * gBones[bi.y]
             + bw.z * gBones[bi.z]
             + bw.w * gBones[bi.w];
    
    float4 posL = float4(input.Position, 1.0f);
    float4 skinnedPos = mul(posL, M);
    float3x3 M3 = (float3x3)M;
    float3 skinnedN = normalize(mul(input.Normal, M3));
    float3 skinnedT = normalize(mul(input.Tangent, M3));
    float3 skinnedB = normalize(mul(input.Binormal, M3));
    
    float4 posW = mul(skinnedPos, gWorld);
    output.Position = mul(mul(posW, gView), gProj);
    output.WorldPos = posW.xyz;
    
    output.Normal   = normalize(mul(float4(skinnedN, 0.0f), gWorld).xyz);
    output.TangentW = normalize(mul(float4(skinnedT, 0.0f), gWorld).xyz);
    output.BitanW   = normalize(mul(float4(skinnedB, 0.0f), gWorld).xyz);
    output.TexCoord = input.TexCoord;
    
    return output;
}
)";

        // Transparent Forward-Style Skinned VS
        // - GBuffer 스키닝 VS와 동일한 출력(월드 좌표/노말/UV/TBN)을 만든 뒤,
        //   Transparent PS에서 직접 조명을 계산하고 알파 블렌딩합니다.
        const char* g_TransparentSkinnedVertexShaderSource = R"(
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
    float4 Color        : COLOR;
    float2 TexCoord     : TEXCOORD0;
    uint4  BoneIndices  : BLENDINDICES;
    float4 BoneWeights  : BLENDWEIGHT;
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

    uint4 bi = input.BoneIndices;
    float4 bw = input.BoneWeights;
    matrix M = bw.x * gBones[bi.x]
             + bw.y * gBones[bi.y]
             + bw.z * gBones[bi.z]
             + bw.w * gBones[bi.w];

    float4 posL = float4(input.Position, 1.0f);
    float4 skinnedPos = mul(posL, M);
    float3x3 M3 = (float3x3)M;
    float3 skinnedN = normalize(mul(input.Normal, M3));
    float3 skinnedT = normalize(mul(input.Tangent, M3));
    float3 skinnedB = normalize(mul(input.Binormal, M3));

    float4 posW = mul(skinnedPos, gWorld);
    output.Position = mul(mul(posW, gView), gProj);
    output.WorldPos = posW.xyz;

    output.Normal   = normalize(mul(float4(skinnedN, 0.0f), gWorld).xyz);
    output.TangentW = normalize(mul(float4(skinnedT, 0.0f), gWorld).xyz);
    output.BitanW   = normalize(mul(float4(skinnedB, 0.0f), gWorld).xyz);
    output.TexCoord = input.TexCoord;

    return output;
}
)";

        // Transparent Forward-Style PS
        // - 알파가 1.0에 가까운 픽셀은 디퍼드(불투명)에서 처리하므로 여기서는 제외(discard)
        // - 0.1 미만은 컷아웃으로 제거(Forward/튜토리얼과 동일 스케일)
        const char* g_TransparentPixelShaderSource = R"(
static const float PI = 3.14159265f;
static const float INV_PI = 0.31830988618f;

float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = max(NdotH * NdotH * (a2 - 1.0f) + 1.0f, 1e-4f);
    return a2 / (PI * denom * denom);
}

float GeometrySchlickGGX(float NdotX, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) * 0.125f;
    return NdotX / (NdotX * (1.0f - k) + k);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    float gv = GeometrySchlickGGX(NdotV, roughness);
    float gl = GeometrySchlickGGX(NdotL, roughness);
    return gv * gl;
}

float3 FresnelSchlick(float3 F0, float cosTheta)
{
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}

// 텍스처
Texture2D  g_DiffuseMap : register(t0);
Texture2D  g_NormalMap  : register(t1);

// IBL
TextureCube g_IBL_Diffuse : register(t5);
TextureCube g_IBL_Specular : register(t6);
Texture2D   g_IBL_BRDF_LUT : register(t7);

SamplerState g_Sam : register(s0);

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

cbuffer CBTransparentLight : register(b1)
{
    float3 g_LightDir;
    float  g_LightIntensity;
    float3 g_LightColor;
    float  _pad0;
    float3 g_CameraPosW;
    float  _pad1;
};

struct PSIn
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
    float3 TangentW : TEXCOORD3;
    float3 BitanW   : TEXCOORD4;
};

float3 LinearToSRGB(float3 linearColor)
{
    return pow(max(linearColor, 0.0f), 1.0f / 2.2f);
}

float4 main(PSIn pIn) : SV_Target
{
    float4 tex = float4(1,1,1,1);
    if (gUseTexture != 0)
        tex = g_DiffuseMap.Sample(g_Sam, pIn.TexCoord);

    float alphaTex = tex.a * gMaterialColor.a;

    // 컷아웃(완전 투명 근처) 제거
    // Deferred에서는 반투명(0.1~1.0)을 GBuffer에 넣으면 합성이 깨집니다.
    // - 거의 불투명(>=0.99)만 GBuffer에 기록하고
    // - 나머지 반투명은 라이트 패스 이후 Forward-Style(알파 블렌드) 패스로 별도 렌더링합니다.
    clip(alphaTex - 0.99f);
    // 거의 불투명은 디퍼드에서 처리하므로 여기서는 제외
    if (alphaTex >= 0.99f) discard;

    float3 baseColor = gMaterialColor.rgb;
    if (gUseTexture != 0)
        baseColor *= tex.rgb;

    float3 albedoLinear = pow(max(baseColor, 0.0f), 2.2f);

    float3 N = normalize(pIn.Normal);
    if (gEnableNormalMap != 0)
    {
        float3 T = normalize(pIn.TangentW);
        float3 B = normalize(pIn.BitanW);
        float handed = dot(cross(T, B), N);
        if (handed < 0.0f) B = -B;
        float3x3 TBN = float3x3(T, B, N);
        float3 N_ts = g_NormalMap.Sample(g_Sam, pIn.TexCoord).xyz * 2.0f - 1.0f;
        N_ts.y = -N_ts.y;
        N = normalize(mul(normalize(N_ts), TBN));
    }

    float metalness = saturate(gMetalness);
    float roughness = max(saturate(gRoughness), 0.04f);
    float ao = 1.0f;

    float3 L = normalize(-g_LightDir);
    float3 V = normalize(g_CameraPosW - pIn.WorldPos);
    float3 H = normalize(L + V);

    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedoLinear, metalness);
    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySmith(NdotV, NdotL, roughness);
    float3 F = FresnelSchlick(F0, VdotH);

    float3 numerator = D * G * F;
    float denomSpec = max(4.0f * NdotV * NdotL, 1e-4f);
    float3 specular = numerator / denomSpec;

    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metalness);
    float3 diffuse = kD * albedoLinear * INV_PI;

    float3 radiance = g_LightColor.rgb * PI * g_LightIntensity;
    float3 direct = (diffuse + specular) * radiance * NdotL * ao;

    // IBL
    float3 diffuseIBL = kD * g_IBL_Diffuse.Sample(g_Sam, N).rgb * albedoLinear;
    float3 Renv = reflect(-V, N);
    const float kMaxSpecularMip = 8.0f;
    float3 prefilteredColor = g_IBL_Specular.SampleLevel(g_Sam, Renv, roughness * kMaxSpecularMip).rgb;
    float2 specBRDF = g_IBL_BRDF_LUT.Sample(g_Sam, float2(NdotV, roughness)).rg;
    float3 specularIBL = prefilteredColor * (F0 * specBRDF.x + specBRDF.y);
    float3 ibl = (diffuseIBL + specularIBL) * ao;

    float3 outLinear = direct + ibl;
    return float4(outLinear, alphaTex);
}
)";

        // Quad Vertex Shader (FullScreen)
        const char* g_QuadVertexShaderSource = R"(
struct VSInput
{
    float3 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.Position = float4(input.Position.xy, 0.0f, 1.0f);
    output.TexCoord = input.TexCoord;
    return output;
}
)";

        // G-Buffer Pixel Shader (인라인)
        const char* g_GBufferPixelShaderSource = R"(
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

struct VertexOut
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
    float3 TangentW : TEXCOORD3;
    float3 BitanW   : TEXCOORD4;
};

struct GBufferOut
{
    float4 PositionWS : SV_Target0;
    float4 NormalWS   : SV_Target1;
    float4 Metalness  : SV_Target2;
    float4 Roughness  : SV_Target3;
    float4 BaseColor  : SV_Target4;
};

Texture2D  g_DiffuseMap : register(t0);
Texture2D  g_NormalMap  : register(t1);
SamplerState g_Sam : register(s0);

GBufferOut main(VertexOut pIn)
{
    GBufferOut gOut;
    
    float4 textureColor = float4(1,1,1,1);
    if (gUseTexture != 0)
    {
        textureColor = g_DiffuseMap.Sample(g_Sam, pIn.TexCoord);
    }
    
    float alphaTex = textureColor.a * gMaterialColor.a;
    clip(alphaTex - 0.1f);
    
    float3 baseColor = gMaterialColor.rgb;
    if (gUseTexture != 0)
    {
        baseColor *= textureColor.rgb;
    }
    
    float3 N = normalize(pIn.Normal);
    if (gEnableNormalMap != 0)
    {
        float3 T = normalize(pIn.TangentW);
        float3 B = normalize(pIn.BitanW);
        float handed = dot(cross(T, B), N);
        if (handed < 0.0f) B = -B;
        float3x3 TBN = float3x3(T, B, N);
        float3 N_ts = g_NormalMap.Sample(g_Sam, pIn.TexCoord).xyz * 2.0f - 1.0f;
        N_ts.y = -N_ts.y;
        N_ts = normalize(N_ts);
        N = normalize(mul(N_ts, TBN));
    }
    
    float metalness = saturate(gMetalness);
    float roughness = saturate(gRoughness);
    
    gOut.PositionWS = float4(pIn.WorldPos, 1.0f);
    gOut.NormalWS   = float4(N, 1.0f);
    gOut.Metalness  = float4(metalness, 0, 0, 1);
    gOut.Roughness  = float4(roughness, 0, 0, 1);
    gOut.BaseColor  = float4(baseColor, 1.0f);
    
    return gOut;
}
)";

        // Deferred Light Pixel Shader (인라인)
        const char* g_DeferredLightPixelShaderSource = R"(
// PBR 헬퍼 함수들
static const float PI = 3.14159265f;
static const float INV_PI = 0.31830988618f;

float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = max(NdotH * NdotH * (a2 - 1.0f) + 1.0f, 1e-4f);
    return a2 / (PI * denom * denom);
}

float GeometrySchlickGGX(float NdotX, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) * 0.125f;
    return NdotX / (NdotX * (1.0f - k) + k);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    float gv = GeometrySchlickGGX(NdotV, roughness);
    float gl = GeometrySchlickGGX(NdotL, roughness);
    return gv * gl;
}

float3 FresnelSchlick(float3 F0, float cosTheta)
{
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}

// 그림자 계산 함수
float CalcShadowFactorDeferred(float3 posW)
{
    // 간단한 그림자 계산 (실제 구현은 더 복잡)
    return 1.0f;
}

// 구조체 정의
struct PS_INPUT_QUAD
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// G-Buffer 텍스처
Texture2D g_PositionWS : register(t0);
Texture2D g_NormalWS : register(t1);
Texture2D g_Metalness : register(t2);
Texture2D g_Roughness : register(t3);
Texture2D g_BaseColor : register(t4);
TextureCube g_IBL_Diffuse : register(t5);
TextureCube g_IBL_Specular : register(t6);
Texture2D   g_IBL_BRDF_LUT : register(t7);
Texture2D g_ShadowMap : register(t8);

SamplerState g_Sam : register(s0);
SamplerState g_ShadowSamp : register(s1);
SamplerState g_SamplerLinear : register(s2);

// 상수 버퍼
cbuffer ConstantBuffer : register(b0)
{
    float4x4 g_World;
    float4x4 g_View;
    float4x4 g_Proj;
    float4x4 g_WorldInvTranspose;
    float4 g_Material_ambient;
    float4 g_Material_diffuse;
    float4 g_Material_specular;
    float4 g_Material_reflect;
    float4 g_DirLight_ambient;
    float4 g_DirLight_diffuse;
    float4 g_DirLight_specular;
    float3 g_DirLight_direction;
    float  g_DirLight_intensity;
    float3 g_EyePosW;
    int    g_ShadingMode;
    int    g_EnableNormalMap;
    int    g_UseSpecularMap;
    int    g_UseDiffuseMap;
    float  g_Pad;
    int    g_UseTextureColor;
    float3 g_PBRPad;
    float4 g_PBRBaseColor;
    float  g_PBRMetalness;
    float  g_PBRRoughness;
    float  g_PBRAmbientOcclusion;
    float  g_PBRPad2;
    float  g_OutlineWidth;
    float  g_OutlinePow;
    float  g_OutlineThickness;
    float  g_OutlineStrength;
    float4 g_OutlineColor;
    float4x4 g_LightViewProj;
    float  g_ShadowBias;
    float  g_ShadowMapSize;
    float  g_ShadowPCFRadius;
    int    g_ShadowEnabled;
    int    g_BoundsBoneIndex;
    float3 g_BoundsPad;
};

cbuffer DirectionalLightBuffer : register(b3)
{
    float4 g_LightDirection;
    float4 g_LightColor;
    float g_intensity;
    float g_pad[3];
};

float4 main(PS_INPUT_QUAD pIn) : SV_Target
{
    // G-Buffer 가져오기
    float4 positionWS = g_PositionWS.Sample(g_Sam, pIn.uv);
    float4 normalWS_packed = g_NormalWS.Sample(g_Sam, pIn.uv);
    float4 metalness_packed = g_Metalness.Sample(g_Sam, pIn.uv);
    float4 roughness_packed = g_Roughness.Sample(g_Sam, pIn.uv);
    float4 baseColor = g_BaseColor.Sample(g_Sam, pIn.uv);
    
    // 배경 체크
    if (length(normalWS_packed.xyz) < 0.1f) discard;

    // 데이터 복원
    float3 posW = positionWS.xyz;
    float3 N = normalize(normalWS_packed.xyz);
    float metalness = metalness_packed.r;
    float roughness = max(roughness_packed.r, 0.04f);
    float3 albedo = baseColor.rgb;
    float3 albedoLinear = pow(max(albedo, 0.0f), 2.2f);
    
    // 라이팅 벡터 계산
    float3 L = normalize(-g_LightDirection.xyz);
    float3 V = normalize(g_EyePosW - posW);
    float3 H = normalize(L + V);
    
    float NdotL = dot(N, L);
    float theta = saturate(NdotL);
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));
    
    // PBR 연산
    float3 albedoPBR = albedoLinear;
    roughness = max(roughness, 0.04f);
    float ao = saturate(g_PBRAmbientOcclusion);
    
    // Direct Light
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedoPBR, metalness);
    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySmith(NdotV, theta, roughness);
    float3 F = FresnelSchlick(F0, VdotH);
    
    float3 numerator = D * G * F;
    float denomSpec = max(4.0f * NdotV * theta, 1e-4f);
    float3 specular = numerator / denomSpec;
    
    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metalness);
    float3 diffuse = kD * albedoPBR * INV_PI;
    
    float shadowVis = CalcShadowFactorDeferred(posW);
    float3 radiance = g_LightColor.rgb * PI;
    float3 directLighting = (diffuse + specular) * radiance * theta * ao * shadowVis * g_intensity;
    
    // Indirect Light (IBL)
    float3 diffuseIBL = kD * g_IBL_Diffuse.Sample(g_Sam, N).rgb * albedoPBR;
    
    float3 Renv = reflect(-V, N);
    const float kMaxSpecularMip = 8.0f;
    float3 prefilteredColor = g_IBL_Specular.SampleLevel(g_Sam, Renv, roughness * kMaxSpecularMip).rgb;
    float2 specBRDF = g_IBL_BRDF_LUT.Sample(g_ShadowSamp, float2(NdotV, roughness)).rg;
    float3 specularIBL = prefilteredColor * (F0 * specBRDF.x + specBRDF.y);
    
    float3 iblColor = (diffuseIBL + specularIBL) * ao;
    
    // 최종 색상
    float3 color = directLighting + iblColor;
    
    return float4(color, 1.0f);
}
)";

        // Skybox Vertex Shader (인라인)
        const char* g_SkyboxVertexShaderSource = R"(
cbuffer CBSkybox : register(b0)
{
    float4x4 gWorldViewProj;
};

struct SkyBoxVertexPos
{
    float3 posL : POSITION;
};

struct SkyBoxVertexPosHL
{
    float4 posH : SV_POSITION;
    float3 posL : POSITION;
};

SkyBoxVertexPosHL VS(SkyBoxVertexPos vIn)
{
    SkyBoxVertexPosHL vOut;
    float4 posH = mul(float4(vIn.posL, 1.0f), gWorldViewProj);
    vOut.posH = posH.xyww;
    vOut.posL = vIn.posL;
    return vOut;
}
)";

        // Skybox Pixel Shader (인라인)
        const char* g_SkyboxPixelShaderSource = R"(
TextureCube g_TexCube : register(t0);
SamplerState g_Sam : register(s0);

struct SkyBoxVertexPosHL
{
    float4 posH : SV_POSITION;
    float3 posL : POSITION;
};

float4 PS(SkyBoxVertexPosHL pIn) : SV_Target
{
    return g_TexCube.Sample(g_Sam, pIn.posL);
}
)";

        // Tone Mapping Pixel Shader (인라인) - 예제 프로젝트 36_ToneMappingPS_LDR.hlsl 참고
        const char* g_ToneMappingPixelShaderSource = R"(
Texture2D g_SceneHDR : register(t0);
SamplerState g_SamplerLinear : register(s0);

cbuffer PostProcessConstantBuffer : register(b2)
{
    float g_Exposure;
    float g_MaxHDRNits;
    float2 g_Padding;
};

struct PS_INPUT_QUAD
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// ACES Filmic Tone Mapping (예제 프로젝트와 동일)
float3 ACESFilm(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate(x * (a * x + b) / (x * (c * x + d) + e));
}

// Linear to sRGB (Gamma Correction) - 예제 프로젝트와 동일
float3 LinearToSRGB(float3 linearColor)
{
    return pow(linearColor, 1.0f / 2.2f);
}

float4 main(PS_INPUT_QUAD input) : SV_Target
{
    // 예제 프로젝트 36_ToneMappingPS_LDR.hlsl와 동일한 로직
    // 1. 선형 HDR 값 로드 (Nits 값으로 간주)
    float3 C_linear709 = g_SceneHDR.Sample(g_SamplerLinear, input.uv).rgb;
    
    // 2. Exposure 적용
    float exposureFactor = pow(2.0f, g_Exposure);
    C_linear709 *= exposureFactor;
    
    // 3. ACES 톤매핑 (HDR -> SDR 변환)
    float3 C_tonemapped = ACESFilm(C_linear709);
    
    // 4. 감마 보정 (Linear -> sRGB)
    float3 C_final = LinearToSRGB(C_tonemapped);
    
    return float4(C_final, 1.0f);
}
)";
    }

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

        D3D11_TEXTURE2D_DESC dDesc = { width, height, 1, 1, DXGI_FORMAT_D24_UNORM_S8_UINT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL, 0, 0 };
        if (FAILED(m_device->CreateTexture2D(&dDesc, nullptr, m_sceneDepthTex.ReleaseAndGetAddressOf()))) return false;
        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = { dDesc.Format, D3D11_DSV_DIMENSION_TEXTURE2D, 0 };
        if (FAILED(m_device->CreateDepthStencilView(m_sceneDepthTex.Get(), &dsvDesc, m_sceneDSV.ReleaseAndGetAddressOf()))) return false;

        // IBL 리소스 생성
        if (!CreateIblResources())
        {
            ALICE_LOG_WARN("DeferredRenderSystem::Initialize: CreateIblResources failed (optional).");
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
        DXGI_FORMAT formats[GBufferCount] = {
            DXGI_FORMAT_R16G16B16A16_FLOAT,  // 0: PositionWS
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
        if (FAILED(D3DCompile(g_GBufferVertexShaderSource, strlen(g_GBufferVertexShaderSource), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), errorBlob.GetAddressOf())))
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
        if (FAILED(D3DCompile(g_GBufferSkinnedVertexShaderSource, strlen(g_GBufferSkinnedVertexShaderSource), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), errorBlob.GetAddressOf())))
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
            {"BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT,    0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0}
        };
        if (FAILED(m_device->CreateInputLayout(skinnedLayout, ARRAYSIZE(skinnedLayout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_gBufferSkinnedInputLayout.ReleaseAndGetAddressOf())))
            return false;

        // Quad Vertex Shader
        vsBlob.Reset();
        if (FAILED(D3DCompile(g_QuadVertexShaderSource, strlen(g_QuadVertexShaderSource), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), errorBlob.GetAddressOf())))
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
        if (FAILED(D3DCompile(g_GBufferPixelShaderSource, strlen(g_GBufferPixelShaderSource), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
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
        if (FAILED(D3DCompile(g_DeferredLightPixelShaderSource, strlen(g_DeferredLightPixelShaderSource), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
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
        if (FAILED(D3DCompile(g_TransparentSkinnedVertexShaderSource,
                              strlen(g_TransparentSkinnedVertexShaderSource),
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
                {"BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT,    0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0}
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
        if (FAILED(D3DCompile(g_TransparentPixelShaderSource,
                              strlen(g_TransparentPixelShaderSource),
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
        if (FAILED(D3DCompile(g_SkyboxVertexShaderSource, strlen(g_SkyboxVertexShaderSource), nullptr, nullptr, nullptr, "VS", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), errorBlob.GetAddressOf())))
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
        if (FAILED(D3DCompile(g_SkyboxPixelShaderSource, strlen(g_SkyboxPixelShaderSource), nullptr, nullptr, nullptr, "PS", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
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

        // 톤매핑 Pixel Shader 컴파일
        psBlob.Reset();
        errorBlob.Reset();
        if (FAILED(D3DCompile(g_ToneMappingPixelShaderSource, strlen(g_ToneMappingPixelShaderSource), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
        {
            if (errorBlob)
            {
                ALICE_LOG_ERRORF("Tone Mapping PS compile error: %s", (char*)errorBlob->GetBufferPointer());
            }
            return false;
        }
        if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_toneMappingPS.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Tone Mapping PS");
            return false;
        }

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
        // PerObject CB (Forward와 동일한 구조: 행렬 + 재질 정보)
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth = sizeof(DirectX::XMMATRIX) * 3 + sizeof(DirectX::XMFLOAT4) + sizeof(float) * 2 + sizeof(int) * 2; // world, view, proj, color, rough, metal, useTex, enableNorm
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

        // Bones CB
        cbDesc.ByteWidth = sizeof(DirectX::XMMATRIX) * 1023 + sizeof(std::uint32_t) * 4;
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_cbBones.ReleaseAndGetAddressOf())))
            return false;

        // PostProcess CB
        cbDesc.ByteWidth = sizeof(float) * 4; // exposure, maxHDRNits, padding
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_cbPostProcess.ReleaseAndGetAddressOf())))
            return false;

        // Transparent Forward-Style Light CB (register(b1))
        // float3 dir + float intensity + float3 color + pad + float3 camPos + pad = 48 bytes (16B 정렬)
        cbDesc.ByteWidth = sizeof(float) * 12;
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_cbTransparentLight.ReleaseAndGetAddressOf())))
            return false;

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

    void DeferredRenderSystem::Render(const World& world,
                                      const Camera& camera,
                                      EntityId entity,
                                      const std::unordered_set<EntityId>& cameraEntities,
                                      int shadingMode,
                                      bool enableFillLight,
                                      const std::vector<ForwardRenderSystem::SkinnedDrawCommand>& skinnedCommands)
    {
        if (!m_device || !m_context) return;

        // Viewport 설정
        D3D11_VIEWPORT vp{};
        vp.Width = (float)m_sceneWidth; vp.Height = (float)m_sceneHeight; vp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &vp);

        // G-Buffer 패스
        PassGBuffer(world, camera, skinnedCommands, cameraEntities);

        // Deferred Light 패스
        PassDeferredLight(camera, shadingMode, enableFillLight);

        // 스카이박스 렌더링
        if (m_skyboxEnabled)
        {
            RenderSkybox(camera);
        }

        // 반투명(알파 블렌딩) 오브젝트는 라이트 패스 이후 Forward-Style로 합성
        PassTransparentForward(camera, skinnedCommands);

        // 에디터 뷰포트 표시용 LDR 텍스처로 톤매핑 (ImGui::Image에서 사용)
        if (m_viewportRTV)
        {
            D3D11_VIEWPORT viewport = {};
            viewport.Width = static_cast<float>(m_sceneWidth);
            viewport.Height = static_cast<float>(m_sceneHeight);
            viewport.MaxDepth = 1.0f;
            RenderToneMapping(m_viewportRTV.Get(), viewport);
        }

        // 최종 백버퍼 복귀 (ImGui 등 UI 렌더링을 위해)
        RestoreBackBuffer();
    }

    void DeferredRenderSystem::PassGBuffer(const World& world,
                                           const Camera& camera,
                                           const std::vector<ForwardRenderSystem::SkinnedDrawCommand>& skinnedCommands,
                                           const std::unordered_set<EntityId>& cameraEntities)
    {
        // G-Buffer 클리어
        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        float clearNormal[4] = { 0.5f, 0.5f, 0.5f, 1.0f };

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

        // 상수 버퍼 업데이트
        XMMATRIX view = camera.GetViewMatrix();
        XMMATRIX proj = camera.GetProjectionMatrix();

        // 1. 정적 메시 (큐브) 렌더링
        // ForwardRenderSystem::SimpleVertex와 동일한 구조체 (private이므로 로컬 정의)
        struct SimpleVertex
        {
            XMFLOAT3 Position;
            XMFLOAT3 Normal;
            XMFLOAT2 TexCoord;
        };
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

            XMMATRIX worldM = BuildWorldMatrix(transform);
            
            // 재질 정보 가져오기
            XMFLOAT4 color = { 1, 1, 1, 1 };
            float rough = 0.5f, metal = 0.0f;
            bool useTex = false;
            ID3D11ShaderResourceView* texSRV = nullptr;
            
            // MaterialComponent가 있으면 값 적용
            if (const MaterialComponent* mat = world.GetComponent<MaterialComponent>(id)) {
                color = { mat->color.x, mat->color.y, mat->color.z, 1.0f };
                rough = mat->roughness; 
                metal = mat->metalness;
                if (!mat->albedoTexturePath.empty()) {
                    texSRV = GetOrCreateTexture(mat->albedoTexturePath);
                    useTex = (texSRV != nullptr);
                }
            }

            // 텍스처 바인딩 (t0: Diffuse, t1: Normal)
            ID3D11ShaderResourceView* srvs[] = { texSRV, nullptr }; // 정적 메시는 노말맵 현재 null
            m_context->PSSetShaderResources(0, 2, srvs);

            // CB 업데이트 (재질 정보 포함)
            UpdatePerObjectCB(worldM, view, proj, color, rough, metal, useTex, false);

            m_context->DrawIndexed(m_cubeIndexCount, 0, 0);
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

                        UpdatePerObjectCB(cmd.world, view, proj, color,
                                          cmd.roughness, cmd.metalness,
                                          (diff != nullptr), (norm != nullptr));

                        m_context->DrawIndexed(sub.indexCount, sub.startIndex, cmd.baseVertex);
                    }
                }
                else
                {
                    // 오버라이드 텍스처 (또는 단일 텍스처)만 있는 경우
                    ID3D11ShaderResourceView* diff = GetOrCreateTexture(cmd.albedoTexturePath);
                    ID3D11ShaderResourceView* srvs[] = { diff, nullptr };
                    m_context->PSSetShaderResources(0, 2, srvs);

                    UpdatePerObjectCB(cmd.world, view, proj, color,
                                      cmd.roughness, cmd.metalness,
                                      (diff != nullptr), false);

                    m_context->DrawIndexed(cmd.indexCount, cmd.startIndex, cmd.baseVertex);
                }
            }
        }

        // RTV 해제
        ID3D11RenderTargetView* nullRTVs[GBufferCount] = { nullptr };
        m_context->OMSetRenderTargets(GBufferCount, nullRTVs, nullptr);
    }

    void DeferredRenderSystem::PassDeferredLight(const Camera& camera, int shadingMode, bool enableFillLight)
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

        // 상수 버퍼 업데이트
        UpdateLightingCB(camera, shadingMode, enableFillLight);

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
        const std::vector<ForwardRenderSystem::SkinnedDrawCommand>& skinnedCommands)
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
        tl.lightDir = DirectX::XMFLOAT3(0.5f, -1.0f, 0.5f);
        tl.intensity = 1.0f;
        tl.lightColor = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
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
            UpdatePerObjectCB(cmd.world, view, proj, color, cmd.roughness, cmd.metalness, true, true);

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
                    UpdatePerObjectCB(cmd.world, view, proj, color, cmd.roughness, cmd.metalness, (diff != nullptr), (norm != nullptr));

                    m_context->DrawIndexed(sub.indexCount, sub.startIndex, cmd.baseVertex);
                }
            }
            else
            {
                // 머티리얼 오버라이드(에디터) 경로가 있으면 그걸 사용
                ID3D11ShaderResourceView* diff = GetOrCreateTexture(cmd.albedoTexturePath);
                ID3D11ShaderResourceView* srvs01[2] = { diff, nullptr };
                m_context->PSSetShaderResources(0, 2, srvs01);
                UpdatePerObjectCB(cmd.world, view, proj, color, cmd.roughness, cmd.metalness, (diff != nullptr), false);
                m_context->DrawIndexed(cmd.indexCount, cmd.startIndex, cmd.baseVertex);
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
                                                  bool enableNormalMap)
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
            m_context->Unmap(m_cbPerObject.Get(), 0);
        }

        m_context->VSSetConstantBuffers(0, 1, m_cbPerObject.GetAddressOf());
        // PS에서도 재질 정보를 사용하므로 반드시 바인딩
        m_context->PSSetConstantBuffers(0, 1, m_cbPerObject.GetAddressOf());
    }

    void DeferredRenderSystem::UpdateLightingCB(const Camera& camera, int shadingMode, bool enableFillLight)
    {
        // ConstantBuffer (register b0) 업데이트
        // HLSL의 ConstantBuffer 구조체와 일치해야 함
        struct ConstantBufferData
        {
            XMMATRIX g_World;
            XMMATRIX g_View;
            XMMATRIX g_Proj;
            XMMATRIX g_WorldInvTranspose;
            XMFLOAT4 g_Material_ambient;
            XMFLOAT4 g_Material_diffuse;
            XMFLOAT4 g_Material_specular;
            XMFLOAT4 g_Material_reflect;
            XMFLOAT4 g_DirLight_ambient;
            XMFLOAT4 g_DirLight_diffuse;
            XMFLOAT4 g_DirLight_specular;
            XMFLOAT3 g_DirLight_direction;
            float    g_DirLight_intensity;
            XMFLOAT3 g_EyePosW;
            int      g_ShadingMode;
            int      g_EnableNormalMap;
            int      g_UseSpecularMap;
            int      g_UseDiffuseMap;
            float    g_Pad;
            int      g_UseTextureColor;
            XMFLOAT3 g_PBRPad;
            XMFLOAT4 g_PBRBaseColor;
            float    g_PBRMetalness;
            float    g_PBRRoughness;
            float    g_PBRAmbientOcclusion;
            float    g_PBRPad2;
            float    g_OutlineWidth;
            float    g_OutlinePow;
            float    g_OutlineThickness;
            float    g_OutlineStrength;
            XMFLOAT4 g_OutlineColor;
            XMMATRIX g_LightViewProj;
            float    g_ShadowBias;
            float    g_ShadowMapSize;
            float    g_ShadowPCFRadius;
            int      g_ShadowEnabled;
            int      g_BoundsBoneIndex;
            XMFLOAT3 g_BoundsPad;
        };

        ConstantBufferData cbData = {};
        
        // 행렬은 Identity로 설정 (Deferred Light 패스에서는 사용하지 않지만 구조체에 필요)
        cbData.g_World = XMMatrixIdentity();
        cbData.g_View = XMMatrixIdentity();
        cbData.g_Proj = XMMatrixIdentity();
        cbData.g_WorldInvTranspose = XMMatrixIdentity();
        cbData.g_LightViewProj = XMMatrixIdentity();
        
        // 머티리얼 기본값
        cbData.g_Material_ambient = XMFLOAT4(0.1f, 0.1f, 0.1f, 1.0f);
        cbData.g_Material_diffuse = XMFLOAT4(0.7f, 0.7f, 0.7f, 1.0f);
        cbData.g_Material_specular = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        cbData.g_Material_reflect = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
        
        // 라이트 기본값
        cbData.g_DirLight_ambient = XMFLOAT4(0.1f, 0.1f, 0.1f, 1.0f);
        cbData.g_DirLight_diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        cbData.g_DirLight_specular = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        cbData.g_DirLight_direction = XMFLOAT3(0.5f, -1.0f, 0.5f);
        cbData.g_DirLight_intensity = 1.0f;
        
        // 카메라 위치
        XMFLOAT3 eyePos = camera.GetPosition();
        cbData.g_EyePosW = eyePos;
        
        // 셰이딩 모드
        cbData.g_ShadingMode = shadingMode;
        cbData.g_EnableNormalMap = 0;
        cbData.g_UseSpecularMap = 0;
        cbData.g_UseDiffuseMap = 0;
        cbData.g_UseTextureColor = 1;
        
        // PBR 파라미터
        cbData.g_PBRBaseColor = XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
        cbData.g_PBRMetalness = 0.0f;
        cbData.g_PBRRoughness = 0.5f;
        cbData.g_PBRAmbientOcclusion = 1.0f;
        
        // 아웃라인 (사용 안 함)
        cbData.g_OutlineWidth = 0.0f;
        cbData.g_OutlinePow = 0.0f;
        cbData.g_OutlineThickness = 0.0f;
        cbData.g_OutlineStrength = 0.0f;
        cbData.g_OutlineColor = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
        
        // 섀도우 (사용 안 함)
        cbData.g_ShadowBias = 0.0015f;
        cbData.g_ShadowMapSize = 2048.0f;
        cbData.g_ShadowPCFRadius = 1.0f;
        cbData.g_ShadowEnabled = 0;
        cbData.g_BoundsBoneIndex = -1;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(m_context->Map(m_cbLighting.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            memcpy(mapped.pData, &cbData, sizeof(ConstantBufferData));
            m_context->Unmap(m_cbLighting.Get(), 0);
        }

        // ConstantBuffer 바인딩 (register b0)
        m_context->PSSetConstantBuffers(0, 1, m_cbLighting.GetAddressOf());

        // Directional Light CB 업데이트 (b3)
        struct DirectionalLightData
        {
            XMFLOAT4 direction;
            XMFLOAT4 color;
            float intensity;
            float pad[3];
        };

        DirectionalLightData lightData = {};
        lightData.direction = XMFLOAT4(0.5f, -1.0f, 0.5f, 0.0f);
        lightData.color = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.0f);
        lightData.intensity = 1.0f;

        if (SUCCEEDED(m_context->Map(m_cbDirectionalLight.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            memcpy(mapped.pData, &lightData, sizeof(DirectionalLightData));
            m_context->Unmap(m_cbDirectionalLight.Get(), 0);
        }

        m_context->PSSetConstantBuffers(3, 1, m_cbDirectionalLight.GetAddressOf());
    }

    void DeferredRenderSystem::UpdateBonesCB(const DirectX::XMFLOAT4X4* boneMatrices, std::uint32_t boneCount)
    {
        // ForwardRenderSystem과 동일한 구현
        if (!m_cbBones || !boneMatrices || boneCount == 0) return;

        static constexpr std::uint32_t MaxBones = 1023;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(m_context->Map(m_cbBones.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;

        struct CBBones
        {
            XMMATRIX bones[MaxBones];
            std::uint32_t boneCount;
            std::uint32_t pad[3];
        };

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
        
        return S * R * T;
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

    void DeferredRenderSystem::RenderToneMapping(ID3D11RenderTargetView* targetRTV, const D3D11_VIEWPORT& viewport)
    {
        if (!m_toneMappingPS || !m_quadVS || !m_sceneColorSRV || !targetRTV) return;

        // 뷰포트 설정
        m_context->RSSetViewports(1, &viewport);

        // 렌더 타겟 설정
        m_context->OMSetRenderTargets(1, &targetRTV, nullptr);

        // 상태 정리 (이전 패스의 상태가 남아있으면 후처리가 이상해짐 조심하셈)
        float blendFactor[4] = { 0, 0, 0, 0 };
        m_context->OMSetBlendState(m_ppBlendOpaque.Get(), blendFactor, 0xFFFFFFFF);
        m_context->OMSetDepthStencilState(m_ppDepthOff.Get(), 0);
        m_context->RSSetState(m_ppRasterNoCull.Get());

        // PostProcess 상수 버퍼 업데이트
        struct PostProcessCB
        {
            float exposure;
            float maxHDRNits;
            float padding[2];
        };
        PostProcessCB cbData = {};
        GetPostProcessParams(cbData.exposure, cbData.maxHDRNits);

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(m_context->Map(m_cbPostProcess.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            memcpy(mapped.pData, &cbData, sizeof(PostProcessCB));
            m_context->Unmap(m_cbPostProcess.Get(), 0);
        }

        // 리소스 바인딩
        ID3D11ShaderResourceView* srv = m_sceneColorSRV.Get();
        ID3D11SamplerState* sampler = m_samplerLinear.Get();
        ID3D11Buffer* cb = m_cbPostProcess.Get();

        m_context->PSSetShaderResources(0, 1, &srv);
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
}

