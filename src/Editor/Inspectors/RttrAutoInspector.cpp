#include "Core/EditorComponentRegistry.h"
#include "Core/World.h"
#include "Core/ReflectionUI.h"
#include "Components/CameraComponent.h"
#include "Components/CameraFollowComponent.h"
#include "Components/CameraSpringArmComponent.h"
#include "Components/CameraLookAtComponent.h"
#include "Components/CameraShakeComponent.h"
#include "Components/CameraBlendComponent.h"
#include "Components/CameraInputComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/EffectComponent.h"
#include "Components/TrailEffectComponent.h"
#include "Components/AdvancedAnimationComponent.h"
#include "PhysX/Components/Phy_RigidBodyComponent.h"

namespace Alice
{
    // RTTR 기반 자동 인스펙터 헬퍼
    static ReflectionUI::UIEditEvent RenderInspectorInstance(rttr::instance inst, World* world)
    {
        ReflectionUI::UIEditEvent result{};
        if (!inst.is_valid()) return result;

        rttr::type t = inst.get_type();
        for (auto& prop : t.get_properties())
        {
            const std::string propName = prop.get_name().to_string();

            ReflectionUI::UIEditEvent ev{};
            if (propName == "roughness" || propName == "metalness")
            {
                ev = ReflectionUI::Detail::RenderPropertyWithRange(prop, inst, 0.0f, 1.0f, "", world);
            }
            else
            {
                ev = ReflectionUI::Detail::RenderProperty(prop, inst, "", world);
            }

            result.changed |= ev.changed;
            result.activated |= ev.activated;
            result.deactivatedAfterEdit |= ev.deactivatedAfterEdit;
        }
        return result;
    }

    template<typename T>
    static void RegisterRttrAutoInspector()
    {
        auto& reg = EditorComponentRegistry::Get();
        const EditorComponentDesc* desc = reg.Find(rttr::type::get<T>());
        if (!desc) return;
        reg.SetInspector<T>(
            [desc](World& w, EntityId e, InspectorUI& ui)
            {
                rttr::instance inst = desc->getInstance(w, e);
                ReflectionUI::UIEditEvent ev = RenderInspectorInstance(inst, &w);
                ui.changed = ev.changed;
                ui.activated = ev.activated;
                ui.deactivatedAfterEdit = ev.deactivatedAfterEdit;
            },
            0, true, true, false);  // includesHeader=false (registry 루프에서 만듦)
    }

    void RegisterRttrAutoInspectors()
    {
        // === 기본 컴포넌트 (RTTR 자동 인스펙터) ===
        RegisterRttrAutoInspector<CameraComponent>();
        RegisterRttrAutoInspector<CameraFollowComponent>();
        RegisterRttrAutoInspector<CameraSpringArmComponent>();
        RegisterRttrAutoInspector<CameraLookAtComponent>();
        RegisterRttrAutoInspector<CameraShakeComponent>();
        RegisterRttrAutoInspector<CameraBlendComponent>();
        RegisterRttrAutoInspector<CameraInputComponent>();
        RegisterRttrAutoInspector<PointLightComponent>();
        RegisterRttrAutoInspector<SpotLightComponent>();
        RegisterRttrAutoInspector<RectLightComponent>();
        RegisterRttrAutoInspector<EffectComponent>();
        RegisterRttrAutoInspector<TrailEffectComponent>();
        RegisterRttrAutoInspector<AdvancedAnimationComponent>();
        RegisterRttrAutoInspector<Phy_RigidBodyComponent>();
    }
}
