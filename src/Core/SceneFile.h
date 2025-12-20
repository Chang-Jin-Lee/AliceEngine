#pragma once

#include <filesystem>

namespace Alice
{
    class World;

    /// 씬(.scene) 파일 저장/로드 유틸리티입니다.
    /// - JSON 기반 저장/로드입니다.
    namespace SceneFile
    {
        /// 현재 World 의 상태를 JSON(.scene)으로 저장합니다.
        bool Save(const World& world, const std::filesystem::path& path);

        /// .scene(JSON)을 읽어서 World 를 재구성합니다.
        /// 기존 엔티티들은 모두 제거됩니다.
        bool Load(World& world, const std::filesystem::path& path);
    }
}


