#pragma once

// SwordEffectShader header
namespace Alice
{
	class SwordEffectShader {
	public:
		inline static const char* g_SwordEffectVS = R"(
cbuffer CBPerSwordEffect : register(b0)
{
    float4x4 gViewProj;
    float3   gColor;
    float    gCurrentTime;
    float    gFadeDuration;
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
    float4 Color    : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 worldPos = float4(input.Position, 1.0f);
    output.Position = mul(worldPos, gViewProj);
    
    // Age 기반 알파 계산
    float age = gCurrentTime - input.BirthTime;
    float alpha = 1.0f - saturate(age / gFadeDuration);
    
    output.Color = float4(gColor, alpha);
    output.TexCoord = input.TexCoord;
    return output;
}
)";

        inline static const char* g_SwordEffectPS = R"(
struct PSInput
{
    float4 Position : SV_POSITION;
    float4 Color    : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    return input.Color;
}
)";
	};
}
