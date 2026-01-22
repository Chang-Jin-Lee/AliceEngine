#include "ComputeEffectSystem.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <d3dcompiler.h>
#include "Rendering/ShaderCode/ComputeEffectShader.h"
#include "Core/Logger.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace Alice
{
    namespace
    {
        struct CBParams
        {
            XMFLOAT4 params0;     // (emitterX, emitterY, emitterRadius, spawnJitter)
            XMFLOAT4 params1;     // (colorR, colorG, colorB, particleSizePx)
            XMFLOAT4 time;        // (timeSec, dtSec, particleCount, 0)
            XMFLOAT4 resolution;  // (w, h, invW, invH)
        };

        struct ParticleInit
        {
            XMFLOAT2 pos;
            XMFLOAT2 vel;
            float life;
            float seed;
        };

        static float ClampFloat(float v, float lo, float hi)
        {
            return std::max(lo, std::min(hi, v));
        }
    }

    static HRESULT CompileCSBlob(
        const char* source,
        const char* entry,
        const char* target,
        ID3DBlob** outBlob,
        ID3DBlob** outError)
    {
        UINT flags = 0;
#if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

        return D3DCompile(
            source,
            std::strlen(source),
            nullptr,
            nullptr,
            nullptr,
            entry,
            target,
            flags,
            0,
            outBlob,
            outError
        );
    }

    ComputeEffectSystem::ComputeEffectSystem(ID3D11RenderDevice& renderDevice)
        : m_renderDevice(renderDevice)
    {
        m_device  = m_renderDevice.GetDevice();
        m_context = m_renderDevice.GetImmediateContext();

        QueryPerformanceFrequency(&m_qpcFreq);
        QueryPerformanceCounter(&m_qpcPrev);
    }

    ComputeEffectSystem::~ComputeEffectSystem()
    {
    }

    bool ComputeEffectSystem::Initialize(std::uint32_t width, std::uint32_t height)
    {
        ALICE_LOG_INFO("ComputeEffectSystem::Initialize: begin (width=%u, height=%u)", width, height);

        if (!m_device || !m_context)
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::Initialize: invalid device/context.");
            return false;
        }

        if (!CreateComputeShader())
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::Initialize: CreateComputeShader failed.");
            return false;
        }

        // 기본 파티클 셰이더 세트 등록
        if (!RegisterParticleShaderSet("Particle",
                                       ComputeEffectShader::ParticleClearCS,
                                       ComputeEffectShader::ParticleUpdateCS,
                                       ComputeEffectShader::ParticleDrawCS))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::Initialize: RegisterParticleShaderSet failed.");
            return false;
        }
        
        // "ParticleEffect"도 같은 셰이더 사용
        if (!RegisterParticleShaderSet("ParticleEffect",
                                       ComputeEffectShader::ParticleClearCS,
                                       ComputeEffectShader::ParticleUpdateCS,
                                       ComputeEffectShader::ParticleDrawCS))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::Initialize: RegisterParticleShaderSet(ParticleEffect) failed.");
            return false;
        }

        if (!CreateConstantBuffer())
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::Initialize: CreateConstantBuffer failed.");
            return false;
        }

        if (!CreateUnorderedAccessViews(width, height))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::Initialize: CreateUnorderedAccessViews failed.");
            return false;
        }

        if (!CreateParticleBuffers(m_particleCount))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::Initialize: CreateParticleBuffers failed.");
            return false;
        }

        m_width  = width;
        m_height = height;

        ALICE_LOG_INFO("ComputeEffectSystem::Initialize: success");
        return true;
    }

    void ComputeEffectSystem::Resize(std::uint32_t width, std::uint32_t height)
    {
        if (m_width == width && m_height == height)
            return;

        m_outputTexture.Reset();
        m_outputUAV.Reset();
        m_outputSRV.Reset();

        if (CreateUnorderedAccessViews(width, height))
        {
            m_width  = width;
            m_height = height;
        }
    }

    void ComputeEffectSystem::SetParticleCount(std::uint32_t particleCount)
    {
        particleCount = std::max<std::uint32_t>(particleCount, 1);
        if (particleCount == m_particleCount)
            return;

        m_particleCount = particleCount;

        m_particleBuffer.Reset();
        m_particleUAV.Reset();
        m_particleSRV.Reset();

        if (!CreateParticleBuffers(m_particleCount))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::SetParticleCount: CreateParticleBuffers failed (count=%u)", m_particleCount);
        }
    }

    void ComputeEffectSystem::SetEmitterNormalized(float x, float y, float radius)
    {
        m_params0.x = ClampFloat(x, 0.0f, 1.0f);
        m_params0.y = ClampFloat(y, 0.0f, 1.0f);
        m_params0.z = std::max(radius, 0.0f);
    }

    void ComputeEffectSystem::SetParticleColor(float r, float g, float b)
    {
        m_params1.x = std::max(r, 0.0f);
        m_params1.y = std::max(g, 0.0f);
        m_params1.z = std::max(b, 0.0f);
    }

    void ComputeEffectSystem::SetParticleSizePx(float sizePx)
    {
        m_params1.w = std::max(sizePx, 1.0f);
    }

    void ComputeEffectSystem::Execute(const World& world)
    {
        if (!m_constantBuffer || !m_outputUAV || !m_particleUAV || !m_particleSRV)
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::Execute: resources not initialized");
            return;
        }

        // ComputeEffectComponent를 찾아서 활성화된 이펙트 실행
        std::string activeShaderName;
        for (auto&& [entityId, effect] : world.GetComponents<ComputeEffectComponent>())
        {
            if (effect.enabled && !effect.shaderName.empty())
            {
                // 등록된 파티클 셰이더 세트가 있는지 확인
                if (m_particleShaderSets.find(effect.shaderName) != m_particleShaderSets.end())
                {
                    activeShaderName = effect.shaderName;
                    break;
                }
            }
        }

        if (activeShaderName.empty())
            return;

        // 해당 셰이더 세트 가져오기
        auto it = m_particleShaderSets.find(activeShaderName);
        if (it == m_particleShaderSets.end())
            return;

        const ParticleShaderSet& shaderSet = it->second;
        if (!shaderSet.clearShader || !shaderSet.updateShader || !shaderSet.drawShader)
            return;

        UpdateConstantBuffer();

        DispatchClear(shaderSet.clearShader.Get());
        DispatchParticlesUpdate(shaderSet.updateShader.Get());
        DispatchParticlesDraw(shaderSet.drawShader.Get());

        UnbindCS();
    }

    bool ComputeEffectSystem::CreateComputeShader()
    {
        const char* shaderCode = ComputeEffectShader::BasicCS;

        ComPtr<ID3DBlob> shaderBlob;
        ComPtr<ID3DBlob> errorBlob;

        HRESULT hr = D3DCompile(
            shaderCode,
            strlen(shaderCode),
            nullptr,
            nullptr,
            nullptr,
            "main",
            "cs_5_0",
            0,
            0,
            shaderBlob.GetAddressOf(),
            errorBlob.GetAddressOf()
        );

        if (FAILED(hr))
        {
            if (errorBlob)
            {
                ALICE_LOG_ERRORF("ComputeEffectSystem::CreateComputeShader: %s", 
                    static_cast<const char*>(errorBlob->GetBufferPointer()));
            }
            return false;
        }

        hr = m_device->CreateComputeShader(
            shaderBlob->GetBufferPointer(),
            shaderBlob->GetBufferSize(),
            nullptr,
            m_computeShader.GetAddressOf()
        );

        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::CreateComputeShader: CreateComputeShader failed.");
            return false;
        }

        return true;
    }

    bool ComputeEffectSystem::RegisterParticleShaderSet(const std::string& name, 
                                                        const char* clearCS, 
                                                        const char* updateCS, 
                                                        const char* drawCS)
    {
        auto createOne = [&](const char* code, ComPtr<ID3D11ComputeShader>& outShader, const char* label) -> bool
        {
            ComPtr<ID3DBlob> blob;
            ComPtr<ID3DBlob> err;

            HRESULT hr = CompileCSBlob(code, "main", "cs_5_0", blob.GetAddressOf(), err.GetAddressOf());
            if (FAILED(hr))
            {
                if (err)
                {
                    ALICE_LOG_ERRORF("ComputeEffectSystem::RegisterParticleShaderSet(%s, %s): %s", name.c_str(), label,
                        static_cast<const char*>(err->GetBufferPointer()));
                }
                else
                {
                    ALICE_LOG_ERRORF("ComputeEffectSystem::RegisterParticleShaderSet(%s, %s): D3DCompile failed (0x%08X)", name.c_str(), label, (unsigned)hr);
                }
                return false;
            }

            hr = m_device->CreateComputeShader(
                blob->GetBufferPointer(),
                blob->GetBufferSize(),
                nullptr,
                outShader.GetAddressOf());

            if (FAILED(hr))
            {
                ALICE_LOG_ERRORF("ComputeEffectSystem::RegisterParticleShaderSet(%s, %s): CreateComputeShader failed (0x%08X)", name.c_str(), label, (unsigned)hr);
                return false;
            }

            return true;
        };

        ParticleShaderSet shaderSet;
        if (!createOne(clearCS,  shaderSet.clearShader,  "ClearCS"))  return false;
        if (!createOne(updateCS, shaderSet.updateShader, "UpdateCS")) return false;
        if (!createOne(drawCS,   shaderSet.drawShader,    "DrawCS"))   return false;

        m_particleShaderSets[name] = std::move(shaderSet);
        ALICE_LOG_INFO("ComputeEffectSystem::RegisterParticleShaderSet: registered '%s'", name.c_str());
        return true;
    }

    bool ComputeEffectSystem::CreateConstantBuffer()
    {
        D3D11_BUFFER_DESC desc = {};
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth      = sizeof(CBParams);
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = m_device->CreateBuffer(&desc, nullptr, m_constantBuffer.GetAddressOf());
        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::CreateConstantBuffer: CreateBuffer failed.");
            return false;
        }

        return true;
    }

    bool ComputeEffectSystem::CreateParticleBuffers(std::uint32_t particleCount)
    {
        static_assert(sizeof(ParticleInit) == 24, "ParticleInit must match HLSL Particle layout (24 bytes).");

        std::vector<ParticleInit> init(particleCount);
        for (std::uint32_t i = 0; i < particleCount; ++i)
        {
            float s = (float)i * 0.6180339887f;
            s -= std::floor(s);

            init[i].pos  = XMFLOAT2(0.0f, 0.0f);
            init[i].vel  = XMFLOAT2(0.0f, 0.0f);
            init[i].life = 0.0f;
            init[i].seed = s;
        }

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth           = sizeof(ParticleInit) * particleCount;
        desc.Usage               = D3D11_USAGE_DEFAULT;
        desc.BindFlags           = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags      = 0;
        desc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(ParticleInit);

        D3D11_SUBRESOURCE_DATA srd = {};
        srd.pSysMem = init.data();

        HRESULT hr = m_device->CreateBuffer(&desc, &srd, m_particleBuffer.GetAddressOf());
        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::CreateParticleBuffers: CreateBuffer failed.");
            return false;
        }

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension       = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Format              = DXGI_FORMAT_UNKNOWN;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements  = particleCount;

        hr = m_device->CreateUnorderedAccessView(m_particleBuffer.Get(), &uavDesc, m_particleUAV.GetAddressOf());
        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::CreateParticleBuffers: CreateUnorderedAccessView failed.");
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension       = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Format              = DXGI_FORMAT_UNKNOWN;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements  = particleCount;

        hr = m_device->CreateShaderResourceView(m_particleBuffer.Get(), &srvDesc, m_particleSRV.GetAddressOf());
        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::CreateParticleBuffers: CreateShaderResourceView failed.");
            return false;
        }

        return true;
    }

    void ComputeEffectSystem::UpdateConstantBuffer()
    {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);

        double dt = (double)(now.QuadPart - m_qpcPrev.QuadPart) / (double)m_qpcFreq.QuadPart;
        m_qpcPrev = now;

        m_dtSec = ClampFloat((float)dt, 0.0f, 1.0f / 20.0f);
        m_timeSec += m_dtSec;

        float w = (float)std::max<std::uint32_t>(m_width, 1);
        float h = (float)std::max<std::uint32_t>(m_height, 1);

        CBParams cb{};
        cb.params0    = m_params0;
        cb.params1    = m_params1;
        cb.time       = XMFLOAT4(m_timeSec, m_dtSec, (float)m_particleCount, 0.0f);
        cb.resolution = XMFLOAT4(w, h, 1.0f / w, 1.0f / h);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = m_context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::UpdateConstantBuffer: Map failed (0x%08X)", (unsigned)hr);
            return;
        }

        std::memcpy(mapped.pData, &cb, sizeof(CBParams));
        m_context->Unmap(m_constantBuffer.Get(), 0);
    }

    void ComputeEffectSystem::DispatchClear(ID3D11ComputeShader* clearShader)
    {
        if (!clearShader) return;
        
        m_context->CSSetShader(clearShader, nullptr, 0);
        m_context->CSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());

        ID3D11UnorderedAccessView* uav = m_outputUAV.Get();
        m_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

        UINT tgX = (m_width + 7) / 8;
        UINT tgY = (m_height + 7) / 8;
        m_context->Dispatch(tgX, tgY, 1);

        ID3D11UnorderedAccessView* nullUAV = nullptr;
        m_context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    }

    void ComputeEffectSystem::DispatchParticlesUpdate(ID3D11ComputeShader* updateShader)
    {
        if (!updateShader) return;
        
        m_context->CSSetShader(updateShader, nullptr, 0);
        m_context->CSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());

        ID3D11UnorderedAccessView* uav = m_particleUAV.Get();
        m_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

        UINT tg = (m_particleCount + 255) / 256;
        m_context->Dispatch(tg, 1, 1);

        ID3D11UnorderedAccessView* nullUAV = nullptr;
        m_context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    }

    void ComputeEffectSystem::DispatchParticlesDraw(ID3D11ComputeShader* drawShader)
    {
        if (!drawShader) return;
        
        m_context->CSSetShader(drawShader, nullptr, 0);
        m_context->CSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());

        ID3D11ShaderResourceView* srv = m_particleSRV.Get();
        m_context->CSSetShaderResources(0, 1, &srv);

        ID3D11UnorderedAccessView* uav = m_outputUAV.Get();
        m_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

        UINT tg = (m_particleCount + 255) / 256;
        m_context->Dispatch(tg, 1, 1);

        ID3D11ShaderResourceView* nullSRV = nullptr;
        m_context->CSSetShaderResources(0, 1, &nullSRV);

        ID3D11UnorderedAccessView* nullUAV = nullptr;
        m_context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    }

    void ComputeEffectSystem::UnbindCS()
    {
        m_context->CSSetShader(nullptr, nullptr, 0);

        ID3D11Buffer* nullCB = nullptr;
        m_context->CSSetConstantBuffers(0, 1, &nullCB);

        ID3D11ShaderResourceView* nullSRV = nullptr;
        m_context->CSSetShaderResources(0, 1, &nullSRV);

        ID3D11UnorderedAccessView* nullUAV = nullptr;
        m_context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    }

    bool ComputeEffectSystem::CreateUnorderedAccessViews(std::uint32_t width, std::uint32_t height)
    {
        // 출력 텍스처 생성
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width              = width;
        texDesc.Height             = height;
        texDesc.MipLevels          = 1;
        texDesc.ArraySize          = 1;
        texDesc.Format             = DXGI_FORMAT_R32G32B32A32_FLOAT;
        texDesc.SampleDesc.Count   = 1;
        texDesc.SampleDesc.Quality = 0;
        texDesc.Usage              = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags          = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        texDesc.CPUAccessFlags      = 0;

        HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, m_outputTexture.GetAddressOf());
        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::CreateUnorderedAccessViews: CreateTexture2D failed.");
            return false;
        }

        // UAV 생성
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format        = DXGI_FORMAT_R32G32B32A32_FLOAT;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;

        hr = m_device->CreateUnorderedAccessView(
            m_outputTexture.Get(),
            &uavDesc,
            m_outputUAV.GetAddressOf()
        );
        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::CreateUnorderedAccessViews: CreateUnorderedAccessView failed.");
            return false;
        }

        // SRV 생성 (결과를 읽기 위해)
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = DXGI_FORMAT_R32G32B32A32_FLOAT;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;

        hr = m_device->CreateShaderResourceView(
            m_outputTexture.Get(),
            &srvDesc,
            m_outputSRV.GetAddressOf()
        );
        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::CreateUnorderedAccessViews: CreateShaderResourceView failed.");
            return false;
        }

        return true;
    }
}
