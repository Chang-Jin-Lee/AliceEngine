#pragma once

#include <string>

#include <DirectXMath.h>

#include "Core/Entity.h"

namespace Alice
{
    /// 카메라 블렌드 전환 정보
    struct CameraBlendComponent
    {
        bool active{ false };
        std::string targetName{};
        float duration{ 0.5f };
        bool useSmoothStep{ true };

        // 슬로우 모션 구간 (블렌드 진행 비율 기준)
        float slowTriggerT{ 0.5f };
        float slowDuration{ 0.0f };
        float slowTimeScale{ 0.2f };

        // 런타임
        EntityId targetId{ InvalidEntityId };
        float elapsed{ 0.0f };
        bool slowTriggered{ false };
        float slowElapsed{ 0.0f };

        DirectX::XMFLOAT3 sourcePosition{};
        DirectX::XMFLOAT3 sourceRotation{};
        float sourceFovY{ DirectX::XM_PIDIV4 };
        float sourceNear{ 0.1f };
        float sourceFar{ 5000.0f };
    };
}
