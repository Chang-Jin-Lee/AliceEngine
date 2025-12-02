#include "Engine/Engine.h"

#include <chrono>

#include "Rendering/D3D11/D3D11RenderDevice.h"

namespace Alice
{
    namespace
    {
        // 윈도우 클래스 이름은 전역 상수로 관리합니다.
        constexpr wchar_t kWindowClassName[] = L"AliceRendererWindowClass";
    }

    Engine::Engine() = default;
    Engine::~Engine() = default;

    bool Engine::Initialize(HINSTANCE hInstance, int nCmdShow)
    {
        // 1) 인스턴스 핸들 보관
        m_hInstance = hInstance;

        // 2) 윈도우 생성
        if (!CreateMainWindow(nCmdShow))
        {
            return false;
        }

        // 3) 렌더 디바이스 생성(D3D11 구현체 사용)
        m_renderDevice = std::make_unique<D3D11RenderDevice>();
        if (!m_renderDevice->Initialize(m_hWnd, m_width, m_height))
        {
            return false;
        }

        // World, 기타 시스템 초기화는 여기서 추가 가능

        return true;
    }

    int Engine::Run()
    {
        m_isRunning = true;

        MSG msg = {};

        using clock = std::chrono::steady_clock;
        auto previousTime = clock::now();

        // 기본 게임 루프
        while (m_isRunning)
        {
            // 1) 윈도우 메시지 처리
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT)
                {
                    m_isRunning = false;
                    break;
                }

                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            if (!m_isRunning)
            {
                break;
            }

            // 2) 델타 타임 계산
            auto currentTime = clock::now();
            std::chrono::duration<float> deltaSeconds = currentTime - previousTime;
            previousTime = currentTime;

            float deltaTime = deltaSeconds.count();

            // 3) 게임 로직 업데이트
            Update(deltaTime);

            // 4) 렌더링
            Render();
        }

        return static_cast<int>(msg.wParam);
    }

    void Engine::Update(float /*deltaTime*/)
    {
        // ECS 시스템 및 게임 로직 업데이트는 추후 World, System을 통해 구현 예정
    }

    void Engine::Render()
    {
        if (!m_renderDevice)
        {
            return;
        }

        // 화면 클리어 색상 (짙은 파란색 계열)
        const float clearColor[4] = { 0.1f, 0.1f, 0.3f, 1.0f };

        m_renderDevice->BeginFrame(clearColor);

        // TODO: 여기에서 World, 렌더 시스템을 통해 실제 3D 씬 렌더링

        m_renderDevice->EndFrame();
    }

    bool Engine::CreateMainWindow(int nCmdShow)
    {
        // 1) 윈도우 클래스 등록
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(WNDCLASSEXW);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = &Engine::WindowProc;
        wc.cbClsExtra    = 0;
        wc.cbWndExtra    = 0;
        wc.hInstance     = m_hInstance;
        wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszMenuName  = nullptr;
        wc.lpszClassName = kWindowClassName;
        wc.hIconSm       = LoadIcon(nullptr, IDI_APPLICATION);

        if (!RegisterClassExW(&wc))
        {
            return false;
        }

        // 2) 윈도우 크기를 클라이언트 기준으로 맞추기 위해 조정
        RECT windowRect = { 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
        AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

        const int windowWidth  = windowRect.right - windowRect.left;
        const int windowHeight = windowRect.bottom - windowRect.top;

        // 3) 윈도우 생성 (this 포인터를 lpParam으로 전달)
        m_hWnd = CreateWindowExW(
            0,
            kWindowClassName,
            L"AliceRenderer",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowWidth,
            windowHeight,
            nullptr,
            nullptr,
            m_hInstance,
            this
        );

        if (!m_hWnd)
        {
            return false;
        }

        ShowWindow(m_hWnd, nCmdShow);
        UpdateWindow(m_hWnd);

        return true;
    }

    void Engine::OnResize(std::uint32_t width, std::uint32_t height)
    {
        m_width  = width;
        m_height = height;

        if (m_renderDevice)
        {
            m_renderDevice->Resize(width, height);
        }
    }

    LRESULT Engine::HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_SIZE:
        {
            const auto newWidth  = static_cast<std::uint32_t>(LOWORD(lParam));
            const auto newHeight = static_cast<std::uint32_t>(HIWORD(lParam));
            OnResize(newWidth, newHeight);
            return 0;
        }
        case WM_DESTROY:
            m_isRunning = false;
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hWnd, message, wParam, lParam);
    }

    LRESULT CALLBACK Engine::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        // 1) WM_NCCREATE 단계에서 Engine 인스턴스 포인터를 HWND에 저장
        if (message == WM_NCCREATE)
        {
            auto createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            auto engine = static_cast<Engine*>(createStruct->lpCreateParams);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(engine));
        }

        // 2) 저장된 Engine 포인터를 가져와서 멤버 함수로 위임
        auto engine = reinterpret_cast<Engine*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (engine)
        {
            return engine->HandleMessage(hWnd, message, wParam, lParam);
        }

        // 3) 엔진 포인터가 없으면 기본 처리
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
}


