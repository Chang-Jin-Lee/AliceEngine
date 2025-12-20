#include "CameraFollow.h"

#include "Core/GameObject.h"

namespace Alice
{
    REGISTER_SCRIPT(CameraFollow);

    void CameraFollow::FixedUpdate(float /*fixedDeltaTime*/)
    {
        auto go = gameObject();
        if (!go.IsValid())
            return;

        auto target = go.FindFirstSkinnedMesh();
        if (!target.IsValid())
            return;

        auto* myT = go.GetComponent<TransformComponent>();
        auto* tT = target.GetComponent<TransformComponent>();
        if (!myT || !tT)
            return;

        myT->position.x = tT->position.x + m_offsetX;
        myT->position.y = tT->position.y + m_offsetY;
        myT->position.z = tT->position.z + m_offsetZ;

        // 타겟을 바라보게 회전(yaw/pitch) 맞추기
        const float dx = tT->position.x - myT->position.x;
        const float dy = tT->position.y - myT->position.y;
        const float dz = tT->position.z - myT->position.z;

        const float yaw = std::atan2(dx, dz);
        const float distXZ = std::sqrt(dx * dx + dz * dz);
        const float pitch = -std::atan2(dy, distXZ);

        myT->rotation.x = pitch;
        myT->rotation.y = yaw;
    }
}


