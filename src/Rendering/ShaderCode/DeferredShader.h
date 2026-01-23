#pragma once

namespace Alice
{
    /// 디퍼드 렌더링 전용 셰이더 코드
    class DeferredShader
    {
    public:
        // G-Buffer Vertex Shader
        inline static const char* GBufferVS = R"(
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
    int      gShadingMode;
    int      gPad0;
    
    // [Fixed] HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gPad2; // 4바이트 패딩
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
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
    
    float3 N = normalize(mul(float4(input.Normal, 0.0f), gWorld).xyz);
    
    // 아웃라인: 모든 쉐이딩 모드에서 normal 방향으로 확장 (아웃라인 두께가 0보다 클 때만)
    float3 posOffset = (gOutlineWidth > 0.0f) ? (N * gOutlineWidth) : float3(0, 0, 0);
    
    float4 posW = mul(float4(input.Position + posOffset, 1.0f), gWorld);
    output.Position = mul(mul(posW, gView), gProj);
    output.WorldPos = posW.xyz;
    
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

        // G-Buffer Skinned Vertex Shader
        inline static const char* GBufferSkinnedVS = R"(
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
    int      gShadingMode;
    int      gPad0;
    
    // HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gPad2; // 4바이트 패딩
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
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
    float3 SmoothNormal : SMOOTHNORMAL; // 아웃라인용 스무스 노멀
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
    
    float3 N = normalize(mul(float4(skinnedN, 0.0f), gWorld).xyz);
    
    // 아웃라인: 스무스 노멀 방향으로 확장 (하드 엣지 모델의 아웃라인 끊김 방지)
    // 스무스 노멀도 스키닝 변환을 적용해야 함
    float3 skinnedSmoothN = normalize(mul(input.SmoothNormal, M3));
    float3 smoothN = normalize(mul(float4(skinnedSmoothN, 0.0f), gWorld).xyz);
    float3 posOffset = (gOutlineWidth > 0.0f) ? (smoothN * gOutlineWidth) : float3(0, 0, 0);
    
    float4 posW = mul(float4(skinnedPos.xyz + posOffset, 1.0f), gWorld);
    output.Position = mul(mul(posW, gView), gProj);
    output.WorldPos = posW.xyz;
    
    output.Normal   = N;
    output.TangentW = normalize(mul(float4(skinnedT, 0.0f), gWorld).xyz);
    output.BitanW   = normalize(mul(float4(skinnedB, 0.0f), gWorld).xyz);
    output.TexCoord = input.TexCoord;
    
    return output;
}
)";

        // G-Buffer Pixel Shader
        inline static const char* GBufferPS = R"(
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
    int      gShadingMode;
    int      gPad0;
    
    // HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gPad2; // 4바이트 패딩
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
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
    
    // 아웃라인 패스 감지: Width가 0보다 크면 아웃라인용 드로우콜임
    if (gOutlineWidth > 0.0f)
    {
        // 1. Position: 깊이 버퍼에 써야 하므로 위치는 저장
        gOut.PositionWS = float4(pIn.WorldPos, 1.0f);
        
        // 2. Normal/Roughness/Metalness: 조명 연산 방해 안 되게 더미 값
        gOut.NormalWS   = float4(0.5f, 0.5f, 1.0f, 0.0f); 
        gOut.Metalness  = float4(0.0f, 0.0f, 0.0f, 1.0f);
        gOut.Roughness  = float4(1.0f, 0.0f, 0.0f, 1.0f);
        
        // 3. BaseColor: 아웃라인 색상
        // 4. Alpha (ShadingMode): 1.0f -> 인코딩 시 mode 6 (TextureOnly/Unlit)이 됨
        //    (LightPS에서 1.0은 Unlit으로 처리되어 BaseColor가 그대로 출력됨)
        gOut.BaseColor  = float4(gOutlineColor, 1.0f);
        
        return gOut;
    }
    
    // --- 아래는 기존 원본 물체 렌더링 로직 (변화 없음) ---
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
        // 노말맵 강도 조절: X, Y 성분에만 Strength를 곱하고 정규화
        N_ts.xy *= gNormalStrength;
        N_ts = normalize(N_ts);
        N = normalize(mul(N_ts, TBN));
    }
    
    float metalness = saturate(gMetalness);
    float roughness = saturate(gRoughness);
    
    // Normal을 [0,1] 범위로 인코딩하여 저장 (LightPS에서 디코딩)
    float3 normalEncoded = N * 0.5f + 0.5f;
    
    gOut.PositionWS = float4(pIn.WorldPos, 1.0f);
    gOut.NormalWS   = float4(normalEncoded, 1.0f);
    gOut.Metalness  = float4(metalness, 0, 0, 1);
    gOut.Roughness  = float4(roughness, 0, 0, 1);
    // shadingMode를 [0,1] 범위로 인코딩하여 저장 (0~6 -> 0.0~1.0)
    gOut.BaseColor  = float4(baseColor, saturate((float)gShadingMode / 6.0f));
    
    return gOut;
}
)";

        // Deferred Light Pixel Shader
        inline static const char* LightPS = R"(
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

// ShadowCB (register b4)
cbuffer ShadowCB : register(b4)
{
    float4x4 g_ShadowLightViewProj;
    float    g_ShadowBias2;
    float    g_ShadowMapSize2;
    float    g_ShadowPCFRadius2;
    int      g_ShadowEnabled2;
    float3   g_ShadowPad2;
};

// 그림자 계산 함수 (PCF)
float CalcShadowFactorDeferred(float3 posW, Texture2D<float> shadowMap, SamplerComparisonState shadowSampler)
{
    if (g_ShadowEnabled2 == 0) return 1.0f;

    float4 shadowPos = mul(float4(posW, 1.0f), g_ShadowLightViewProj);
    shadowPos.xyz /= shadowPos.w;

    float2 shadowTex;
    shadowTex.x = shadowPos.x * 0.5f + 0.5f;
    shadowTex.y = -shadowPos.y * 0.5f + 0.5f;
    float depth = shadowPos.z;

    if (shadowTex.x < 0.0f || shadowTex.x > 1.0f || shadowTex.y < 0.0f || shadowTex.y > 1.0f)
        return 1.0f;

    const float2 texelSize = float2(1.0f, 1.0f) / max(g_ShadowMapSize2, 1.0f);
    const float2 pcfStep = max(g_ShadowPCFRadius2, 0.0f) * texelSize;

    float sum = 0.0f;
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2(x, y) * pcfStep;
            sum += shadowMap.SampleCmpLevelZero(shadowSampler, shadowTex + offset, depth - g_ShadowBias2);
        }
    }
    return sum / 9.0f;
}

// 구조체 정의
struct PS_INPUT_QUAD
{
    float4 position : SV_POSITION;
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
Texture2D<float> g_ShadowMap : register(t8);

SamplerState g_Sam : register(s0);
SamplerComparisonState g_ShadowSampler : register(s1);
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

#define MAX_POINT_LIGHTS 16
#define MAX_SPOT_LIGHTS 16
#define MAX_RECT_LIGHTS 16

struct PointLight
{
    float3 position;
    float  range;
    float3 color;
    float  intensity;
};

struct SpotLight
{
    float3 position;
    float  range;
    float3 direction;
    float  innerCos;
    float3 color;
    float  outerCos;
    float  intensity;
    float  pad0;
};

struct RectLight
{
    float3 position;
    float  range;
    float3 direction;
    float  width;
    float3 color;
    float  height;
    float  intensity;
    float  pad0;
};

cbuffer ExtraLightsBuffer : register(b5)
{
    int g_PointLightCount;
    int g_SpotLightCount;
    int g_RectLightCount;
    int g_ExtraPad0;
    PointLight g_PointLights[MAX_POINT_LIGHTS];
    SpotLight  g_SpotLights[MAX_SPOT_LIGHTS];
    RectLight  g_RectLights[MAX_RECT_LIGHTS];
};

float ComputeAttenuation(float dist, float range)
{
    float r = max(range, 0.001f);
    float att = saturate(1.0f - dist / r);
    return att * att;
}

float ComputeSpotFactor(float3 L, float3 lightDir, float innerCos, float outerCos)
{
    float cosTheta = dot(-L, normalize(lightDir));
    float denom = max(innerCos - outerCos, 1e-4f);
    return saturate((cosTheta - outerCos) / denom);
}

float ComputeRectFactor(float3 L, float3 lightDir)
{
    return saturate(dot(-L, normalize(lightDir)));
}

float3 EvaluatePBRLight(float3 N, float3 V, float3 L, float3 albedoPBR, float metalness, float roughness, float3 lightColor)
{
    float3 H = normalize(L + V);
    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedoPBR, metalness);
    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySmith(NdotV, NdotL, roughness);
    float3 F = FresnelSchlick(F0, VdotH);

    float3 numerator = D * G * F;
    float denomSpec = max(4.0f * NdotV * NdotL, 1e-4f);
    float3 specular = numerator / denomSpec;

    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metalness);
    float3 diffuse = kD * albedoPBR * INV_PI;

    return (diffuse + specular) * lightColor * NdotL;
}

float ToonLevel(float n)
{
    if (n > 0.95f) return 1.0f;
    if (n > 0.5f)  return 0.7f;
    if (n > 0.2f)  return 0.4f;
    return 0.1f;
}

void AccumulateLegacy(float3 N, float3 V, float3 L, float3 lightColor, float atten, int mode, float shininess,
                      inout float3 outDiffuse, inout float3 outSpecular)
{
    float NdotL = max(dot(N, L), 0.0f);
    if (mode == 3)
    {
        float level = ToonLevel(NdotL);
        outDiffuse += level * lightColor * atten;
        return;
    }

    outDiffuse += NdotL * lightColor * atten;

    if (mode == 0 || NdotL <= 0.0f)
        return;

    float specTerm = 0.0f;
    if (mode == 2) // Blinn-Phong
    {
        float3 H = normalize(L + V);
        specTerm = pow(max(dot(N, H), 0.0f), shininess);
    }
    else // Phong
    {
        float3 R = reflect(-L, N);
        specTerm = pow(max(dot(R, V), 0.0f), shininess);
    }
    outSpecular += specTerm * lightColor * atten;
}

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
    // Normal을 [0,1]에서 [-1,1]로 디코딩
    float3 N = normalize(normalWS_packed.xyz * 2.0f - 1.0f);
    float metalness = metalness_packed.r;
    float roughness = max(roughness_packed.r, 0.04f);
    float3 albedo = baseColor.rgb;
    float3 albedoLinear = pow(max(albedo, 0.0f), 2.2f);
    
    // shadingMode 디코딩 (0~6 범위)
    int shadingMode = (int)floor(baseColor.a * 6.0f + 0.5f);
    shadingMode = clamp(shadingMode, 0, 6);
    
    // shadingMode == 6: TextureOnly (빛의 영향을 받지 않는 텍스처만 반환)
    if (shadingMode == 6)
    {
        return float4(albedoLinear, 1.0f);
    }

    // 라이팅 벡터 계산
    float3 L = normalize(-g_LightDirection.xyz);
    float3 V = normalize(g_EyePosW - posW);
    
    float NdotV = saturate(dot(N, V));

    const bool usePbr = (shadingMode == 4 || shadingMode == 5);
    const bool toonPbr = (shadingMode == 5);

    float shadowVis = CalcShadowFactorDeferred(posW, g_ShadowMap, g_ShadowSampler);

    if (!usePbr)
    {
        float3 totalDiffuse = float3(0.0f, 0.0f, 0.0f);
        float3 totalSpecular = float3(0.0f, 0.0f, 0.0f);
        float shininess = max(g_Material_specular.a, 1.0f);
        float3 lightColorDir = g_LightColor.rgb * g_intensity;

        AccumulateLegacy(N, V, L, lightColorDir, shadowVis, shadingMode, shininess, totalDiffuse, totalSpecular);

        [loop] for (int i = 0; i < g_PointLightCount; ++i)
        {
            PointLight pl = g_PointLights[i];
            float3 toLight = pl.position - posW;
            float dist = length(toLight);
            float3 Lp = (dist > 0.0001f) ? (toLight / dist) : float3(0, 0, 1);
            float atten = ComputeAttenuation(dist, pl.range);
            float3 lc = pl.color * pl.intensity * atten;
            AccumulateLegacy(N, V, Lp, lc, 1.0f, shadingMode, shininess, totalDiffuse, totalSpecular);
        }

        [loop] for (int i = 0; i < g_SpotLightCount; ++i)
        {
            SpotLight sl = g_SpotLights[i];
            float3 toLight = sl.position - posW;
            float dist = length(toLight);
            float3 Ls = (dist > 0.0001f) ? (toLight / dist) : float3(0, 0, 1);
            float atten = ComputeAttenuation(dist, sl.range);
            float spot = ComputeSpotFactor(Ls, sl.direction, sl.innerCos, sl.outerCos);
            float3 lc = sl.color * sl.intensity * atten * spot;
            AccumulateLegacy(N, V, Ls, lc, 1.0f, shadingMode, shininess, totalDiffuse, totalSpecular);
        }

        [loop] for (int i = 0; i < g_RectLightCount; ++i)
        {
            RectLight rl = g_RectLights[i];
            float3 toLight = rl.position - posW;
            float dist = length(toLight);
            float3 Lr = (dist > 0.0001f) ? (toLight / dist) : float3(0, 0, 1);
            float atten = ComputeAttenuation(dist, rl.range);
            float facing = ComputeRectFactor(Lr, rl.direction);
            float areaScale = max(rl.width * rl.height, 0.01f);
            float3 lc = rl.color * rl.intensity * atten * facing * areaScale;
            AccumulateLegacy(N, V, Lr, lc, 1.0f, shadingMode, shininess, totalDiffuse, totalSpecular);
        }

        float3 ambient = g_DirLight_ambient.rgb * albedoLinear;
        float3 color = ambient + totalDiffuse * albedoLinear + totalSpecular * g_Material_specular.rgb;
        return float4(color, 1.0f);
    }

    // PBR 연산
    float3 albedoPBR = albedoLinear;
    roughness = max(roughness, 0.04f);
    float ao = saturate(g_PBRAmbientOcclusion);

    // IBL 계산을 위해 필요한 F0와 kD를 여기서 미리 계산해야 합니다.
    // --------------------------------------------------------------------------
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedoPBR, metalness);
    float3 kS_IBL = FresnelSchlick(F0, NdotV);
    float3 kD = (1.0f - kS_IBL) * (1.0f - metalness);
    
    // Direct Light (Directional + Extra Lights)
    float3 lightColorDir = g_LightColor.rgb * g_intensity * PI;
    float3 directLighting = 0.0f;
    {
        float3 lit = EvaluatePBRLight(N, V, L, albedoPBR, metalness, roughness, lightColorDir);
        float ndotl = max(dot(N, L), 0.0f);
        if (toonPbr && ndotl > 0.0f)
        {
            float level = ToonLevel(ndotl);
            lit *= level / max(ndotl, 1e-4f);
        }
        directLighting += lit * shadowVis * ao;
    }

    float3 extraLighting = float3(0.0f, 0.0f, 0.0f);

    [loop] for (int i = 0; i < g_PointLightCount; ++i)
    {
        PointLight pl = g_PointLights[i];
        float3 toLight = pl.position - posW;
        float dist = length(toLight);
        float3 Lp = (dist > 0.0001f) ? (toLight / dist) : float3(0, 0, 1);
        float atten = ComputeAttenuation(dist, pl.range);
        float3 lc = pl.color * pl.intensity * atten * PI;
        float3 lit = EvaluatePBRLight(N, V, Lp, albedoPBR, metalness, roughness, lc);
        float ndotl = max(dot(N, Lp), 0.0f);
        if (toonPbr && ndotl > 0.0f)
        {
            float level = ToonLevel(ndotl);
            lit *= level / max(ndotl, 1e-4f);
        }
        extraLighting += lit * ao;
    }

    [loop] for (int i = 0; i < g_SpotLightCount; ++i)
    {
        SpotLight sl = g_SpotLights[i];
        float3 toLight = sl.position - posW;
        float dist = length(toLight);
        float3 Ls = (dist > 0.0001f) ? (toLight / dist) : float3(0, 0, 1);
        float atten = ComputeAttenuation(dist, sl.range);
        float spot = ComputeSpotFactor(Ls, sl.direction, sl.innerCos, sl.outerCos);
        float3 lc = sl.color * sl.intensity * atten * spot * PI;
        float3 lit = EvaluatePBRLight(N, V, Ls, albedoPBR, metalness, roughness, lc);
        float ndotl = max(dot(N, Ls), 0.0f);
        if (toonPbr && ndotl > 0.0f)
        {
            float level = ToonLevel(ndotl);
            lit *= level / max(ndotl, 1e-4f);
        }
        extraLighting += lit * ao;
    }

    [loop] for (int i = 0; i < g_RectLightCount; ++i)
    {
        RectLight rl = g_RectLights[i];
        float3 toLight = rl.position - posW;
        float dist = length(toLight);
        float3 Lr = (dist > 0.0001f) ? (toLight / dist) : float3(0, 0, 1);
        float atten = ComputeAttenuation(dist, rl.range);
        float facing = ComputeRectFactor(Lr, rl.direction);
        float areaScale = max(rl.width * rl.height, 0.01f);
        float3 lc = rl.color * rl.intensity * atten * facing * areaScale * PI;
        float3 lit = EvaluatePBRLight(N, V, Lr, albedoPBR, metalness, roughness, lc);
        float ndotl = max(dot(N, Lr), 0.0f);
        if (toonPbr && ndotl > 0.0f)
        {
            float level = ToonLevel(ndotl);
            lit *= level / max(ndotl, 1e-4f);
        }
        extraLighting += lit * ao;
    }
    
    // Indirect Light (IBL)
    float3 diffuseIBL = kD * g_IBL_Diffuse.Sample(g_Sam, N).rgb * albedoPBR;
    
    float3 Renv = reflect(-V, N);
    const float kMaxSpecularMip = 8.0f;
    float3 prefilteredColor = g_IBL_Specular.SampleLevel(g_Sam, Renv, roughness * kMaxSpecularMip).rgb;
    float2 specBRDF = g_IBL_BRDF_LUT.Sample(g_SamplerLinear, float2(NdotV, roughness)).rg;
    float3 specularIBL = prefilteredColor * (F0 * specBRDF.x + specBRDF.y);
    
    float3 iblColor = (diffuseIBL + specularIBL) * ao;

    // 최종 색상 계산
    float3 color = directLighting + extraLighting + iblColor;
    
    return float4(color, 1.0f);
}
)";

        // Transparent Forward-Style Skinned VS
        inline static const char* TransparentSkinnedVS = R"(
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
    int      gShadingMode;
    int      gPad0;
    
    // HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gPad2; // 4바이트 패딩
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
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
    float3 SmoothNormal : SMOOTHNORMAL; // 아웃라인용 스무스 노멀
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

    float3 N = normalize(mul(float4(skinnedN, 0.0f), gWorld).xyz);
    
    // 아웃라인: 스무스 노멀 방향으로 확장 (하드 엣지 모델의 아웃라인 끊김 방지)
    // 스무스 노멀도 스키닝 변환을 적용해야 함
    float3 skinnedSmoothN = normalize(mul(input.SmoothNormal, M3));
    float3 smoothN = normalize(mul(float4(skinnedSmoothN, 0.0f), gWorld).xyz);
    float3 posOffset = (gOutlineWidth > 0.0f) ? (smoothN * gOutlineWidth) : float3(0, 0, 0);
    
    float4 posW = mul(float4(skinnedPos.xyz + posOffset, 1.0f), gWorld);
    output.Position = mul(mul(posW, gView), gProj);
    output.WorldPos = posW.xyz;

    output.Normal   = N;
    output.TangentW = normalize(mul(float4(skinnedT, 0.0f), gWorld).xyz);
    output.BitanW   = normalize(mul(float4(skinnedB, 0.0f), gWorld).xyz);
    output.TexCoord = input.TexCoord;

    return output;
}
)";

        // Transparent Forward-Style PS
        inline static const char* TransparentPS = R"(
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
    int      gShadingMode;
    int      gPad0;
    
    // HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gPad2; // 4바이트 패딩
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
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
    clip(alphaTex - 0.99f);
    // 거의 불투명은 디퍼드에서 처리하므로 여기서는 제외
    if (alphaTex >= 0.99f) discard;

    float3 baseColor = gMaterialColor.rgb;
    if (gUseTexture != 0)
        baseColor *= tex.rgb;

    // shadingMode == 6: TextureOnly (빛의 영향을 받지 않는 텍스처만 반환)
    if (gShadingMode == 6)
    {
        return float4(baseColor, alphaTex);
    }

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
        // 노말맵 강도 조절: X, Y 성분에만 Strength를 곱하고 정규화
        N_ts.xy *= gNormalStrength;
        N_ts = normalize(N_ts);
        N = normalize(mul(N_ts, TBN));
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

        // Shadow VS (Static)
        inline static const char* ShadowVS = R"(
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
    int      gShadingMode;
    int      gPad0;
    
    // [Fixed] HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gPad2; // 4바이트 패딩
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

struct VSInput
{
    float3 Position : POSITION;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
};

VSOutput main(VSInput input)
{
    VSOutput o;
    float4 posW = mul(float4(input.Position, 1.0f), gWorld);
    o.Position = mul(mul(posW, gView), gProj);
    return o;
}
)";

        // Shadow Skinned VS
        inline static const char* ShadowSkinnedVS = R"(
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
    int      gShadingMode;
    int      gPad0;
    
    // [Fixed] HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gPad2; // 4바이트 패딩
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
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
};

VSOutput main(VSInput input)
{
    VSOutput o;
    
    // 본 인덱스와 가중치를 가져옴
    uint4 bi = input.BoneIndices;
    float4 bw = input.BoneWeights;
    
    // 스키닝 행렬 계산
    matrix M = bw.x * gBones[bi.x]
             + bw.y * gBones[bi.y]
             + bw.z * gBones[bi.z]
             + bw.w * gBones[bi.w];
    
    // 위치 변환 (Local -> Skinned -> World -> View -> Proj)
    float4 posL = float4(input.Position, 1.0f);
    float4 skinnedPos = mul(posL, M);
    float4 posW = mul(skinnedPos, gWorld);
    
    o.Position = mul(mul(posW, gView), gProj);
    
    return o;
}
)";
    };
}
