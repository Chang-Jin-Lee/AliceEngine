#pragma once

#include "Core/EditorComponentRegistry.h"
#include "Core/World.h"
#include "Core/JsonRttr.h"
#include "Core/Logger.h"
#include <algorithm>

namespace Alice
{
    template<typename T>
    EditorComponentDesc MakeDefaultDesc(std::string_view displayName,
                                       std::string_view category,
                                       std::function<void(World&, EntityId)> addFn,
                                       bool addable,
                                       bool removable,
                                       std::string_view fileKey,
                                       std::function<JsonRttr::json(const World&, EntityId)> serialize,
                                       std::function<bool(World&, EntityId, const JsonRttr::json&)> deserialize)
    {
        const rttr::type typeObj = rttr::type::get<T>();
        EditorComponentDesc d(typeObj);
        d.displayName = std::string(displayName.empty() ? typeObj.get_name().to_string() : std::string(displayName));
        d.category = std::string(category.empty() ? "Misc" : std::string(category));
        d.fileKey = fileKey.empty() ? typeObj.get_name().to_string() : std::string(fileKey);

        d.addable = addable;
        d.removable = removable;

        d.has = [](const World& w, EntityId e) { return w.GetComponent<T>(e) != nullptr; };

        if (addFn)
            d.add = std::move(addFn);
        else
            d.add = [](World& w, EntityId e) { w.AddComponent<T>(e); };

        d.remove = [removable](World& w, EntityId e)
        {
            if (!removable) return;
            w.RemoveComponent<T>(e);
        };

        d.getInstance = [](World& w, EntityId e) -> rttr::instance
        {
            if (auto* p = w.GetComponent<T>(e))
                return *p; // lvalue ref instance
            return rttr::instance{};
        };

        // 기본 serialize/deserialize (커스텀 훅이 없으면 RTTR 기반)
        if (serialize)
        {
            d.serialize = std::move(serialize);
        }
        else
        {
            d.serialize = [](const World& w, EntityId e) -> JsonRttr::json
            {
                auto* p = w.GetComponent<T>(e);
                if (!p) return JsonRttr::json(); // null
                rttr::instance inst = const_cast<T&>(*p);
                return JsonRttr::ToJsonObject(inst);
            };
        }

        if (deserialize)
        {
            d.deserialize = std::move(deserialize);
        }
        else
        {
            d.deserialize = [addFn = d.add](World& w, EntityId e, const JsonRttr::json& j) -> bool
            {
                if (!w.GetComponent<T>(e))
                {
                    if (addFn) addFn(w, e);
                    else w.AddComponent<T>(e);
                }
                auto* p = w.GetComponent<T>(e);
                if (!p) return false;
                rttr::instance inst = *p;
                return JsonRttr::FromJsonObject(inst, j);
            };
        }

        return d;
    }

    template<typename T>
    void EditorComponentRegistry::Register(std::string_view displayName,
                                          std::string_view category,
                                          std::function<void(World&, EntityId)> addFn,
                                          bool addable,
                                          bool removable,
                                          std::string_view fileKey,
                                          std::function<JsonRttr::json(const World&, EntityId)> serialize,
                                          std::function<bool(World&, EntityId, const JsonRttr::json&)> deserialize)
    {
        const rttr::type t = rttr::type::get<T>();
        if (Find(t)) return;

        m_list.push_back(MakeDefaultDesc<T>(displayName, category, std::move(addFn), addable, removable, fileKey, std::move(serialize), std::move(deserialize)));
    }

    template<typename T>
    void EditorComponentRegistry::SetInspector(std::function<void(World&, EntityId, InspectorUI&)> fn,
                                              int order,
                                              bool exposeInspector,
                                              bool exposeAddMenu,
                                              bool includesHeader)
    {
        EditorComponentDesc* d = FindMutable(rttr::type::get<T>());
        if (!d)
        {
            ALICE_LOG_ERRORF("SetInspector failed: component type '%s' not registered in EditorComponentRegistry", 
                            rttr::type::get<T>().get_name().to_string().c_str());
            return;
        }
        d->drawInspector = std::move(fn);
        d->inspectorOrder = order;
        d->exposeInInspector = exposeInspector;
        d->exposeInAddMenu = exposeAddMenu;
        d->drawInspectorIncludesHeader = includesHeader;
    }
}
