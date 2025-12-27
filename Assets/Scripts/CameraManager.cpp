#include "CameraManager.h"

#include "Core/GameObject.h"
#include "CameraFollow.h"
#include "AddGetRemoveComponentTest.h"

namespace Alice
{
    REGISTER_SCRIPT(CameraManager);

    void CameraManager::Awake()
    {
		auto* camFollow = GetComponent<CameraFollow>();
        if (camFollow)
        {
            ALICE_LOG_INFO("[CameraManager] CameraFollow script is attached. {%f}", camFollow->Get_m_smoothSpeed());
			return;
        }

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
            cam = &world->AddComponent<CameraComponent>(GetOwner());
        }

        cam->primary = true;
        ALICE_LOG_INFO("[CameraManager] Ready. primary=1");
    }

	void CameraManager::Update(float deltaTime)
    {
        if (Input()->GetKeyDown(KeyCode::H))
        {
            AddComponent<AddGetRemoveComponentTest>();
        }
        if (Input()->GetKeyDown(KeyCode::J))
        {
			RemoveComponent<AddGetRemoveComponentTest>();
        }
        if (Input()->GetKeyDown(KeyCode::K))
        {
            std::vector<AddGetRemoveComponentTest*> t = GetComponents<AddGetRemoveComponentTest>();
			ALICE_LOG_INFO("Found {%d} AddGetRemoveComponentTest components.", t.size());
        }
	}
}


