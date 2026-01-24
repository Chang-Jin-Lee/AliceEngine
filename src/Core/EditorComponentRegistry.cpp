#include "Core/EditorComponentRegistry.h"
#include "imgui.h"
#include <algorithm>

namespace Alice
{
    void InspectorUI::Capture(bool editHappened)
    {
        if (editHappened)
            changed = true;
        if (ImGui::IsItemActivated())
            activated = true;
        if (ImGui::IsItemDeactivatedAfterEdit())
            deactivatedAfterEdit = true;
    }

    bool InspectorUI::DragFloat(const char* label, float* v, float speed)
    {
        bool ch = ImGui::DragFloat(label, v, speed);
        Capture(ch);
        return ch;
    }

    bool InspectorUI::DragFloatRange(const char* label, float* v, float speed, float min, float max)
    {
        bool ch = ImGui::DragFloat(label, v, speed, min, max);
        Capture(ch);
        return ch;
    }

    bool InspectorUI::DragInt(const char* label, int* v, float speed)
    {
        bool ch = ImGui::DragInt(label, v, speed);
        Capture(ch);
        return ch;
    }

    bool InspectorUI::DragIntRange(const char* label, int* v, float speed, int min, int max)
    {
        bool ch = ImGui::DragInt(label, v, speed, min, max);
        Capture(ch);
        return ch;
    }

    bool InspectorUI::Checkbox(const char* label, bool* v)
    {
        bool ch = ImGui::Checkbox(label, v);
        Capture(ch);
        return ch;
    }

    bool InspectorUI::InputText(const char* label, char* buf, size_t bufSize)
    {
        bool ch = ImGui::InputText(label, buf, bufSize);
        Capture(ch);
        return ch;
    }

    void LinkEditorComponentRegistry() {}

    void EditorComponentRegistry::SortByCategoryThenName()
    {
        std::sort(m_list.begin(), m_list.end(),
            [](const EditorComponentDesc& a, const EditorComponentDesc& b)
            {
                if (a.category != b.category)
                    return a.category < b.category;
                return a.displayName < b.displayName;
            });
    }
}
