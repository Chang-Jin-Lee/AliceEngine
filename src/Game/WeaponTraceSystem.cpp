#include "Game/WeaponTraceSystem.h"

#include <cmath>
#include <vector>

#include <DirectXMath.h>

#include "Core/World.h"
#include "Components/IDComponent.h"
#include "Components/WeaponTraceComponent.h"
#include "Components/HurtboxComponent.h"
#include "Components/AdvancedAnimationComponent.h"
#include "Components/SocketComponent.h"
#include "Components/SocketPoseOutputComponent.h"
#include "Game/CombatPhysicsLayers.h"
#include "Game/CombatHitEvent.h"
#include "PhysX/IPhysicsWorld.h"

namespace Alice
{
    namespace
    {
        EntityId ResolveOwner(World& world, WeaponTraceComponent& trace, EntityId self)
        {
            if (trace.ownerCached != InvalidEntityId)
            {
                if (trace.ownerGuid == 0)
                    return trace.ownerCached;

                if (const auto* idc = world.GetComponent<IDComponent>(trace.ownerCached))
                {
                    if (idc->guid == trace.ownerGuid)
                        return trace.ownerCached;
                }
            }

            if (trace.ownerGuid == 0)
                return self;

            EntityId resolved = world.FindEntityByGuid(trace.ownerGuid);
            if (resolved == InvalidEntityId)
                return InvalidEntityId;

            trace.ownerCached = resolved;
            return resolved;
        }

        bool TryGetSocketWorldMatrix(World& world, EntityId owner, const std::string& socketName, DirectX::XMMATRIX& out)
        {
            if (auto* poses = world.GetComponent<SocketPoseOutputComponent>(owner))
            {
                for (const auto& p : poses->poses)
                {
                    if (p.name == socketName)
                    {
                        out = DirectX::XMLoadFloat4x4(&p.world);
                        return true;
                    }
                }
            }

            // 1) Match by socket name (e.g. "Trace_Base", "Trace_Tip")
            if (auto* adv = world.GetComponent<AdvancedAnimationComponent>(owner))
            {
                for (const auto& s : adv->sockets)
                {
                    if (s.name == socketName)
                    {
                        out = DirectX::XMLoadFloat4x4(&s.worldMatrix);
                        return true;
                    }
                }
                // 2) Fallback: match by parent bone name (e.g. "??.R")
                for (const auto& s : adv->sockets)
                {
                    if (s.parentBone == socketName)
                    {
                        out = DirectX::XMLoadFloat4x4(&s.worldMatrix);
                        return true;
                    }
                }
            }

            if (auto* sc = world.GetComponent<SocketComponent>(owner))
            {
                for (const auto& s : sc->sockets)
                {
                    if (s.name == socketName)
                    {
                        out = DirectX::XMLoadFloat4x4(&s.world);
                        return true;
                    }
                }
                for (const auto& s : sc->sockets)
                {
                    if (s.parentBone == socketName)
                    {
                        out = DirectX::XMLoadFloat4x4(&s.world);
                        return true;
                    }
                }
            }

            return false;
        }
    }

    void WeaponTraceSystem::Update(World& world, float /*dtSec*/, std::vector<CombatHitEvent>* outHits)
    {
        auto* physics = world.GetPhysicsWorld();
        if (!physics)
            return;

        auto&& traces = world.GetComponents<WeaponTraceComponent>(); // & -> &&�� �ٲ�
        if (traces.empty())
            return;

        using namespace DirectX;

        for (auto&& [eid, trace] : traces)
        {
            if (!trace.active)
            {
                trace.prevPositions.clear();
                trace.hasPrevPositions = false;
                trace.hitVictims.clear();
                continue;
            }

            const EntityId owner = ResolveOwner(world, trace, eid);
            if (owner == InvalidEntityId || trace.traceSocketNames.empty())
                continue;

            if (trace.lastAttackInstanceId != trace.attackInstanceId)
            {
                trace.hitVictims.clear();
                trace.lastAttackInstanceId = trace.attackInstanceId;
            }

            const size_t socketCount = trace.traceSocketNames.size();
            if (trace.prevPositions.size() != socketCount)
            {
                trace.prevPositions.assign(socketCount, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
                trace.hasPrevPositions = false;
            }

            std::vector<DirectX::XMFLOAT3> currPositions(socketCount);
            for (size_t i = 0; i < socketCount; ++i)
            {
                XMMATRIX socketWorld = XMMatrixIdentity();
                if (!TryGetSocketWorldMatrix(world, owner, trace.traceSocketNames[i], socketWorld))
                {
                    currPositions[i] = trace.prevPositions[i];
                    continue;
                }

                XMVECTOR S, R, T;
                if (!XMMatrixDecompose(&S, &R, &T, socketWorld))
                {
                    currPositions[i] = trace.prevPositions[i];
                    continue;
                }

                XMStoreFloat3(&currPositions[i], T);
            }

            if (!trace.hasPrevPositions)
            {
                trace.prevPositions = currPositions;
                trace.hasPrevPositions = true;
                continue;
            }

            const uint32_t targetLayerBits = (trace.targetLayerBits != 0u)
                ? trace.targetLayerBits
                : ((trace.teamId == 0) ? CombatPhysicsLayers::EnemyHurtboxBit : CombatPhysicsLayers::PlayerHurtboxBit);
            const uint32_t queryLayerBits = (trace.queryLayerBits != 0u)
                ? trace.queryLayerBits
                : CombatPhysicsLayers::AttackQueryLayerBit(trace.teamId);

            SceneQueryFilter filter{};
            filter.layerMask = targetLayerBits;
            filter.queryMask = queryLayerBits;
            filter.hitTriggers = true;

            std::vector<SweepHit> hits;
            hits.reserve(32);

            for (size_t i = 0; i < socketCount; ++i)
            {
                const DirectX::XMFLOAT3& prev = trace.prevPositions[i];
                const DirectX::XMFLOAT3& cur = currPositions[i];
                const DirectX::XMFLOAT3 delta{ cur.x - prev.x, cur.y - prev.y, cur.z - prev.z };

                const float dist = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
                if (dist < 0.0001f)
                    continue;

                const Vec3 origin(prev.x, prev.y, prev.z);
                const Vec3 dir(delta.x / dist, delta.y / dist, delta.z / dist);

                hits.clear();
                const uint32_t hitCount = physics->SweepSphereAllQ(origin, trace.radius, dir, dist, hits, filter);
                if (hitCount == 0)
                    continue;

                for (uint32_t h = 0; h < hitCount && h < hits.size(); ++h)
                {
                    const auto& hit = hits[h];
                    if (!hit.userData)
                        continue;

                    EntityId hitEntity = world.ExtractEntityIdFromUserData(hit.userData);
                    if (hitEntity == InvalidEntityId)
                        continue;

                    auto* hurt = world.GetComponent<HurtboxComponent>(hitEntity);
                    if (!hurt)
                        continue;

                    if (hurt->teamId == trace.teamId)
                        continue;

                    const std::uint64_t victimGuid = (hurt->ownerGuid != 0)
                        ? hurt->ownerGuid
                        : static_cast<std::uint64_t>(hitEntity);

                    if (trace.hitVictims.find(victimGuid) != trace.hitVictims.end())
                        continue;

                    trace.hitVictims.insert(victimGuid);

                    if (outHits)
                    {
                        CombatHitEvent ev{};
                        ev.attackerOwner = owner;
                        ev.victimOwner = (hurt->ownerGuid != 0)
                            ? world.FindEntityByGuid(hurt->ownerGuid)
                            : (hurt->ownerCached != InvalidEntityId ? hurt->ownerCached : hitEntity);
                        ev.hurtboxEntity = hitEntity;
                        ev.part = hurt->part;
                        ev.attackInstanceId = trace.attackInstanceId;
                        ev.damage = trace.baseDamage * hurt->damageScale;
                        ev.debugLog = trace.debugDraw;
                        ev.hitPosWS = DirectX::XMFLOAT3(hit.position.x, hit.position.y, hit.position.z);
                        ev.hitNormalWS = DirectX::XMFLOAT3(hit.normal.x, hit.normal.y, hit.normal.z);
                        outHits->push_back(ev);
                    }
                }
            }

            trace.prevPositions = currPositions;
        }
    }
}
