#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Alice
{
    struct FbxInstanceAsset
    {
        std::string              sourceFbx;
        std::string              meshAssetPath;
        std::vector<std::string> materialAssetPaths;
    };

    /// JSON(.fbxasset) 읽기/쓰기
    /// - { "source_fbx": "...", "mesh": "...", "materials": ["..."] }
    bool LoadFbxInstanceAsset(const std::filesystem::path& path,
                              FbxInstanceAsset& out);

    bool SaveFbxInstanceAsset(const std::filesystem::path& path,
                              const FbxInstanceAsset& asset);
}


