#pragma once

#include "Core/World.h"
#include "Rendering/PostProcessSettings.h"
#include "Components/PostProcessVolumeComponent.h"
#include "Components/TransformComponent.h"
#include <DirectXCollision.h>
#include <algorithm>
#include <vector>

namespace Alice
{
    /// Post Process Volume 후보 정보 (weight 계산 결과)
    struct PostProcessVolumeCandidate
    {
        EntityId entityId;
        PostProcessVolumeComponent* volume;
        TransformComponent* transform;
        float weight;  // 최종 블렌딩 가중치 (0~1)
    };

    /// Post Process Volume 시스템
    /// 카메라 위치 기준으로 볼륨을 수집하고 블렌딩하여 최종 PostProcessSettings를 계산합니다.
    class PostProcessVolumeSystem
    {
    public:
        PostProcessVolumeSystem() = default;

        /// 카메라 위치 기준으로 최종 PostProcessSettings를 계산합니다.
        /// @param world World 객체
        /// @param cameraPosition 카메라 월드 위치
        /// @param defaultSettings 기본 설정 (모든 override = false)
        /// @return 블렌딩된 최종 PostProcessSettings
        PostProcessSettings CalculateFinalSettings(
            World& world,
            const DirectX::XMFLOAT3& cameraPosition,
            const PostProcessSettings& defaultSettings
        );

        /// Box 볼륨의 표면까지 최소 거리를 계산합니다 (월드 공간).
        /// @param point 월드 공간 점
        /// @param boxCenter 월드 공간 박스 중심
        /// @param boxSize 월드 공간 박스 크기 (스케일 적용됨)
        /// @param boxRotation 월드 공간 박스 회전 (쿼터니언 또는 행렬)
        /// @return 표면까지의 최소 거리 (내부면 음수, 외부면 양수)
        static float DistanceToBoxSurface(
            const DirectX::XMFLOAT3& point,
            const DirectX::XMFLOAT3& boxCenter,
            const DirectX::XMFLOAT3& boxSize,
            const DirectX::XMFLOAT3& boxRotationRad
        );

    private:
        /// 볼륨 후보 수집 및 weight 계산
        void CollectCandidates(
            World& world,
            const DirectX::XMFLOAT3& cameraPosition,
            std::vector<PostProcessVolumeCandidate>& outCandidates
        );

        /// 단일 볼륨의 weight 계산
        float CalculateVolumeWeight(
            const PostProcessVolumeComponent& volume,
            const TransformComponent& transform,
            const DirectX::XMFLOAT3& cameraPosition
        );

        /// Box 내부 여부 확인 (월드 공간, 회전 고려)
        bool IsPointInsideBox(
            const DirectX::XMFLOAT3& point,
            const DirectX::XMFLOAT3& boxCenter,
            const DirectX::XMFLOAT3& boxSize,
            const DirectX::XMFLOAT3& boxRotationRad
        );
    };
}
