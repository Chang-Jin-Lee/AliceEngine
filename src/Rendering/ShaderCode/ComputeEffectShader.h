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
    float3 pos;   // 월드 좌표
    float3 vel;   // 월드 좌표 기준 속도
    float  life;  // sec
    float  seed;
};

cbuffer CBParams : register(b0)
{
    float4 params0;     // emitterX,Y,Z,radius
    float4 params1;     // colorRGB,sizePx
    float4 time;        // timeSec, dtSec, particleCount, spawnJitter
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

    float3 emitter = params0.xyz;
    float radius   = max(params0.w, 0.0001);
    float jitter   = time.w;

    if (p.life <= 0.0)
    {
        float base = (float)i * 1.2345 + t * 13.37 + p.seed * 101.0;

        float2 r01 = Hash21(base);
        float3 r11 = float3(r01 * 2.0 - 1.0, Hash11(base + 37.0) * 2.0 - 1.0);

        float3 dir = normalize(r11 + 1e-5);
        float  rr  = sqrt(Hash11(base + 91.0)) * radius;

        p.pos = emitter + dir * rr;

        float3 rv = float3(Hash21(base + 191.0) * 2.0 - 1.0, Hash11(base + 251.0) * 2.0 - 1.0);
        p.vel = normalize(rv + 1e-5) * float3(0.20, 0.45, 0.20);

        p.life = 0.8 + Hash11(base + 311.0) * 1.6;
        p.seed = frac(p.seed + Hash11(base + 401.0) * (1.0 + jitter));
    }
    else
    {
        float3 gravity = float3(0.0, -0.65, 0.0);

        p.vel += gravity * dt;
        p.vel *= pow(0.12, dt); // drag
        p.pos += p.vel * dt;
        p.life -= dt;
    }

    particles[i] = p;
}
)";

        inline static const char* ParticleDrawCS = R"(
struct Particle
{
    float3 pos;
    float3 vel;
    float  life;
    float  seed;
};

cbuffer CBParams : register(b0)
{
    float4 params0;
    float4 params1;     // colorRGB,sizePx
    float4 time;        // timeSec, dtSec, particleCount, spawnJitter
    float4 resolution;  // w,h,invW,invH
    float4x4 viewProj;
    float4 cameraPos;
};

StructuredBuffer<Particle> particles : register(t0);
    Texture2D<float> sceneDepth : register(t1);
    SamplerState LinearSampler : register(s0);  // 실제로는 Point 샘플러 (depth는 Point 샘플링이 정확함)
RWTexture2D<float4> outTex : register(u0);

// sceneDepth가 null일 수 있으므로 체크 필요 (HLSL에서는 포인터 체크 불가, 일단 사용)

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    uint maxCount = (uint)time.z;
    if (i >= maxCount) return;

    Particle p = particles[i];
    if (p.life <= 0.0) return;

    // 월드 좌표 → 클립 공간
    // viewProj는 XMMatrixTranspose로 전달되므로 mul(vector, matrix) 사용 (다른 셰이더와 동일)
    float4 worldPos = float4(p.pos, 1.0);
    float4 clipPos = mul(worldPos, viewProj);
    
    // 클립 공간 → NDC → 스크린 좌표
    if (clipPos.w <= 0.0) return; // 카메라 뒤에 있으면 스킵
    
    float3 ndc = clipPos.xyz / clipPos.w;
    // DirectX 좌표계: NDC Y는 +1(위)~-1(아래), 스크린 Y는 0(위)~height(아래)
    // 따라서 Y축을 뒤집어야 함: screenY = (1.0 - (ndc.y * 0.5 + 0.5)) * height
    float2 screenPos = float2(
        (ndc.x * 0.5 + 0.5) * resolution.x,
        (1.0 - (ndc.y * 0.5 + 0.5)) * resolution.y
    );
    
    // 간단한 depth test (가짜)
    // depth가 0이면 아무것도 렌더링되지 않은 영역이므로 파티클 표시
    float2 depthUV = screenPos * resolution.zw;
    float sceneDepthValue = sceneDepth.SampleLevel(LinearSampler, depthUV, 0).r;
    float particleDepth = ndc.z;
    if (sceneDepthValue > 0.001 && particleDepth > sceneDepthValue + 0.01) return;
    
    int2 ip = int2(screenPos);

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


        //============================================================
        // 추가 파티클 프리셋들
        //  - ClearCS는 ParticleClearCS 재사용
        //  - Update/Draw만 타입별로 다르게 구성
        //============================================================

        // --------------------------
        // Sparks (스파크/불꽃 튐)
        // --------------------------
        inline static const char* SparksUpdateCS = R"(
struct Particle
{
    float3 pos;   // 월드 좌표
    float3 vel;   // 월드 좌표 기준 속도
    float  life;  // sec
    float  seed;
};

cbuffer CBParams : register(b0)
{
    float4 params0;     // emitterX,Y,Z,radius
    float4 params1;     // colorRGB,sizePx
    float4 time;        // timeSec, dtSec, particleCount, spawnJitter
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

    float3 emitter = params0.xyz;
    float radius   = max(params0.w, 0.0001);
    float jitter   = time.w;

    if (p.life <= 0.0)
    {
        float base = (float)i * 1.913 + t * 19.71 + p.seed * 97.0;
        float2 r = Hash21(base);
        float3 r3 = float3(r * 2.0 - 1.0, Hash11(base + 37.0) * 2.0 - 1.0);

        float3 dir = normalize(r3 + 1e-5);
        float rr = sqrt(Hash11(base + 91.0)) * radius;
        p.pos = emitter + dir * rr;

        // 스파크는 빠르고 짧게
        float3 rv = float3(Hash21(base + 131.0) * 2.0 - 1.0, Hash11(base + 251.0) * 2.0 - 1.0);
        float spd = 0.45 + Hash11(base + 171.0) * 0.85;
        p.vel = normalize(rv + 1e-5) * spd;

        p.life = 0.15 + Hash11(base + 211.0) * 0.65;
        p.seed = frac(p.seed + Hash11(base + 401.0) * (1.0 + jitter));
    }
    else
    {
        float3 gravity = float3(0.0, -1.8, 0.0);
        p.vel += gravity * dt;
        p.vel *= pow(0.03, dt); // 강한 드래그
        p.pos += p.vel * dt;
        p.life -= dt;
    }

    particles[i] = p;
}
)";

        inline static const char* SparksDrawCS = R"(
struct Particle
{
    float3 pos;
    float3 vel;
    float  life;
    float  seed;
};

cbuffer CBParams : register(b0)
{
    float4 params0;
    float4 params1;     // colorRGB,sizePx
    float4 time;        // timeSec, dtSec, particleCount, spawnJitter
    float4 resolution;  // w,h,invW,invH
    float4x4 viewProj;
    float4 cameraPos;
};

StructuredBuffer<Particle> particles : register(t0);
    Texture2D<float> sceneDepth : register(t1);
    SamplerState LinearSampler : register(s0);  // 실제로는 Point 샘플러 (depth는 Point 샘플링이 정확함)
RWTexture2D<float4> outTex : register(u0);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    uint maxCount = (uint)time.z;
    if (i >= maxCount) return;

    Particle p = particles[i];
    if (p.life <= 0.0) return;

    // 월드 좌표 → 클립 공간
    // viewProj는 XMMatrixTranspose로 전달되므로 mul(vector, matrix) 사용 (다른 셰이더와 동일)
    float4 worldPos = float4(p.pos, 1.0);
    float4 clipPos = mul(worldPos, viewProj);
    
    if (clipPos.w <= 0.0) return;
    
    float3 ndc = clipPos.xyz / clipPos.w;
    // DirectX 좌표계: NDC Y는 +1(위)~-1(아래), 스크린 Y는 0(위)~height(아래)
    // 따라서 Y축을 뒤집어야 함: screenY = (1.0 - (ndc.y * 0.5 + 0.5)) * height
    float2 screenPos = float2(
        (ndc.x * 0.5 + 0.5) * resolution.x,
        (1.0 - (ndc.y * 0.5 + 0.5)) * resolution.y
    );
    
    // depth test
    float2 depthUV = screenPos * resolution.zw;
    float sceneDepthValue = sceneDepth.SampleLevel(LinearSampler, depthUV, 0).r;
    float particleDepth = ndc.z;
    if (sceneDepthValue > 0.001 && particleDepth > sceneDepthValue + 0.01) return;
    
    // 속도 벡터를 화면 공간으로 변환 (두 점을 투영해서 화면 속도 계산)
    // viewProj로 방향벡터를 직접 변환하면 투영까지 섞여서 카메라 각도에 따라 크기가 변함
    float2 vPx = float2(0.0, 0.0);
    float4 clipPos2 = mul(float4(p.pos + p.vel * 0.03, 1.0), viewProj);
    if (clipPos2.w > 0.0)
    {
        float3 ndc2 = clipPos2.xyz / clipPos2.w;
        float2 screenPos2 = float2(
            (ndc2.x * 0.5 + 0.5) * resolution.x,
            (1.0 - (ndc2.y * 0.5 + 0.5)) * resolution.y
        );
        vPx = screenPos2 - screenPos;
    }

    float speed = max(length(vPx), 1.0);
    float2 dir  = normalize(vPx + 1e-3);

    float baseSize = max(params1.w, 1.0);
    float L = clamp(speed * 0.03 + baseSize * 1.2, 3.0, 22.0);
    float R = clamp(baseSize * 0.35, 1.0, 5.0);

    int2 ip = int2(screenPos);
    int maxR = (int)ceil(max(L, R)) + 1;

    float3 color = params1.rgb;
    float fade = saturate(p.life / 0.8);

    for (int y = -maxR; y <= maxR; ++y)
    for (int x = -maxR; x <= maxR; ++x)
    {
        int2 q = ip + int2(x, y);
        if (q.x < 0 || q.y < 0 || q.x >= (int)resolution.x || q.y >= (int)resolution.y) continue;

        float2 pPx = (float2(q) + 0.5) - screenPos;
        float  along = dot(pPx, dir);
        float2 perpV = pPx - dir * along;
        float  perp = length(perpV);

        // 꼬리: 뒤로 조금, 앞으로 길게
        if (along < -L * 0.25 || along > L) continue;
        if (perp > R * 3.0) continue;

        float wAlong = exp(- (along*along) / (L*L));
        float wPerp  = exp(- (perp*perp) / (R*R));
        float a = wAlong * wPerp * fade;

        float4 stamp = float4(color * a, a);
        outTex[q] = max(outTex[q], stamp);
    }
}
)";

        // --------------------------
        // Smoke (연기)
        // --------------------------
        inline static const char* SmokeUpdateCS = R"(
struct Particle
{
    float3 pos;
    float3 vel;
    float  life;
    float  seed;
};

cbuffer CBParams : register(b0)
{
    float4 params0;     // emitterX,Y,Z,radius
    float4 params1;     // colorRGB,sizePx
    float4 time;        // timeSec, dtSec, particleCount, spawnJitter
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

    float3 emitter = params0.xyz;
    float radius   = max(params0.w, 0.0001);
    float jitter   = time.w;

    // seed 기반 "최대 수명" (드로우에서도 동일 계산 가능)
    float maxLife = 2.0 + frac(p.seed * 19.1) * 2.0;

    if (p.life <= 0.0)
    {
        float base = (float)i * 0.777 + t * 3.1 + p.seed * 113.0;
        float2 r = Hash21(base);
        float3 r3 = float3(r * 2.0 - 1.0, Hash11(base + 33.0) * 2.0 - 1.0);

        float3 dir = normalize(r3 + 1e-5);
        float rr = sqrt(Hash11(base + 33.0)) * radius;
        p.pos = emitter + dir * rr;

        // 천천히 위로
        float3 rv = float3(Hash21(base + 77.0) * 2.0 - 1.0, Hash11(base + 97.0) * 2.0 - 1.0);
        p.vel = float3(rv.x * 0.05, 0.10 + abs(rv.y) * 0.08, rv.z * 0.05);

        p.life = maxLife;
        p.seed = frac(p.seed + Hash11(base + 401.0) * (1.0 + jitter));
    }
    else
    {
        // 약한 난류/소용돌이 느낌
        float a = 6.2831853 * Hash11(p.seed * 31.0 + floor(t * 2.0));
        float3 swirl = float3(cos(a) * 0.02, 0.0, sin(a) * 0.02);

        // 부력 + 드래그
        float3 buoyancy = float3(0.0, 0.08, 0.0);
        p.vel += (buoyancy + swirl) * dt;
        p.vel *= pow(0.35, dt);
        p.pos += p.vel * dt;
        p.life -= dt;
    }

    particles[i] = p;
}
)";

        inline static const char* SmokeDrawCS = R"(
struct Particle
{
    float3 pos;
    float3 vel;
    float  life;
    float  seed;
};

cbuffer CBParams : register(b0)
{
    float4 params0;
    float4 params1;     // colorRGB,sizePx
    float4 time;        // timeSec, dtSec, particleCount, spawnJitter
    float4 resolution;  // w,h,invW,invH
    float4x4 viewProj;
    float4 cameraPos;
};

StructuredBuffer<Particle> particles : register(t0);
    Texture2D<float> sceneDepth : register(t1);
    SamplerState LinearSampler : register(s0);  // 실제로는 Point 샘플러 (depth는 Point 샘플링이 정확함)
RWTexture2D<float4> outTex : register(u0);

float Hash11(float n) { return frac(sin(n) * 43758.5453123); }

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    uint maxCount = (uint)time.z;
    if (i >= maxCount) return;

    Particle p = particles[i];
    if (p.life <= 0.0) return;

    // 월드 좌표 → 클립 공간
    // viewProj는 XMMatrixTranspose로 전달되므로 mul(vector, matrix) 사용 (다른 셰이더와 동일)
    float4 worldPos = float4(p.pos, 1.0);
    float4 clipPos = mul(worldPos, viewProj);
    
    if (clipPos.w <= 0.0) return;
    
    float3 ndc = clipPos.xyz / clipPos.w;
    // DirectX 좌표계: NDC Y는 +1(위)~-1(아래), 스크린 Y는 0(위)~height(아래)
    // 따라서 Y축을 뒤집어야 함: screenY = (1.0 - (ndc.y * 0.5 + 0.5)) * height
    float2 screenPos = float2(
        (ndc.x * 0.5 + 0.5) * resolution.x,
        (1.0 - (ndc.y * 0.5 + 0.5)) * resolution.y
    );
    
    // depth test
    float2 depthUV = screenPos * resolution.zw;
    float sceneDepthValue = sceneDepth.SampleLevel(LinearSampler, depthUV, 0).r;
    float particleDepth = ndc.z;
    if (sceneDepthValue > 0.001 && particleDepth > sceneDepthValue + 0.01) return;
    
    // seed 기반 maxLife(업데이트와 동일)
    float maxLife = 2.0 + frac(p.seed * 19.1) * 2.0;
    float age01 = 1.0 - saturate(p.life / maxLife);

    int2 ip = int2(screenPos);

    float baseSize = max(params1.w, 1.0);
    float size = clamp(baseSize * (0.7 + age01 * 2.2), 2.0, 32.0);
    int r = (int)clamp(size * 0.5, 2.0, 16.0);

    float3 color = params1.rgb;
    float fade = (1.0 - age01) * 0.55; // 연기는 은근해야 함

    for (int y = -r; y <= r; ++y)
    for (int x = -r; x <= r; ++x)
    {
        int2 q = ip + int2(x, y);
        if (q.x < 0 || q.y < 0 || q.x >= (int)resolution.x || q.y >= (int)resolution.y) continue;

        float d2 = (float)(x*x + y*y);
        float w = exp(-d2 / (size * size));

        // 픽셀마다 미세한 알파 노이즈(구름 질감)
        float n = Hash11((float)(q.x * 17 + q.y * 131) + p.seed * 997.0);
        float a = w * fade * (0.75 + 0.35 * n);

        float4 stamp = float4(color * a, a);
        outTex[q] = max(outTex[q], stamp);
    }
}
)";

        // --------------------------
        // Vortex (소용돌이)
        // --------------------------
        inline static const char* VortexUpdateCS = R"(
struct Particle
{
    float3 pos;
    float3 vel;
    float  life;
    float  seed;
};

cbuffer CBParams : register(b0)
{
    float4 params0;     // emitterX,Y,Z,radius
    float4 params1;     // colorRGB,sizePx
    float4 time;        // timeSec, dtSec, particleCount, spawnJitter
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

    float3 emitter = params0.xyz;
    float radius   = max(params0.w, 0.0001);
    float jitter   = time.w;

    if (p.life <= 0.0)
    {
        float base = (float)i * 2.11 + t * 7.7 + p.seed * 111.0;
        float2 r01 = Hash21(base);
        float ang = r01.x * 6.2831853;
        float rr = sqrt(r01.y) * radius;
        float h = (Hash11(base + 19.0) - 0.5) * radius * 0.5;

        p.pos = emitter + float3(cos(ang) * rr, h, sin(ang) * rr);

        // 초기 속도는 약하게
        float3 tang = float3(-sin(ang), 0.0, cos(ang));
        p.vel = tang * (0.05 + Hash11(base + 19.0) * 0.10);

        p.life = 1.5 + Hash11(base + 211.0) * 2.5;
        p.seed = frac(p.seed + Hash11(base + 401.0) * (1.0 + jitter));
    }
    else
    {
        float3 toC = emitter - p.pos;
        float dist = length(toC) + 1e-4;
        float3 dirC = toC / dist;
        float3 tang = cross(dirC, float3(0.0, 1.0, 0.0));
        if (length(tang) < 1e-3) tang = float3(1.0, 0.0, 0.0);
        tang = normalize(tang);

        // 원심 + 접선 가속
        float pull = 0.20 + 0.35 * saturate(dist / radius);
        float spin = 0.65;

        p.vel += (dirC * pull + tang * spin) * dt;
        p.vel *= pow(0.30, dt);
        p.pos += p.vel * dt;
        p.life -= dt;

        // 너무 멀어지면 리셋
        if (dist > radius * 2.5) p.life = 0.0;
    }

    particles[i] = p;
}
)";

        inline static const char* VortexDrawCS = R"(
struct Particle
{
    float3 pos;
    float3 vel;
    float  life;
    float  seed;
};

cbuffer CBParams : register(b0)
{
    float4 params0;
    float4 params1;     // colorRGB,sizePx
    float4 time;        // timeSec, dtSec, particleCount, spawnJitter
    float4 resolution;  // w,h,invW,invH
    float4x4 viewProj;
    float4 cameraPos;
};

StructuredBuffer<Particle> particles : register(t0);
    Texture2D<float> sceneDepth : register(t1);
    SamplerState LinearSampler : register(s0);  // 실제로는 Point 샘플러 (depth는 Point 샘플링이 정확함)
RWTexture2D<float4> outTex : register(u0);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    uint maxCount = (uint)time.z;
    if (i >= maxCount) return;

    Particle p = particles[i];
    if (p.life <= 0.0) return;

    // 월드 좌표 → 클립 공간
    // viewProj는 XMMatrixTranspose로 전달되므로 mul(vector, matrix) 사용 (다른 셰이더와 동일)
    float4 worldPos = float4(p.pos, 1.0);
    float4 clipPos = mul(worldPos, viewProj);
    
    if (clipPos.w <= 0.0) return;
    
    float3 ndc = clipPos.xyz / clipPos.w;
    // DirectX 좌표계: NDC Y는 +1(위)~-1(아래), 스크린 Y는 0(위)~height(아래)
    // 따라서 Y축을 뒤집어야 함: screenY = (1.0 - (ndc.y * 0.5 + 0.5)) * height
    float2 screenPos = float2(
        (ndc.x * 0.5 + 0.5) * resolution.x,
        (1.0 - (ndc.y * 0.5 + 0.5)) * resolution.y
    );
    
    // depth test
    float2 depthUV = screenPos * resolution.zw;
    float sceneDepthValue = sceneDepth.SampleLevel(LinearSampler, depthUV, 0).r;
    float particleDepth = ndc.z;
    if (sceneDepthValue > 0.001 && particleDepth > sceneDepthValue + 0.01) return;
    
    int2 ip = int2(screenPos);

    float size = clamp(max(params1.w, 1.0) * 0.9, 1.0, 10.0);
    int r = (int)clamp(size * 0.5, 1.0, 6.0);

    float3 color = params1.rgb;
    float intensity = saturate(p.life * 0.5);

    for (int y = -r; y <= r; ++y)
    for (int x = -r; x <= r; ++x)
    {
        int2 q = ip + int2(x, y);
        if (q.x < 0 || q.y < 0 || q.x >= (int)resolution.x || q.y >= (int)resolution.y) continue;

        float d2 = (float)(x*x + y*y);
        float w = exp(-d2 / (size * size));

        float a = w * intensity;
        float4 stamp = float4(color * a, a);
        outTex[q] = max(outTex[q], stamp);
    }
}
)";

        // --------------------------
        // Snow (눈/먼지)
        // --------------------------
        inline static const char* SnowUpdateCS = R"(
struct Particle
{
    float3 pos;
    float3 vel;
    float  life;
    float  seed;
};

cbuffer CBParams : register(b0)
{
    float4 params0;     // emitterX,Y,Z,radius (x,y는 무시 가능)
    float4 params1;     // colorRGB,sizePx
    float4 time;        // timeSec, dtSec, particleCount, spawnJitter
    float4 resolution;  // w,h,invW,invH
    float4x4 viewProj;  // DrawCS에서 사용 (UpdateCS에서는 사용 안 함)
    float4 cameraPos;   // Snow는 카메라 위치 필요
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

    float3 emitter = params0.xyz;
    float radius   = max(params0.w, 0.0001);
    
    if (p.life <= 0.0)
    {
        float base = (float)i * 0.91 + t * 0.37 + p.seed * 401.0;
        float2 r = Hash21(base);

        // emitter 위치 위에서 시작 (좌표 고정)
        p.pos = emitter + float3((r.x - 0.5) * radius * 2.0, radius * 0.5 + r.y * radius, (r.y - 0.5) * radius * 2.0);
        p.vel = float3((r.y - 0.5) * 0.05, -(0.05 + r.x * 0.08), 0.0);
        p.life = 4.0 + Hash11(base + 19.0) * 6.0;
        p.seed = frac(p.seed + Hash11(base + 401.0));
    }
    else
    {
        float sway = (Hash11(p.seed * 91.0 + floor(t * 2.0)) - 0.5) * 0.02;
        p.vel.x += sway * dt;
        p.vel.x = clamp(p.vel.x, -0.08, 0.08);

        p.pos += p.vel * dt;
        p.life -= dt;

        // emitter 아래로 떨어지면 리셋
        if (p.pos.y < emitter.y - radius * 0.5) p.life = 0.0;
    }

    particles[i] = p;
}
)";

        inline static const char* SnowDrawCS = R"(
struct Particle
{
    float3 pos;
    float3 vel;
    float  life;
    float  seed;
};

cbuffer CBParams : register(b0)
{
    float4 params0;
    float4 params1;     // colorRGB,sizePx
    float4 time;        // timeSec, dtSec, particleCount, spawnJitter
    float4 resolution;  // w,h,invW,invH
    float4x4 viewProj;
    float4 cameraPos;
};

StructuredBuffer<Particle> particles : register(t0);
    Texture2D<float> sceneDepth : register(t1);
    SamplerState LinearSampler : register(s0);  // 실제로는 Point 샘플러 (depth는 Point 샘플링이 정확함)
RWTexture2D<float4> outTex : register(u0);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    uint maxCount = (uint)time.z;
    if (i >= maxCount) return;

    Particle p = particles[i];
    if (p.life <= 0.0) return;

    // 월드 좌표 → 클립 공간
    // viewProj는 XMMatrixTranspose로 전달되므로 mul(vector, matrix) 사용 (다른 셰이더와 동일)
    float4 worldPos = float4(p.pos, 1.0);
    float4 clipPos = mul(worldPos, viewProj);
    
    if (clipPos.w <= 0.0) return;
    
    float3 ndc = clipPos.xyz / clipPos.w;
    // DirectX 좌표계: NDC Y는 +1(위)~-1(아래), 스크린 Y는 0(위)~height(아래)
    // 따라서 Y축을 뒤집어야 함: screenY = (1.0 - (ndc.y * 0.5 + 0.5)) * height
    float2 screenPos = float2(
        (ndc.x * 0.5 + 0.5) * resolution.x,
        (1.0 - (ndc.y * 0.5 + 0.5)) * resolution.y
    );
    
    // depth test
    float2 depthUV = screenPos * resolution.zw;
    float sceneDepthValue = sceneDepth.SampleLevel(LinearSampler, depthUV, 0).r;
    float particleDepth = ndc.z;
    if (sceneDepthValue > 0.001 && particleDepth > sceneDepthValue + 0.01) return;
    
    int2 ip = int2(screenPos);

    float size = clamp(max(params1.w, 1.0) * 0.6, 1.0, 6.0);
    int r = (int)clamp(size * 0.5, 1.0, 3.0);

    float3 color = params1.rgb;
    float intensity = saturate(p.life / 6.0);

    for (int y = -r; y <= r; ++y)
    for (int x = -r; x <= r; ++x)
    {
        int2 q = ip + int2(x, y);
        if (q.x < 0 || q.y < 0 || q.x >= (int)resolution.x || q.y >= (int)resolution.y) continue;

        float d2 = (float)(x*x + y*y);
        float w = exp(-d2 / (size * size));

        float a = w * intensity;
        float4 stamp = float4(color * a, a);
        outTex[q] = max(outTex[q], stamp);
    }
}
)";

        // --------------------------
        // Explosion (폭발/버스트)
        // --------------------------
        inline static const char* ExplosionUpdateCS = R"(
struct Particle
{
    float3 pos;
    float3 vel;
    float  life;
    float  seed;
};

cbuffer CBParams : register(b0)
{
    float4 params0;     // emitterX,Y,Z,radius
    float4 params1;     // colorRGB,sizePx
    float4 time;        // timeSec, dtSec, particleCount, spawnJitter
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

    float3 emitter = params0.xyz;
    float radius   = max(params0.w, 0.0001);
    float jitter   = time.w;

    if (p.life <= 0.0)
    {
        float base = (float)i * 1.57 + t * 9.0 + p.seed * 181.0;
        float2 r = Hash21(base);
        float3 r3 = float3(r * 2.0 - 1.0, Hash11(base + 31.0) * 2.0 - 1.0);
        float3 dir = normalize(r3 + 1e-5);

        float rr = sqrt(Hash11(base + 31.0)) * radius;
        p.pos = emitter + dir * rr;

        float spd = 0.35 + Hash11(base + 71.0) * 0.95;
        p.vel = dir * spd;

        p.life = 0.35 + Hash11(base + 211.0) * 0.95;
        p.seed = frac(p.seed + Hash11(base + 401.0) * (1.0 + jitter));
    }
    else
    {
        p.vel *= pow(0.22, dt);
        p.pos += p.vel * dt;
        p.life -= dt;
    }

    particles[i] = p;
}
)";

        inline static const char* ExplosionDrawCS = R"(
struct Particle
{
    float3 pos;
    float3 vel;
    float  life;
    float  seed;
};

cbuffer CBParams : register(b0)
{
    float4 params0;
    float4 params1;     // colorRGB,sizePx
    float4 time;        // timeSec, dtSec, particleCount, spawnJitter
    float4 resolution;  // w,h,invW,invH
    float4x4 viewProj;
    float4 cameraPos;
};

StructuredBuffer<Particle> particles : register(t0);
    Texture2D<float> sceneDepth : register(t1);
    SamplerState LinearSampler : register(s0);  // 실제로는 Point 샘플러 (depth는 Point 샘플링이 정확함)
RWTexture2D<float4> outTex : register(u0);

float Hash11(float n) { return frac(sin(n) * 43758.5453123); }

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    uint maxCount = (uint)time.z;
    if (i >= maxCount) return;

    Particle p = particles[i];
    if (p.life <= 0.0) return;

    // 월드 좌표 → 클립 공간
    // viewProj는 XMMatrixTranspose로 전달되므로 mul(vector, matrix) 사용 (다른 셰이더와 동일)
    float4 worldPos = float4(p.pos, 1.0);
    float4 clipPos = mul(worldPos, viewProj);
    
    if (clipPos.w <= 0.0) return;
    
    float3 ndc = clipPos.xyz / clipPos.w;
    // DirectX 좌표계: NDC Y는 +1(위)~-1(아래), 스크린 Y는 0(위)~height(아래)
    // 따라서 Y축을 뒤집어야 함: screenY = (1.0 - (ndc.y * 0.5 + 0.5)) * height
    float2 screenPos = float2(
        (ndc.x * 0.5 + 0.5) * resolution.x,
        (1.0 - (ndc.y * 0.5 + 0.5)) * resolution.y
    );
    
    // depth test
    float2 depthUV = screenPos * resolution.zw;
    float sceneDepthValue = sceneDepth.SampleLevel(LinearSampler, depthUV, 0).r;
    float particleDepth = ndc.z;
    if (sceneDepthValue > 0.001 && particleDepth > sceneDepthValue + 0.01) return;
    
    int2 ip = int2(screenPos);

    float speed = length(p.vel);
    float baseSize = max(params1.w, 1.0);
    float size = clamp(baseSize * (1.0 + speed * 1.5), 2.0, 18.0);
    int r = (int)clamp(size * 0.5, 2.0, 10.0);

    float3 color = params1.rgb;
    float intensity = saturate(p.life * 1.5);

    for (int y = -r; y <= r; ++y)
    for (int x = -r; x <= r; ++x)
    {
        int2 q = ip + int2(x, y);
        if (q.x < 0 || q.y < 0 || q.x >= (int)resolution.x || q.y >= (int)resolution.y) continue;

        float d2 = (float)(x*x + y*y);
        float w = exp(-d2 / (size * size));

        float a = w * intensity;
        float4 stamp = float4(color * a, a);
        outTex[q] = max(outTex[q], stamp);
    }
}
)";
    };
}
