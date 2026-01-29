#pragma once

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"
#include "Components/PostProcessVolumeComponent.h"

namespace Alice
{
    /// Post Process Volume을 스크립트로 사용할 수 있게 하는 래퍼 클래스
    /// Inspector의 Script 목록에서 추가할 수 있습니다.
    class PostProcessVolume : public IScript
    {
        ALICE_BODY(PostProcessVolume);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        // PostProcessVolumeComponent에 접근
        PostProcessVolumeComponent* GetVolume() { return GetComponent<PostProcessVolumeComponent>(); }
        const PostProcessVolumeComponent* GetVolume() const { return GetComponent<PostProcessVolumeComponent>(); }

        // 편의 속성들 (컴포넌트에 위임)
        bool GetUnbound() const;
        void SetUnbound(bool val);
        
        float GetBlendRadius() const;
        void SetBlendRadius(float val);
        
        float GetBlendWeight() const;
        void SetBlendWeight(float val);
        
        int GetPriority() const;
        void SetPriority(int val);
        
        const DirectX::XMFLOAT3& GetBoxSize() const;
        void SetBoxSize(const DirectX::XMFLOAT3& val);
        
        const std::string& GetReferenceObjectName() const;
        void SetReferenceObjectName(const std::string& val);
        
        bool GetUseReferenceObject() const;
        void SetUseReferenceObject(bool val);
    };
}
