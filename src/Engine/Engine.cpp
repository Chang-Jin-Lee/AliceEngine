#include "Engine/Engine.h"

#include "Rendering/D3D11/D3D11RenderDevice.h"

// ImGui
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

// Win32 메시지 헬퍼 (GET_X/Y_LPARAM)
#include <Windowsx.h>

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
        // ImGui 리소스를 정리합니다.
        if (ImGui::GetCurrentContext() != nullptr)
        {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }
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

        // 5) ImGui 초기화
        {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGui::StyleColorsDark();

            auto& io = ImGui::GetIO();

            // 폰트 아틀라스를 모두 지우고, 한글/일본어를 포함한 폰트를 기본 폰트로 사용합니다.
            io.Fonts->Clear();

            // 한글 폰트 (NotoSansKR-Regular.ttf)를 기본 폰트로 설정
            ImFontConfig baseConfig;
            baseConfig.MergeMode = false;
            io.FontDefault = io.Fonts->AddFontFromFileTTF(
                "../Resource/Fonts/NotoSansKR-Regular.ttf",
                18.0f,
                &baseConfig,
                io.Fonts->GetGlyphRangesKorean());

            // 일본어 폰트 (meiryo.ttc)를 기본 폰트에 머지
            ImFontConfig jpConfig;
            jpConfig.MergeMode = true;
            jpConfig.PixelSnapH = true;
            io.Fonts->AddFontFromFileTTF(
                "../Resource/Fonts/meiryo.ttc",
                18.0f,
                &jpConfig,
                io.Fonts->GetGlyphRangesJapanese());

            auto* d3dDevice  = m_renderDevice->GetDevice();
            auto* d3dContext = m_renderDevice->GetImmediateContext();

            ImGui_ImplWin32_Init(m_hWnd);
            ImGui_ImplDX11_Init(d3dDevice, d3dContext);
        }

        // 6) Forward 렌더 시스템 초기화
        m_forwardRenderSystem = std::make_unique<ForwardRenderSystem>(*m_renderDevice);
        if (!m_forwardRenderSystem->Initialize())
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

        // 4) 현재 씬 업데이트 (트랜스폼 등)
        if (m_sceneManager)
        {
            m_sceneManager->Update(m_timer.DeltaTime());
        }
    }

    void Engine::Render()
    {
        if (!m_renderDevice || !m_forwardRenderSystem)
            return;

        // 화면 클리어 색상 (짙은 파란색 계열)
        const float clearColor[4] = { 0.1f, 0.1f, 0.3f, 1.0f };

        m_renderDevice->BeginFrame(clearColor);

        // ImGui 프레임 시작
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // 간단한 설정 UI
        if (ImGui::Begin("AliceRenderer Settings"))
        {
            // 쉐이딩 모드 선택
            int mode = static_cast<int>(m_shadingMode);
            if (ImGui::RadioButton("Lambert", mode == 0))   mode = 0;
            ImGui::SameLine();
            if (ImGui::RadioButton("Phong", mode == 1))     mode = 1;
            ImGui::SameLine();
            if (ImGui::RadioButton("Blinn-Phong", mode == 2)) mode = 2;
            m_shadingMode = static_cast<ShadingMode>(mode);

            // Fill Light(보조광) 토글
            Alice::ImGuiCheckbox(L"Fill Light (보조광)", &m_useFillLight);

            // 조명 파라미터 (주광/보조광)
            auto& lighting = m_forwardRenderSystem->GetLightingParameters();
            Alice::ImGuiSliderFloat(L"Key Intensity (주광)",
                                    &lighting.keyIntensity,
                                    0.0f,
                                    3.0f);
            Alice::ImGuiSliderFloat(L"Fill Intensity (보조광)",
                                    &lighting.fillIntensity,
                                    0.0f,
                                    3.0f);
            ImGui::SliderFloat("Shininess",      &lighting.shininess,     2.0f, 128.0f);

            ImGui::ColorEdit3("Diffuse Color",  &lighting.diffuseColor.x);
            ImGui::ColorEdit3("Specular Color", &lighting.specularColor.x);

            // 광원 방향 (단순 -1~1 슬라이더, 내부에서 정규화)
            Alice::ImGuiSliderFloat3(L"Key Direction (주광)",
                                     &lighting.keyDirection.x,
                                     -1.0f,
                                     1.0f);
            Alice::ImGuiSliderFloat3(L"Fill Direction (보조광)",
                                     &lighting.fillDirection.x,
                                     -1.0f,
                                     1.0f);

            // 카메라 파라미터
            ImGui::SliderFloat("Move Speed",        &m_cameraMoveSpeed,        0.5f, 20.0f);
            ImGui::SliderFloat("Mouse Sensitivity", &m_cameraMouseSensitivity, 0.0005f, 0.01f);
        }
        ImGui::End();

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
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

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


