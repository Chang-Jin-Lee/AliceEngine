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

    /// 매우 단순한 텍스트 포맷(.fbxasset)을 읽어서 인스턴스 정보를 파싱합니다.
    /// - 형식:
    ///   source_fbx=원본FBXPath
    ///   mesh=논리메시이름
    ///   mat=... (여러 줄)
    bool LoadFbxInstanceAsset(const std::filesystem::path& path,
                              FbxInstanceAsset& out);
}


