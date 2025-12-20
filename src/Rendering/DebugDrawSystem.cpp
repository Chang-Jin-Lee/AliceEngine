#include "Rendering/DebugDrawSystem.h"

#include <d3dcompiler.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace Alice
{
    namespace
    {
        // 아주 단순한 컬러 라인 전용 셰이더입니다.
        const char* g_DebugLineVS = R"(
cbuffer CBViewProj : register(b0)
{
    float4x4 gViewProj;
};

struct VSInput
{
    float3 Position : POSITION;
    float4 Color    : COLOR0;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float4 Color    : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 worldPos = float4(input.Position, 1.0f);
    output.Position = mul(worldPos, gViewProj);
    output.Color    = input.Color;
    return output;
}
)";

        const char* g_DebugLinePS = R"(
struct PSInput
{
    float4 Position : SV_POSITION;
    float4 Color    : COLOR0;
};

float4 main(PSInput input) : SV_TARGET
{
    return input.Color;
}
)";
    }

    DebugDrawSystem::DebugDrawSystem(ID3D11RenderDevice& renderDevice)
        : m_renderDevice(renderDevice)
    {
        m_device  = m_renderDevice.GetDevice();
        m_context = m_renderDevice.GetImmediateContext();
    }

    bool DebugDrawSystem::Initialize()
    {
        if (!m_device || !m_context || !CreateShadersAndInputLayout()) return false;

        // ViewProj 상수 버퍼 생성
        D3D11_BUFFER_DESC desc = { sizeof(CBViewProj), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
        if (FAILED(m_device->CreateBuffer(&desc, nullptr, m_cbViewProj.ReleaseAndGetAddressOf()))) return false;

        return true;
    }

    void DebugDrawSystem::Clear()
    {
        m_vertices.clear();
    }

    void DebugDrawSystem::AddLine(const XMFLOAT3& from, const XMFLOAT3& to, const XMFLOAT4& color)
    {
        m_vertices.push_back({ from, color });
        m_vertices.push_back({ to,   color });
    }

    void DebugDrawSystem::Render(const Camera& camera)
    {
        if (m_vertices.empty() || !m_vertexShader || !m_pixelShader || !m_inputLayout) return;
        if (!EnsureVertexBufferSize(m_vertices.size())) return;

        // 1. Vertex Buffer 업데이트 (Map -> Copy -> Unmap)
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(m_context->Map(m_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
        std::memcpy(mapped.pData, m_vertices.data(), m_vertices.size() * sizeof(DebugVertex));
        m_context->Unmap(m_vertexBuffer.Get(), 0);

        // 2. View-Proj 행렬 업데이트
        CBViewProj cb = { XMMatrixTranspose(camera.GetViewMatrix() * camera.GetProjectionMatrix()) };
        m_context->UpdateSubresource(m_cbViewProj.Get(), 0, nullptr, &cb, 0, 0);

        // 3. 파이프라인 설정 및 그리기
        UINT stride = sizeof(DebugVertex), offset = 0;
        ID3D11Buffer* vb = m_vertexBuffer.Get();

        m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        m_context->IASetInputLayout(m_inputLayout.Get());
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

        m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
        m_context->VSSetConstantBuffers(0, 1, m_cbViewProj.GetAddressOf());
        m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);

        m_context->Draw((UINT)m_vertices.size(), 0);
    }

    bool DebugDrawSystem::CreateShadersAndInputLayout()
    {
        ComPtr<ID3DBlob> vsBlob, psBlob;

        // 1. VS 컴파일 및 생성
        if (FAILED(D3DCompile(g_DebugLineVS, std::strlen(g_DebugLineVS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), nullptr))) return false;
        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_vertexShader.ReleaseAndGetAddressOf()))) return false;

        // 2. PS 컴파일 및 생성
        if (FAILED(D3DCompile(g_DebugLinePS, std::strlen(g_DebugLinePS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), nullptr))) return false;
        if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_pixelShader.ReleaseAndGetAddressOf()))) return false;

        // 3. Input Layout 생성 (오프셋 자동 정렬 사용)
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,                            D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        if (FAILED(m_device->CreateInputLayout(desc, (UINT)std::size(desc), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_inputLayout.ReleaseAndGetAddressOf()))) return false;

        return true;
    }

    bool DebugDrawSystem::EnsureVertexBufferSize(std::size_t vertexCount)
    {
        if (vertexCount == 0 || (m_vertexBuffer && vertexCount <= m_vertexCapacity)) return true;

        m_vertexBuffer.Reset();
        m_vertexCapacity = vertexCount;

        // 동적 버퍼 생성 (Dynamic Usage, CPU Write)
        D3D11_BUFFER_DESC desc = { (UINT)(sizeof(DebugVertex) * vertexCount), D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        if (FAILED(m_device->CreateBuffer(&desc, nullptr, m_vertexBuffer.ReleaseAndGetAddressOf()))) return false;

        return true;
    }
}



