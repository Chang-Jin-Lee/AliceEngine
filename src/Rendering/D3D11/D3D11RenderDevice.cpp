#include "Rendering/D3D11/D3D11RenderDevice.h"

#include <cassert>

namespace Alice
{
    // 내부에서 사용할 헬퍼 타입
    using Microsoft::WRL::ComPtr;

    bool D3D11RenderDevice::Initialize(HWND window, std::uint32_t width, std::uint32_t height)
    {
        // 1) 기본 상태값 저장
        m_width  = width;
        m_height = height;

        // 2) 스왑 체인 설명 구조체 설정
        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        swapChainDesc.BufferCount = 2; // 더블 버퍼링
        swapChainDesc.BufferDesc.Width  = width;
        swapChainDesc.BufferDesc.Height = height;
        swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.OutputWindow = window;
        swapChainDesc.SampleDesc.Count = 1; // 멀티 샘플링 없음(간단 버전)
        swapChainDesc.Windowed = TRUE;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        UINT createDeviceFlags = 0;
#ifdef _DEBUG
        // 디버그 빌드에서는 D3D 디버그 레이어를 활성화합니다.
        createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;

        // 3) 디바이스 + 스왑 체인을 한 번에 생성
        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr,                    // 기본 어댑터 사용
            D3D_DRIVER_TYPE_HARDWARE,   // 하드웨어 가속
            nullptr,                    // 소프트웨어 래스터라이저 미사용
            createDeviceFlags,
            &featureLevel,
            1,
            D3D11_SDK_VERSION,
            &swapChainDesc,
            m_swapChain.ReleaseAndGetAddressOf(),
            m_device.ReleaseAndGetAddressOf(),
            nullptr, // 실제 생성된 feature level은 필요 없으므로 nullptr
            m_immediateContext.ReleaseAndGetAddressOf()
        );
        if (FAILED(hr)) return false;

        // 4) 렌더 타깃 생성
        if (!CreateRenderTarget()) return false;

        // 5) 깊이/스텐실 버퍼 생성
        if (!CreateDepthStencil(m_width, m_height)) return false;

        // 6) 깊이 스텐실 상태 객체 생성 (한 번만 생성)
        D3D11_DEPTH_STENCIL_DESC dsDesc = {};
        dsDesc.DepthEnable = TRUE;
        dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        dsDesc.StencilEnable = FALSE;

        hr = m_device->CreateDepthStencilState(&dsDesc, m_depthStencilState.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        // 7) 래스터라이저 상태 생성
        //    - CCW를 앞면으로 간주 (우리 큐브 정점 데이터가 CCW 기준이기 때문)
        //    - 뒷면을 컬링(CULL_BACK) 하여, 앞면만 보이도록 합니다.
        D3D11_RASTERIZER_DESC rsDesc = {};
        rsDesc.FillMode = D3D11_FILL_SOLID;
        rsDesc.CullMode = D3D11_CULL_BACK;
        rsDesc.FrontCounterClockwise = TRUE; // CCW가 Front
        rsDesc.DepthClipEnable = TRUE;

        hr = m_device->CreateRasterizerState(&rsDesc, m_rasterizerState.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        // 8) 뷰포트 설정
        SetupViewport();

        return true;
    }

    void D3D11RenderDevice::Resize(std::uint32_t width, std::uint32_t height)
    {
        // 1) 0 크기는 무시 (최소화 등)
        if (width == 0 || height == 0)
            return;

        m_width  = width;
        m_height = height;

        // 2) 기존 렌더 타깃 / 깊이 뷰 해제
        m_renderTargetView.Reset();
        m_depthStencilView.Reset();
        m_depthStencil.Reset();

        // 3) 백버퍼 크기 재조정
        HRESULT hr = m_swapChain->ResizeBuffers(
            0,                          // 0이면 기존 버퍼 개수 유지
            m_width,
            m_height,
            DXGI_FORMAT_UNKNOWN,        // 기존 포맷 유지
            0
        );
        if (FAILED(hr)) return;

        // 4) 새 렌더 타깃, 깊이 버퍼 생성 및 뷰포트 재설정
        if (CreateRenderTarget() && CreateDepthStencil(m_width, m_height))
            SetupViewport();
    }

    void D3D11RenderDevice::BeginFrame(const float clearColor[4])
    {
        if (!m_renderTargetView) return;

        // 1) 현재 렌더 타깃과 깊이 스텐실을 파이프라인에 바인딩
        ID3D11RenderTargetView* views[] = { m_renderTargetView.Get() };
        m_immediateContext->OMSetRenderTargets(1, views, m_depthStencilView.Get());

        // 깊이 스텐실 / 래스터라이저 상태 설정 (한 번 생성한 것을 재사용)
        if (m_depthStencilState)
            m_immediateContext->OMSetDepthStencilState(m_depthStencilState.Get(), 0);
        if (m_rasterizerState)
            m_immediateContext->RSSetState(m_rasterizerState.Get());

        // 2) 화면을 지정한 색으로 지우고, 깊이 버퍼도 초기화합니다.
        m_immediateContext->ClearRenderTargetView(m_renderTargetView.Get(), clearColor);
        if (m_depthStencilView)
            m_immediateContext->ClearDepthStencilView(m_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    }

    void D3D11RenderDevice::EndFrame()
    {
        // vsync를 1로 두어 화면 주사율에 맞춰 표시합니다.
        if (m_swapChain) m_swapChain->Present(1, 0);
    }

    bool D3D11RenderDevice::CreateRenderTarget()
    {
        // 1) 스왑 체인의 백버퍼를 얻어옵니다.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
        HRESULT hr = m_swapChain->GetBuffer(
            0,
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(backBuffer.GetAddressOf())
        );

        if (FAILED(hr)) return false;

        // 2) 백버퍼로부터 렌더 타깃 뷰를 생성합니다.
        hr = m_device->CreateRenderTargetView(
            backBuffer.Get(),
            nullptr,
            m_renderTargetView.ReleaseAndGetAddressOf()
        );

        if (FAILED(hr)) return false;

        return true;
    }

    bool D3D11RenderDevice::CreateDepthStencil(std::uint32_t width, std::uint32_t height)
    {
        // 깊이 스텐실 텍스처 기술서
        D3D11_TEXTURE2D_DESC depthDesc = {};
        depthDesc.Width = width;
        depthDesc.Height = height;
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.SampleDesc.Quality = 0;
        depthDesc.Usage = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        depthDesc.CPUAccessFlags = 0;
        depthDesc.MiscFlags = 0;

        HRESULT hr = m_device->CreateTexture2D(&depthDesc, nullptr, m_depthStencil.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        // 깊이 스텐실 뷰 기술서
        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = depthDesc.Format;
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;

        hr = m_device->CreateDepthStencilView(
            m_depthStencil.Get(),
            &dsvDesc,
            m_depthStencilView.ReleaseAndGetAddressOf()
        );

        if (FAILED(hr)) return false;

        return true;
    }

    void D3D11RenderDevice::SetupViewport()
    {
        D3D11_VIEWPORT viewport = {};
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width    = static_cast<FLOAT>(m_width);
        viewport.Height   = static_cast<FLOAT>(m_height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        m_immediateContext->RSSetViewports(1, &viewport);
    }
}


