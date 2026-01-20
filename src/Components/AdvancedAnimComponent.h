#pragma once

#include <string>
#include <DirectXMath.h>

namespace Alice
{
    /// 간단한 코드 기반 고급 애니메이션 설정 (블렌드/상하체/IK/Additive)
    struct AdvancedAnimLayer
    {
        bool enabled{ true };
        std::string clipA;
        std::string clipB;
        float blend01{ 0.0f };   // A->B 블렌드 비율 (0..1)
        float alpha{ 1.0f };     // 레이어 알파 (0..1)
        float speedA{ 1.0f };
        float speedB{ 1.0f };
        bool loopA{ true };
        bool loopB{ true };

        // UE/Unity 스타일 CrossFade (clipA -> clipB)
        bool useCrossFade{ false };
        float fadeDuration{ 0.2f };
        bool useExitTime{ true };
        float exitNorm{ 0.85f };
        float entryNorm{ 0.0f };
        bool smoothStep{ true };
    };

    struct AdvancedAnimAdditive
    {
        bool enabled{ false };
        std::string clip;        // Additive clip
        std::string refClip;     // Reference clip (보통 Idle)
        float weight{ 1.0f };    // 0..1
        float speed{ 1.0f };
        bool loop{ true };
        float refTime{ 0.0f };   // reference 샘플 시간
    };

    struct AdvancedAnimIK
    {
        bool enabled{ false };
        std::string tipBone;     // End effector bone name
        int chainLength{ 3 };
        int iterations{ 8 };
        float weight{ 1.0f };    // 0..1
        DirectX::XMFLOAT3 targetWorld{ 0.0f, 0.0f, 0.0f };
    };

    struct AdvancedAnimComponent
    {
        bool enabled{ true };
        bool playing{ true };
        float globalSpeed{ 1.0f };

        AdvancedAnimLayer base;

        bool upperUseMask{ true };
        std::string upperMaskKeywords{ "Spine,Chest,Neck,Head,Arm,Hand,Shoulder" };
        AdvancedAnimLayer upper;

        AdvancedAnimAdditive additive;
        AdvancedAnimIK ik;
    };
}

