#include "CharacterMovement.h"

#include "Core/GameObject.h"

namespace Alice
{
    REGISTER_SCRIPT(CharacterMovement);

    void CharacterMovement::FixedUpdate(float fixedDeltaTime)
    {
        auto* input = Input();
        if (!input)
            return;

        auto go = gameObject();
        auto* t = go.GetComponent<TransformComponent>();
        if (!t)
            return;

        float mx = 0.0f;
        float mz = 0.0f;
        if (input->GetKey(KeyCode::W)) mz += 1.0f;
        if (input->GetKey(KeyCode::S)) mz -= 1.0f;
        if (input->GetKey(KeyCode::D)) mx += 1.0f;
        if (input->GetKey(KeyCode::A)) mx -= 1.0f;

        const float len = std::sqrt(mx * mx + mz * mz);
        if (len > 0.0001f)
        {
            mx /= len;
            mz /= len;
        }

        t->position.x += mx * m_moveSpeed * fixedDeltaTime;
        t->position.z += mz * m_moveSpeed * fixedDeltaTime;

        const bool grounded = (t->position.y <= 0.0f);
        if (grounded)
        {
            t->position.y = 0.0f;
            if (m_velY < 0.0f) m_velY = 0.0f;
            if (input->GetKeyDown(KeyCode::Space))
                m_velY = m_jumpSpeed;
        }

        m_velY -= m_gravity * fixedDeltaTime;
        t->position.y += m_velY * fixedDeltaTime;
        if (t->position.y < 0.0f)
            t->position.y = 0.0f;
    }
}


