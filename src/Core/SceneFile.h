#pragma once

#include <filesystem>

namespace Alice
{
    class World;

    /// 씬(.scene) 파일 저장/로드 유틸리티입니다.
    /// - 현재는 Transform / Script / Material 컴포넌트를 직렬화합니다.
    namespace SceneFile
    {
        /// 현재 World 의 상태를 지정된 경로의 .scene 파일로 저장합니다.
        bool Save(const World& world, const std::filesystem::path& path);

        /// .scene 파일을 읽어서 World 를 재구성합니다.
        /// 기존 엔티티들은 모두 제거됩니다.
        bool Load(World& world, const std::filesystem::path& path);
    }
}


