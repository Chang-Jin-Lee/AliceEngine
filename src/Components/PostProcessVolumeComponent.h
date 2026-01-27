#pragma once

#include <DirectXMath.h>
#include "Rendering/PostProcessSettings.h"

namespace Alice
{
    /// Post Process Volume의 Shape 타입
    enum class PostProcessVolumeShape
    {
        Box = 0,    // AABB 또는 OBB 박스
        Sphere = 1  // 구 (미래 확장)
    };

    /// Unreal Engine 스타일의 Post Process Volume 컴포넌트
    /// 씬에 배치하여 카메라 위치에 따라 Post Process 설정을 블렌딩합니다.
    class PostProcessVolumeComponent
    {
    public:
        PostProcessVolumeComponent() = default;
        virtual ~PostProcessVolumeComponent() = default;

        // ==== Shape 설정 ====
        PostProcessVolumeShape shape = PostProcessVolumeShape::Box;
        
        /// Unbound: true면 항상 활성화 (무한 범위)
        bool unbound = false;

        /// Box 크기 (월드 스케일 기준, TransformComponent의 scale과 곱해짐)
        DirectX::XMFLOAT3 boxSize = { 10.0f, 10.0f, 10.0f };

        /// Sphere 반지름 (미래 확장용, 현재는 사용 안 함)
        float sphereRadius = 5.0f;

        // ==== 블렌딩 파라미터 ====
        /// BlendRadius: 볼륨 외부에서도 블렌딩되는 거리 (>= 0)
        /// 0이면 볼륨 내부에서만 적용
        float blendRadius = 0.0f;

        /// BlendWeight: 이 볼륨의 블렌딩 가중치 (0~1)
        float blendWeight = 1.0f;

        /// Priority: 우선순위 (높을수록 나중에 블렌딩되어 영향이 큼)
        /// UE와 동일: 높은 priority가 나중에 적용되어 최종값에 더 큰 영향
        int priority = 0;

        // ==== Post Process Settings ====
        PostProcessSettings settings;

        // ==== RTTR 연동용 Getter/Setter ====
        bool GetUnbound() const { return unbound; }
        void SetUnbound(bool val) { unbound = val; }

        float GetBlendRadius() const { return blendRadius; }
        void SetBlendRadius(float val) { blendRadius = std::max(0.0f, val); }

        float GetBlendWeight() const { return blendWeight; }
        void SetBlendWeight(float val) { blendWeight = std::clamp(val, 0.0f, 1.0f); }

        int GetPriority() const { return priority; }
        void SetPriority(int val) { priority = val; }

        PostProcessVolumeShape GetShape() const { return shape; }
        void SetShape(PostProcessVolumeShape val) { shape = val; }

        const DirectX::XMFLOAT3& GetBoxSize() const { return boxSize; }
        void SetBoxSize(const DirectX::XMFLOAT3& val) 
        { 
            boxSize.x = std::max(0.01f, val.x);
            boxSize.y = std::max(0.01f, val.y);
            boxSize.z = std::max(0.01f, val.z);
        }

        float GetSphereRadius() const { return sphereRadius; }
        void SetSphereRadius(float val) { sphereRadius = std::max(0.01f, val); }
    };
}
