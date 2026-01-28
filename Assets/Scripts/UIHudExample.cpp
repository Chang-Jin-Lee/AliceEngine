#include "UIHudExample.h"

#include "Core/ScriptFactory.h"
#include "Core/World.h"
#include "Core/Logger.h"

#include <rttr/registration>
#include <algorithm>

namespace Alice
{
    REGISTER_SCRIPT(UIHudExample);

    RTTR_REGISTRATION
    {
        using ThisClass = UIHudExample;
        rttr::registration::class_<UIHudExample>("UIHudExample")
            .constructor<>()
            .property("rootWidgetName", &UIHudExample::rootWidgetName)
                (rttr::metadata("SerializeField", true))
            ALICE_BIND_WIDGET(okButton, "OK_Button")
            ALICE_BIND_WIDGET(titleText, "Title_Text")
            ALICE_BIND_WIDGET_OPTIONAL(hpGauge, "HP_Gauge");
    }

    static EntityId FindRootWidgetByName(World& world, const std::string& name)
    {
        for (auto& [id, widget] : world.GetComponents<UIWidgetComponent>())
        {
            const std::string widgetName = widget.widgetName.empty() ? world.GetEntityName(id) : widget.widgetName;
            if (!widgetName.empty() && widgetName == name)
                return id;
        }
        return InvalidEntityId;
    }

    void UIHudExample::Start()
    {
        World* w = GetWorld();
        if (!w)
            return;

        const EntityId root = FindRootWidgetByName(*w, rootWidgetName);
        if (root == InvalidEntityId)
        {
            ALICE_LOG_WARN("[UIHudExample] Root widget not found: %s", rootWidgetName.c_str());
            return;
        }

        const auto result = AliceUI::BindWidgets(this, *w, root);
        if (result.missingRequired > 0)
        {
            ALICE_LOG_WARN("[UIHudExample] Missing required widgets: %d", result.missingRequired);
        }
    }

    void UIHudExample::Update(float /*deltaTime*/)
    {
        if (okButton && okButton->ConsumeClick())
        {
            ALICE_LOG_INFO("[UIHudExample] OK Button Clicked");
        }

        if (titleText)
        {
            titleText->text = "AliceUI HUD";
        }

        if (hpGauge)
        {
            hpGauge->value = std::max(0.0f, hpGauge->value - 0.001f);
        }
    }
}
