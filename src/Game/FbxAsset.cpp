#include "Game/FbxAsset.h"

#include <fstream>

#include "json/json.hpp"

namespace Alice
{
    bool LoadFbxInstanceAsset(const std::filesystem::path& path,
                              FbxInstanceAsset& out)
    {
        out = {};

        std::ifstream ifs(path);
        if (!ifs.is_open())
            return false;

        nlohmann::json j;
        try
        {
            ifs >> j;
        }
        catch (...)
        {
            return false;
        }

        out.sourceFbx = j.value("source_fbx", std::string{});
        out.meshAssetPath = j.value("mesh", std::string{});
        out.materialAssetPaths.clear();

        auto it = j.find("materials");
        if (it != j.end() && it->is_array())
        {
            for (const auto& v : *it)
            {
                if (v.is_string())
                    out.materialAssetPaths.push_back(v.get<std::string>());
            }
        }

        if (out.meshAssetPath.empty())
            return false;

        return true;
    }

    bool SaveFbxInstanceAsset(const std::filesystem::path& path,
                              const FbxInstanceAsset& asset)
    {
        auto parent = path.parent_path();
        if (!parent.empty() && !std::filesystem::exists(parent))
        {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
        }

        std::ofstream ofs(path);
        if (!ofs.is_open())
            return false;

        nlohmann::json j;
        j["source_fbx"] = asset.sourceFbx;
        j["mesh"] = asset.meshAssetPath;
        j["materials"] = asset.materialAssetPaths;

        ofs << j.dump(4);
        return true;
    }
}


