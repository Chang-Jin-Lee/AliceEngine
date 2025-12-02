#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <memory>

#include "Core/World.h"
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
        void Update(float deltaTime);

        /// 한 프레임의 렌더링을 수행합니다.
        void Render();

        /// 기본 윈도우를 생성합니다.
        bool CreateMainWindow(int nCmdShow);

        /// 윈도우 크기 변경 시 호출됩니다.
        void OnResize(std::uint32_t width, std::uint32_t height);

    private:
        HINSTANCE m_hInstance = nullptr;
        HWND      m_hWnd      = nullptr;

        std::uint32_t m_width  = 1280;
        std::uint32_t m_height = 720;

        bool m_isRunning = false;

        World  m_world;
        Camera m_camera;

        EntityId m_cubeEntity { InvalidEntityId };

        std::unique_ptr<ID3D11RenderDevice> m_renderDevice;
        std::unique_ptr<ForwardRenderSystem> m_forwardRenderSystem;
    };
}


