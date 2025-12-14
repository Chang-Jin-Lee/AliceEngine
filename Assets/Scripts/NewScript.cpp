#include "NewScript.h"
#include "Core/World.h"

namespace Alice
{
    // 이 스크립트를 리플렉션/팩토리 시스템에 등록합니다.
    REGISTER_SCRIPT(NewScript);

    void NewScript::Start()
    {
        // 초기화 로직
    }

    void NewScript::Update(float /*deltaTime*/)
    {
        // 매 프레임 로직
    }
}
