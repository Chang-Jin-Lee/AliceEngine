#include "C_PlayerInputSource.h"

#include "Core/ScriptFactory.h"
#include "Core/ScriptAPI.h"

namespace Alice
{
    REGISTER_SCRIPT(C_PlayerInputSource);

    void C_PlayerInputSource::Start()
    {
    }

    void C_PlayerInputSource::Update(float deltaTime)
    {
        m_cached = GetIntent(deltaTime);
    }

    void C_PlayerInputSource::OnDisable()
    {
        m_cached = {};
    }

    Combat::Intent C_PlayerInputSource::GetIntent(float /*deltaTime*/)
    {
        Combat::Intent intent{};
        if (!Input())
            return intent;

        auto toKey = [](int v) { return static_cast<KeyCode>(v); };

        float x = 0.0f;
        float y = 0.0f;
        if (Input()->GetKey(toKey(m_keyLeft))) x -= 1.0f;
        if (Input()->GetKey(toKey(m_keyRight))) x += 1.0f;
        if (Input()->GetKey(toKey(m_keyForward))) y += 1.0f;
        if (Input()->GetKey(toKey(m_keyBackward))) y -= 1.0f;

        intent.move = { x, y };
        intent.attackPressed = Input()->GetKeyDown(toKey(m_keyAttack))
            || (m_useMouseAttack && Input()->GetMouseButtonDown(MouseCode::Left));
        intent.dodgePressed = Input()->GetKeyDown(toKey(m_keyDodge));
        intent.guardHeld = Input()->GetKey(toKey(m_keyGuard))
            || (m_useMouseAttack && Input()->GetMouseButton(MouseCode::Right));

        return intent;
    }
}
