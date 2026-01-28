#include "AliceUI/UIShaderCode.h"

namespace Alice
{
	namespace AliceUIShader
	{
		const char* UIVS = R"(
cbuffer UIConstants : register(b0)
{
    float4x4 gViewProj;
};

struct VSInput
{
    float3 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput o;
    o.Position = mul(float4(input.Position, 1.0f), gViewProj);
    o.TexCoord = input.TexCoord;
    o.Color = input.Color;
    return o;
}
)";

		const char* UIPixelPS = R"(
Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

float4 main(PSInput input) : SV_Target
{
    float4 tex = gTexture.Sample(gSampler, input.TexCoord);
    return tex * input.Color;
}
)";

		const char* UIGrayPS = R"(
Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

float4 main(PSInput input) : SV_Target
{
    float4 tex = gTexture.Sample(gSampler, input.TexCoord);
    float lum = dot(tex.rgb, float3(0.299f, 0.587f, 0.114f));
    float4 gray = float4(lum, lum, lum, tex.a);
    return gray * input.Color;
}
)";
	}
}
