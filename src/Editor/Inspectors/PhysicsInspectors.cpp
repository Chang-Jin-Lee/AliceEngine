#include "Editor/EditorCore.h"
#include "Core/EditorComponentRegistry.h"
#include "Core/World.h"
#include "PhysX/Components/Phy_ColliderComponent.h"
#include "PhysX/Components/Phy_MeshColliderComponent.h"
#include "PhysX/Components/Phy_CCTComponent.h"
#include "PhysX/Components/Phy_TerrainHeightFieldComponent.h"
#include "PhysX/Components/Phy_SettingsComponent.h"
#include "PhysX/Components/Phy_JointComponent.h"

namespace Alice
{
    void RegisterPhysicsInspectors(EditorCore& core)
    {
        auto& reg = EditorComponentRegistry::Get();

        // === 물리 컴포넌트 (전용 인스펙터) ===
        // 기존 DrawInspectorXXX 함수를 그대로 사용 (CollapsingHeader 포함)
        reg.SetInspector<Phy_ColliderComponent>(
            [&core](World& w, EntityId e, InspectorUI& ui)
            {
                core.DrawInspectorCollider(w, e);
            },
            0, true, true, true);  // includesHeader=true

        reg.SetInspector<Phy_MeshColliderComponent>(
            [&core](World& w, EntityId e, InspectorUI& ui)
            {
                core.DrawInspectorMeshCollider(w, e);
            },
            0, true, true, true);

        reg.SetInspector<Phy_CCTComponent>(
            [&core](World& w, EntityId e, InspectorUI& ui)
            {
                core.DrawInspectorCharacterController(w, e);
            },
            0, true, true, true);

        reg.SetInspector<Phy_TerrainHeightFieldComponent>(
            [&core](World& w, EntityId e, InspectorUI& ui)
            {
                core.DrawInspectorTerrainHeightField(w, e);
            },
            0, true, true, true);

        reg.SetInspector<Phy_SettingsComponent>(
            [&core](World& w, EntityId e, InspectorUI& ui)
            {
                core.DrawInspectorPhysicsSceneSettings(w, e);
            },
            0, true, true, true);

        reg.SetInspector<Phy_JointComponent>(
            [&core](World& w, EntityId e, InspectorUI& ui)
            {
                core.DrawInspectorJoint(w, e);
            },
            0, true, true, true);
    }
}
