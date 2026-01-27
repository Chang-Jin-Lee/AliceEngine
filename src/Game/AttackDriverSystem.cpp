#include "Game/AttackDriverSystem.h"

#include <functional>
#include <algorithm>

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

        std::uint64_t HashClip(const AttackDriverClip& clip, const std::string& resolvedName, float startTime, float endTime)
        {
            std::uint64_t h = 0;
            h = HashCombine(h, std::hash<std::string>{}(resolvedName));
            h = HashCombine(h, std::hash<float>{}(startTime));
            h = HashCombine(h, std::hash<float>{}(endTime));
            h = HashCombine(h, std::hash<bool>{}(clip.enabled));
            h = HashCombine(h, std::hash<int>{}(static_cast<int>(clip.source)));
            return h;
        }

        std::string ResolveClipName(const AttackDriverClip& clip, const AdvancedAnimationComponent& anim)
        {
            switch (clip.source)
            {
            case AttackDriverClipSource::BaseA:
                return anim.base.clipA;
            case AttackDriverClipSource::BaseB:
                return anim.base.clipB;
            case AttackDriverClipSource::UpperA:
                return anim.upper.clipA;
            case AttackDriverClipSource::UpperB:
                return anim.upper.clipB;
            case AttackDriverClipSource::Additive:
                return anim.additive.clip;
            case AttackDriverClipSource::Explicit:
            default:
                return clip.clipName;
            }
        }

        void SanitizeTimes(const AttackDriverClip& clip, float& outStart, float& outEnd)
        {
            outStart = std::max(0.0f, clip.startTimeSec);
            outEnd = std::max(0.0f, clip.endTimeSec);
            if (outEnd < outStart)
                std::swap(outStart, outEnd);
        }

        std::uint64_t HashClipList(const std::vector<AttackDriverClip>& clips, const AdvancedAnimationComponent& anim)
        {
            std::uint64_t h = 0;
            for (const auto& clip : clips)
            {
                const std::string resolved = ResolveClipName(clip, anim);
                float startTime = 0.0f;
                float endTime = 0.0f;
                SanitizeTimes(clip, startTime, endTime);
                h = HashCombine(h, HashClip(clip, resolved, startTime, endTime));
            }
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
            if (!anim || !anim->enabled || !anim->playing)
            {
                EntityId traceId = ResolveTraceEntity(world, driver, entityId);
                DeactivateTrace(world, traceId);
                continue;
            }

            const std::uint32_t gen = world.GetEntityGeneration(entityId);
            if (driver.notifyTag == 0)
            {
                driver.notifyTag = static_cast<std::uint64_t>(entityId);
            }

            const std::uint64_t currentHash = HashClipList(driver.clips, *anim);
            if (currentHash == driver.registeredHash)
                continue;

            anim->RemoveNotifiesByTag(driver.notifyTag);

            bool registeredAny = false;
            for (const auto& clip : driver.clips)
            {
                if (!clip.enabled)
                    continue;

                const std::string resolvedName = ResolveClipName(clip, *anim);
                if (resolvedName.empty())
                    continue;

                float startTime = 0.0f;
                float endTime = 0.0f;
                SanitizeTimes(clip, startTime, endTime);
                registeredAny = true;

                anim->AddNotify(resolvedName, startTime, [entityId, gen, &world]() {
                    if (!world.IsEntityValid(entityId, gen))
                        return;
                    auto* driverComp = world.GetComponent<AttackDriverComponent>(entityId);
                    if (!driverComp)
                        return;
                    EntityId traceId = ResolveTraceEntity(world, *driverComp, entityId);
                    ActivateTrace(world, traceId);
                }, driver.notifyTag);

                anim->AddNotify(resolvedName, endTime, [entityId, gen, &world]() {
                    if (!world.IsEntityValid(entityId, gen))
                        return;
                    auto* driverComp = world.GetComponent<AttackDriverComponent>(entityId);
                    if (!driverComp)
                        return;
                    EntityId traceId = ResolveTraceEntity(world, *driverComp, entityId);
                    DeactivateTrace(world, traceId);
                }, driver.notifyTag);
            }

            if (!registeredAny)
            {
                EntityId traceId = ResolveTraceEntity(world, driver, entityId);
                DeactivateTrace(world, traceId);
            }

            driver.registeredHash = currentHash;
        }
    }
}
