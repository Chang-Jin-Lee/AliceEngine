#include "Game/AttackDriverSystem.h"

#include <functional>

#include "Core/World.h"
#include "Components/AttackDriverComponent.h"
#include "Components/WeaponTraceComponent.h"
#include "Components/AdvancedAnimationComponent.h"

namespace Alice
{
    namespace
    {
        std::uint64_t HashCombine(std::uint64_t seed, std::uint64_t value)
        {
            return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
        }

        std::uint64_t HashClip(const AttackDriverClip& clip)
        {
            std::uint64_t h = 0;
            h = HashCombine(h, std::hash<std::string>{}(clip.clipName));
            h = HashCombine(h, std::hash<float>{}(clip.startTimeSec));
            h = HashCombine(h, std::hash<float>{}(clip.endTimeSec));
            h = HashCombine(h, std::hash<bool>{}(clip.enabled));
            return h;
        }

        std::uint64_t HashClipList(const std::vector<AttackDriverClip>& clips)
        {
            std::uint64_t h = 0;
            for (const auto& clip : clips)
                h = HashCombine(h, HashClip(clip));
            return h;
        }

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

            const std::uint32_t gen = world.GetEntityGeneration(entityId);
            const std::uint64_t currentHash = HashClipList(driver.clips);
            if (currentHash == driver.registeredHash)
                continue;

            for (const auto& clipName : driver.registeredClipNames)
            {
                anim->notifies.erase(clipName);
            }
            driver.registeredClipNames.clear();

            for (const auto& clip : driver.clips)
            {
                if (!clip.enabled || clip.clipName.empty())
                    continue;

                driver.registeredClipNames.insert(clip.clipName);

                anim->AddNotify(clip.clipName, clip.startTimeSec, [entityId, gen, &world]() {
                    if (!world.IsEntityValid(entityId, gen))
                        return;
                    auto* driverComp = world.GetComponent<AttackDriverComponent>(entityId);
                    if (!driverComp)
                        return;
                    EntityId traceId = ResolveTraceEntity(world, *driverComp, entityId);
                    ActivateTrace(world, traceId);
                });

                anim->AddNotify(clip.clipName, clip.endTimeSec, [entityId, gen, &world]() {
                    if (!world.IsEntityValid(entityId, gen))
                        return;
                    auto* driverComp = world.GetComponent<AttackDriverComponent>(entityId);
                    if (!driverComp)
                        return;
                    EntityId traceId = ResolveTraceEntity(world, *driverComp, entityId);
                    DeactivateTrace(world, traceId);
                });
            }

            driver.registeredHash = currentHash;
        }
    }
}
