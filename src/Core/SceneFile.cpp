#include "Core/SceneFile.h"

#include <fstream>
#include <sstream>

#include "Core/World.h"
#include "Core/Script.h"

namespace Alice
{
    namespace
    {
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

    namespace SceneFile
    {
        bool Save(const World& world, const std::filesystem::path& path)
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

            ofs << "# AliceRenderer scene\n";

            const auto& transforms = world.GetTransforms();
            for (const auto& [id, transform] : transforms)
            {
                const ScriptComponent* script = world.GetScript(id);
                const MaterialComponent* mat  = world.GetMaterial(id);

                ofs << "entity: " << static_cast<std::uint32_t>(id) << "\n";
                ofs << "position: "
                    << transform.position.x << " "
                    << transform.position.y << " "
                    << transform.position.z << "\n";
                ofs << "rotation: "
                    << transform.rotation.x << " "
                    << transform.rotation.y << " "
                    << transform.rotation.z << "\n";
                ofs << "scale: "
                    << transform.scale.x << " "
                    << transform.scale.y << " "
                    << transform.scale.z << "\n";

                ofs << "script: ";
                if (script)
                    ofs << script->scriptName;
                ofs << "\n";

                ofs << "material_color: ";
                if (mat)
                {
                    ofs << mat->color.x << " "
                        << mat->color.y << " "
                        << mat->color.z;
                }
                ofs << "\n";

                ofs << "material_asset: ";
                if (mat && !mat->assetPath.empty())
                    ofs << mat->assetPath;
                ofs << "\n";

                ofs << "\n";
            }

            return true;
        }

        bool Load(World& world, const std::filesystem::path& path)
        {
            std::ifstream ifs(path);
            if (!ifs.is_open())
                return false;

            // 현재 월드 비우기
            {
                std::vector<EntityId> ids;
                ids.reserve(world.GetTransforms().size());
                for (const auto& [id, _] : world.GetTransforms())
                {
                    ids.push_back(id);
                }
                for (EntityId id : ids)
                {
                    world.DestroyEntity(id);
                }
            }

            // 한 엔티티에 대한 임시 버퍼
            DirectX::XMFLOAT3 position { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 rotation { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 scale    { 1.0f, 1.0f, 1.0f };
            std::string       scriptName;
            DirectX::XMFLOAT3 materialColor { 0.7f, 0.7f, 0.7f };
            bool              hasMaterialColor = false;
            std::string       materialAsset;
            bool              hasAnyField      = false;

            auto commitEntity = [&]()
            {
                if (!hasAnyField)
                    return;

                EntityId e = world.CreateEntity();
                auto& t = world.AddTransform(e);
                t.SetPosition(position.x, position.y, position.z)
                 .SetRotation(rotation.x, rotation.y, rotation.z)
                 .SetScale(scale.x, scale.y, scale.z);

                if (!scriptName.empty())
                {
                    world.AddScript(e, scriptName);
                }

                if (hasMaterialColor || !materialAsset.empty())
                {
                    DirectX::XMFLOAT3 col = hasMaterialColor ? materialColor
                                                             : DirectX::XMFLOAT3(0.7f, 0.7f, 0.7f);
                    world.AddMaterial(e, col, materialAsset);
                }

                // 다음 엔티티를 위해 초기화
                position = { 0.0f, 0.0f, 0.0f };
                rotation = { 0.0f, 0.0f, 0.0f };
                scale    = { 1.0f, 1.0f, 1.0f };
                scriptName.clear();
                materialColor      = { 0.7f, 0.7f, 0.7f };
                hasMaterialColor   = false;
                materialAsset.clear();
                hasAnyField        = false;
            };

            std::string line;
            while (std::getline(ifs, line))
            {
                if (line.empty())
                {
                    commitEntity();
                    continue;
                }

                if (!line.empty() && line[0] == '#')
                    continue;

                std::istringstream iss(line);
                std::string key;
                if (!std::getline(iss, key, ':'))
                    continue;

                std::string value;
                std::getline(iss, value);

                Trim(key);
                Trim(value);

                if (key == "entity")
                {
                    // 새 엔티티 시작: 이전 엔티티를 커밋
                    commitEntity();
                    hasAnyField = true;
                }
                else if (key == "position")
                {
                    std::istringstream vs(value);
                    vs >> position.x >> position.y >> position.z;
                    hasAnyField = true;
                }
                else if (key == "rotation")
                {
                    std::istringstream vs(value);
                    vs >> rotation.x >> rotation.y >> rotation.z;
                    hasAnyField = true;
                }
                else if (key == "scale")
                {
                    std::istringstream vs(value);
                    vs >> scale.x >> scale.y >> scale.z;
                    hasAnyField = true;
                }
                else if (key == "script")
                {
                    scriptName = value;
                    hasAnyField = true;
                }
                else if (key == "material_color")
                {
                    std::istringstream vs(value);
                    vs >> materialColor.x >> materialColor.y >> materialColor.z;
                    hasMaterialColor = true;
                    hasAnyField      = true;
                }
                else if (key == "material_asset")
                {
                    materialAsset = value;
                    hasAnyField   = true;
                }
            }

            // 마지막 엔티티 커밋
            commitEntity();

            return true;
        }
    }
}


