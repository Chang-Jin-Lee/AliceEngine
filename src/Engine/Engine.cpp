#include "Engine/Engine.h"

#include "Rendering/D3D11/D3D11RenderDevice.h"

// ImGui
#include "imgui.h"
#include "imgui_internal.h"   // DockBuilder API 사용
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

// Win32 메시지 헬퍼 (GET_X/Y_LPARAM)
#include <Windowsx.h>

// 표준 라이브러리
#include <filesystem>
#include <cfloat>      // FLT_MAX
#include <algorithm>   // std::max
#include <memory>

// 문자열 변환 / ImGui 래퍼
#include "Core/StringUtils.h"
#include "Core/ImGuiEx.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Alice
{
    namespace
    {
        // 윈도우 클래스 이름은 전역 상수로 관리합니다.
        constexpr wchar_t kWindowClassName[] = L"AliceRendererWindowClass";
    }

    Engine::Engine() = default;

    Engine::~Engine()
    {
        m_editorCore.Shutdown();
    }

    bool Engine::Initialize(HINSTANCE hInstance, int nCmdShow)
    {
        // 1) 인스턴스 핸들 보관
        m_hInstance = hInstance;

        // 2) 윈도우 생성
        if (!CreateMainWindow(nCmdShow)) return false;

        // 3) 입력 시스템 초기화 (DirectXTK Keyboard/Mouse)
        m_inputSystem.Initialize(m_hWnd);

		// 4) 렌더 디바이스 생성(D3D11 구현체 사용)
        m_renderDevice = std::make_unique<D3D11RenderDevice>();
        if (!m_renderDevice->Initialize(m_hWnd, m_width, m_height))
            return false;

        // 5) ImGui / Editor 코어 초기화
        if (!m_editorCore.Initialize(m_hWnd, *m_renderDevice))
            return false;

        // 6) Forward 렌더 시스템 초기화
        m_forwardRenderSystem = std::make_unique<ForwardRenderSystem>(*m_renderDevice);
        if (!m_forwardRenderSystem->Initialize(m_width, m_height))
            return false;

        // 7) 카메라 설정
        const float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
        m_cameraPosition = DirectX::XMFLOAT3(0.0f, 2.0f, -5.0f);
        DirectX::XMFLOAT3 target(0.0f, 0.0f, 0.0f);
        m_camera.SetLookAt(m_cameraPosition, target, DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f));
        m_camera.SetPerspective(DirectX::XM_PIDIV4, aspect, 0.1f, 100.0f);

        // 8) 씬 매니저 생성 및 기본 씬 로드
        m_resourceManager.Clear();
        m_sceneManager = std::make_unique<SceneManager>(m_world, m_resourceManager);
        m_sceneManager->SwitchTo("SampleScene");

        return true;
    }

    int Engine::Run()
    {
        m_isRunning = true;

        MSG msg = {};

        // 고해상도 타이머 초기화
        m_timer.Reset();
        m_timer.Start();

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

            if (!m_isRunning) break;

            Update();
            Render();
        }

        return static_cast<int>(msg.wParam);
    }

    void Engine::Update()
    {
        m_timer.Tick();
        m_inputSystem.Update(m_timer.DeltaTime());

        using namespace DirectX;

        // 1) 카메라 이동 (WASD + Q/E) - 오른쪽 마우스 버튼을 누르고 있을 때만 동작
        const bool canControlCamera = m_inputSystem.IsRightButtonDown(); // 우클릭 상태에서만 이동/회전

        XMVECTOR moveDir = XMVectorZero();

        if (canControlCamera)
        {
            if (m_inputSystem.IsKeyDown(Keyboard::W))
            {
                moveDir = XMVectorAdd(moveDir, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
            }
            if (m_inputSystem.IsKeyDown(Keyboard::S))
            {
                moveDir = XMVectorAdd(moveDir, XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f));
            }
            if (m_inputSystem.IsKeyDown(Keyboard::D))
            {
                moveDir = XMVectorAdd(moveDir, XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
            }
            if (m_inputSystem.IsKeyDown(Keyboard::A))
            {
                moveDir = XMVectorAdd(moveDir, XMVectorSet(-1.0f, 0.0f, 0.0f, 0.0f));
            }
            // E: 위로, Q: 아래로 이동
            if (m_inputSystem.IsKeyDown(Keyboard::E))
            {
                moveDir = XMVectorAdd(moveDir, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
            }
            if (m_inputSystem.IsKeyDown(Keyboard::Q))
            {
                moveDir = XMVectorAdd(moveDir, XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f));
            }

            if (!XMVector3Equal(moveDir, XMVectorZero()))
            {
                // 카메라의 현재 회전에 맞춰 이동 벡터를 회전
                XMMATRIX rotMatrix = XMMatrixRotationRollPitchYaw(m_cameraPitchRadians, m_cameraYawRadians, 0.0f);
                XMVECTOR worldMoveDir = XMVector3TransformNormal(moveDir, rotMatrix);
                worldMoveDir = XMVector3Normalize(worldMoveDir);

                XMVECTOR pos = XMLoadFloat3(&m_cameraPosition);
                pos = XMVectorAdd(pos, XMVectorScale(worldMoveDir, m_cameraMoveSpeed * m_timer.DeltaTime()));
                XMStoreFloat3(&m_cameraPosition, pos);
            }

            // 2) 마우스 이동으로 카메라 회전 (우클릭 상태에서만)
            POINT mouseDelta = m_inputSystem.GetMouseDelta();
            m_cameraYawRadians   += static_cast<float>(mouseDelta.x) * m_cameraMouseSensitivity;
            // 마우스를 아래로 내리면 화면도 아래를 보도록 Y축 회전을 반대로 적용합니다.
            m_cameraPitchRadians += static_cast<float>(mouseDelta.y) * m_cameraMouseSensitivity;
        }

        // 피치 각도는 -89 ~ 89도 사이로 제한
        const float pitchLimit = XMConvertToRadians(89.0f);
        if (m_cameraPitchRadians > pitchLimit)  m_cameraPitchRadians = pitchLimit;
        if (m_cameraPitchRadians < -pitchLimit) m_cameraPitchRadians = -pitchLimit;

        // 3) 카메라 LookAt 갱신
        XMMATRIX rotMatrix = XMMatrixRotationRollPitchYaw(m_cameraPitchRadians, m_cameraYawRadians, 0.0f);
        XMVECTOR forward = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotMatrix);

        XMVECTOR pos = XMLoadFloat3(&m_cameraPosition);
        XMVECTOR target = XMVectorAdd(pos, forward);

        XMFLOAT3 targetFloat3;
        XMStoreFloat3(&targetFloat3, target);

        m_camera.SetLookAt(m_cameraPosition, targetFloat3, XMFLOAT3(0.0f, 1.0f, 0.0f));

        // 4) 현재 씬 및 스크립트 업데이트
        //    - 에디터에서 Play 버튼이 눌렸을 때만 게임 로직이 진행되도록 합니다.
        if (m_isPlaying)
        {
            if (m_sceneManager)
            {
                m_sceneManager->Update(m_timer.DeltaTime());
            }

            // 엔티티에 붙어 있는 모든 ScriptComponent 를 갱신합니다.
            m_scriptSystem.Update(m_world, m_timer.DeltaTime());
        }
    }

    void Engine::Render()
    {
        if (!m_renderDevice || !m_forwardRenderSystem)
            return;

        // 화면 클리어 색상 (짙은 파란색 계열)
        const float clearColor[4] = { 0.1f, 0.1f, 0.3f, 1.0f };

        m_renderDevice->BeginFrame(clearColor);

        // ImGui 프레임 시작 (EditorCore 에 위임)
        m_editorCore.BeginFrame();

        // 에디터 스타일 UI (도킹, 하이러키, 인스펙터, 프로젝트 뷰 등)
        const float dt  = m_timer.DeltaTime();
        const float fps = (dt > 0.0f) ? (1.0f / dt) : 0.0f;
        int shadingModeValue = static_cast<int>(m_shadingMode);
        m_editorCore.DrawEditorUI(
            m_world,
            m_camera,
            *m_forwardRenderSystem,
            m_sceneManager.get(),
            dt,
            fps,
            m_isPlaying,
            shadingModeValue,
            m_useFillLight,
            m_selectedEntity,
            m_viewportPicker);
        m_shadingMode = static_cast<ShadingMode>(shadingModeValue);

        // 간단한 Forward 렌더링
        EntityId renderEntity = InvalidEntityId;
        if (m_sceneManager)
        {
            renderEntity = m_sceneManager->GetPrimaryRenderableEntity();
        }

        if (renderEntity != InvalidEntityId)
        {
            const int shadingModeValue = static_cast<int>(m_shadingMode);
            m_forwardRenderSystem->Render(
                m_world,
                m_camera,
                renderEntity,
                shadingModeValue,
                m_useFillLight);
        }

        // ImGui 렌더링
        m_editorCore.RenderDrawData();

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
        // 엔진 전용 아이콘을 로드합니다. (실패하면 기본 아이콘을 사용)
        HICON hIconBig = static_cast<HICON>(LoadImageW(
            nullptr,
            L"../Resource/Icon/Alice.ico",
            IMAGE_ICON,
            32,
            32,
            LR_LOADFROMFILE));
        if (!hIconBig) hIconBig = LoadIcon(nullptr, IDI_APPLICATION);
        HICON hIconSmall = static_cast<HICON>(LoadImageW(
            nullptr,
            L"../Resource/Icon/Alice.ico",
            IMAGE_ICON,
            16,
            16,
            LR_LOADFROMFILE));
        if (!hIconSmall) hIconSmall = LoadIcon(nullptr, IDI_APPLICATION);

        wc.hIcon         = hIconBig;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszMenuName  = nullptr;
        wc.lpszClassName = kWindowClassName;
        wc.hIconSm       = hIconSmall;

        if (!RegisterClassExW(&wc)) return false;

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

        if (!m_hWnd) return false;

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

            const float aspect = (height != 0)
                ? static_cast<float>(width) / static_cast<float>(height)
                : 1.0f;
            m_camera.SetPerspective(DirectX::XM_PIDIV4, aspect, 0.1f, 100.0f);
        }

        if (m_forwardRenderSystem)
        {
            m_forwardRenderSystem->Resize(width, height);
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
        // ImGui가 먼저 Win32 메시지를 처리할 수 있도록 전달합니다.
        if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
            return true;

        // DirectXTK Keyboard / Mouse 에 Win32 메시지 전달 (GameApp::WndProc 패턴)
        switch (message)
        {
        case WM_ACTIVATEAPP:
            DirectX::Keyboard::ProcessMessage(message, wParam, lParam);
            DirectX::Mouse::ProcessMessage(message, wParam, lParam);
            break;

        case WM_INPUT:
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MOUSEWHEEL:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_MOUSEHOVER:
            DirectX::Mouse::ProcessMessage(message, wParam, lParam);
            break;

        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP:
            DirectX::Keyboard::ProcessMessage(message, wParam, lParam);
            break;

        default:
            break;
        }

        // 1) WM_NCCREATE 단계에서 Engine 인스턴스 포인터를 HWND에 저장
        if (message == WM_NCCREATE)
        {
            auto createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            auto engine = static_cast<Engine*>(createStruct->lpCreateParams);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(engine));
        }

        // 2) 저장된 Engine 포인터를 가져와서 멤버 함수로 위임
        auto engine = reinterpret_cast<Engine*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (engine) return engine->HandleMessage(hWnd, message, wParam, lParam);

        // 3) 엔진 포인터가 없으면 기본 처리
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
}


