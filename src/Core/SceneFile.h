#pragma once

#include "Core/World.h"
#include "Core/JsonRttr.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include <cstdint>

// 전방 선언
class UIWorldManager;

namespace Alice
{
    class ResourceManager;

    /// 씬(.scene) 파일 저장/로드 유틸리티입니다.
    /// - JSON 기반 저장/로드입니다.
    // 에디터에서 씬을 만들고, 저장하는 기능에 해당하는 코드임
    namespace SceneFile
    {
        /// 현재 World 의 상태를 JSON(.scene)으로 저장합니다.
        bool Save(const World& world, const std::filesystem::path& path);
        
        /// World와 UI를 함께 저장합니다.
        /// UIWorldManager가 nullptr이면 World만 저장합니다.
        bool Save(const World& world, const std::filesystem::path& path, UIWorldManager* uiWorldManager);

        /// .scene(JSON)을 읽어서 World 를 재구성합니다.
        /// 기존 엔티티들은 모두 제거됩니다.
        bool Load(World& world, const std::filesystem::path& path);
        
        /// World와 UI를 함께 로드합니다.
        /// UIWorldManager가 nullptr이면 World만 로드합니다.
        bool Load(World& world, const std::filesystem::path& path, UIWorldManager* uiWorldManager);

        /// World 상태를 JSON 문자열로 직렬화합니다. (Play 스냅샷용)
        bool SaveToJsonString(const World& world, std::string& out);

        /// JSON 문자열에서 World 를 복원합니다. (Stop 시 편집본 복원용)
        /// 기존 엔티티는 Clear 후 로드됩니다.
        bool LoadFromJsonString(World& world, const std::string& json);

        /// 에디터/최종빌드 모두에서 동작하는 자동 로더입니다.
        /// - editorMode: 실제 파일(Assets/...)을 읽습니다.
        /// - gameMode  : ResourceManager를 통해 Metas/Chunks에서 바이트를 로드해서 JSON으로 파싱합니다.
        /// UIWorldManager가 nullptr이면 World만 로드합니다.
        bool LoadAuto(World& world, const ResourceManager& resources, const std::filesystem::path& logicalPath, UIWorldManager* uiWorldManager = nullptr);

        // -------------------------------------------------------------------------
        // 단일 엔티티 codec (Undo 등에서 SceneFile과 동일한 직렬화 규칙 사용)
        // - WriteEntity: GUID 기반 Script EntityRef, Material 경로 정규화, registry 루프 등
        // - ApplyEntity: guidToEntity로 EntityRef 복원, pendingParents로 부모 연결
        // -------------------------------------------------------------------------
        bool WriteEntity(JsonRttr::json& out, const World& world, EntityId id);

        bool ApplyEntity(World& world,
                        const JsonRttr::json& e,
                        std::unordered_map<std::uint64_t, EntityId>& guidToEntity,
                        std::vector<std::pair<EntityId, std::uint64_t>>& pendingParents,
                        EntityId* outCreatedId = nullptr);
    }
}


