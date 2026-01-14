#include "CameraManager.h"

#include "Core/GameObject.h"
#include "CameraFollow.h"
#include "AddGetRemoveComponentTest.h"

namespace Alice
{
    REGISTER_SCRIPT(CameraManager);

    void CameraManager::Awake()
    {
        // 이미 CameraFollow 스크립트가 붙어 있으면 재생만 제어
        auto* camFollow = GetComponent<CameraFollow>();
        if (camFollow)
        {
            ALICE_LOG_INFO("[CameraManager] CameraFollow script is attached. {%f}", camFollow->Get_m_smoothSpeed());
			return;
        }

        // 나 자신이 붙어 있는 GameObject 가져오기
        if (auto* owner = GetOwner())
        {
            // CameraComponent가 없으면 생성
            auto* cam = owner->GetComponent<CameraComponent>();
            if (!cam)
            {
                auto* world = GetWorld();
                if (!world)
                    return;
                cam = &world->AddComponent<CameraComponent>(GetOwnerId());
            }

            // 메인 카메라로 설정
            cam->primary = true;
            ALICE_LOG_INFO("[CameraManager] Ready. primary=1");
        }
    }

    void CameraManager::Update(float /*deltaTime*/)
    {
        if (Input()->GetKeyDown(KeyCode::H))
        {
            AddComponent<AddGetRemoveComponentTest>();
        }
        if (Input()->GetKeyDown(KeyCode::J))
        {
            RemoveComponent<AddGetRemoveComponentTest>();
        }
        if (Input()->GetMouseButtonDown(KeyCode::K))
        {
            auto comps = GetComponents<AddGetRemoveComponentTest>();
            ALICE_LOG_INFO("[CameraManager] Found {%d} AddGetRemoveComponentTest components.", comps.size());
        }
    }
}
