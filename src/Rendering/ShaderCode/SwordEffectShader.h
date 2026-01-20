#pragma once

// SwordEffectShader header
namespace Alice
{
	class SwordEffectShader {
	public:
		inline static const char* g_SwordEffectVS = R"(
cbuffer CBPerSwordEffectVS : register(b0)
{
    float4x4 gViewProj;
    float4x4 gWorld;    // 게임오브젝트의 월드 행렬
    float2   gUV;       // 기본 UV (선택적)
    float    gCurrentTime;
    float    gFadeDuration;
    float    gWidth;    // 트레일의 기본 폭
};

struct VSInput
{
    float3 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
    float  BirthTime : COLOR0;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD1;
    float2 TexCoord : TEXCOORD0;
    float  Age : TEXCOORD2;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    // 월드 행렬을 사용하여 로컬 위치를 월드 좌표로 변환
    float4 worldPos = mul(float4(input.Position, 1.0f), gWorld);
    output.WorldPos = worldPos.xyz;
    output.Position = mul(worldPos, gViewProj);
    
    // Age 기반 계산
    float age = gCurrentTime - input.BirthTime;
    output.Age = age;
    output.TexCoord = input.TexCoord;
    
    return output;
}
)";

        inline static const char* g_SwordEffectPS = R"(
Texture2D gSwordTexture : register(t20);
SamplerState gSwordSampler : register(s0);

struct PSInput
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD1;
    float2 TexCoord : TEXCOORD0;
    float  Age : TEXCOORD2;
};

cbuffer CBPerSwordEffectPS : register(b1)
{
    float3   gColor;
    float    gFadeDuration;
    float    gWidth;    // 트레일의 기본 폭
    float    padding0;
    float    padding1;
    float    padding2;
};

float4 main(PSInput input) : SV_TARGET
{
    // 텍스처 샘플링
    float4 texColor = gSwordTexture.Sample(gSwordSampler, input.TexCoord);
    
    // Age 기반 알파 계산 (서서히 사라짐)
    float alpha = 1.0f - saturate(input.Age / gFadeDuration);
    
    // Age 기반 폭 감소 (점차 줄어들면서 사라짐)
    float widthFactor = 1.0f - saturate(input.Age / gFadeDuration);
    
    // 최종 색상 (폭 감소를 알파에 반영)
    float finalAlpha = alpha * widthFactor;
    
    return float4(texColor.rgb * gColor, texColor.a * finalAlpha);
}
)";
	};
}
