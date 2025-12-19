#include "Core/Prefab.h"
#include "Core/ReflectionSerializer.h"
#include "Core/ComponentRegistry.h"  // RTTR 등록 코드 포함

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

            TransformComponent transform;
            std::string scriptName;

            // RTTR 기반으로 Transform 로드
            if (!ReflectionSerializer::Load(path, transform))
                return InvalidEntityId;

            // Script는 별도로 처리 (문자열만)
            std::ifstream file(path);
            if (file.is_open())
            {
                std::string line;
                while (std::getline(file, line))
                {
                    std::istringstream iss(line);
                    std::string key;
                    if (!std::getline(iss, key, ':'))
                        continue;

                    std::string value;
                    std::getline(iss, value);

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

                    if (key == "script")
                    {
                        scriptName = value;
                        break;
                    }
                }
            }

            // 엔티티 생성 및 Transform / Script 부착
            EntityId entity = world.CreateEntity();
            world.AddTransform(entity) = transform;

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
            if (entity == InvalidEntityId) return false;

            // Transform / Script 정보를 조회합니다.
            const TransformComponent* transform = world.GetTransform(entity);
            if (!transform)
            {
                // Transform 이 없는 엔티티는 프리팹으로 저장하지 않습니다.
                return false;
            }

            // RTTR 기반으로 Transform 저장
            bool result = ReflectionSerializer::Save(path, *transform);
            if (!result)
                return false;

            // Script는 별도로 추가 (문자열만)
            const ScriptComponent* script = world.GetScript(entity);
            if (script && !script->scriptName.empty())
            {
                std::ofstream ofs(path, std::ios::app);
                if (ofs.is_open())
                {
                    ofs << "script: " << script->scriptName << "\n";
                }
            }

            return true;
        }
    }
}



