#include "NewScript1.h"
#include "Core/World.h"

namespace Alice
{
    // 이 스크립트를 리플렉션/팩토리 시스템에 등록합니다.
    REGISTER_SCRIPT(NewScript1);

    void NewScript1::OnCreate(World& world, EntityId entity)
    {
        // 초기화 로직을 여기에 작성하세요.
    }

    void NewScript1::OnUpdate(World& world, EntityId entity, float deltaTime)
    {
        // 매 프레임 호출되는 로직을 여기에 작성하세요.
    }
}
