#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <cstdint>

#include "Core/Entity.h"

namespace Alice
{
    struct AttackDriverClip
    {
        std::string clipName;
        float startTimeSec = 0.1f;
        float endTimeSec = 0.2f;
        bool enabled = true;
    };

    struct AttackDriverComponent
    {
        // 공격을 제어할 트레이스 엔티티 GUID (0이면 자기 자신)
        std::uint64_t traceGuid = 0;
        EntityId traceCached = InvalidEntityId; // 런타임 캐시 (직렬화 금지)

        // AnimNotify 타이밍 목록
        std::vector<AttackDriverClip> clips;

        // 내부 상태
        std::uint64_t registeredHash = 0;
        std::unordered_set<std::string> registeredClipNames;
    };
}
