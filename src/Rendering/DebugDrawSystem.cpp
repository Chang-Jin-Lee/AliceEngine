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
        if (!m_device || !m_context)
            return false;

        if (!CreateShadersAndInputLayout())
            return false;

        // 뷰-프로젝션 상수 버퍼 생성
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth      = sizeof(CBViewProj);
        cbDesc.Usage          = D3D11_USAGE_DEFAULT;
        cbDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = 0;

        HRESULT hr = m_device->CreateBuffer(&cbDesc, nullptr, m_cbViewProj.ReleaseAndGetAddressOf());
        if (FAILED(hr))
            return false;

        return true;
    }

    void DebugDrawSystem::Clear()
    {
        m_vertices.clear();
    }

    void DebugDrawSystem::AddLine(const XMFLOAT3& from,
                                  const XMFLOAT3& to,
                                  const XMFLOAT4& color)
    {
        DebugVertex v0 { from, color };
        DebugVertex v1 { to,   color };

        m_vertices.push_back(v0);
        m_vertices.push_back(v1);
    }

    void DebugDrawSystem::Render(const Camera& camera)
    {
        if (m_vertices.empty() || !m_vertexShader || !m_pixelShader || !m_inputLayout)
            return;

        const std::size_t vertexCount = m_vertices.size();
        if (!EnsureVertexBufferSize(vertexCount))
            return;

        // 정점 데이터를 GPU 버퍼에 업로드
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = m_context->Map(m_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr))
            return;

        std::memcpy(mapped.pData, m_vertices.data(), vertexCount * sizeof(DebugVertex));
        m_context->Unmap(m_vertexBuffer.Get(), 0);

        // 뷰-프로젝션 행렬 설정
        XMMATRIX view  = camera.GetViewMatrix();
        XMMATRIX proj  = camera.GetProjectionMatrix();
        XMMATRIX vp    = XMMatrixTranspose(view * proj);

        CBViewProj cbData = {};
        cbData.viewProj = vp;
        m_context->UpdateSubresource(m_cbViewProj.Get(), 0, nullptr, &cbData, 0, 0);

        // 입력 어셈블러 설정
        UINT stride = sizeof(DebugVertex);
        UINT offset = 0;
        ID3D11Buffer* vb = m_vertexBuffer.Get();
        m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        m_context->IASetInputLayout(m_inputLayout.Get());
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

        // 셰이더와 상수 버퍼 바인딩
        m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
        m_context->VSSetConstantBuffers(0, 1, m_cbViewProj.GetAddressOf());
        m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);

        // 라인 그리기
        m_context->Draw(static_cast<UINT>(vertexCount), 0);
    }

    bool DebugDrawSystem::CreateShadersAndInputLayout()
    {
        ComPtr<ID3DBlob> vsBlob;
        ComPtr<ID3DBlob> psBlob;
        ComPtr<ID3DBlob> errorBlob;

        HRESULT hr = D3DCompile(
            g_DebugLineVS,
            std::strlen(g_DebugLineVS),
            nullptr,
            nullptr,
            nullptr,
            "main",
            "vs_5_0",
            0,
            0,
            vsBlob.ReleaseAndGetAddressOf(),
            errorBlob.ReleaseAndGetAddressOf());

        if (FAILED(hr))
        {
            return false;
        }

        hr = D3DCompile(
            g_DebugLinePS,
            std::strlen(g_DebugLinePS),
            nullptr,
            nullptr,
            nullptr,
            "main",
            "ps_5_0",
            0,
            0,
            psBlob.ReleaseAndGetAddressOf(),
            errorBlob.ReleaseAndGetAddressOf());

        if (FAILED(hr))
        {
            return false;
        }

        hr = m_device->CreateVertexShader(
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            nullptr,
            m_vertexShader.ReleaseAndGetAddressOf());
        if (FAILED(hr))
            return false;

        hr = m_device->CreatePixelShader(
            psBlob->GetBufferPointer(),
            psBlob->GetBufferSize(),
            nullptr,
            m_pixelShader.ReleaseAndGetAddressOf());
        if (FAILED(hr))
            return false;

        // 입력 레이아웃 (POSITION, COLOR)
        D3D11_INPUT_ELEMENT_DESC layout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
              D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
              D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        hr = m_device->CreateInputLayout(
            layout,
            static_cast<UINT>(std::size(layout)),
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            m_inputLayout.ReleaseAndGetAddressOf());
        if (FAILED(hr))
            return false;

        return true;
    }

    bool DebugDrawSystem::EnsureVertexBufferSize(std::size_t vertexCount)
    {
        if (vertexCount == 0)
            return true;

        if (m_vertexBuffer && vertexCount <= m_vertexCapacity)
            return true;

        m_vertexBuffer.Reset();
        m_vertexCapacity = vertexCount;

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth      = static_cast<UINT>(sizeof(DebugVertex) * m_vertexCapacity);
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = m_device->CreateBuffer(&desc, nullptr, m_vertexBuffer.ReleaseAndGetAddressOf());
        if (FAILED(hr))
            return false;

        return true;
    }
}



