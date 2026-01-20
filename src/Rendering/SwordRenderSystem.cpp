#include "Rendering/SwordRenderSystem.h"
#include "Components/SwordEffectComponent.h"
#include "Rendering/ShaderCode/SwordEffectShader.h"
#include "Core/ResourceManager.h"
#include "Components/TransformComponent.h"
#include "Core/Logger.h"

#include <d3dcompiler.h>
#include <cmath>
#include <algorithm>
#include <Windows.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace Alice
{
	SwordRenderSystem::SwordRenderSystem(ID3D11RenderDevice& renderDevice)
		: m_renderDevice(renderDevice)
	{
		m_device = m_renderDevice.GetDevice();
		m_context = m_renderDevice.GetImmediateContext();
	}


	bool SwordRenderSystem::Initialize()
	{
		if (!m_device || !m_context) return false;
		if (!CreateShadersAndInputLayout()) return false;

		// PerSwordEffect VS 상수 버퍼 생성
		D3D11_BUFFER_DESC descVS = { sizeof(CBPerSwordEffectVS), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
		if (FAILED(m_device->CreateBuffer(&descVS, nullptr, m_cbPerSwordEffectVS.ReleaseAndGetAddressOf()))) return false;

		// PerSwordEffect PS 상수 버퍼 생성
		D3D11_BUFFER_DESC descPS = { sizeof(CBPerSwordEffectPS), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
		if (FAILED(m_device->CreateBuffer(&descPS, nullptr, m_cbPerSwordEffectPS.ReleaseAndGetAddressOf()))) return false;

		// 알파 블렌딩용 블렌드 스테이트 생성
		D3D11_BLEND_DESC blendDesc = {};
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(m_device->CreateBlendState(&blendDesc, m_blendState.ReleaseAndGetAddressOf()))) return false;

		// 샘플러 스테이트 생성
		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.MipLODBias = 0.0f;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		samplerDesc.BorderColor[0] = 0.0f;
		samplerDesc.BorderColor[1] = 0.0f;
		samplerDesc.BorderColor[2] = 0.0f;
		samplerDesc.BorderColor[3] = 0.0f;
		samplerDesc.MinLOD = 0.0f;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(m_device->CreateSamplerState(&samplerDesc, m_samplerState.ReleaseAndGetAddressOf()))) return false;

		// 텍스처 로드
		if (!LoadTexture()) return false;

		return true;
	}

	bool SwordRenderSystem::LoadTexture()
	{
		if (!m_resources || !m_device) return false;

		auto srv = m_resources->LoadData<ID3D11ShaderResourceView>("Resource/Image/Hanako.png", m_device.Get());
		if (!srv)
		{
			ALICE_LOG_WARN("[SwordRenderSystem] Failed to load texture: Resource/Image/Hanako.png");
			return false;
		}

		m_textureSRV = srv;
		ALICE_LOG_INFO("[SwordRenderSystem] Texture loaded: Resource/Image/Hanako.png");
		return true;
	}

	void SwordRenderSystem::Render(const World& world, const Camera& camera)
	{
		if (!m_vertexShader || !m_pixelShader || !m_inputLayout) return;

		XMMATRIX view = camera.GetViewMatrix();
		XMMATRIX proj = camera.GetProjectionMatrix();
		XMMATRIX viewProj = view * proj;

		// 파이프라인 설정
		m_context->IASetInputLayout(m_inputLayout.Get());
		m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
		m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);

		// 알파 블렌딩 활성화
		float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		m_context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xffffffff);

		// SwordEffectComponent를 가진 모든 엔티티 렌더링
		const auto& swordEffects = world.GetComponents<SwordEffectComponent>();
		for (const auto& [entityId, swordEffectComp] : swordEffects)
		{
			if (!swordEffectComp.enabled) continue;

			// 트레일 샘플이 2개 미만이면 스킵 (Triangle Strip 최소 요구)
			if (swordEffectComp.trailSamples.size() < 2) continue;

			// 버텍스 데이터 생성 (Triangle Strip)
			std::vector<TrailVertex> vertices;
			const auto& samples = swordEffectComp.trailSamples;
			vertices.reserve(samples.size() * 2); // 각 샘플마다 루트/팁 2개 버텍스

			float totalLength = swordEffectComp.totalLength;
			if (totalLength <= 0.0f)
			{
				// 길이를 다시 계산 (fallback)
				totalLength = 0.0f;
				for (size_t i = 1; i < samples.size(); ++i)
				{
					XMVECTOR v0 = XMLoadFloat3(&samples[i - 1].tipPos);
					XMVECTOR v1 = XMLoadFloat3(&samples[i].tipPos);
					XMVECTOR diff = XMVectorSubtract(v1, v0);
					totalLength += XMVectorGetX(XMVector3Length(diff));
				}
				totalLength = std::max(totalLength, 0.01f); // 0으로 나누기 방지
			}

			// Triangle Strip 생성: 각 샘플마다 루트/팁 쌍 생성
			for (size_t i = 0; i < samples.size(); ++i)
			{
				const auto& sample = samples[i];
				
				// UV 계산 (길이 누적 기반)
				float u = (totalLength > 0.0f) ? (sample.length / totalLength) : (static_cast<float>(i) / static_cast<float>(samples.size()));

				// 루트 버텍스 (v=0)
				TrailVertex rootVertex;
				rootVertex.position = sample.rootPos;
				rootVertex.texCoord = DirectX::XMFLOAT2(u, 0.0f);
				rootVertex.birthTime = sample.birthTime;
				vertices.push_back(rootVertex);

				// 팁 버텍스 (v=1)
				TrailVertex tipVertex;
				tipVertex.position = sample.tipPos;
				tipVertex.texCoord = DirectX::XMFLOAT2(u, 1.0f);
				tipVertex.birthTime = sample.birthTime;
				vertices.push_back(tipVertex);
			}

			if (vertices.empty()) continue;

			// Vertex Buffer 업데이트
			if (!EnsureVertexBufferSize(vertices.size())) continue;

			D3D11_MAPPED_SUBRESOURCE mapped;
			if (FAILED(m_context->Map(m_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) continue;
			std::memcpy(mapped.pData, vertices.data(), vertices.size() * sizeof(TrailVertex));
			m_context->Unmap(m_vertexBuffer.Get(), 0);

			// 엔티티의 TransformComponent 가져오기 (월드 행렬 계산용)
			const TransformComponent* transform = world.GetComponent<TransformComponent>(entityId);
			XMMATRIX worldMatrix = XMMatrixIdentity();
			if (transform)
			{
				worldMatrix = BuildWorldMatrix(*transform);
			}

			// VS 상수 버퍼 업데이트
			CBPerSwordEffectVS cbVS;
			cbVS.viewProj = XMMatrixTranspose(viewProj);
			cbVS.world = XMMatrixTranspose(worldMatrix);
			cbVS.uv = XMFLOAT2(0.0f, 0.0f); // 기본 UV
			cbVS.currentTime = swordEffectComp.currentTime;
			cbVS.fadeDuration = swordEffectComp.fadeDuration;
			cbVS.width = 0.5f; // 기본 폭
			m_context->UpdateSubresource(m_cbPerSwordEffectVS.Get(), 0, nullptr, &cbVS, 0, 0);

			// PS 상수 버퍼 업데이트
			CBPerSwordEffectPS cbPS;
			cbPS.color = swordEffectComp.color;
			cbPS.fadeDuration = swordEffectComp.fadeDuration;
			cbPS.width = 0.5f; // 기본 폭
			m_context->UpdateSubresource(m_cbPerSwordEffectPS.Get(), 0, nullptr, &cbPS, 0, 0);

			// 텍스처 및 샘플러 바인딩 (register(t20))
			ID3D11ShaderResourceView* textureSRV = m_textureSRV.Get();
			m_context->PSSetShaderResources(20, 1, &textureSRV);
			ID3D11SamplerState* sampler = m_samplerState.Get();
			m_context->PSSetSamplers(0, 1, &sampler);

			// Vertex Buffer 바인딩
			UINT stride = sizeof(TrailVertex), offset = 0;
			ID3D11Buffer* vb = m_vertexBuffer.Get();
			m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
			m_context->VSSetConstantBuffers(0, 1, m_cbPerSwordEffectVS.GetAddressOf());
			m_context->PSSetConstantBuffers(1, 1, m_cbPerSwordEffectPS.GetAddressOf());

			// 렌더링 (Triangle Strip: vertexCount - 2 개의 삼각형)
			m_context->Draw((UINT)vertices.size(), 0);
		}

		// 블렌드 스테이트 복원
		m_context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
	}

	bool SwordRenderSystem::CreateShadersAndInputLayout()
	{
		ComPtr<ID3DBlob> vsBlob, psBlob;

		// 1. VS 컴파일 및 생성
		if (FAILED(D3DCompile(SwordEffectShader::g_SwordEffectVS, std::strlen(SwordEffectShader::g_SwordEffectVS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), nullptr))) return false;
		if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_vertexShader.ReleaseAndGetAddressOf()))) return false;

		// 2. PS 컴파일 및 생성
		if (FAILED(D3DCompile(SwordEffectShader::g_SwordEffectPS, std::strlen(SwordEffectShader::g_SwordEffectPS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), nullptr))) return false;
		if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_pixelShader.ReleaseAndGetAddressOf()))) return false;

		// 3. Input Layout 생성
		D3D11_INPUT_ELEMENT_DESC desc[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,                            D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,      0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR",    0, DXGI_FORMAT_R32_FLOAT,         0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};

		if (FAILED(m_device->CreateInputLayout(desc, (UINT)std::size(desc), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_inputLayout.ReleaseAndGetAddressOf()))) return false;

		return true;
	}

	bool SwordRenderSystem::EnsureVertexBufferSize(std::size_t vertexCount)
	{
		if (vertexCount <= m_vertexCapacity) return true;

		m_vertexBuffer.Reset();

		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = (UINT)(vertexCount * sizeof(TrailVertex));
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		if (FAILED(m_device->CreateBuffer(&desc, nullptr, m_vertexBuffer.ReleaseAndGetAddressOf()))) return false;

		m_vertexCapacity = vertexCount;
		return true;
	}

	XMMATRIX SwordRenderSystem::BuildWorldMatrix(const TransformComponent& transform) const
	{
		XMVECTOR scale = XMLoadFloat3(&transform.scale);
		XMVECTOR rotation = XMLoadFloat3(&transform.rotation);
		XMVECTOR translation = XMLoadFloat3(&transform.position);

		XMMATRIX S = XMMatrixScalingFromVector(scale);
		XMMATRIX R = XMMatrixRotationRollPitchYawFromVector(rotation);
		XMMATRIX T = XMMatrixTranslationFromVector(translation);

		return S * R * T;
	}
}
