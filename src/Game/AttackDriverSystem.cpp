#include "Game/AttackDriverSystem.h"

#include "Core/World.h"
#include "Components/AttackDriverComponent.h"
#include "Components/WeaponTraceComponent.h"
#include "Components/AdvancedAnimationComponent.h"

namespace Alice
{
    namespace
    {
        EntityId ResolveTraceEntity(World& world, AttackDriverComponent& driver, EntityId self)
        {
            if (driver.traceCached != InvalidEntityId)
            {
                return driver.traceCached;
            }

            if (driver.traceGuid != 0)
            {
                EntityId resolved = world.FindEntityByGuid(driver.traceGuid);
                if (resolved != InvalidEntityId)
                {
                    driver.traceCached = resolved;
                    return resolved;
                }
            }

            return self;
        }

        void ActivateTrace(World& world, EntityId traceId)
        {
            auto* trace = world.GetComponent<WeaponTraceComponent>(traceId);
            if (!trace)
                return;

            trace->attackInstanceId++;
            trace->active = true;
            trace->hasPrevBasis = false;
            trace->hasPrevShapes = false;
            trace->prevCentersWS.clear();
            trace->prevRotsWS.clear();
            trace->hitVictims.clear();
            trace->lastAttackInstanceId = trace->attackInstanceId;
        }

        void DeactivateTrace(World& world, EntityId traceId)
        {
            auto* trace = world.GetComponent<WeaponTraceComponent>(traceId);
            if (!trace)
                return;

            trace->active = false;
        }
    }

    void AttackDriverSystem::Update(World& world)
    {
        for (auto&& [entityId, driver] : world.GetComponents<AttackDriverComponent>())
        {
            auto* anim = world.GetComponent<AdvancedAnimationComponent>(entityId);
            if (!anim || !anim->enabled)
                continue;

            if (driver.clipName.empty())
                continue;

            if (driver.registered && driver.registeredClipName != driver.clipName)
            {
                if (!driver.registeredClipName.empty())
                {
                    // Remove previously registered notifies to avoid duplicate firing.
                    anim->notifies.erase(driver.registeredClipName);
                }
                driver.registered = false;
            }

            if (driver.registered)
                continue;

            const std::uint32_t gen = world.GetEntityGeneration(entityId);
            const std::string clip = driver.clipName;

            anim->AddNotify(clip, driver.startTimeSec, [entityId, gen, &world]() {
                if (!world.IsEntityValid(entityId, gen))
                    return;
                auto* driverComp = world.GetComponent<AttackDriverComponent>(entityId);
                if (!driverComp)
                    return;
                EntityId traceId = ResolveTraceEntity(world, *driverComp, entityId);
                ActivateTrace(world, traceId);
            });

            anim->AddNotify(clip, driver.endTimeSec, [entityId, gen, &world]() {
                if (!world.IsEntityValid(entityId, gen))
                    return;
                auto* driverComp = world.GetComponent<AttackDriverComponent>(entityId);
                if (!driverComp)
                    return;
                EntityId traceId = ResolveTraceEntity(world, *driverComp, entityId);
                DeactivateTrace(world, traceId);
            });

            driver.registered = true;
            driver.registeredClipName = driver.clipName;
        }
    }
}
