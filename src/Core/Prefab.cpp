#include "Core/Prefab.h"

#include <fstream>
#include <sstream>

#include "Core/World.h"

namespace Alice
{
    namespace Prefab
    {
        // 매우 단순한 텍스트 포맷:
        //
        // name: MyPrefab
        // position: 0 0 0
        // rotation: 0 0 0
        // scale: 1 1 1
        // script: Rotator
        //
        EntityId InstantiateFromFile(World& world, const std::filesystem::path& path)
        {
            if (!std::filesystem::exists(path))
                return InvalidEntityId;

            std::ifstream file(path);
            if (!file.is_open())
                return InvalidEntityId;

            DirectX::XMFLOAT3 position { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 rotation { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 scale    { 1.0f, 1.0f, 1.0f };
            std::string       scriptName;

            std::string line;
            while (std::getline(file, line))
            {
                std::istringstream iss(line);
                std::string key;
                if (!std::getline(iss, key, ':'))
                    continue;

                std::string value;
                std::getline(iss, value);

                // 공백 제거
                auto trim = [](std::string& s)
                {
                    const char* ws = " \t\r\n";
                    const auto  b  = s.find_first_not_of(ws);
                    const auto  e  = s.find_last_not_of(ws);
                    if (b == std::string::npos) { s.clear(); return; }
                    s = s.substr(b, e - b + 1);
                };

                trim(key);
                trim(value);

                if (key == "position")
                {
                    std::istringstream vs(value);
                    vs >> position.x >> position.y >> position.z;
                }
                else if (key == "rotation")
                {
                    std::istringstream vs(value);
                    vs >> rotation.x >> rotation.y >> rotation.z;
                }
                else if (key == "scale")
                {
                    std::istringstream vs(value);
                    vs >> scale.x >> scale.y >> scale.z;
                }
                else if (key == "script")
                {
                    scriptName = value;
                }
            }

            // 엔티티 생성 및 Transform / Script 부착
            EntityId entity = world.CreateEntity();
            auto& t = world.AddTransform(entity);
            t.SetPosition(position.x, position.y, position.z)
             .SetRotation(rotation.x, rotation.y, rotation.z)
             .SetScale(scale.x, scale.y, scale.z);

            if (!scriptName.empty())
            {
                world.AddScript(entity, scriptName);
            }

            return entity;
        }

        bool SaveToFile(const World& world,
                        EntityId entity,
                        const std::filesystem::path& path)
        {
            if (entity == InvalidEntityId)
                return false;

            // Transform / Script 정보를 가져옵니다.
            const TransformComponent* transform = world.GetTransform(entity);
            if (!transform)
                return false;

            const ScriptComponent* script = world.GetScript(entity);
            std::string scriptName;
            if (script)
            {
                scriptName = script->scriptName;
            }

            // 부모 디렉터리가 없다면 생성합니다.
            const auto parent = path.parent_path();
            if (!parent.empty() && !std::filesystem::exists(parent))
            {
                std::filesystem::create_directories(parent);
            }

            std::ofstream ofs(path);
            if (!ofs.is_open())
                return false;

            // name 은 파일 이름(확장자 제외)으로 저장합니다.
            ofs << "name: " << path.stem().string() << "\n";
            ofs << "position: "
                << transform->position.x << " "
                << transform->position.y << " "
                << transform->position.z << "\n";
            ofs << "rotation: "
                << transform->rotation.x << " "
                << transform->rotation.y << " "
                << transform->rotation.z << "\n";
            ofs << "scale: "
                << transform->scale.x << " "
                << transform->scale.y << " "
                << transform->scale.z << "\n";

            ofs << "script: " << scriptName << "\n";

            return true;
        }
    }
}



