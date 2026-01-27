#include "Rendering/PostProcessVolumeSystem.h"
#include "Core/World.h"
#include <DirectXMath.h>
#include <algorithm>

using namespace DirectX;

namespace Alice
{
    PostProcessSettings PostProcessVolumeSystem::CalculateFinalSettings(
        World& world,
        const XMFLOAT3& cameraPosition,
        const PostProcessSettings& defaultSettings)
    {
        // 1. 후보 수집 및 weight 계산
        std::vector<PostProcessVolumeCandidate> candidates;
        CollectCandidates(world, cameraPosition, candidates);

        // 2. Priority 기준 정렬 (낮은 priority가 먼저, 높은 priority가 나중에 블렌딩)
        // UE 스타일: 높은 priority가 나중에 적용되어 최종값에 더 큰 영향
        std::sort(candidates.begin(), candidates.end(),
            [](const PostProcessVolumeCandidate& a, const PostProcessVolumeCandidate& b)
            {
                return a.volume->priority < b.volume->priority;
            });

        // 3. 기본 설정으로 시작
        PostProcessSettings final = defaultSettings;

        // 4. 각 볼륨에 대해 블렌딩
        for (const auto& candidate : candidates)
        {
            if (candidate.weight > 0.0f)
            {
                PostProcessBlend::BlendSettings(final, candidate.volume->settings, candidate.weight);
            }
        }

        return final;
    }

    void PostProcessVolumeSystem::CollectCandidates(
        World& world,
        const XMFLOAT3& cameraPosition,
        std::vector<PostProcessVolumeCandidate>& outCandidates)
    {
        outCandidates.clear();

        // 모든 PostProcessVolumeComponent를 순회
        for (const auto& [entityId, volume] : world.GetComponents<PostProcessVolumeComponent>())
        {
            // TransformComponent 필요
            auto* transform = world.GetComponent<TransformComponent>(entityId);
            if (!transform || !transform->enabled)
                continue;

            // Weight 계산
            //float weight = CalculateVolumeWeight(*volume, *transform, cameraPosition);
            float weight = CalculateVolumeWeight(volume, *transform, cameraPosition);

            if (weight > 0.0f)
            {
                PostProcessVolumeCandidate candidate;
                candidate.entityId = entityId;
                candidate.volume = const_cast<PostProcessVolumeComponent*>(&volume);
                candidate.transform = transform;
                candidate.weight = weight;
                outCandidates.push_back(candidate);
            }
        }
    }

    float PostProcessVolumeSystem::CalculateVolumeWeight(
        const PostProcessVolumeComponent& volume,
        const TransformComponent& transform,
        const XMFLOAT3& cameraPosition)
    {
        // A) Unbound: 항상 후보, w = BlendWeight
        if (volume.unbound)
        {
            return volume.blendWeight;
        }

        // B) Bound(Box): 카메라 위치 기준으로 weight 계산
        // 월드 공간 박스 크기 계산 (Transform의 scale과 volume의 boxSize 곱)
        XMFLOAT3 worldBoxSize;
        worldBoxSize.x = volume.boxSize.x * transform.scale.x;
        worldBoxSize.y = volume.boxSize.y * transform.scale.y;
        worldBoxSize.z = volume.boxSize.z * transform.scale.z;

        // 월드 공간 박스 중심 = Transform의 position
        const XMFLOAT3& boxCenter = transform.position;

        // 카메라가 볼륨 내부인지 판정
        bool inside = IsPointInsideBox(cameraPosition, boxCenter, worldBoxSize, transform.rotation);

        if (inside)
        {
            // 내부: w = BlendWeight (강하게 적용)
            return volume.blendWeight;
        }
        else
        {
            // 외부: 표면까지 거리 d를 구함
            float distance = DistanceToBoxSurface(cameraPosition, boxCenter, worldBoxSize, transform.rotation);
            
            // BlendRadius > 0이고 d <= BlendRadius일 때만 페이드 적용
            if (volume.blendRadius > 0.0f && distance <= volume.blendRadius)
            {
                // t = saturate(1 - d / BlendRadius)
                float t = 1.0f - (distance / volume.blendRadius);
                t = std::clamp(t, 0.0f, 1.0f);
                
                // w = BlendWeight * t
                return volume.blendWeight * t;
            }
            else
            {
                // 그 외는 w = 0
                return 0.0f;
            }
        }
    }

    bool PostProcessVolumeSystem::IsPointInsideBox(
        const XMFLOAT3& point,
        const XMFLOAT3& boxCenter,
        const XMFLOAT3& boxSize,
        const XMFLOAT3& boxRotationRad)
    {
        // 회전 행렬 계산
        XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYaw(
            boxRotationRad.x,  // Pitch
            boxRotationRad.y,  // Yaw
            boxRotationRad.z   // Roll
        );

        // 박스 중심을 원점으로 이동
        XMVECTOR localPoint = XMVectorSubtract(XMLoadFloat3(&point), XMLoadFloat3(&boxCenter));

        // 회전의 역행렬을 적용하여 로컬 공간으로 변환
        XMMATRIX invRotation = XMMatrixTranspose(rotationMatrix);  // 회전 행렬의 역행렬 = 전치 행렬
        localPoint = XMVector3Transform(localPoint, invRotation);

        // 로컬 공간에서 AABB 내부 여부 확인
        XMFLOAT3 local;
        XMStoreFloat3(&local, localPoint);

        float halfSizeX = boxSize.x * 0.5f;
        float halfSizeY = boxSize.y * 0.5f;
        float halfSizeZ = boxSize.z * 0.5f;

        return (local.x >= -halfSizeX && local.x <= halfSizeX &&
                local.y >= -halfSizeY && local.y <= halfSizeY &&
                local.z >= -halfSizeZ && local.z <= halfSizeZ);
    }

    float PostProcessVolumeSystem::DistanceToBoxSurface(
        const XMFLOAT3& point,
        const XMFLOAT3& boxCenter,
        const XMFLOAT3& boxSize,
        const XMFLOAT3& boxRotationRad)
    {
        // 회전 행렬 계산
        XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYaw(
            boxRotationRad.x,  // Pitch
            boxRotationRad.y,  // Yaw
            boxRotationRad.z   // Roll
        );

        // 박스 중심을 원점으로 이동
        XMVECTOR localPoint = XMVectorSubtract(XMLoadFloat3(&point), XMLoadFloat3(&boxCenter));

        // 회전의 역행렬을 적용하여 로컬 공간으로 변환
        XMMATRIX invRotation = XMMatrixTranspose(rotationMatrix);
        localPoint = XMVector3Transform(localPoint, invRotation);

        // 로컬 공간 좌표
        XMFLOAT3 local;
        XMStoreFloat3(&local, localPoint);

        float halfSizeX = boxSize.x * 0.5f;
        float halfSizeY = boxSize.y * 0.5f;
        float halfSizeZ = boxSize.z * 0.5f;

        // AABB 내부 여부 확인
        bool inside = (local.x >= -halfSizeX && local.x <= halfSizeX &&
                       local.y >= -halfSizeY && local.y <= halfSizeY &&
                       local.z >= -halfSizeZ && local.z <= halfSizeZ);

        if (inside)
        {
            // 내부: 가장 가까운 면까지의 거리 (음수로 반환)
            float distX = std::min(local.x + halfSizeX, halfSizeX - local.x);
            float distY = std::min(local.y + halfSizeY, halfSizeY - local.y);
            float distZ = std::min(local.z + halfSizeZ, halfSizeZ - local.z);
            float minDist = std::min({ distX, distY, distZ });
            return -minDist;  // 음수 = 내부
        }
        else
        {
            // 외부: 가장 가까운 점까지의 거리
            float closestX = std::clamp(local.x, -halfSizeX, halfSizeX);
            float closestY = std::clamp(local.y, -halfSizeY, halfSizeY);
            float closestZ = std::clamp(local.z, -halfSizeZ, halfSizeZ);

            XMVECTOR closest = XMVectorSet(closestX, closestY, closestZ, 0.0f);
            XMVECTOR diff = XMVectorSubtract(localPoint, closest);
            float distance = XMVectorGetX(XMVector3Length(diff));
            return distance;  // 양수 = 외부 거리
        }
    }
}
