#pragma once

#include <string>
#include <cstdint>

#include "Core/Entity.h"

namespace Alice
{
    struct AttackDriverComponent
    {
        // 공격을 제어할 트레이스 엔티티 GUID (0이면 자기 자신)
        std::uint64_t traceGuid = 0;
        EntityId traceCached = InvalidEntityId; // 런타임 캐시 (직렬화 금지)

        // AnimNotify가 걸릴 클립 이름 (빈 문자열이면 등록하지 않음)
        std::string clipName;

        float startTimeSec = 0.1f;
        float endTimeSec = 0.2f;

        // 내부 상태
        bool registered = false;
        std::string registeredClipName;
    };
}
