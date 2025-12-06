#include "Game/FbxAsset.h"

#include <fstream>

namespace Alice
{
    bool LoadFbxInstanceAsset(const std::filesystem::path& path,
                              FbxInstanceAsset& out)
    {
        out = {};

        std::ifstream ifs(path);
        if (!ifs.is_open())
            return false;

        std::string line;
        while (std::getline(ifs, line))
        {
            if (line.rfind("source_fbx=", 0) == 0)
            {
                out.sourceFbx = line.substr(std::string("source_fbx=").size());
            }
            else if (line.rfind("mesh=", 0) == 0)
            {
                out.meshAssetPath = line.substr(std::string("mesh=").size());
            }
            else if (line.rfind("mat=", 0) == 0)
            {
                out.materialAssetPaths.push_back(
                    line.substr(std::string("mat=").size()));
            }
        }

        return !out.meshAssetPath.empty();
    }
}


