#pragma once

#include <filesystem>

namespace Alice
{
    class World;
    class ResourceManager;

    /// 씬(.scene) 파일 저장/로드 유틸리티입니다.
    /// - JSON 기반 저장/로드입니다.
    // 에디터에서 씬을 만들고, 저장하는 기능에 해당하는 코드임
    namespace SceneFile
    {
        /// 현재 World 의 상태를 JSON(.scene)으로 저장합니다.
        bool Save(const World& world, const std::filesystem::path& path);

        /// .scene(JSON)을 읽어서 World 를 재구성합니다.
        /// 기존 엔티티들은 모두 제거됩니다.
        bool Load(World& world, const std::filesystem::path& path);

        /// 에디터/최종빌드 모두에서 동작하는 자동 로더입니다.
        /// - editorMode: 실제 파일(Assets/...)을 읽습니다.
        /// - gameMode  : ResourceManager를 통해 Metas/Chunks에서 바이트를 로드해서 JSON으로 파싱합니다.
        bool LoadAuto(World& world, const ResourceManager& resources, const std::filesystem::path& logicalPath);
    }
}


