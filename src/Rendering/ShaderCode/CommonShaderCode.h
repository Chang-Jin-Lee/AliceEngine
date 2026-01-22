#pragma once

namespace Alice
{
    /// 포워드/디퍼드 양쪽에서 공통으로 사용되는 셰이더 코드
    class CommonShaderCode
    {
    public:
        // Quad Vertex Shader (FullScreen) - 톤매핑/포스트프로세스용
        inline static const char* QuadVS = R"(
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

        // Skybox Vertex Shader
        inline static const char* SkyboxVS = R"(
cbuffer CBSkybox : register(b0)
{
    float4x4 gWorldViewProj;
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
    o.Direction = input.Position;
    float4 posH = mul(float4(input.Position, 1.0f), gWorldViewProj);
    o.Position = posH.xyww;
    return o;
}
)";

        // Skybox Pixel Shader
        inline static const char* SkyboxPS = R"(
TextureCube g_TexCube : register(t0);
SamplerState g_Sam : register(s0);

struct PSInput
{
    float4 Position : SV_POSITION;
    float3 Direction : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    return g_TexCube.Sample(g_Sam, input.Direction);
}
)";

        // Tone Mapping Pixel Shader (LDR/Standard ACES)
        inline static const char* ToneMappingPS_LDR = R"(
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

// ACES Filmic Tone Mapping
float3 ACESFilm(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate(x * (a * x + b) / (x * (c * x + d) + e));
}

// Linear to sRGB (Gamma Correction)
float3 LinearToSRGB(float3 linearColor)
{
    return pow(max(linearColor, 0.0f), 1.0f / 2.2f);
}

float4 main(PS_INPUT_QUAD input) : SV_Target
{
    float3 C_linear709 = g_SceneHDR.Sample(g_SamplerLinear, input.uv).rgb;
    float exposureFactor = pow(2.0f, g_Exposure);
    C_linear709 *= exposureFactor;
    float3 C_tonemapped = ACESFilm(C_linear709);
    float3 C_final = LinearToSRGB(C_tonemapped);
    return float4(C_final, 1.0f);
}
)";

		// Tone Mapping Pixel Shader - HDR (포워드 전용)
		inline static const char* ToneMappingPS_HDR = R"(
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

// ACES Filmic Tone Mapping
float3 ACESFilm(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate(x * (a * x + b) / (x * (c * x + d) + e));
}

// Rec709 to Rec2020 색공간 변환
float3 Rec709ToRec2020(float3 color)
{
    static const float3x3 conversion =
    {
        0.627402, 0.329292, 0.043306,
        0.069095, 0.919544, 0.011360,
        0.016394, 0.088028, 0.895578
    };
    return mul(conversion, color);
}

// Linear to ST2084 (PQ 인코딩)
float3 LinearToST2084(float3 color)
{
    // g_MaxHDRNits를 반영하여 HDR 스케일링 (10000 nits 기준으로 정규화)
    const float st2084max = 10000.0;
    float hdrScalar = g_MaxHDRNits / st2084max;
    float3 scaledColor = color * hdrScalar;
    
    float m1 = 2610.0 / 4096.0 / 4;
    float m2 = 2523.0 / 4096.0 * 128;
    float c1 = 3424.0 / 4096.0;
    float c2 = 2413.0 / 4096.0 * 32;
    float c3 = 2392.0 / 4096.0 * 32;
    float3 cp = pow(abs(scaledColor), m1);
    return pow((c1 + c2 * cp) / (1 + c3 * cp), m2);
}

float4 main(PS_INPUT_QUAD input) : SV_Target
{
    // 예제 프로젝트 36_ToneMappingPS_HDR.hlsl와 동일한 로직
    float3 C_linear709 = g_SceneHDR.Sample(g_SamplerLinear, input.uv).rgb;
    float3 C_exposure = C_linear709 * pow(2.0f, g_Exposure);
    float3 C_tonemapped = ACESFilm(C_exposure);
    
    // Rec709 → Rec2020 색공간 변환 (LinearToST2084 내부에서 g_MaxHDRNits 처리)
    float3 C_Rec2020 = Rec709ToRec2020(C_tonemapped);
    float3 C_ST2084 = LinearToST2084(C_Rec2020);
    
    // 최종 PQ 인코딩된 값 [0.0, 1.0]을 R10G10B10A2_UNORM 백버퍼에 출력
    return float4(C_ST2084, 1.0);
}
)";

        // Particle Overlay Pixel Shader (Additive blending)
        inline static const char* ParticleOverlayPS = R"(
Texture2D g_ParticleTexture : register(t0);
SamplerState g_SamplerLinear : register(s0);

struct PS_INPUT_QUAD
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PS_INPUT_QUAD input) : SV_TARGET
{
    float4 particleColor = g_ParticleTexture.Sample(g_SamplerLinear, input.uv);
    // Alpha를 곱해서 additive blending 효과
    return float4(particleColor.rgb * particleColor.a, particleColor.a);
}
)";
    };
}
