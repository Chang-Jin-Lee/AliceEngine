#include "Rendering/ForwardRenderSystem.h"

#include <d3dcompiler.h>
// 텍스처 로더 (vcpkg의 DirectXTK 사용)
#include <DirectXTK/WICTextureLoader.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace Alice
{
    // 간단한 Lambert / Phong / Blinn-Phong 셰이더 코드
    // (D3D11 튜토리얼의 기본 조명 코드를 참고한 단순 버전)
    namespace
    {
        const char* g_PhongVertexShaderSource = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
};

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float2 TexCoord : TEXCOORD0;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    float4 worldPos = mul(float4(input.Position, 1.0f), gWorld);
    float4 viewPos  = mul(worldPos, gView);
    output.Position = mul(viewPos, gProj);

    output.WorldPos = worldPos.xyz;
    output.Normal   = mul(float4(input.Normal, 0.0f), gWorld).xyz;
    output.TexCoord = input.TexCoord;

    return output;
}
)";

        const char* g_PhongPixelShaderSource = R"(
Texture2D gDiffuseMap  : register(t0);
Texture2D gNormalMap   : register(t1);
Texture2D gSpecularMap : register(t2);
SamplerState gSampler  : register(s0);

cbuffer CBLighting : register(b1)
{
    // Key Light
    float3 gKeyLightDir;
    float  gKeyLightPad0;

    float3 gKeyLightColor;
    float  gKeyLightIntensity;

    // Fill Light
    float3 gFillLightDir;
    float  gFillLightPad0;

    float3 gFillLightColor;
    float  gFillLightIntensity;

    float3 gCameraPos;
    float  gPad1;

    float4 gMaterialDiffuse;   // rgb: diffuse color
    float4 gMaterialSpecular;  // rgb: specular color, a: shininess

    int    gShadingMode;       // 0: Lambert, 1: Phong, 2: Blinn-Phong
    int3   gPad2;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
};

float4 main(PSInput input) : SV_TARGET
{
    // 노말맵에서 노멀을 가져와 [-1,1] 범위로 변환 후 정규화합니다.
    float3 normalTex = gNormalMap.Sample(gSampler, input.TexCoord).xyz * 2.0f - 1.0f;
    float3 N = normalize(normalTex);

    float3 V = normalize(gCameraPos - input.WorldPos);

    float3 totalDiffuse  = float3(0.0f, 0.0f, 0.0f);
    float3 totalSpecular = float3(0.0f, 0.0f, 0.0f);

    // Key Light
    {
        float3 L = normalize(-gKeyLightDir);
        float  NdotL = max(dot(N, L), 0.0f);
        float3 lightColor = gKeyLightColor * gKeyLightIntensity;

        totalDiffuse += NdotL * lightColor;

        if (gShadingMode != 0 && NdotL > 0.0f)
        {
            float specularTerm = 0.0f;
            if (gShadingMode == 2) // Blinn-Phong
            {
                float3 H = normalize(L + V);
                float NdotH = max(dot(N, H), 0.0f);
                specularTerm = pow(NdotH, gMaterialSpecular.a);
            }
            else // Phong
            {
                float3 R = reflect(-L, N);
                float RdotV = max(dot(R, V), 0.0f);
                specularTerm = pow(RdotV, gMaterialSpecular.a);
            }

            totalSpecular += specularTerm * lightColor;
        }
    }

    // Fill Light (옵션)
    {
        float3 L = normalize(-gFillLightDir);
        float  NdotL = max(dot(N, L), 0.0f);
        float3 lightColor = gFillLightColor * gFillLightIntensity;

        totalDiffuse += NdotL * lightColor;

        if (gShadingMode != 0 && NdotL > 0.0f)
        {
            float specularTerm = 0.0f;
            if (gShadingMode == 2) // Blinn-Phong
            {
                float3 H = normalize(L + V);
                float NdotH = max(dot(N, H), 0.0f);
                specularTerm = pow(NdotH, gMaterialSpecular.a);
            }
            else // Phong
            {
                float3 R = reflect(-L, N);
                float RdotV = max(dot(R, V), 0.0f);
                specularTerm = pow(RdotV, gMaterialSpecular.a);
            }

            totalSpecular += specularTerm * lightColor;
        }
    }

    // 텍스처 샘플링
    float3 albedo    = gDiffuseMap.Sample(gSampler,  input.TexCoord).rgb;
    float3 specColor = gSpecularMap.Sample(gSampler, input.TexCoord).rgb;

    float3 ambient = 0.1f * gKeyLightColor;

    float3 finalColor =
        ambient * albedo +
        totalDiffuse * albedo +
        totalSpecular * specColor;

    return float4(finalColor, 1.0f);
}
)";
    }

    ForwardRenderSystem::ForwardRenderSystem(ID3D11RenderDevice& renderDevice)
        : m_renderDevice(renderDevice)
    {
        m_device  = m_renderDevice.GetDevice();
        m_context = m_renderDevice.GetImmediateContext();
    }

    bool ForwardRenderSystem::Initialize(std::uint32_t width, std::uint32_t height)
    {
        if (!m_device || !m_context) return false;
        if (!CreateSceneRenderTarget(width, height)) return false;
        if (!CreateCubeGeometry()) return false;
        if (!CreateShadersAndInputLayout()) return false;
        if (!CreateConstantBuffers()) return false;
        if (!CreateTextures())  return false;
        if (!CreateSamplerState()) return false;
        return true;
    }

    void ForwardRenderSystem::Resize(std::uint32_t width, std::uint32_t height)
    {
        if (!m_device) return;
        if (width == 0 || height == 0) return;
        // 기존 리소스 해제 후 새로 생성
        m_sceneColorTex.Reset();
        m_sceneRTV.Reset();
        m_sceneSRV.Reset();
        m_sceneDepthTex.Reset();
        m_sceneDSV.Reset();

        CreateSceneRenderTarget(width, height);
    }

    bool ForwardRenderSystem::CreateSceneRenderTarget(std::uint32_t width, std::uint32_t height)
    {
        m_sceneWidth  = width;
        m_sceneHeight = height;

        if (width == 0 || height == 0) return false;

        // 색 텍스처 (RTV + SRV)
        D3D11_TEXTURE2D_DESC colorDesc = {};
        colorDesc.Width              = width;
        colorDesc.Height             = height;
        colorDesc.MipLevels          = 1;
        colorDesc.ArraySize          = 1;
        colorDesc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
        colorDesc.SampleDesc.Count   = 1;
        colorDesc.Usage              = D3D11_USAGE_DEFAULT;
        colorDesc.BindFlags          = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = m_device->CreateTexture2D(&colorDesc, nullptr, m_sceneColorTex.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        hr = m_device->CreateRenderTargetView(
            m_sceneColorTex.Get(),
            nullptr,
            m_sceneRTV.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        hr = m_device->CreateShaderResourceView(
            m_sceneColorTex.Get(),
            nullptr,
            m_sceneSRV.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        // 깊이/스텐실 텍스처 + 뷰
        D3D11_TEXTURE2D_DESC depthDesc = {};
        depthDesc.Width              = width;
        depthDesc.Height             = height;
        depthDesc.MipLevels          = 1;
        depthDesc.ArraySize          = 1;
        depthDesc.Format             = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.SampleDesc.Count   = 1;
        depthDesc.Usage              = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags          = D3D11_BIND_DEPTH_STENCIL;

        hr = m_device->CreateTexture2D(&depthDesc, nullptr, m_sceneDepthTex.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format             = depthDesc.Format;
        dsvDesc.ViewDimension      = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;

        hr = m_device->CreateDepthStencilView(
            m_sceneDepthTex.Get(),
            &dsvDesc,
            m_sceneDSV.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateCubeGeometry()
    {
        // 단순 단위 큐브 정점/인덱스 데이터
        // (각 면에 대한 법선과 텍스처 좌표를 명시해서 조명/텍스처링이 자연스럽도록)
        SimpleVertex vertices[] =
        {
            // Front (+Z)
            { XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f,  0.0f,  1.0f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(-1.0f,  1.0f,  1.0f), XMFLOAT3(0.0f,  0.0f,  1.0f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3( 1.0f,  1.0f,  1.0f), XMFLOAT3(0.0f,  0.0f,  1.0f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3( 1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f,  0.0f,  1.0f), XMFLOAT2(1.0f, 1.0f) },

            // Back (-Z)
            { XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f,  0.0f, -1.0f), XMFLOAT2(1.0f, 1.0f) },
            { XMFLOAT3( 1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f,  0.0f, -1.0f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3( 1.0f,  1.0f, -1.0f), XMFLOAT3(0.0f,  0.0f, -1.0f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3(-1.0f,  1.0f, -1.0f), XMFLOAT3(0.0f,  0.0f, -1.0f), XMFLOAT2(1.0f, 0.0f) },

            // Top (+Y)
            { XMFLOAT3(-1.0f,  1.0f, -1.0f), XMFLOAT3(0.0f,  1.0f,  0.0f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3( 1.0f,  1.0f, -1.0f), XMFLOAT3(0.0f,  1.0f,  0.0f), XMFLOAT2(1.0f, 1.0f) },
            { XMFLOAT3( 1.0f,  1.0f,  1.0f), XMFLOAT3(0.0f,  1.0f,  0.0f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3(-1.0f,  1.0f,  1.0f), XMFLOAT3(0.0f,  1.0f,  0.0f), XMFLOAT2(0.0f, 0.0f) },

            // Bottom (-Y)
            { XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, -1.0f,  0.0f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f, -1.0f,  0.0f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3( 1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f, -1.0f,  0.0f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3( 1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, -1.0f,  0.0f), XMFLOAT2(1.0f, 1.0f) },

            // Left (-X)
            { XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(-1.0f,  0.0f,  0.0f), XMFLOAT2(1.0f, 1.0f) },
            { XMFLOAT3(-1.0f,  1.0f, -1.0f), XMFLOAT3(-1.0f,  0.0f,  0.0f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3(-1.0f,  1.0f,  1.0f), XMFLOAT3(-1.0f,  0.0f,  0.0f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(-1.0f,  0.0f,  0.0f), XMFLOAT2(0.0f, 1.0f) },

            // Right (+X)
            { XMFLOAT3( 1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f,  0.0f,  0.0f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3( 1.0f, -1.0f,  1.0f), XMFLOAT3(1.0f,  0.0f,  0.0f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3( 1.0f,  1.0f,  1.0f), XMFLOAT3(1.0f,  0.0f,  0.0f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3( 1.0f,  1.0f, -1.0f), XMFLOAT3(1.0f,  0.0f,  0.0f), XMFLOAT2(1.0f, 1.0f) },
        };

        uint16_t indices[] =
        {
            // Front
            0, 1, 2, 0, 2, 3,
            // Back
            4, 5, 6, 4, 6, 7,
            // Top
            8, 9,10, 8,10,11,
            // Bottom
            12,13,14, 12,14,15,
            // Left
            16,17,18, 16,18,19,
            // Right
            20,21,22, 20,22,23
        };

        m_indexCount = static_cast<UINT>(std::size(indices));

        // 정점 버퍼 생성
        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.Usage = D3D11_USAGE_DEFAULT;
        vbDesc.ByteWidth = static_cast<UINT>(sizeof(vertices));
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA vbData = {};
        vbData.pSysMem = vertices;

        HRESULT hr = m_device->CreateBuffer(&vbDesc, &vbData, m_vertexBuffer.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        // 인덱스 버퍼 생성
        D3D11_BUFFER_DESC ibDesc = {};
        ibDesc.Usage = D3D11_USAGE_DEFAULT;
        ibDesc.ByteWidth = static_cast<UINT>(sizeof(indices));
        ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

        D3D11_SUBRESOURCE_DATA ibData = {};
        ibData.pSysMem = indices;

        hr = m_device->CreateBuffer(&ibDesc, &ibData, m_indexBuffer.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateShadersAndInputLayout()
    {
        ComPtr<ID3DBlob> vsBlob;
        ComPtr<ID3DBlob> psBlob;
        ComPtr<ID3DBlob> errorBlob;

        // Vertex Shader 컴파일
        HRESULT hr = D3DCompile(
            g_PhongVertexShaderSource,
            strlen(g_PhongVertexShaderSource),
            nullptr,
            nullptr,
            nullptr,
            "main",
            "vs_5_0",
            0,
            0,
            vsBlob.GetAddressOf(),
            errorBlob.GetAddressOf()
        );

        if (FAILED(hr)) return false;

        // Pixel Shader 컴파일
        errorBlob.Reset();
        hr = D3DCompile(
            g_PhongPixelShaderSource,
            strlen(g_PhongPixelShaderSource),
            nullptr,
            nullptr,
            nullptr,
            "main",
            "ps_5_0",
            0,
            0,
            psBlob.GetAddressOf(),
            errorBlob.GetAddressOf()
        );

        if (FAILED(hr)) return false;

        // 실제 셰이더 객체 생성
        hr = m_device->CreateVertexShader(
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            nullptr,
            m_vertexShader.ReleaseAndGetAddressOf()
        );
        if (FAILED(hr)) return false;

        hr = m_device->CreatePixelShader(
            psBlob->GetBufferPointer(),
            psBlob->GetBufferSize(),
            nullptr,
            m_pixelShader.ReleaseAndGetAddressOf()
        );
        if (FAILED(hr)) return false;

        // 입력 레이아웃 생성 (POSITION, NORMAL)
        D3D11_INPUT_ELEMENT_DESC layoutDesc[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,                            D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, sizeof(XMFLOAT3),             D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, sizeof(XMFLOAT3) * 2,         D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };

        hr = m_device->CreateInputLayout(
            layoutDesc,
            static_cast<UINT>(std::size(layoutDesc)),
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            m_inputLayout.ReleaseAndGetAddressOf()
        );

        if (FAILED(hr)) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateConstantBuffers()
    {
        // CBPerObject
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.Usage = D3D11_USAGE_DEFAULT;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

        cbDesc.ByteWidth = sizeof(CBPerObject);
        HRESULT hr = m_device->CreateBuffer(&cbDesc, nullptr, m_cbPerObject.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        cbDesc.ByteWidth = sizeof(CBLighting);
        hr = m_device->CreateBuffer(&cbDesc, nullptr, m_cbLighting.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateTextures()
    {
        // 실행 파일 기준으로 Resource/Image 폴더의 브릭 텍스처를 읽어옵니다.
        const wchar_t* diffusePath  = L"../Resource/Image/Bricks059_1K-JPG_Color.jpg";
        const wchar_t* normalPath   = L"../Resource/Image/Bricks059_1K-JPG_NormalDX.jpg";
        const wchar_t* specularPath = L"../Resource/Image/Bricks059_Specular.png";

        HRESULT hr = DirectX::CreateWICTextureFromFile(
            m_device.Get(),
            diffusePath,
            nullptr,
            m_diffuseSRV.ReleaseAndGetAddressOf()
        );
        if (FAILED(hr)) return false;

        hr = DirectX::CreateWICTextureFromFile(
            m_device.Get(),
            normalPath,
            nullptr,
            m_normalSRV.ReleaseAndGetAddressOf()
        );
        if (FAILED(hr)) return false;

        hr = DirectX::CreateWICTextureFromFile(
            m_device.Get(),
            specularPath,
            nullptr,
            m_specularSRV.ReleaseAndGetAddressOf()
        );
        if (FAILED(hr)) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateSamplerState()
    {
        D3D11_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        samplerDesc.MinLOD = 0;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

        HRESULT hr = m_device->CreateSamplerState(&samplerDesc, m_samplerState.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        return true;
    }

    void ForwardRenderSystem::UpdatePerObjectCB(const XMMATRIX& world,
                                                const XMMATRIX& view,
                                                const XMMATRIX& projection)
    {
        CBPerObject data = {};
        // HLSL에서 row-major로 사용할 수 있도록 전치 행렬 사용
        data.world      = XMMatrixTranspose(world);
        data.view       = XMMatrixTranspose(view);
        data.projection = XMMatrixTranspose(projection);

        m_context->UpdateSubresource(m_cbPerObject.Get(), 0, nullptr, &data, 0, 0);
        m_context->VSSetConstantBuffers(0, 1, m_cbPerObject.GetAddressOf());
    }

    void ForwardRenderSystem::UpdateLightingCB(const Camera& camera,
                                               int shadingMode,
                                               bool enableFillLight)
    {
        CBLighting data = {};

        // Key Light 방향은 파라미터에서 받아와 정규화합니다.
        {
            XMVECTOR dir = XMVectorSet(
                m_lightingParameters.keyDirection.x,
                m_lightingParameters.keyDirection.y,
                m_lightingParameters.keyDirection.z,
                0.0f);

            if (XMVector3Equal(dir, XMVectorZero()))
                dir = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);

            dir = XMVector3Normalize(dir);
            XMStoreFloat3(&data.keyLight.direction, dir);
        }
        data.keyLight.color     = m_lightingParameters.diffuseColor;
        data.keyLight.intensity = m_lightingParameters.keyIntensity;

        // Fill Light: 반대편에서 살짝 채워주는 부드러운 광원
        if (enableFillLight)
        {
            XMVECTOR dir = XMVectorSet(
                m_lightingParameters.fillDirection.x,
                m_lightingParameters.fillDirection.y,
                m_lightingParameters.fillDirection.z,
                0.0f);

            if (XMVector3Equal(dir, XMVectorZero()))
                dir = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);

            dir = XMVector3Normalize(dir);
            XMStoreFloat3(&data.fillLight.direction, dir);

            data.fillLight.color     = m_lightingParameters.diffuseColor;
            data.fillLight.intensity = m_lightingParameters.fillIntensity;
        }
        else
        {
            data.fillLight.direction = XMFLOAT3(0.0f, -1.0f, 0.0f);
            data.fillLight.color     = XMFLOAT3(0.0f, 0.0f, 0.0f);
            data.fillLight.intensity = 0.0f;
        }

        // 카메라 및 재질 정보
        data.cameraPosition = camera.GetPosition();

        data.materialDiffuse  = XMFLOAT4(
            m_lightingParameters.diffuseColor.x,
            m_lightingParameters.diffuseColor.y,
            m_lightingParameters.diffuseColor.z,
            1.0f);

        data.materialSpecular = XMFLOAT4(
            m_lightingParameters.specularColor.x,
            m_lightingParameters.specularColor.y,
            m_lightingParameters.specularColor.z,
            m_lightingParameters.shininess); // a: shininess

        data.shadingMode = shadingMode;

        m_context->UpdateSubresource(m_cbLighting.Get(), 0, nullptr, &data, 0, 0);
        m_context->PSSetConstantBuffers(1, 1, m_cbLighting.GetAddressOf());
    }

    XMMATRIX ForwardRenderSystem::BuildWorldMatrix(const TransformComponent& transform) const
    {
        XMVECTOR scale = XMLoadFloat3(&transform.scale);
        XMVECTOR rotation = XMLoadFloat3(&transform.rotation);
        XMVECTOR translation = XMLoadFloat3(&transform.position);

        XMMATRIX S = XMMatrixScalingFromVector(scale);
        XMMATRIX R = XMMatrixRotationRollPitchYawFromVector(rotation);
        XMMATRIX T = XMMatrixTranslationFromVector(translation);

        return S * R * T;
    }

    void ForwardRenderSystem::Render(const World& world,
                                     const Camera& camera,
                                     EntityId entity,
                                     int shadingMode,
                                     bool enableFillLight)
    {
        if (!m_vertexBuffer || !m_indexBuffer || !m_vertexShader || !m_pixelShader)
            return;

        const TransformComponent* transform = world.GetTransform(entity);
        if (!transform) return;

        // 씬 렌더 타깃이 없다면 아무 것도 하지 않습니다.
        if (!m_sceneRTV || !m_sceneDSV)
            return;

        // 0) 현재 백버퍼 렌더 타깃을 저장해 두었다가, 렌더 후에 복원합니다.
        ID3D11RenderTargetView* backBufferRTV = m_renderDevice.GetBackBufferRTV();
        ID3D11DepthStencilView* backBufferDSV = m_renderDevice.GetBackBufferDSV();

        // 1) 게임 뷰포트 렌더 타깃으로 전환하고, 색/깊이를 클리어합니다.
        const float sceneClearColor[4] = { 0.1f, 0.1f, 0.3f, 1.0f };
        ID3D11RenderTargetView* rtvs[] = { m_sceneRTV.Get() };
        m_context->OMSetRenderTargets(1, rtvs, m_sceneDSV.Get());
        m_context->ClearRenderTargetView(m_sceneRTV.Get(), sceneClearColor);
        m_context->ClearDepthStencilView(m_sceneDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

        // 2) 파이프라인 상태 설정
        UINT stride = sizeof(SimpleVertex);
        UINT offset = 0;
        ID3D11Buffer* vb = m_vertexBuffer.Get();
        m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
        m_context->IASetInputLayout(m_inputLayout.Get());
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
        m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);

        // 3) 상수 버퍼 / 텍스처 바인딩
        XMMATRIX worldM = BuildWorldMatrix(*transform);
        XMMATRIX viewM  = camera.GetViewMatrix();
        XMMATRIX projM  = camera.GetProjectionMatrix();

        UpdatePerObjectCB(worldM, viewM, projM);
        UpdateLightingCB(camera, shadingMode, enableFillLight);

        // 브릭 텍스처, 노말맵, 스페큘러 맵과 샘플러를 픽셀 셰이더에 바인딩합니다.
        ID3D11ShaderResourceView* srvs[] =
        {
            m_diffuseSRV.Get(),
            m_normalSRV.Get(),
            m_specularSRV.Get()
        };
        m_context->PSSetShaderResources(0, 3, srvs);
        ID3D11SamplerState* samplers[] = { m_samplerState.Get() };
        m_context->PSSetSamplers(0, 1, samplers);

        // 4) 드로우 콜
        m_context->DrawIndexed(m_indexCount, 0, 0);

        // 5) ImGui 렌더링을 위해 기본 백버퍼 렌더 타깃으로 복원합니다.
        if (backBufferRTV)
        {
            ID3D11RenderTargetView* bbRtvs[] = { backBufferRTV };
            m_context->OMSetRenderTargets(1, bbRtvs, backBufferDSV);
        }
    }
}


