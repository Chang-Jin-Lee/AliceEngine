#include "Core/Material.h"

#include <fstream>
#include <sstream>

#include "Core/World.h"

namespace Alice
{
    namespace
    {
        // 간단한 문자열 좌우 공백 제거 함수
        inline void Trim(std::string& s)
        {
            const char* ws = " \t\r\n";
            const auto  b  = s.find_first_not_of(ws);
            const auto  e  = s.find_last_not_of(ws);
            if (b == std::string::npos)
            {
                s.clear();
                return;
            }
            s = s.substr(b, e - b + 1);
        }
    }

    namespace MaterialFile
    {
        bool Load(const std::filesystem::path& path, MaterialComponent& outMaterial)
        {
            std::ifstream ifs(path);
            if (!ifs.is_open())
                return false;

            std::string line;
            while (std::getline(ifs, line))
            {
                std::istringstream iss(line);
                std::string key;
                if (!std::getline(iss, key, ':'))
                    continue;

                std::string value;
                std::getline(iss, value);

                Trim(key);
                Trim(value);

                if (key == "color")
                {
                    std::istringstream vs(value);
                    vs >> outMaterial.color.x
                       >> outMaterial.color.y
                       >> outMaterial.color.z;
                }
            }

            return true;
        }

        bool Save(const std::filesystem::path& path, const MaterialComponent& material)
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

            ofs << "name: " << path.stem().string() << "\n";
            ofs << "color: "
                << material.color.x << " "
                << material.color.y << " "
                << material.color.z << "\n";

            return true;
        }
    }
}


