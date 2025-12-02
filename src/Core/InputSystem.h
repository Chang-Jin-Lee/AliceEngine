#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdint>
#include <memory>

#include <directXTK/Keyboard.h>
#include <directXTK/Mouse.h>

namespace Alice
{
    /// DirectXTK Keyboard/Mouse 를 사용하는 간단한 입력 시스템입니다.
    /// - Win32 메시지를 DirectXTK 로 전달하고
    /// - 한 프레임 동안의 키/마우스 상태와 마우스 델타를 질의할 수 있게 합니다.
    class InputSystem
    {
    public:
        InputSystem();
        ~InputSystem() = default;

        /// HWND 를 설정하고 Keyboard/Mouse 객체를 초기화합니다.
        bool Initialize(HWND hWnd);

        /// 매 프레임 한 번 호출하여
        /// DirectXTK Keyboard/Mouse 상태를 갱신하고 마우스 델타를 계산합니다.
        void Update(const float& deltaTime);

        // ---- 키보드/마우스 상태 질의 ----

        /// 지정한 키가 현재 눌려 있는지 여부를 반환합니다.
        bool IsKeyDown(DirectX::Keyboard::Keys key) const;

        /// 오른쪽 마우스 버튼이 눌려 있는지 여부를 반환합니다.
        bool IsRightButtonDown() const;

        /// 왼쪽 마우스 버튼이 눌려 있는지 여부를 반환합니다.
        bool IsLeftButtonDown() const;

        /// 직전 프레임 이후 누적된 마우스 이동량을 반환합니다.
        POINT GetMouseDelta() const { return m_mouseDelta; }

    private:
        std::unique_ptr<DirectX::Keyboard> m_keyboard;
        std::unique_ptr<DirectX::Mouse>    m_mouse;

        DirectX::Keyboard::State                m_keyboardState{};
        DirectX::Keyboard::KeyboardStateTracker m_keyboardTracker{};

        DirectX::Mouse::State              m_mouseState{};
        DirectX::Mouse::ButtonStateTracker m_mouseTracker{};

        POINT m_prevMousePos{ 0, 0 };
        POINT m_mouseDelta{ 0, 0 };
        bool  m_hasPrevMousePos = false;
    };
}


