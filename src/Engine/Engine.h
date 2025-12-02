#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <memory>

#include "Core/World.h"
#include "Core/InputSystem.h"
#include "Core/TimeSystem.h"
#include "Core/ResourceManager.h"
#include "Core/Scene.h"
#include "Rendering/Camera.h"
#include "Rendering/D3D11/ID3D11RenderDevice.h"
#include "Rendering/ForwardRenderSystem.h"

namespace Alice
{
    /// 엔진 전체를 관리하는 가장 상위 레벨 클래스입니다.
    /// - 윈도우 생성 및 메시지 루프 관리
    /// - World 및 시스템 업데이트
    /// - 렌더 디바이스에게 렌더링을 요청
    class Engine
    {
    public:
        Engine();
        ~Engine();

        /// 엔진과 윈도우, 렌더 디바이스를 초기화합니다.
        bool Initialize(HINSTANCE hInstance, int nCmdShow);

        /// 메인 루프를 실행합니다.
        int Run();

        /// 윈도우 메시지를 처리하는 멤버 함수입니다.
        LRESULT HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    private:
        // Win32 전역 윈도우 프로시저 → Engine 인스턴스로 위임
        static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

        /// 한 프레임의 업데이트(게임 로직)를 수행합니다.
        void Update();

        /// 한 프레임의 렌더링을 수행합니다.
        void Render();

        /// 기본 윈도우를 생성합니다.
        bool CreateMainWindow(int nCmdShow);

        /// 윈도우 크기 변경 시 호출됩니다.
        void OnResize(std::uint32_t width, std::uint32_t height);

    private:
        enum class ShadingMode
        {
            Lambert   = 0,
            Phong     = 1,
            BlinnPhong = 2
        };

        HINSTANCE m_hInstance = nullptr;
        HWND      m_hWnd      = nullptr;

        std::uint32_t m_width  = 1280;
        std::uint32_t m_height = 720;

        bool m_isRunning = false;

        World          m_world;
        Camera         m_camera;
        InputSystem    m_inputSystem;
        GameTimer      m_timer;
        ResourceManager m_resourceManager;
        std::unique_ptr<SceneManager> m_sceneManager;

        ShadingMode m_shadingMode { ShadingMode::BlinnPhong };
        bool        m_useFillLight { false };

        // 카메라 이동/회전을 위한 내부 상태 값들
        DirectX::XMFLOAT3 m_cameraPosition { 0.0f, 2.0f, -5.0f };
        float             m_cameraYawRadians   = 0.0f;  // Yaw (좌우 회전)
        float             m_cameraPitchRadians = 0.0f;  // Pitch (상하 회전)

        float             m_cameraMoveSpeed       = 3.0f;     // 초당 이동 속도
        float             m_cameraMouseSensitivity = 0.0025f; // 마우스 감도 (라디안/픽셀)

        std::unique_ptr<ID3D11RenderDevice> m_renderDevice;
        std::unique_ptr<ForwardRenderSystem> m_forwardRenderSystem;
    };
}


