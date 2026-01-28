#pragma once

#include <string>
#include "Components/PostProcessVolumeComponent.h"
#include "Core/Entity.h"

// 전방 선언
namespace Alice
{
    class World;
}

namespace Alice
{
    /// Post Process Volume을 특정 GameObject에 적용하는 컴포넌트
    /// Inspector에서 GameObject 이름을 입력받아 해당 오브젝트에 PostProcessVolumeComponent를 적용합니다.
    class PostProcessVolumeAssignerComponent
    {
    public:
        PostProcessVolumeAssignerComponent() = default;
        virtual ~PostProcessVolumeAssignerComponent() = default;

        // ==== 대상 GameObject 설정 ====
        /// 대상 GameObject 이름
        std::string targetGameObjectName;

        /// AutoApply: true면 값 변경 시 자동 적용 (기본값: false)
        bool autoApply = false;

        /// CreateIfMissing: 대상에 컴포넌트가 없으면 생성 (기본값: true)
        bool createIfMissing = true;

        // ==== 템플릿 설정 ====
        /// 적용할 PostProcessVolumeComponent 템플릿
        PostProcessVolumeComponent templateVolume;

        // ==== RTTR 연동용 Getter/Setter ====
        const std::string& GetTargetGameObjectName() const { return targetGameObjectName; }
        void SetTargetGameObjectName(const std::string& name) 
        { 
            targetGameObjectName = name;
            m_lastAppliedTargetName.clear(); // 이름 변경 시 캐시 무효화
        }

        bool GetAutoApply() const { return autoApply; }
        void SetAutoApply(bool val) { autoApply = val; }

        bool GetCreateIfMissing() const { return createIfMissing; }
        void SetCreateIfMissing(bool val) { createIfMissing = val; }

        // ==== 적용 결과 (읽기 전용) ====
        /// 마지막 적용 성공 여부
        bool GetLastApplySuccess() const { return m_lastApplySuccess; }

        /// 마지막 적용 결과 메시지
        const std::string& GetLastApplyMessage() const { return m_lastApplyMessage; }

        /// 마지막 적용된 대상 EntityId
        EntityId GetLastAppliedEntityId() const { return m_lastAppliedEntityId; }

        /// 템플릿 설정을 대상 GameObject에 적용합니다.
        /// @param world World 객체
        /// @return 적용 성공 여부
        bool Apply(World& world);

    private:
        // 내부 상태 (Inspector 표시용)
        bool m_lastApplySuccess = false;
        std::string m_lastApplyMessage;
        EntityId m_lastAppliedEntityId = InvalidEntityId;
        std::string m_lastAppliedTargetName;  // 변경 감지용
    };
}
