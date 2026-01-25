#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <rttr/type.h>
#include <rttr/instance.h>

#include "Core/Entity.h"
#include "Core/JsonRttr.h"

namespace Alice
{
    class World;

    /// 인스펙터 수동 그리기 시 사용. ImGui 위젯 + changed/activated/deactivated 누적.
    struct InspectorUI
    {
        bool changed = false;
        bool activated = false;
        bool deactivatedAfterEdit = false;

        void Capture(bool editHappened);

        bool DragFloat(const char* label, float* v, float speed = 0.1f);
        bool DragFloatRange(const char* label, float* v, float speed, float min, float max);
        bool DragInt(const char* label, int* v, float speed = 1.0f);
        bool DragIntRange(const char* label, int* v, float speed, int min, int max);
        bool Checkbox(const char* label, bool* v);
        bool InputText(const char* label, char* buf, size_t bufSize);
    };

    struct EditorComponentDesc
    {
        rttr::type type;
        std::string displayName;
        std::string category;

        std::string fileKey;

        std::function<JsonRttr::json(const World&, EntityId)> serialize;
        std::function<bool(World&, EntityId, const JsonRttr::json&)> deserialize;

        bool addable = true;
        bool removable = true;

        std::function<bool(const World&, EntityId)> has;
        std::function<void(World&, EntityId)> add;
        std::function<void(World&, EntityId)> remove;
        std::function<rttr::instance(World&, EntityId)> getInstance;

        /// 인스펙터 수동 등록. 없으면 인스펙터에 안 뜸.
        std::function<void(World&, EntityId, InspectorUI&)> drawInspector;

        bool exposeInInspector = false;
        bool exposeInAddMenu = false;  // drawInspector 등록 시 true로 설정
        int inspectorOrder = 0;
        bool drawInspectorIncludesHeader = false;  // drawInspector가 CollapsingHeader를 포함하는지

        EditorComponentDesc(rttr::type t) : type(t) {}
    };

    class EditorComponentRegistry
    {
    public:
        static EditorComponentRegistry& Get()
        {
            static EditorComponentRegistry inst;
            return inst;
        }

        const std::vector<EditorComponentDesc>& All() const { return m_list; }

        const EditorComponentDesc* Find(rttr::type t) const
        {
            for (auto& d : m_list)
                if (d.type == t)
                    return &d;
            return nullptr;
        }

        EditorComponentDesc* FindMutable(rttr::type t)
        {
            for (auto& d : m_list)
                if (d.type == t)
                    return &d;
            return nullptr;
        }

        template<typename T>
        void Register(std::string_view displayName,
                     std::string_view category,
                     std::function<void(World&, EntityId)> addFn = {},
                     bool addable = true,
                     bool removable = true,
                     std::string_view fileKey = {},
                     std::function<JsonRttr::json(const World&, EntityId)> serialize = {},
                     std::function<bool(World&, EntityId, const JsonRttr::json&)> deserialize = {});

        template<typename T>
        void SetInspector(std::function<void(World&, EntityId, InspectorUI&)> fn,
                         int order = 0,
                         bool exposeInspector = true,
                         bool exposeAddMenu = true,
                         bool includesHeader = false);

        void SortByCategoryThenName();

    private:
        std::vector<EditorComponentDesc> m_list;
    };

    void LinkEditorComponentRegistry();
}

// 템플릿 구현은 헤더에 포함 (링크 이슈 방지)
#include "Core/EditorComponentRegistry.inl"
