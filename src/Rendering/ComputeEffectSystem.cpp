#include "ComputeEffectSystem.h"

#include <d3dcompiler.h>
#include "Rendering/ShaderCode/ComputeEffectShader.h"
#include "Core/Logger.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace Alice
{
    ComputeEffectSystem::ComputeEffectSystem(ID3D11RenderDevice& renderDevice)
        : m_renderDevice(renderDevice)
    {
        m_device  = m_renderDevice.GetDevice();
        m_context = m_renderDevice.GetImmediateContext();
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

    void ComputeEffectSystem::Execute(const World& world)
    {
        if (!m_computeShader || !m_outputUAV)
            return;

        // 컴퓨트 셰이더 바인딩
        m_context->CSSetShader(m_computeShader.Get(), nullptr, 0);

        // 상수 버퍼 바인딩
        m_context->CSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());

        // UAV 바인딩
        ID3D11UnorderedAccessView* uavs[] = { m_outputUAV.Get() };
        UINT initialCounts[] = { 0 };
        m_context->CSSetUnorderedAccessViews(0, 1, uavs, initialCounts);

        // 스레드 그룹 실행 (예: 8x8 스레드 그룹)
        UINT threadGroupX = (m_width  + 7) / 8;
        UINT threadGroupY = (m_height + 7) / 8;
        m_context->Dispatch(threadGroupX, threadGroupY, 1);

        // UAV 언바인딩
        ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
        m_context->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
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

    bool ComputeEffectSystem::CreateConstantBuffer()
    {
        D3D11_BUFFER_DESC desc = {};
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth      = sizeof(DirectX::XMFLOAT4) * 4; // 기본 상수 버퍼 크기
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
