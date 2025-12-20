#include "CameraManager.h"

#include "Core/GameObject.h"

namespace Alice
{
    REGISTER_SCRIPT(CameraManager);

    void CameraManager::Awake()
    {
        auto go = gameObject();
        if (!go.IsValid())
            return;

        // CameraComponent가 없으면 생성
        auto* cam = go.GetComponent<CameraComponent>();
        if (!cam)
        {
            auto* world = GetWorld();
            if (!world)
                return;
            cam = &world->AddCamera(GetOwner());
        }

        cam->primary = true;
        ALICE_LOG_INFO("[CameraManager] Ready. primary=1");
    }
}


