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
}


