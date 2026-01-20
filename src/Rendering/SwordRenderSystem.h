#pragma once

#include <vector>
#include <wrl/client.h>
#include <d3d11.h>
#include <DirectXMath.h>

#include "Rendering/Camera.h"
#include "Rendering/D3D11/ID3D11RenderDevice.h"
#include "Core/World.h"

namespace Alice
{
	/// 검기 이펙트 시스템 (스플라인 라인 렌더링)
	class SwordRenderSystem
	{
	public:
		SwordRenderSystem() = default;
		explicit SwordRenderSystem(ID3D11RenderDevice& renderDevice);
		~SwordRenderSystem() = default;

		/// 셰이더, 버퍼 등을 초기화합니다.
		bool Initialize();

		/// World에서 SwordEffectComponent를 가진 모든 엔티티를 렌더링합니다.
		void Render(const World& world, const Camera& camera);

	private:
		struct SplineVertex
		{
			DirectX::XMFLOAT3 position;
			float alpha;
		};

		struct CBPerSwordEffect
		{
			DirectX::XMMATRIX viewProj;
			DirectX::XMFLOAT3 color;
		};

		bool CreateShadersAndInputLayout();
		bool EnsureVertexBufferSize(std::size_t vertexCount);

	private:
		ID3D11RenderDevice& m_renderDevice;

		Microsoft::WRL::ComPtr<ID3D11Device>        m_device;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;

		Microsoft::WRL::ComPtr<ID3D11Buffer>        m_vertexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer>        m_cbPerSwordEffect;
		Microsoft::WRL::ComPtr<ID3D11VertexShader>  m_vertexShader;
		Microsoft::WRL::ComPtr<ID3D11PixelShader>   m_pixelShader;
		Microsoft::WRL::ComPtr<ID3D11InputLayout>   m_inputLayout;
		Microsoft::WRL::ComPtr<ID3D11BlendState>     m_blendState; // 알파 블렌딩용

		std::size_t m_vertexCapacity{ 0 };
	};
}
