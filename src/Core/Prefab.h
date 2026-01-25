#pragma once

#include <filesystem>

#include "Core/Entity.h"

namespace Alice
{
    class World;

    /// 프리팹 로더/인스턴시에이터 및 저장 유틸입니다.
    /// - JSON 파일에서 Transform + Scripts[](+프로퍼티) 를 읽어오거나 저장합니다.
    /// - Unity 의 Prefab / Instantiate 개념을 간단하게 흉내내기 위한 용도입니다.
    ///
    /// **스크립트 EntityRef 정책**
    /// - 스크립트 EntityRef는 씬 내 엔티티 GUID로 저장됩니다. Instantiate 시 guidToEntity는
    ///   현재 월드 전체 기준으로 resolve 합니다.
    /// - **다른 씬/프로젝트에서 해당 프리팹을 인스턴스하면**, 저장된 GUID가 해당 월드에 없어
    ///   EntityRef가 전부 InvalidEntityId로 남습니다.
    /// - **권장**: 프리팹은 단일 엔티티이며, 가급적 "프리팹 내부" 참조만 사용하세요.
    ///   (다중 엔티티 프리팹 지원 시에는 내부 참조만 resolve 되도록 제한하는 것이 유지보수에 유리합니다.)
    namespace Prefab
    {
        /// 프리팹 파일을 읽어 새로운 엔티티를 생성합니다.
        /// \return 생성된 엔티티 ID (실패 시 InvalidEntityId)
        EntityId InstantiateFromFile(World& world, const std::filesystem::path& path);

        /// 현재 월드에 존재하는 엔티티를 프리팹 파일로 저장합니다.
        /// - Transform 과 Script 이름 한 개를 간단한 텍스트 포맷으로 기록합니다.
        /// - 같은 포맷을 InstantiateFromFile 이 다시 읽어서 엔티티를 생성할 수 있습니다.
        /// \return 저장 성공 여부
        bool SaveToFile(const World& world,
                        EntityId entity,
                        const std::filesystem::path& path);
    }
}



