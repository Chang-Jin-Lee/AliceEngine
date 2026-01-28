#pragma once

#include <string>
#include <rttr/type>
#include <rttr/instance>
#include <rttr/registration>

#include "Core/Entity.h"
#include "Core/World.h"
#include "AliceUI/UIWidgetComponent.h"
#include "AliceUI/UITransformComponent.h"
#include "AliceUI/UIImageComponent.h"
#include "AliceUI/UITextComponent.h"
#include "AliceUI/UIButtonComponent.h"
#include "AliceUI/UIGaugeComponent.h"

#define UI_META_BIND_WIDGET "BindWidget"
#define UI_META_OPTIONAL    "Optional"
#define UI_META_WIDGET_NAME "WidgetName"

#define ALICE_BIND_WIDGET(member, ...) \
    .property(#member, &ThisClass::member) \
    ( rttr::metadata(UI_META_BIND_WIDGET, true), \
      rttr::metadata(UI_META_OPTIONAL, false) \
      __VA_OPT__(, rttr::metadata(UI_META_WIDGET_NAME, __VA_ARGS__)) )

#define ALICE_BIND_WIDGET_OPTIONAL(member, ...) \
    .property(#member, &ThisClass::member) \
    ( rttr::metadata(UI_META_BIND_WIDGET, true), \
      rttr::metadata(UI_META_OPTIONAL, true) \
      __VA_OPT__(, rttr::metadata(UI_META_WIDGET_NAME, __VA_ARGS__)) )

namespace Alice
{
	namespace AliceUI
	{
		inline bool IsBindWidgetProperty(const rttr::property& prop)
		{
			auto v = prop.get_metadata(UI_META_BIND_WIDGET);
			return v.is_valid() && v.to_bool();
		}

		inline bool IsOptionalBind(const rttr::property& prop)
		{
			auto v = prop.get_metadata(UI_META_OPTIONAL);
			return v.is_valid() && v.to_bool();
		}

		inline std::string GetWidgetBindName(const rttr::property& prop)
		{
			auto v = prop.get_metadata(UI_META_WIDGET_NAME);
			if (v.is_valid()) return v.to_string();
			return prop.get_name().to_string();
		}

		inline std::string GetWidgetNameForEntity(World& world, EntityId id)
		{
			if (const auto* widget = world.GetComponent<UIWidgetComponent>(id))
			{
				if (!widget->widgetName.empty())
					return widget->widgetName;
			}
			return world.GetEntityName(id);
		}

		inline EntityId FindWidgetByName(World& world, EntityId root, const std::string& name)
		{
			if (root == InvalidEntityId)
				return InvalidEntityId;

			const std::string widgetName = GetWidgetNameForEntity(world, root);
			if (!widgetName.empty() && widgetName == name)
				return root;

			for (EntityId child : world.GetChildren(root))
			{
				EntityId found = FindWidgetByName(world, child, name);
				if (found != InvalidEntityId)
					return found;
			}

			return InvalidEntityId;
		}

		struct BindWidgetResult
		{
			int boundCount = 0;
			int missingRequired = 0;
		};

		inline void* ResolveWidgetPointer(World& world, EntityId id, const rttr::type& rawType)
		{
			if (rawType == rttr::type::get<UIWidgetComponent>()) return world.GetComponent<UIWidgetComponent>(id);
			if (rawType == rttr::type::get<UITransformComponent>()) return world.GetComponent<UITransformComponent>(id);
			if (rawType == rttr::type::get<UIImageComponent>()) return world.GetComponent<UIImageComponent>(id);
			if (rawType == rttr::type::get<UITextComponent>()) return world.GetComponent<UITextComponent>(id);
			if (rawType == rttr::type::get<UIButtonComponent>()) return world.GetComponent<UIButtonComponent>(id);
			if (rawType == rttr::type::get<UIGaugeComponent>()) return world.GetComponent<UIGaugeComponent>(id);
			return nullptr;
		}

		template<typename TOwner>
		BindWidgetResult BindWidgets(TOwner* owner, World& world, EntityId root)
		{
			BindWidgetResult res;
			if (!owner || root == InvalidEntityId)
				return res;

			rttr::instance inst = *owner;
			rttr::type t = inst.get_type();

			for (auto& prop : t.get_properties())
			{
				if (!IsBindWidgetProperty(prop))
					continue;

				const std::string widgetName = GetWidgetBindName(prop);
				EntityId found = FindWidgetByName(world, root, widgetName);
				const bool optional = IsOptionalBind(prop);

				if (found == InvalidEntityId)
				{
					if (!optional)
						res.missingRequired++;
					continue;
				}

				const rttr::type propType = prop.get_type();
				if (propType == rttr::type::get<EntityId>())
				{
					prop.set_value(inst, found);
					res.boundCount++;
					continue;
				}

				if (!propType.is_pointer())
					continue;

				const rttr::type raw = propType.get_raw_type();
				void* ptr = ResolveWidgetPointer(world, found, raw);
				if (!ptr)
				{
					if (!optional)
						res.missingRequired++;
					continue;
				}

				prop.set_value(inst, ptr);
				res.boundCount++;
			}

			return res;
		}
	}
}
