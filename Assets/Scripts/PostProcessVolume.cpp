#include "PostProcessVolume.h"
#include "Core/ScriptFactory.h"
#include "Core/Logger.h"
#include "Core/World.h"
#include "Components/PostProcessVolumeComponent.h"
#include "Components/DebugDrawBoxComponent.h"
#include "Components/TransformComponent.h"
#include <DirectXMath.h>

namespace Alice
{
    // 이 스크립트를 리플렉션/팩토리 시스템에 등록합니다.
    REGISTER_SCRIPT(PostProcessVolume);

    void PostProcessVolume::Start()
    {
        // PostProcessVolumeComponent가 없으면 추가
        if (!GetComponent<PostProcessVolumeComponent>())
        {
            AddComponent<PostProcessVolumeComponent>();
        }

        // DebugDrawBoxComponent가 없으면 추가
        if (!GetComponent<DebugDrawBoxComponent>())
        {
            auto* debugBox = &AddComponent<DebugDrawBoxComponent>();
            debugBox->enabled = true;
            debugBox->color = DirectX::XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f); // 초록색
            debugBox->depthTest = false;
        }
    }

    void PostProcessVolume::Update(float deltaTime)
    {
        auto* volume = GetComponent<PostProcessVolumeComponent>();
        auto* transform = GetComponent<TransformComponent>();
        auto* debugBox = GetComponent<DebugDrawBoxComponent>();

        if (!volume || !transform || !debugBox)
            return;

        // Unbound 볼륨은 그리지 않음
        if (volume->unbound)
        {
            debugBox->enabled = false;
            return;
        }

        // Bound 볼륨만 그리기
        debugBox->enabled = true;

        // Transform의 scale과 PostProcessVolumeComponent의 boxSize를 곱해서 실제 크기 계산
        DirectX::XMFLOAT3 worldBoxSize;
        worldBoxSize.x = volume->boxSize.x * transform->scale.x;
        worldBoxSize.y = volume->boxSize.y * transform->scale.y;
        worldBoxSize.z = volume->boxSize.z * transform->scale.z;

        // BlendRadius를 고려한 외부 경계 계산
        float blendRadius = volume->blendRadius;
        DirectX::XMFLOAT3 halfExtents;
        halfExtents.x = worldBoxSize.x * 0.5f + blendRadius;
        halfExtents.y = worldBoxSize.y * 0.5f + blendRadius;
        halfExtents.z = worldBoxSize.z * 0.5f + blendRadius;

        // 회전을 고려한 AABB 계산 (박스의 8개 코너를 월드 공간으로 변환 후 AABB 계산)
        using namespace DirectX;
        XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYaw(
            transform->rotation.x, 
            transform->rotation.y, 
            transform->rotation.z
        );

        // 로컬 공간의 8개 코너
        XMFLOAT3 corners[8] = {
            { -halfExtents.x, -halfExtents.y, -halfExtents.z },
            {  halfExtents.x, -halfExtents.y, -halfExtents.z },
            {  halfExtents.x,  halfExtents.y, -halfExtents.z },
            { -halfExtents.x,  halfExtents.y, -halfExtents.z },
            { -halfExtents.x, -halfExtents.y,  halfExtents.z },
            {  halfExtents.x, -halfExtents.y,  halfExtents.z },
            {  halfExtents.x,  halfExtents.y,  halfExtents.z },
            { -halfExtents.x,  halfExtents.y,  halfExtents.z }
        };

        // 월드 공간으로 변환
        XMVECTOR worldPos = XMLoadFloat3(&transform->position);
        XMFLOAT3 worldCorners[8];
        for (int i = 0; i < 8; ++i)
        {
            XMVECTOR localCorner = XMLoadFloat3(&corners[i]);
            XMVECTOR rotatedCorner = XMVector3Transform(localCorner, rotationMatrix);
            XMVECTOR worldCorner = XMVectorAdd(worldPos, rotatedCorner);
            XMStoreFloat3(&worldCorners[i], worldCorner);
        }

        // AABB 계산
        XMFLOAT3 minBounds = worldCorners[0];
        XMFLOAT3 maxBounds = worldCorners[0];
        for (int i = 1; i < 8; ++i)
        {
            minBounds.x = std::min(minBounds.x, worldCorners[i].x);
            minBounds.y = std::min(minBounds.y, worldCorners[i].y);
            minBounds.z = std::min(minBounds.z, worldCorners[i].z);
            maxBounds.x = std::max(maxBounds.x, worldCorners[i].x);
            maxBounds.y = std::max(maxBounds.y, worldCorners[i].y);
            maxBounds.z = std::max(maxBounds.z, worldCorners[i].z);
        }

        debugBox->boundsMin = minBounds;
        debugBox->boundsMax = maxBounds;

        // 색상 설정 (BlendWeight에 따라 투명도 조절)
        float alpha = 0.5f + volume->blendWeight * 0.5f; // 0.5 ~ 1.0
        debugBox->color = DirectX::XMFLOAT4(0.0f, 1.0f, 0.0f, alpha);
    }

    bool PostProcessVolume::GetUnbound() const
    {
        auto* vol = GetComponent<PostProcessVolumeComponent>();
        return vol ? vol->GetUnbound() : false;
    }

    void PostProcessVolume::SetUnbound(bool val)
    {
        auto* vol = GetComponent<PostProcessVolumeComponent>();
        if (vol) vol->SetUnbound(val);
    }

    float PostProcessVolume::GetBlendRadius() const
    {
        auto* vol = GetComponent<PostProcessVolumeComponent>();
        return vol ? vol->GetBlendRadius() : 0.0f;
    }

    void PostProcessVolume::SetBlendRadius(float val)
    {
        auto* vol = GetComponent<PostProcessVolumeComponent>();
        if (vol) vol->SetBlendRadius(val);
    }

    float PostProcessVolume::GetBlendWeight() const
    {
        auto* vol = GetComponent<PostProcessVolumeComponent>();
        return vol ? vol->GetBlendWeight() : 1.0f;
    }

    void PostProcessVolume::SetBlendWeight(float val)
    {
        auto* vol = GetComponent<PostProcessVolumeComponent>();
        if (vol) vol->SetBlendWeight(val);
    }

    int PostProcessVolume::GetPriority() const
    {
        auto* vol = GetComponent<PostProcessVolumeComponent>();
        return vol ? vol->GetPriority() : 0;
    }

    void PostProcessVolume::SetPriority(int val)
    {
        auto* vol = GetComponent<PostProcessVolumeComponent>();
        if (vol) vol->SetPriority(val);
    }

    const DirectX::XMFLOAT3& PostProcessVolume::GetBoxSize() const
    {
        static const DirectX::XMFLOAT3 defaultSize = { 10.0f, 10.0f, 10.0f };
        auto* vol = GetComponent<PostProcessVolumeComponent>();
        return vol ? vol->GetBoxSize() : defaultSize;
    }

    void PostProcessVolume::SetBoxSize(const DirectX::XMFLOAT3& val)
    {
        auto* vol = GetComponent<PostProcessVolumeComponent>();
        if (vol) vol->SetBoxSize(val);
    }

    const std::string& PostProcessVolume::GetReferenceObjectName() const
    {
        static const std::string empty;
        auto* vol = GetComponent<PostProcessVolumeComponent>();
        return vol ? vol->GetReferenceObjectName() : empty;
    }

    void PostProcessVolume::SetReferenceObjectName(const std::string& val)
    {
        auto* vol = GetComponent<PostProcessVolumeComponent>();
        if (vol) vol->SetReferenceObjectName(val);
    }

    bool PostProcessVolume::GetUseReferenceObject() const
    {
        auto* vol = GetComponent<PostProcessVolumeComponent>();
        return vol ? vol->GetUseReferenceObject() : false;
    }

    void PostProcessVolume::SetUseReferenceObject(bool val)
    {
        auto* vol = GetComponent<PostProcessVolumeComponent>();
        if (vol) vol->SetUseReferenceObject(val);
    }
}
