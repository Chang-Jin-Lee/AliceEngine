#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <cstdint>

#include <DirectXMath.h>

#include "Core/Entity.h"

namespace Alice
{
    struct WeaponTraceComponent
    {
        // 소켓 제공자 GUID (직렬화 대상)
        std::uint64_t ownerGuid = 0;
        std::string ownerNameDebug;

        // 런타임 캐시 (직렬화 금지)
        EntityId ownerCached = InvalidEntityId;

        // 추적할 소켓 리스트 (Tip/Mid/Base 등)
        std::vector<std::string> traceSocketNames;

        float radius = 0.05f;
        bool active = false;
        bool debugDraw = false;
        float baseDamage = 10.0f;
        uint32_t teamId = 0;
        uint32_t attackInstanceId = 0;
        uint32_t targetLayerBits = 0;
        uint32_t queryLayerBits = 0;

        // 내부 캐시
        bool hasPrevPositions = false;
        std::vector<DirectX::XMFLOAT3> prevPositions;

        uint32_t lastAttackInstanceId = 0;
        std::unordered_set<std::uint64_t> hitVictims;
    };
}
