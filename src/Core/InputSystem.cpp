#include "Core/InputSystem.h"

using namespace DirectX;

namespace Alice
{
    InputSystem::InputSystem() = default;

    bool InputSystem::Initialize(HWND hWnd)
    {
        m_keyboard = std::make_unique<Keyboard>();
        m_mouse    = std::make_unique<Mouse>();

        m_mouse->SetWindow(hWnd);
        m_hWnd = hWnd; // 커서 제어를 위해 핸들 저장

        m_prevMousePos = POINT{ 0, 0 };
        m_mouseDelta   = POINT{ 0, 0 };
        m_hasPrevMousePos = false;

        return true;
    }

    void InputSystem::Update(const float& /*deltaTime*/)
    {
        // 델타는 프레임마다 초기화합니다.
        m_mouseDelta.x = 0;
        m_mouseDelta.y = 0;
        m_mouseScrollDelta = 0.0f;

        if (!m_keyboard || !m_mouse) return;

        // DirectXTK 입력 상태 갱신
        m_mouseState = m_mouse->GetState();
        m_mouseTracker.Update(m_mouseState);

        m_keyboardState = m_keyboard->GetState();
        m_keyboardTracker.Update(m_keyboardState);

        // 마우스 델타 계산 (클라이언트 좌표 기준)
        POINT current{ m_mouseState.x, m_mouseState.y };

        if (!m_hasPrevMousePos)
        {
            m_prevMousePos    = current;
            m_hasPrevMousePos = true;
        }

        m_mouseDelta.x += current.x - m_prevMousePos.x;
        m_mouseDelta.y += current.y - m_prevMousePos.y;

        m_prevMousePos = current;

        // 마우스 스크롤 델타 계산
        // DirectXTK의 scrollWheelValue는 누적값이므로 이전 값과의 차이를 계산합니다.
        const int currentScrollWheelValue = m_mouseState.scrollWheelValue;
        m_mouseScrollDelta = static_cast<float>(currentScrollWheelValue - m_prevScrollWheelValue);
        m_prevScrollWheelValue = currentScrollWheelValue;
    }

    bool InputSystem::IsKeyDown(Keyboard::Keys key) const
    {
        return m_keyboardState.IsKeyDown(key);
    }

    bool InputSystem::IsKeyPressed(Keyboard::Keys key) const
    {
        return m_keyboardTracker.IsKeyPressed(key);
    }

    bool InputSystem::IsRightButtonDown() const
    {
        return m_mouseState.rightButton;
    }

    bool InputSystem::IsLeftButtonDown() const
    {
        return m_mouseState.leftButton;
    }

    bool InputSystem::IsMiddleButtonDown() const
    {
        return m_mouseState.middleButton;
    }

    bool InputSystem::IsMouseButtonDown(int buttonIndex) const
    {
        switch (buttonIndex)
        {
        case 0: return m_mouseState.leftButton;
        case 1: return m_mouseState.rightButton;
        case 2: return m_mouseState.middleButton;
        default: return false;
        }
    }

    bool InputSystem::IsMouseButtonPressed(int buttonIndex) const
    {
        switch (buttonIndex)
        {
        case 0: return m_mouseTracker.leftButton == DirectX::Mouse::ButtonStateTracker::PRESSED;
        case 1: return m_mouseTracker.rightButton == DirectX::Mouse::ButtonStateTracker::PRESSED;
        case 2: return m_mouseTracker.middleButton == DirectX::Mouse::ButtonStateTracker::PRESSED;
        default: return false;
        }
    }

    bool InputSystem::IsMouseButtonReleased(int buttonIndex) const
    {
        switch (buttonIndex)
        {
        case 0: return m_mouseTracker.leftButton == DirectX::Mouse::ButtonStateTracker::RELEASED;
        case 1: return m_mouseTracker.rightButton == DirectX::Mouse::ButtonStateTracker::RELEASED;
        case 2: return m_mouseTracker.middleButton == DirectX::Mouse::ButtonStateTracker::RELEASED;
        default: return false;
        }
    }

    POINT InputSystem::GetMousePosition() const
    {
        return POINT{ m_mouseState.x, m_mouseState.y };
    }

    void InputSystem::SetCursorVisible(bool visible)
    {
        // ShowCursor는 카운터 방식이므로 강제로 상태를 맞춤
        if (visible)
        {
            while (::ShowCursor(TRUE) < 0);
        }
        else
        {
            while (::ShowCursor(FALSE) >= 0);
        }
    }

    void InputSystem::SetCursorLocked(bool locked)
    {
        if (locked && m_hWnd)
        {
            // 윈도우 영역 안으로 커서 가두기
            RECT rect;
            ::GetClientRect(m_hWnd, &rect);
            
            // 클라이언트 영역을 스크린 좌표로 변환
            POINT pt = { rect.left, rect.top };
            POINT pt2 = { rect.right, rect.bottom };
            ::ClientToScreen(m_hWnd, &pt);
            ::ClientToScreen(m_hWnd, &pt2);
            
            RECT clipRect = { pt.x, pt.y, pt2.x, pt2.y };
            ::ClipCursor(&clipRect);
        }
        else
        {
            // 가두기 해제
            ::ClipCursor(nullptr);
        }
    }
}


