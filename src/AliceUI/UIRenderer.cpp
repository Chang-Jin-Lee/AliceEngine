#include "AliceUI/UIRenderer.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <filesystem>
#include <cfloat>
#include <cstring>

#include "AliceUI/UIShaderCode.h"
#include "AliceUI/UITransformComponent.h"
#include "AliceUI/UIWidgetComponent.h"
#include "AliceUI/UIImageComponent.h"
#include "AliceUI/UITextComponent.h"
#include "AliceUI/UIButtonComponent.h"
#include "AliceUI/UIGaugeComponent.h"

#include "Core/World.h"
#include "Core/InputSystem.h"
#include "Core/Logger.h"
#include "Core/ResourceManager.h"
#include "Components/TransformComponent.h"
#include "Rendering/Camera.h"

namespace Alice
{
	namespace
	{
		constexpr std::size_t kMaxVerts = 4096;
		constexpr std::size_t kMaxQuads = kMaxVerts / 4;

		inline bool IsZero(const DirectX::XMFLOAT2& v)
		{
			return v.x == 0.0f && v.y == 0.0f;
		}
	}

	bool UIRenderer::Initialize(ID3D11Device* device, ID3D11DeviceContext* context, ResourceManager* resources)
	{
		if (!device || !context)
			return false;

		m_device = device;
		m_context = context;
		m_resources = resources;

		// Shader compile
		Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
		Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
		Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

		if (FAILED(D3DCompile(AliceUIShader::UIVS, strlen(AliceUIShader::UIVS),
			"UIVS", nullptr, nullptr, "main", "vs_5_0", 0, 0,
			vsBlob.GetAddressOf(), errorBlob.GetAddressOf())))
		{
			if (errorBlob)
				ALICE_LOG_ERRORF("[AliceUI] VS compile failed: %s", (char*)errorBlob->GetBufferPointer());
			return false;
		}
		if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_vs.ReleaseAndGetAddressOf())))
		{
			ALICE_LOG_ERRORF("[AliceUI] CreateVertexShader failed.");
			return false;
		}

		D3D11_INPUT_ELEMENT_DESC layout[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};
		if (FAILED(m_device->CreateInputLayout(layout, ARRAYSIZE(layout),
			vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_layout.ReleaseAndGetAddressOf())))
		{
			ALICE_LOG_ERRORF("[AliceUI] CreateInputLayout failed.");
			return false;
		}

		if (FAILED(D3DCompile(AliceUIShader::UIPixelPS, strlen(AliceUIShader::UIPixelPS),
			"UIPixelPS", nullptr, nullptr, "main", "ps_5_0", 0, 0,
			psBlob.ReleaseAndGetAddressOf(), errorBlob.ReleaseAndGetAddressOf())))
		{
			if (errorBlob)
				ALICE_LOG_ERRORF("[AliceUI] PS compile failed: %s", (char*)errorBlob->GetBufferPointer());
			return false;
		}
		if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_psDefault.ReleaseAndGetAddressOf())))
		{
			ALICE_LOG_ERRORF("[AliceUI] CreatePixelShader failed.");
			return false;
		}

		psBlob.Reset();
		errorBlob.Reset();
		if (FAILED(D3DCompile(AliceUIShader::UIGrayPS, strlen(AliceUIShader::UIGrayPS),
			"UIGrayPS", nullptr, nullptr, "main", "ps_5_0", 0, 0,
			psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
		{
			if (errorBlob)
				ALICE_LOG_ERRORF("[AliceUI] Gray PS compile failed: %s", (char*)errorBlob->GetBufferPointer());
			return false;
		}
		if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_psGray.ReleaseAndGetAddressOf())))
		{
			ALICE_LOG_ERRORF("[AliceUI] CreateGrayPixelShader failed.");
			return false;
		}

		// Constant buffer
		D3D11_BUFFER_DESC cbDesc{};
		cbDesc.ByteWidth = sizeof(UIConstants);
		cbDesc.Usage = D3D11_USAGE_DYNAMIC;
		cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_cbUI.ReleaseAndGetAddressOf())))
		{
			ALICE_LOG_ERRORF("[AliceUI] CreateConstantBuffer failed.");
			return false;
		}

		// Dynamic VB
		D3D11_BUFFER_DESC vbDesc{};
		vbDesc.ByteWidth = static_cast<UINT>(sizeof(UIVertex) * kMaxVerts);
		vbDesc.Usage = D3D11_USAGE_DYNAMIC;
		vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(m_device->CreateBuffer(&vbDesc, nullptr, m_vb.ReleaseAndGetAddressOf())))
		{
			ALICE_LOG_ERRORF("[AliceUI] CreateVertexBuffer failed.");
			return false;
		}
		m_vbStride = sizeof(UIVertex);

		// Index buffer
		std::vector<uint16_t> indices;
		indices.reserve(kMaxQuads * 6);
		for (uint16_t i = 0; i < kMaxQuads; ++i)
		{
			const uint16_t base = static_cast<uint16_t>(i * 4);
			indices.push_back(base + 0);
			indices.push_back(base + 1);
			indices.push_back(base + 2);
			indices.push_back(base + 2);
			indices.push_back(base + 1);
			indices.push_back(base + 3);
		}
		D3D11_BUFFER_DESC ibDesc{};
		ibDesc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint16_t));
		ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
		ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		D3D11_SUBRESOURCE_DATA ibData{};
		ibData.pSysMem = indices.data();
		if (FAILED(m_device->CreateBuffer(&ibDesc, &ibData, m_ib.ReleaseAndGetAddressOf())))
		{
			ALICE_LOG_ERRORF("[AliceUI] CreateIndexBuffer failed.");
			return false;
		}

		// Sampler
		D3D11_SAMPLER_DESC samp{};
		samp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samp.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samp.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samp.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(m_device->CreateSamplerState(&samp, m_sampler.ReleaseAndGetAddressOf())))
		{
			ALICE_LOG_ERRORF("[AliceUI] CreateSamplerState failed.");
			return false;
		}

		// Blend (Alpha)
		D3D11_BLEND_DESC blend{};
		blend.RenderTarget[0].BlendEnable = TRUE;
		blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(m_device->CreateBlendState(&blend, m_blendAlpha.ReleaseAndGetAddressOf())))
		{
			ALICE_LOG_ERRORF("[AliceUI] CreateBlendState failed.");
			return false;
		}

		// Depth states
		D3D11_DEPTH_STENCIL_DESC dsOff{};
		dsOff.DepthEnable = FALSE;
		dsOff.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		dsOff.DepthFunc = D3D11_COMPARISON_ALWAYS;
		if (FAILED(m_device->CreateDepthStencilState(&dsOff, m_depthOff.ReleaseAndGetAddressOf())))
		{
			ALICE_LOG_ERRORF("[AliceUI] CreateDepthOff failed.");
			return false;
		}

		D3D11_DEPTH_STENCIL_DESC dsRead{};
		dsRead.DepthEnable = TRUE;
		dsRead.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		dsRead.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		if (FAILED(m_device->CreateDepthStencilState(&dsRead, m_depthRead.ReleaseAndGetAddressOf())))
		{
			ALICE_LOG_ERRORF("[AliceUI] CreateDepthRead failed.");
			return false;
		}

		// Rasterizer
		D3D11_RASTERIZER_DESC rs{};
		rs.FillMode = D3D11_FILL_SOLID;
		rs.CullMode = D3D11_CULL_NONE;
		rs.DepthClipEnable = TRUE;
		if (FAILED(m_device->CreateRasterizerState(&rs, m_rsNoCull.ReleaseAndGetAddressOf())))
		{
			ALICE_LOG_ERRORF("[AliceUI] CreateRasterizerState failed.");
			return false;
		}

		// White texture
		const uint32_t white = 0xFFFFFFFFu;
		D3D11_TEXTURE2D_DESC texDesc{};
		texDesc.Width = 1;
		texDesc.Height = 1;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		texDesc.SampleDesc.Count = 1;
		texDesc.Usage = D3D11_USAGE_IMMUTABLE;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA texData{};
		texData.pSysMem = &white;
		texData.SysMemPitch = sizeof(uint32_t);
		Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
		if (FAILED(m_device->CreateTexture2D(&texDesc, &texData, tex.ReleaseAndGetAddressOf())))
		{
			ALICE_LOG_ERRORF("[AliceUI] CreateWhiteTexture failed.");
			return false;
		}
		if (FAILED(m_device->CreateShaderResourceView(tex.Get(), nullptr, m_whiteSRV.ReleaseAndGetAddressOf())))
		{
			ALICE_LOG_ERRORF("[AliceUI] CreateWhiteSRV failed.");
			return false;
		}

		return true;
	}

	void UIRenderer::Shutdown()
	{
		m_textureCache.clear();
		m_customPS.clear();
		m_screenLayouts.clear();
		m_screenRects.clear();

		m_whiteSRV.Reset();
		m_vs.Reset();
		m_psDefault.Reset();
		m_psGray.Reset();
		m_layout.Reset();
		m_cbUI.Reset();
		m_vb.Reset();
		m_ib.Reset();
		m_sampler.Reset();
		m_blendAlpha.Reset();
		m_rsNoCull.Reset();
		m_depthOff.Reset();
		m_depthRead.Reset();
	}

	bool UIRenderer::RegisterShader(const std::string& name, const char* pixelShaderSource)
	{
		if (name.empty() || !pixelShaderSource || !m_device)
			return false;

		Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
		Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
		if (FAILED(D3DCompile(pixelShaderSource, strlen(pixelShaderSource),
			name.c_str(), nullptr, nullptr, "main", "ps_5_0", 0, 0,
			psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
		{
			if (errorBlob)
				ALICE_LOG_ERRORF("[AliceUI] PS compile failed (%s): %s", name.c_str(), (char*)errorBlob->GetBufferPointer());
			return false;
		}

		Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;
		if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, ps.ReleaseAndGetAddressOf())))
		{
			ALICE_LOG_ERRORF("[AliceUI] CreatePixelShader failed (%s).", name.c_str());
			return false;
		}

		m_customPS[name] = ps;
		return true;
	}

	void UIRenderer::Update(World& world, InputSystem& input, const Camera& /*camera*/, float screenW, float screenH)
	{
		BuildScreenLayout(world, screenW, screenH);
		UpdateButtonStates(world, input, screenW, screenH);

		for (auto [id, gauge] : world.GetComponents<UIGaugeComponent>())
		{
			float target = gauge.normalized ? gauge.value : (gauge.value - gauge.minValue) / std::max(0.0001f, gauge.maxValue - gauge.minValue);
			target = AliceUI::Clamp01(target);

			if (gauge.smoothing > 0.0f)
			{
				gauge.displayedValue = gauge.displayedValue + (target - gauge.displayedValue) * gauge.smoothing;
			}
			else
			{
				gauge.displayedValue = target;
			}
		}
	}

	void UIRenderer::RenderScreen(const World& world, const Camera& camera, ID3D11RenderTargetView* targetRTV, float screenW, float screenH)
	{
		if (!targetRTV || !m_context || !m_vs)
			return;

		BuildScreenLayout(world, screenW, screenH);

		D3D11_VIEWPORT vp{};
		vp.Width = screenW;
		vp.Height = screenH;
		vp.MaxDepth = 1.0f;
		m_context->RSSetViewports(1, &vp);

		m_context->OMSetRenderTargets(1, &targetRTV, nullptr);
		float blendFactor[4] = { 0, 0, 0, 0 };
		m_context->OMSetBlendState(m_blendAlpha.Get(), blendFactor, 0xFFFFFFFF);
		m_context->OMSetDepthStencilState(m_depthOff.Get(), 0);
		m_context->RSSetState(m_rsNoCull.Get());

		UIConstants cb{};
		cb.viewProj = DirectX::XMMatrixTranspose(
			DirectX::XMMatrixOrthographicOffCenterLH(0.0f, screenW, screenH, 0.0f, 0.0f, 1.0f));

		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (SUCCEEDED(m_context->Map(m_cbUI.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			memcpy(mapped.pData, &cb, sizeof(cb));
			m_context->Unmap(m_cbUI.Get(), 0);
		}

		m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_context->IASetInputLayout(m_layout.Get());

		m_context->VSSetShader(m_vs.Get(), nullptr, 0);
		ID3D11Buffer* cbuffers[] = { m_cbUI.Get() };
		m_context->VSSetConstantBuffers(0, 1, cbuffers);

		std::vector<EntityId> drawList;
		drawList.reserve(m_screenLayouts.size());
		for (const auto& [id, widget] : world.GetComponents<UIWidgetComponent>())
		{
			if (widget.space != AliceUI::UISpace::Screen)
				continue;
			if (widget.visibility == AliceUI::UIVisibility::Collapsed)
				continue;
			if (m_screenLayouts.find(id) == m_screenLayouts.end())
				continue;
			drawList.push_back(id);
		}

		std::sort(drawList.begin(), drawList.end(), [&](EntityId a, EntityId b)
		{
			const auto* ta = world.GetComponent<UITransformComponent>(a);
			const auto* tb = world.GetComponent<UITransformComponent>(b);
			const int sa = ta ? ta->sortOrder : 0;
			const int sb = tb ? tb->sortOrder : 0;
			if (sa != sb) return sa < sb;
			return a < b;
		});

		for (EntityId id : drawList)
		{
			const auto* widget = world.GetComponent<UIWidgetComponent>(id);
			if (!widget || widget->visibility != AliceUI::UIVisibility::Visible)
				continue;

			ScreenLayout layout{};
			if (!GetScreenLayout(id, layout))
				continue;

			const auto* button = world.GetComponent<UIButtonComponent>(id);
			const auto* gauge = world.GetComponent<UIGaugeComponent>(id);
			const auto* text = world.GetComponent<UITextComponent>(id);
			const auto* image = world.GetComponent<UIImageComponent>(id);

			if (gauge)
			{
				RenderGauge(world, id, layout);
				if (text)
					RenderText(world, id, layout);
				continue;
			}

			if (button)
			{
				DirectX::XMFLOAT4 tint = button->normalTint;
				std::string texture = button->normalTexture;

				switch (button->state)
				{
				case AliceUI::UIButtonState::Hovered: tint = button->hoveredTint; texture = button->hoveredTexture; break;
				case AliceUI::UIButtonState::Pressed: tint = button->pressedTint; texture = button->pressedTexture; break;
				case AliceUI::UIButtonState::Disabled: tint = button->disabledTint; texture = button->disabledTexture; break;
				default: break;
				}

				RenderImage(world, id, layout, tint, texture);
				if (text)
					RenderText(world, id, layout);
				continue;
			}

			if (image)
			{
				RenderImage(world, id, layout, DirectX::XMFLOAT4(1, 1, 1, 1), "");
			}

			if (text)
			{
				RenderText(world, id, layout);
			}
		}
	}

	void UIRenderer::RenderWorld(const World& world, const Camera& camera, ID3D11RenderTargetView* targetRTV, ID3D11DepthStencilView* dsv)
	{
		if (!targetRTV || !m_context || !m_vs)
			return;

		m_context->OMSetRenderTargets(1, &targetRTV, dsv);
		float blendFactor[4] = { 0, 0, 0, 0 };
		m_context->OMSetBlendState(m_blendAlpha.Get(), blendFactor, 0xFFFFFFFF);
		m_context->OMSetDepthStencilState(m_depthRead.Get(), 0);
		m_context->RSSetState(m_rsNoCull.Get());

		UIConstants cb{};
		cb.viewProj = DirectX::XMMatrixTranspose(camera.GetViewProjectionMatrix());

		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (SUCCEEDED(m_context->Map(m_cbUI.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			memcpy(mapped.pData, &cb, sizeof(cb));
			m_context->Unmap(m_cbUI.Get(), 0);
		}

		m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_context->IASetInputLayout(m_layout.Get());
		m_context->VSSetShader(m_vs.Get(), nullptr, 0);
		ID3D11Buffer* cbuffers[] = { m_cbUI.Get() };
		m_context->VSSetConstantBuffers(0, 1, cbuffers);

		for (const auto& [id, widget] : world.GetComponents<UIWidgetComponent>())
		{
			if (widget.space != AliceUI::UISpace::World)
				continue;
			if (widget.visibility != AliceUI::UIVisibility::Visible)
				continue;

			const auto* uiTransform = world.GetComponent<UITransformComponent>(id);
			if (!uiTransform)
				continue;

			const auto* image = world.GetComponent<UIImageComponent>(id);
			const auto* text = world.GetComponent<UITextComponent>(id);
			const auto* gauge = world.GetComponent<UIGaugeComponent>(id);
			const auto* button = world.GetComponent<UIButtonComponent>(id);

			ScreenLayout layout{};
			layout.size = DirectX::XMFLOAT2(
				uiTransform->size.x * uiTransform->scale.x,
				uiTransform->size.y * uiTransform->scale.y);
			layout.pivot = ResolvePivot(*uiTransform);
			layout.pivotBaked = false;

			DirectX::XMMATRIX worldM = world.ComputeWorldMatrix(id);

			if (widget.billboard)
			{
				DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, camera.GetViewMatrix());
				DirectX::XMVECTOR right = invView.r[0];
				DirectX::XMVECTOR up = invView.r[1];
				DirectX::XMVECTOR forward = invView.r[2];
				DirectX::XMVECTOR pos = worldM.r[3];

				DirectX::XMMATRIX billboard = DirectX::XMMatrixIdentity();
				billboard.r[0] = right;
				billboard.r[1] = up;
				billboard.r[2] = forward;
				billboard.r[3] = pos;
				layout.world = billboard;
			}
			else
			{
				layout.world = worldM;
			}

			if (gauge)
			{
				RenderGauge(world, id, layout);
				if (text)
					RenderText(world, id, layout);
				continue;
			}

			if (button)
			{
				DirectX::XMFLOAT4 tint = button->normalTint;
				std::string texture = button->normalTexture;
				switch (button->state)
				{
				case AliceUI::UIButtonState::Hovered: tint = button->hoveredTint; texture = button->hoveredTexture; break;
				case AliceUI::UIButtonState::Pressed: tint = button->pressedTint; texture = button->pressedTexture; break;
				case AliceUI::UIButtonState::Disabled: tint = button->disabledTint; texture = button->disabledTexture; break;
				default: break;
				}
				RenderImage(world, id, layout, tint, texture);
				if (text)
					RenderText(world, id, layout);
				continue;
			}

			if (image)
				RenderImage(world, id, layout, DirectX::XMFLOAT4(1, 1, 1, 1), "");
			if (text)
				RenderText(world, id, layout);
		}
	}

	void UIRenderer::BuildScreenLayout(const World& world, float screenW, float screenH)
	{
		m_screenLayouts.clear();
		m_screenRects.clear();

		std::vector<EntityId> roots;
		for (const auto& [id, widget] : world.GetComponents<UIWidgetComponent>())
		{
			if (widget.space != AliceUI::UISpace::Screen)
				continue;
			EntityId parent = world.GetParent(id);
			const auto* parentWidget = world.GetComponent<UIWidgetComponent>(parent);
			if (!parentWidget || parentWidget->space != AliceUI::UISpace::Screen)
				roots.push_back(id);
		}

		const DirectX::XMMATRIX identity = DirectX::XMMatrixIdentity();
		const DirectX::XMFLOAT2 screenSize(screenW, screenH);
		for (EntityId root : roots)
		{
			BuildScreenLayoutRecursive(world, root, identity, screenSize);
		}
	}

	void UIRenderer::BuildScreenLayoutRecursive(const World& world,
		EntityId id,
		const DirectX::XMMATRIX& parent,
		const DirectX::XMFLOAT2& parentSize)
	{
		const auto* widget = world.GetComponent<UIWidgetComponent>(id);
		const auto* transform = world.GetComponent<UITransformComponent>(id);
		if (!widget || !transform)
			return;

		if (widget->visibility == AliceUI::UIVisibility::Collapsed)
			return;

		DirectX::XMFLOAT2 size;
		DirectX::XMFLOAT2 pivot;
		DirectX::XMMATRIX local = BuildScreenLocalMatrix(*transform, parentSize, size, pivot);
		const DirectX::XMMATRIX worldM = local * parent;

		ScreenLayout layout{};
		layout.world = worldM;
		layout.size = size;
		layout.pivot = pivot;
		layout.pivotBaked = true;
		m_screenLayouts[id] = layout;

		// AABB (for input)
		DirectX::XMFLOAT3 corners[4] = {
			DirectX::XMFLOAT3(0, 0, 0),
			DirectX::XMFLOAT3(size.x, 0, 0),
			DirectX::XMFLOAT3(0, size.y, 0),
			DirectX::XMFLOAT3(size.x, size.y, 0)
		};

		ScreenRect rect{};
		rect.minX = rect.minY = FLT_MAX;
		rect.maxX = rect.maxY = -FLT_MAX;
		for (int i = 0; i < 4; ++i)
		{
			DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&corners[i]), worldM);
			DirectX::XMFLOAT3 out;
			DirectX::XMStoreFloat3(&out, p);
			rect.minX = std::min(rect.minX, out.x);
			rect.minY = std::min(rect.minY, out.y);
			rect.maxX = std::max(rect.maxX, out.x);
			rect.maxY = std::max(rect.maxY, out.y);
		}
		m_screenRects[id] = rect;

		for (EntityId child : world.GetChildren(id))
		{
			const auto* childWidget = world.GetComponent<UIWidgetComponent>(child);
			if (!childWidget || childWidget->space != AliceUI::UISpace::Screen)
				continue;
			BuildScreenLayoutRecursive(world, child, worldM, size);
		}
	}

	bool UIRenderer::GetScreenLayout(EntityId id, ScreenLayout& out) const
	{
		auto it = m_screenLayouts.find(id);
		if (it == m_screenLayouts.end())
			return false;
		out = it->second;
		return true;
	}

	bool UIRenderer::GetScreenRect(EntityId id, ScreenRect& out) const
	{
		auto it = m_screenRects.find(id);
		if (it == m_screenRects.end())
			return false;
		out = it->second;
		return true;
	}

	void UIRenderer::UpdateButtonStates(World& world, InputSystem& input, float /*screenW*/, float /*screenH*/)
	{
		const POINT mouse = input.GetMousePosition();
		const bool leftDown = input.IsLeftButtonDown();
		const bool leftPressed = input.IsMouseButtonPressed(0);
		const bool leftReleased = input.IsMouseButtonReleased(0);

		for (auto [id, button] : world.GetComponents<UIButtonComponent>())
		{
			button.clicked = false;

			const auto* widget = world.GetComponent<UIWidgetComponent>(id);
			if (!widget || widget->space != AliceUI::UISpace::Screen || widget->visibility != AliceUI::UIVisibility::Visible)
			{
				button.state = AliceUI::UIButtonState::Normal;
				button.wasPressed = false;
				continue;
			}

			if (!button.enabled || !widget->interactable)
			{
				button.state = AliceUI::UIButtonState::Disabled;
				button.wasPressed = false;
				continue;
			}

			ScreenRect rect{};
			if (!GetScreenRect(id, rect))
			{
				button.state = AliceUI::UIButtonState::Normal;
				button.wasPressed = false;
				continue;
			}

			const bool hovered = (mouse.x >= rect.minX && mouse.x <= rect.maxX &&
				mouse.y >= rect.minY && mouse.y <= rect.maxY);

			if (hovered && leftPressed)
				button.wasPressed = true;

			if (leftReleased)
			{
				if (hovered && button.wasPressed)
					button.clicked = true;
				button.wasPressed = false;
			}

			if (hovered && leftDown)
				button.state = AliceUI::UIButtonState::Pressed;
			else if (hovered)
				button.state = AliceUI::UIButtonState::Hovered;
			else
				button.state = AliceUI::UIButtonState::Normal;
		}
	}

	void UIRenderer::RenderImage(const World& world, EntityId id, const ScreenLayout& layout, const DirectX::XMFLOAT4& tintOverride, const std::string& overrideTexture)
	{
		const auto* widget = world.GetComponent<UIWidgetComponent>(id);
		if (!widget)
			return;

		const auto* image = world.GetComponent<UIImageComponent>(id);

		DirectX::XMFLOAT4 baseColor(1, 1, 1, 1);
		DirectX::XMFLOAT4 uvRect(0, 0, 1, 1);
		std::string texPath = overrideTexture;

		if (image)
		{
			baseColor = image->color;
			uvRect = image->uvRect;
			if (texPath.empty())
				texPath = image->texturePath;
		}

		const DirectX::XMFLOAT4 color(
			baseColor.x * tintOverride.x,
			baseColor.y * tintOverride.y,
			baseColor.z * tintOverride.z,
			baseColor.w * tintOverride.w);

		const float originX = layout.pivotBaked ? 0.0f : -layout.pivot.x * layout.size.x;
		const float originY = layout.pivotBaked ? 0.0f : -layout.pivot.y * layout.size.y;
		const float left = originX;
		const float top = originY;
		const float right = originX + layout.size.x;
		const float bottom = originY + layout.size.y;

		DirectX::XMFLOAT3 corners[4] = {
			DirectX::XMFLOAT3(left, top, 0),
			DirectX::XMFLOAT3(right, top, 0),
			DirectX::XMFLOAT3(left, bottom, 0),
			DirectX::XMFLOAT3(right, bottom, 0)
		};

		UIVertex verts[4]{};
		for (int i = 0; i < 4; ++i)
		{
			DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&corners[i]), layout.world);
			DirectX::XMStoreFloat3(&verts[i].position, p);
			verts[i].color = color;
		}

		verts[0].uv = DirectX::XMFLOAT2(uvRect.x, uvRect.y);
		verts[1].uv = DirectX::XMFLOAT2(uvRect.z, uvRect.y);
		verts[2].uv = DirectX::XMFLOAT2(uvRect.x, uvRect.w);
		verts[3].uv = DirectX::XMFLOAT2(uvRect.z, uvRect.w);

		DrawQuad(verts, GetTexture(texPath), GetPixelShader(widget->shaderName));
	}

	void UIRenderer::RenderText(const World& world, EntityId id, const ScreenLayout& layout)
	{
		const auto* widget = world.GetComponent<UIWidgetComponent>(id);
		const auto* text = world.GetComponent<UITextComponent>(id);
		if (!widget || !text)
			return;
		if (text->fontPath.empty() || text->text.empty())
			return;
		if (!m_resources || !m_device)
			return;

		const UIFont* font = m_fontCache.Load(*m_resources, m_device, text->fontPath);
		if (!font || font->lineHeight <= 0.0f)
			return;

		auto fontTexture = m_fontCache.GetFontTexture(text->fontPath);
		ID3D11ShaderResourceView* srv = fontTexture.Get() ? fontTexture.Get() : m_whiteSRV.Get();

		const float scale = text->fontSize / font->lineHeight;
		const float lineHeight = font->lineHeight * scale + text->lineSpacing;
		const float maxWidth = (text->maxWidth > 0.0f) ? text->maxWidth : (layout.size.x > 0.0f ? layout.size.x : 0.0f);
		const bool wrap = text->wrap && maxWidth > 0.0f;

		std::vector<float> lineWidths;
		float lineWidth = 0.0f;
		float maxLineWidth = 0.0f;
		int lineCount = 1;

		for (char c : text->text)
		{
			if (c == '\n')
			{
				lineWidths.push_back(lineWidth);
				maxLineWidth = std::max(maxLineWidth, lineWidth);
				lineWidth = 0.0f;
				++lineCount;
				continue;
			}

			auto it = font->glyphs.find(static_cast<int>(static_cast<unsigned char>(c)));
			if (it == font->glyphs.end())
				continue;

			const float adv = it->second.xAdvance * scale;
			if (wrap && lineWidth + adv > maxWidth && lineWidth > 0.0f)
			{
				lineWidths.push_back(lineWidth);
				maxLineWidth = std::max(maxLineWidth, lineWidth);
				lineWidth = 0.0f;
				++lineCount;
			}
			lineWidth += adv;
		}
		lineWidths.push_back(lineWidth);
		maxLineWidth = std::max(maxLineWidth, lineWidth);

		const float textWidth = maxLineWidth;
		const float textHeight = lineCount * lineHeight;

		const float originX = layout.pivotBaked ? 0.0f : -layout.pivot.x * layout.size.x;
		const float originY = layout.pivotBaked ? 0.0f : -layout.pivot.y * layout.size.y;

		float baseX = originX;
		float baseY = originY;
		if (layout.size.x > 0.0f)
		{
			if (text->alignH == AliceUI::UIAlignH::Center)
				baseX = (layout.size.x - textWidth) * 0.5f;
			else if (text->alignH == AliceUI::UIAlignH::Right)
				baseX = (layout.size.x - textWidth);
		}

		if (layout.size.y > 0.0f)
		{
			if (text->alignV == AliceUI::UIAlignV::Center)
				baseY = (layout.size.y - textHeight) * 0.5f;
			else if (text->alignV == AliceUI::UIAlignV::Bottom)
				baseY = (layout.size.y - textHeight);
		}

		std::vector<UIVertex> verts;
		verts.reserve(text->text.size() * 4);

		float x = baseX;
		float y = baseY;
		for (char c : text->text)
		{
			if (c == '\n')
			{
				x = baseX;
				y += lineHeight;
				continue;
			}

			auto it = font->glyphs.find(static_cast<int>(static_cast<unsigned char>(c)));
			if (it == font->glyphs.end())
				continue;

			const UIFontGlyph& g = it->second;
			const float adv = g.xAdvance * scale;
			if (wrap && maxWidth > 0.0f && x + adv > baseX + maxWidth && x > baseX)
			{
				x = baseX;
				y += lineHeight;
			}

			const float gx = x + g.xOffset * scale;
			const float gy = y + g.yOffset * scale;
			const float gw = g.w * scale;
			const float gh = g.h * scale;

			DirectX::XMFLOAT3 local[4] = {
				DirectX::XMFLOAT3(gx, gy, 0),
				DirectX::XMFLOAT3(gx + gw, gy, 0),
				DirectX::XMFLOAT3(gx, gy + gh, 0),
				DirectX::XMFLOAT3(gx + gw, gy + gh, 0)
			};

			UIVertex v[4]{};
			for (int i = 0; i < 4; ++i)
			{
				DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&local[i]), layout.world);
				DirectX::XMStoreFloat3(&v[i].position, p);
				v[i].color = text->color;
			}

			v[0].uv = DirectX::XMFLOAT2(g.u0, g.v0);
			v[1].uv = DirectX::XMFLOAT2(g.u1, g.v0);
			v[2].uv = DirectX::XMFLOAT2(g.u0, g.v1);
			v[3].uv = DirectX::XMFLOAT2(g.u1, g.v1);

			verts.push_back(v[0]);
			verts.push_back(v[1]);
			verts.push_back(v[2]);
			verts.push_back(v[3]);

			x += adv;
		}

		DrawGlyphs(verts, srv, GetPixelShader(widget->shaderName));
	}

	void UIRenderer::RenderGauge(const World& world, EntityId id, const ScreenLayout& layout)
	{
		const auto* widget = world.GetComponent<UIWidgetComponent>(id);
		const auto* gauge = world.GetComponent<UIGaugeComponent>(id);
		if (!widget || !gauge)
			return;

		DirectX::XMFLOAT4 bgColor = gauge->backgroundColor;
		DirectX::XMFLOAT4 fillColor = gauge->fillColor;

		const float originX = layout.pivotBaked ? 0.0f : -layout.pivot.x * layout.size.x;
		const float originY = layout.pivotBaked ? 0.0f : -layout.pivot.y * layout.size.y;

		// Background
		{
			UIVertex verts[4]{};
			DirectX::XMFLOAT3 corners[4] = {
				DirectX::XMFLOAT3(originX, originY, 0),
				DirectX::XMFLOAT3(originX + layout.size.x, originY, 0),
				DirectX::XMFLOAT3(originX, originY + layout.size.y, 0),
				DirectX::XMFLOAT3(originX + layout.size.x, originY + layout.size.y, 0)
			};
			for (int i = 0; i < 4; ++i)
			{
				DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&corners[i]), layout.world);
				DirectX::XMStoreFloat3(&verts[i].position, p);
				verts[i].color = bgColor;
			}
			verts[0].uv = DirectX::XMFLOAT2(0, 0);
			verts[1].uv = DirectX::XMFLOAT2(1, 0);
			verts[2].uv = DirectX::XMFLOAT2(0, 1);
			verts[3].uv = DirectX::XMFLOAT2(1, 1);

			DrawQuad(verts, GetTexture(gauge->backgroundTexture), GetPixelShader(widget->shaderName));
		}

		float ratio = gauge->displayedValue;
		ratio = AliceUI::Clamp01(ratio);
		if (ratio <= 0.0f)
			return;

		float x0 = originX, x1 = originX + layout.size.x;
		float y0 = originY, y1 = originY + layout.size.y;
		float u0 = 0.0f, u1 = 1.0f;
		float v0 = 0.0f, v1 = 1.0f;

		switch (gauge->direction)
		{
		case AliceUI::UIGaugeDirection::LeftToRight:
			x1 = originX + layout.size.x * ratio;
			u1 = ratio;
			break;
		case AliceUI::UIGaugeDirection::RightToLeft:
			x0 = originX + layout.size.x * (1.0f - ratio);
			u0 = 1.0f - ratio;
			break;
		case AliceUI::UIGaugeDirection::BottomToTop:
			y0 = originY + layout.size.y * (1.0f - ratio);
			v0 = 1.0f - ratio;
			break;
		case AliceUI::UIGaugeDirection::TopToBottom:
			y1 = originY + layout.size.y * ratio;
			v1 = ratio;
			break;
		}

		UIVertex verts[4]{};
		DirectX::XMFLOAT3 corners[4] = {
			DirectX::XMFLOAT3(x0, y0, 0),
			DirectX::XMFLOAT3(x1, y0, 0),
			DirectX::XMFLOAT3(x0, y1, 0),
			DirectX::XMFLOAT3(x1, y1, 0)
		};
		for (int i = 0; i < 4; ++i)
		{
			DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&corners[i]), layout.world);
			DirectX::XMStoreFloat3(&verts[i].position, p);
			verts[i].color = fillColor;
		}
		verts[0].uv = DirectX::XMFLOAT2(u0, v0);
		verts[1].uv = DirectX::XMFLOAT2(u1, v0);
		verts[2].uv = DirectX::XMFLOAT2(u0, v1);
		verts[3].uv = DirectX::XMFLOAT2(u1, v1);

		DrawQuad(verts, GetTexture(gauge->fillTexture), GetPixelShader(widget->shaderName));
	}

	void UIRenderer::DrawQuad(const UIVertex* verts, ID3D11ShaderResourceView* texture, ID3D11PixelShader* ps)
	{
		if (!verts)
			return;

		UIVertex temp[4] = { verts[0], verts[1], verts[2], verts[3] };
		std::vector<UIVertex> list(temp, temp + 4);
		DrawGlyphs(list, texture, ps);
	}

	void UIRenderer::DrawGlyphs(const std::vector<UIVertex>& verts, ID3D11ShaderResourceView* texture, ID3D11PixelShader* ps)
	{
		if (verts.empty())
			return;

		ID3D11ShaderResourceView* srv = texture ? texture : m_whiteSRV.Get();
		ID3D11PixelShader* pixel = ps ? ps : m_psDefault.Get();

		m_context->PSSetShader(pixel, nullptr, 0);
		ID3D11SamplerState* sampler = m_sampler.Get();
		m_context->PSSetSamplers(0, 1, &sampler);
		m_context->PSSetShaderResources(0, 1, &srv);

		std::size_t offset = 0;
		while (offset < verts.size())
		{
			std::size_t count = std::min(kMaxVerts, verts.size() - offset);
			count -= (count % 4);
			if (count == 0)
				break;

			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (SUCCEEDED(m_context->Map(m_vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				memcpy(mapped.pData, verts.data() + offset, sizeof(UIVertex) * count);
				m_context->Unmap(m_vb.Get(), 0);
			}

			UINT stride = m_vbStride;
			UINT offsetBytes = 0;
			ID3D11Buffer* vb = m_vb.Get();
			m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offsetBytes);
			m_context->IASetIndexBuffer(m_ib.Get(), DXGI_FORMAT_R16_UINT, 0);
			m_context->DrawIndexed(static_cast<UINT>((count / 4) * 6), 0, 0);

			offset += count;
		}
	}

	DirectX::XMMATRIX UIRenderer::BuildScreenLocalMatrix(const UITransformComponent& t, const DirectX::XMFLOAT2& refSize, DirectX::XMFLOAT2& outSize, DirectX::XMFLOAT2& outPivot) const
	{
		outPivot = ResolvePivot(t);

		outSize = t.size;
		DirectX::XMFLOAT2 anchorDelta(
			(t.anchorMax.x - t.anchorMin.x) * refSize.x,
			(t.anchorMax.y - t.anchorMin.y) * refSize.y);

		if (!IsZero(anchorDelta))
		{
			outSize.x += anchorDelta.x;
			outSize.y += anchorDelta.y;
		}

		DirectX::XMFLOAT2 pos(
			t.anchorMin.x * refSize.x + t.position.x,
			t.anchorMin.y * refSize.y + t.position.y);

		const DirectX::XMVECTOR scale = DirectX::XMVectorSet(t.scale.x, t.scale.y, 1.0f, 0.0f);
		const DirectX::XMVECTOR pivot = DirectX::XMVectorSet(outPivot.x * outSize.x, outPivot.y * outSize.y, 0.0f, 0.0f);
		const DirectX::XMVECTOR trans = DirectX::XMVectorSet(pos.x, pos.y, 0.0f, 1.0f);

		return DirectX::XMMatrixAffineTransformation2D(scale, pivot, t.rotationRad, trans);
	}

	DirectX::XMFLOAT2 UIRenderer::ResolvePivot(const UITransformComponent& t) const
	{
		if (t.useAlignment)
			return AliceUI::AlignToPivot(t.alignH, t.alignV);
		return t.pivot;
	}

	ID3D11ShaderResourceView* UIRenderer::GetTexture(const std::string& path)
	{
		if (path.empty())
			return m_whiteSRV.Get();

		auto it = m_textureCache.find(path);
		if (it != m_textureCache.end())
			return it->second.Get();

		if (!m_resources || !m_device)
			return m_whiteSRV.Get();

		auto tex = m_resources->Load<ID3D11ShaderResourceView>(path, m_device);
		m_textureCache[path] = tex;
		return tex.Get() ? tex.Get() : m_whiteSRV.Get();
	}

	ID3D11PixelShader* UIRenderer::GetPixelShader(const std::string& name) const
	{
		auto it = m_customPS.find(name);
		if (it != m_customPS.end() && it->second.Get())
			return it->second.Get();
		if (name == "Grayscale")
			return m_psGray.Get();
		return m_psDefault.Get();
	}
}
