#pragma once
#include <DirectXMath.h>

namespace Alice {
    /// 씬 내 카메라(유니티의 Main Camera 느낌)
    /// - 게임 모드에서는 "첫번째(primary 우선)" 카메라 엔티티를 따라
    /// Camera(view/proj)를 갱신합니다.
    struct CameraComponent 
    {
        bool primary{ true };
        float fovYRad{ DirectX::XM_PIDIV4 };
        float nearPlane{ 0.1f };
        float farPlane{ 5000.0f };
        // 0 또는 useAspectOverride=false면 엔진의 뷰포트 비율을 사용합니다.
        bool  useAspectOverride{ false };
        float aspectOverride{ 0.0f };
    };
}