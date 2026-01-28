#include "Rendering/DebugDrawComponentSystem.h"
#include "Rendering/DebugDrawSystem.h"

#include "Components/DebugDrawBoxComponent.h"
#include "Components/SoundBoxComponent.h"
#include "Components/TransformComponent.h"

#include <algorithm>
#include <vector>

namespace Alice
{
	namespace
	{
        void AddBoxLines(DebugDrawSystem& dbg, const DirectX::XMFLOAT3 corners[8], const DirectX::XMFLOAT4& col)
        {
            // bottom
            dbg.AddLine(corners[0], corners[1], col);
            dbg.AddLine(corners[1], corners[2], col);
            dbg.AddLine(corners[2], corners[3], col);
            dbg.AddLine(corners[3], corners[0], col);
            // top
            dbg.AddLine(corners[4], corners[5], col);
            dbg.AddLine(corners[5], corners[6], col);
            dbg.AddLine(corners[6], corners[7], col);
            dbg.AddLine(corners[7], corners[4], col);
            // sides
            dbg.AddLine(corners[0], corners[4], col);
            dbg.AddLine(corners[1], corners[5], col);
            dbg.AddLine(corners[2], corners[6], col);
            dbg.AddLine(corners[3], corners[7], col);
        }
    }

    void DebugDrawComponentSystem::Build(World& world,
                                         DebugDrawSystem* overlay,
                                         DebugDrawSystem* depth,
                                         EntityId selectedEntity,
                                         bool debugEnabled,
                                         bool editorMode)
    {
        if (!overlay && !depth) return;

        // SoundBox -> DebugDrawBox 자동 연결 및 동기화
        {
            std::vector<EntityId> toAdd;
            toAdd.reserve(world.GetComponents<SoundBoxComponent>().size());

            for (const auto& [entityId, sb] : world.GetComponents<SoundBoxComponent>())
            {
                if (!world.GetComponent<DebugDrawBoxComponent>(entityId))
                {
                    toAdd.push_back(entityId);
                }
            }

            for (EntityId id : toAdd)
            {
                world.AddComponent<DebugDrawBoxComponent>(id);
            }

            for (const auto& [entityId, sb] : world.GetComponents<SoundBoxComponent>())
            {
                auto* dbg = world.GetComponent<DebugDrawBoxComponent>(entityId);
                if (!dbg) continue;
                dbg->boundsMin = sb.boundsMin;
                dbg->boundsMax = sb.boundsMax;
                dbg->enabled = editorMode ? true : sb.debugDraw;
                dbg->depthTest = false;

                if (editorMode)
                {
                    dbg->color = (entityId == selectedEntity)
                        ? DirectX::XMFLOAT4(1.0f, 0.2f, 0.2f, 1.0f)
                        : DirectX::XMFLOAT4(0.3f, 0.7f, 1.0f, 1.0f);
                }
            }
        }

        for (const auto& [entityId, box] : world.GetComponents<DebugDrawBoxComponent>())
        {
            if (!box.enabled) continue;

            const bool isSoundBox = (world.GetComponent<SoundBoxComponent>(entityId) != nullptr);
            if (!debugEnabled && !isSoundBox) continue;

            DebugDrawSystem* target = (box.depthTest && depth) ? depth : (overlay ? overlay : depth);
            if (!target) continue;

            const TransformComponent* tr = world.GetComponent<TransformComponent>(entityId);
            const DirectX::XMFLOAT3 pos = tr ? tr->position : DirectX::XMFLOAT3(0, 0, 0);
            const DirectX::XMFLOAT3 scale = tr ? tr->scale : DirectX::XMFLOAT3(1, 1, 1);

            const float minX = (std::min)(box.boundsMin.x * scale.x, box.boundsMax.x * scale.x);
            const float maxX = (std::max)(box.boundsMin.x * scale.x, box.boundsMax.x * scale.x);
            const float minY = (std::min)(box.boundsMin.y * scale.y, box.boundsMax.y * scale.y);
            const float maxY = (std::max)(box.boundsMin.y * scale.y, box.boundsMax.y * scale.y);
            const float minZ = (std::min)(box.boundsMin.z * scale.z, box.boundsMax.z * scale.z);
            const float maxZ = (std::max)(box.boundsMin.z * scale.z, box.boundsMax.z * scale.z);

            DirectX::XMFLOAT3 mn{ minX + pos.x, minY + pos.y, minZ + pos.z };
            DirectX::XMFLOAT3 mx{ maxX + pos.x, maxY + pos.y, maxZ + pos.z };

            DirectX::XMFLOAT3 corners[8] = {
                { mn.x, mn.y, mn.z }, { mx.x, mn.y, mn.z }, { mx.x, mn.y, mx.z }, { mn.x, mn.y, mx.z },
                { mn.x, mx.y, mn.z }, { mx.x, mx.y, mn.z }, { mx.x, mx.y, mx.z }, { mn.x, mx.y, mx.z }
            };

            AddBoxLines(*target, corners, box.color);
        }
    }
}
