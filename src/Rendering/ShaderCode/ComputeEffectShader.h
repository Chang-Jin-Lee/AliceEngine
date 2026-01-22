#pragma once

namespace Alice
{
    /// 컴퓨트 셰이더 이펙트 전용 셰이더 코드
    class ComputeEffectShader
    {
    public:
        // 기본 컴퓨트 셰이더
        // - 간단한 이미지 처리 예제 (각 픽셀에 색상 조정)
        inline static const char* BasicCS = R"(
cbuffer CBParams : register(b0)
{
    float4 gParams;      // 이펙트 파라미터
    float4 gTime;        // 시간 정보
    float4 gResolution;  // 해상도 정보 (width, height, 1/width, 1/height)
    float4 gPadding;
};

// 입력 텍스처 (옵션)
Texture2D<float4> gInputTexture : register(t0);
SamplerState gSampler : register(s0);

// 출력 UAV
RWTexture2D<float4> gOutputTexture : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixelCoord = dispatchThreadID.xy;
    
    // 해상도 체크
    if (pixelCoord.x >= (uint)gResolution.x || pixelCoord.y >= (uint)gResolution.y)
        return;

    // UV 좌표 계산
    float2 uv = (pixelCoord + 0.5) * gResolution.zw;
    
    // 기본 색상 (또는 입력 텍스처에서 샘플링)
    float4 color = float4(uv.x, uv.y, 0.5, 1.0);
    
    // 간단한 이펙트 적용 (예: 그라데이션)
    color.rgb *= gParams.rgb;
    color.a *= gParams.a;
    
    // 출력
    gOutputTexture[pixelCoord] = color;
}
)";
    };
}
