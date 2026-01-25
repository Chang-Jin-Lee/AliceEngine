#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Core/JsonRttr.h"
#include "PhysX/Components/Phy_SettingsComponent.h"
#include <string>
#include <DirectXMath.h>

namespace Alice
{
    /// 씬/프리팹 직렬화에 공통으로 쓰는 헬퍼.
    /// SceneFile, ComponentRegistry, Prefab에서 정식 API로 사용.
    namespace SceneSerializationHelpers
    {
        /// 절대 경로를 프로젝트 루트 기준 상대 경로로 변환.
        /// Assets/Resource/Cooked로 시작하는 경로는 그대로 유지.
        std::string NormalizePathToRelative(const std::string& path);

        /// Phy_SettingsComponent 수동 직렬화 (중첩 배열 등)
        JsonRttr::json WritePhysicsSceneSettings(const Phy_SettingsComponent& settings);

        /// Phy_SettingsComponent 수동 역직렬화
        bool LoadPhysicsSceneSettings(Phy_SettingsComponent& settings, const JsonRttr::json& root);

        /// 스키닝 메시가 애니메이션과 연결되기 전 사용하는 항등 본 1개 팔레트.
        extern DirectX::XMFLOAT4X4 g_IdentityBone;
    }
}
