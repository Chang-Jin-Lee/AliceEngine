#include "Editor/EditorCore.h"
#include "Core/EditorComponentRegistry.h"
#include "Core/World.h"
#include "Components/TransformComponent.h"
#include "Components/MaterialComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/ComputeEffectComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkinnedAnimationComponent.h"

namespace Alice
{
    void RegisterFixedLayoutInspectors(EditorCore& core)
    {
        auto& reg = EditorComponentRegistry::Get();

        // === 고정 레이아웃 컴포넌트 (특별한 순서로 그려지는 것들) ===
        // 기존 DrawInspectorXXX 함수를 그대로 사용
        reg.SetInspector<TransformComponent>(
            [&core](World& w, EntityId e, InspectorUI& ui)
            {
                core.DrawInspectorTransform(w, e);
            },
            -100, true, false, true);  // inspectorOrder -100, Add 메뉴 안 뜸, includesHeader=true

        reg.SetInspector<MaterialComponent>(
            [&core](World& w, EntityId e, InspectorUI& ui)
            {
                core.DrawInspectorMaterial(w, e);
            },
            -90, true, true, false);  // Material은 CollapsingHeader 없음

        reg.SetInspector<PointLightComponent>(
            [&core](World& w, EntityId e, InspectorUI& ui)
            {
                core.DrawInspectorPointLight(w, e);
            },
            -80, true, true, true);  // includesHeader=true

        reg.SetInspector<SpotLightComponent>(
            [&core](World& w, EntityId e, InspectorUI& ui)
            {
                core.DrawInspectorSpotLight(w, e);
            },
            -80, true, true, true);

        reg.SetInspector<RectLightComponent>(
            [&core](World& w, EntityId e, InspectorUI& ui)
            {
                core.DrawInspectorRectLight(w, e);
            },
            -80, true, true, true);

        reg.SetInspector<ComputeEffectComponent>(
            [&core](World& w, EntityId e, InspectorUI& ui)
            {
                core.DrawInspectorComputeEffect(w, e);
            },
            -70, true, true, true);

        // SkinnedMesh와 SkinnedAnimation은 DrawInspectorAnimationStatus에서 처리되므로
        // 여기서는 등록하지 않음 (또는 별도 처리)
    }
}
