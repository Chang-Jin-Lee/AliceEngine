#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Core/Prefab.h"
#include "Core/ComponentRegistry.h"  // RTTR 등록 코드 포함
#include "Core/JsonRttr.h"

#include "Core/World.h"

namespace Alice
{
    namespace Prefab
    {
        EntityId InstantiateFromFile(World& world, const std::filesystem::path& path)
        {
            if (!std::filesystem::exists(path))
                return InvalidEntityId;

            JsonRttr::json root;
            if (!JsonRttr::LoadJsonFile(path, root))
                return InvalidEntityId;
            if (!root.is_object())
                return InvalidEntityId;

            // 엔티티 생성 및 Transform / Script 부착
            EntityId entity = world.CreateEntity();
            TransformComponent& t = world.AddTransform(entity);
            auto itT = root.find("Transform");
            if (itT != root.end() && itT->is_object())
            {
                rttr::instance inst = t;
                if (!JsonRttr::FromJsonObject(inst, *itT))
                    return InvalidEntityId;
            }

            const std::string name = root.value("name", std::string{});
            if (!name.empty())
                world.SetEntityName(entity, name);

            auto itS = root.find("Scripts");
            if (itS != root.end() && itS->is_array())
            {
                for (const auto& s : *itS)
                {
                    if (!s.is_object()) continue;
                    const std::string sn = s.value("name", std::string{});
                    if (sn.empty()) continue;

                    ScriptComponent& sc = world.AddScript(entity, sn);
                    sc.enabled = s.value("enabled", true);

                    auto itP = s.find("props");
                    if (itP != s.end() && itP->is_object() && sc.instance)
                    {
                        rttr::instance inst = *sc.instance;
                        const rttr::type t = rttr::type::get_by_name(sc.scriptName);
                        if (!JsonRttr::FromJsonObject(inst, *itP, t))
                            return InvalidEntityId;
                        sc.defaultsApplied = true; // 프리팹이 값 주입 완료
                    }
                }
            }

            return entity;
        }

        bool SaveToFile(const World& world,
                        EntityId entity,
                        const std::filesystem::path& path)
        {
            if (entity == InvalidEntityId) return false;

            const TransformComponent* t = world.GetTransform(entity);
            if (!t)
                return false;

            JsonRttr::json root = JsonRttr::json::object();
            root["version"] = 1;

            const std::string name = world.GetEntityName(entity);
            if (!name.empty())
                root["name"] = name;

            {
                rttr::instance inst = const_cast<TransformComponent&>(*t);
                root["Transform"] = JsonRttr::ToJsonObject(inst);
            }

            root["Scripts"] = JsonRttr::json::array();
            if (const auto* scripts = world.GetScripts(entity); scripts)
            {
                for (const auto& sc : *scripts)
                {
                    JsonRttr::json s = JsonRttr::json::object();
                    s["name"] = sc.scriptName;
                    s["enabled"] = sc.enabled;

                    if (sc.instance)
                    {
                        rttr::instance inst = *sc.instance;
                        const rttr::type t = rttr::type::get_by_name(sc.scriptName);
                        s["props"] = JsonRttr::ToJsonObject(inst, t);
                    }
                    root["Scripts"].push_back(s);
                }
            }

            return JsonRttr::SaveJsonFile(path, root, 4);
        }
    }
}



