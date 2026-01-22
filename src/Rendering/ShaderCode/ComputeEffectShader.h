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

        //============================================================
        // GPU 파티클 (2D 오버레이) 예제
        //
        // 리소스 레이아웃(각 셰이더 별)
        //  - ClearCS : u0 = RWTexture2D<float4>
        //  - UpdateCS: u0 = RWStructuredBuffer<Particle>
        //  - DrawCS  : t0 = StructuredBuffer<Particle>, u0 = RWTexture2D<float4>
        //
        // 상수 버퍼(b0)
        //  gParams0 = (emitterX, emitterY, emitterRadius, spawnJitter)
        //  gParams1 = (colorR, colorG, colorB, particleSizePx)
        //  gTime    = (timeSec, dtSec, particleCount, 0)
        //  gResolution = (width, height, 1/width, 1/height)
        //============================================================

        inline static const char* ParticleClearCS = R"(
cbuffer CBParams : register(b0)
{
    float4 params0;
    float4 params1;
    float4 time;
    float4 resolution;
};

RWTexture2D<float4> outTex : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint2 p = id.xy;
    if (p.x >= (uint)resolution.x || p.y >= (uint)resolution.y) return;
    outTex[p] = float4(0,0,0,0);
}
)";

        inline static const char* ParticleUpdateCS = R"(
struct Particle
{
    float2 pos;   // 0..1
    float2 vel;   // 0..1/sec
    float  life;  // sec
    float  seed;
};

cbuffer CBParams : register(b0)
{
    float4 params0;     // emitterX,Y,radius,jitter
    float4 params1;     // colorRGB,sizePx
    float4 time;        // timeSec, dtSec, particleCount, 0
    float4 resolution;  // w,h,invW,invH
};

RWStructuredBuffer<Particle> particles : register(u0);

float Hash11(float n) { return frac(sin(n) * 43758.5453123); }
float2 Hash21(float n)
{
    float x = Hash11(n);
    float y = Hash11(n + 17.0);
    return float2(x, y);
}

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    uint maxCount = (uint)time.z;
    if (i >= maxCount) return;

    Particle p = particles[i];

    float t  = time.x;
    float dt = time.y;

    float2 emitter = params0.xy;
    float radius   = max(params0.z, 0.0001);
    float jitter   = params0.w;

    if (p.life <= 0.0)
    {
        float base = (float)i * 1.2345 + t * 13.37 + p.seed * 101.0;

        float2 r01 = Hash21(base);
        float2 r11 = r01 * 2.0 - 1.0;

        float2 dir = normalize(r11 + 1e-5);
        float  rr  = sqrt(Hash11(base + 91.0)) * radius;

        p.pos = emitter + dir * rr;

        float2 rv = Hash21(base + 191.0) * 2.0 - 1.0;
        p.vel = float2(rv.x * 0.20, abs(rv.y) * 0.45 + 0.15);

        p.life = 0.8 + Hash11(base + 311.0) * 1.6;
        p.seed = frac(p.seed + Hash11(base + 401.0) * (1.0 + jitter));
    }
    else
    {
        float2 gravity = float2(0.0, -0.65);

        p.vel += gravity * dt;
        p.vel *= pow(0.12, dt); // drag
        p.pos += p.vel * dt;
        p.life -= dt;

        // screen bounds bounce
        if (p.pos.x < 0.0) { p.pos.x = 0.0; p.vel.x *= -0.6; }
        if (p.pos.x > 1.0) { p.pos.x = 1.0; p.vel.x *= -0.6; }
        if (p.pos.y < 0.0) { p.pos.y = 0.0; p.vel.y *= -0.6; }
        if (p.pos.y > 1.0) { p.pos.y = 1.0; p.vel.y *= -0.6; }
    }

    particles[i] = p;
}
)";

        inline static const char* ParticleDrawCS = R"(
struct Particle
{
    float2 pos;
    float2 vel;
    float  life;
    float  seed;
};

cbuffer CBParams : register(b0)
{
    float4 params0;
    float4 params1;     // colorRGB,sizePx
    float4 time;        // timeSec, dtSec, particleCount, 0
    float4 resolution;  // w,h,invW,invH
};

StructuredBuffer<Particle> particles : register(t0);
RWTexture2D<float4> outTex : register(u0);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    uint maxCount = (uint)time.z;
    if (i >= maxCount) return;

    Particle p = particles[i];
    if (p.life <= 0.0) return;

    float2 posPx = p.pos * resolution.xy;
    int2 ip = int2(posPx);

    float size = max(params1.w, 1.0);
    int r = (int)clamp(size * 0.5, 1.0, 6.0);

    float3 color = params1.rgb;

    float speed = length(p.vel);
    float intensity = saturate(speed * 1.5) * saturate(p.life);

    for (int y = -r; y <= r; ++y)
    for (int x = -r; x <= r; ++x)
    {
        int2 q = ip + int2(x, y);
        if (q.x < 0 || q.y < 0 || q.x >= (int)resolution.x || q.y >= (int)resolution.y) continue;

        float d2 = (float)(x*x + y*y);
        float w = exp(-d2 / (size * size));

        float a = w * intensity;
        float4 stamp = float4(color * a, a);

        // 원자적 누적이 아니라서 완벽한 additive는 아님(그래도 데모로는 충분히 보임)
        outTex[q] = max(outTex[q], stamp);
    }
}
)";
    };
}
