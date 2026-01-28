#pragma once

#include <unordered_map>
#include <vector>
#include <wrl/client.h>
#include <DirectXMath.h>

#include "AliceUI/UICommon.h"
#include "AliceUI/UIFont.h"
#include "Core/Entity.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;
struct ID3D11ShaderResourceView;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;
struct ID3D11Buffer;
struct ID3D11SamplerState;
struct ID3D11BlendState;
struct ID3D11RasterizerState;
struct ID3D11DepthStencilState;

namespace Alice
{
	class World;
	class InputSystem;
	class Camera;
	class ResourceManager;

	struct UITransformComponent;
	struct UIWidgetComponent;

	class UIRenderer
	{
	public:
		bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context, ResourceManager* resources);
		void Shutdown();

		void Update(World& world, InputSystem& input, const Camera& camera, float screenW, float screenH);

		void RenderScreen(const World& world, const Camera& camera, ID3D11RenderTargetView* targetRTV, float screenW, float screenH);
		void RenderWorld(const World& world, const Camera& camera, ID3D11RenderTargetView* targetRTV, ID3D11DepthStencilView* dsv);

		// 셰이더 확장: 커스텀 픽셀 셰이더 등록
		bool RegisterShader(const std::string& name, const char* pixelShaderSource);

	private:
		struct ScreenLayout
		{
			DirectX::XMMATRIX world = DirectX::XMMatrixIdentity();
			DirectX::XMFLOAT2 size{ 0.0f, 0.0f };
			DirectX::XMFLOAT2 pivot{ 0.5f, 0.5f };
			bool pivotBaked = true;
		};

		struct ScreenRect
		{
			float minX = 0.0f;
			float minY = 0.0f;
			float maxX = 0.0f;
			float maxY = 0.0f;
		};

		struct UIVertex
		{
			DirectX::XMFLOAT3 position;
			DirectX::XMFLOAT2 uv;
			DirectX::XMFLOAT4 color;
		};

		struct UIConstants
		{
			DirectX::XMMATRIX viewProj;
		};

		void BuildScreenLayout(const World& world, float screenW, float screenH);
		void BuildScreenLayoutRecursive(const World& world,
			EntityId id,
			const DirectX::XMMATRIX& parent,
			const DirectX::XMFLOAT2& parentSize);
		bool GetScreenLayout(EntityId id, ScreenLayout& out) const;
		bool GetScreenRect(EntityId id, ScreenRect& out) const;

		void UpdateButtonStates(World& world, InputSystem& input, float screenW, float screenH);

		void RenderImage(const World& world, EntityId id, const ScreenLayout& layout, const DirectX::XMFLOAT4& tintOverride, const std::string& overrideTexture);
		void RenderText(const World& world, EntityId id, const ScreenLayout& layout);
		void RenderGauge(const World& world, EntityId id, const ScreenLayout& layout);

		void DrawQuad(const UIVertex* verts, ID3D11ShaderResourceView* texture, ID3D11PixelShader* ps);
		void DrawGlyphs(const std::vector<UIVertex>& verts, ID3D11ShaderResourceView* texture, ID3D11PixelShader* ps);

		DirectX::XMMATRIX BuildScreenLocalMatrix(const UITransformComponent& t, const DirectX::XMFLOAT2& refSize, DirectX::XMFLOAT2& outSize, DirectX::XMFLOAT2& outPivot) const;
		DirectX::XMFLOAT2 ResolvePivot(const UITransformComponent& t) const;

		ID3D11ShaderResourceView* GetTexture(const std::string& path);
		ID3D11PixelShader* GetPixelShader(const std::string& name) const;

		ID3D11Device* m_device = nullptr;
		ID3D11DeviceContext* m_context = nullptr;
		ResourceManager* m_resources = nullptr;

		std::unordered_map<EntityId, ScreenLayout> m_screenLayouts;
		std::unordered_map<EntityId, ScreenRect> m_screenRects;

		std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> m_textureCache;
		std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D11PixelShader>> m_customPS;
		UIFontCache m_fontCache;

		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_whiteSRV;

		Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vs;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> m_psDefault;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> m_psGray;
		Microsoft::WRL::ComPtr<ID3D11InputLayout> m_layout;
		Microsoft::WRL::ComPtr<ID3D11Buffer> m_cbUI;

		Microsoft::WRL::ComPtr<ID3D11Buffer> m_vb;
		Microsoft::WRL::ComPtr<ID3D11Buffer> m_ib;
		UINT m_vbStride = 0;

		Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampler;
		Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendAlpha;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rsNoCull;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthOff;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthRead;
	};
}
