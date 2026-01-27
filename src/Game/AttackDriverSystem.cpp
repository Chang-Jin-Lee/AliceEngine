#include "Game/AttackDriverSystem.h"

#include <functional>
#include <algorithm>
#include <string>

#include <assimp/scene.h>

#include "Core/World.h"
#include "Components/AttackDriverComponent.h"
#include "Components/WeaponTraceComponent.h"
#include "Components/AdvancedAnimationComponent.h"
#include "Components/SkinnedAnimationComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Rendering/SkinnedMeshRegistry.h"
#include "3Dmodel/FbxModel.h"

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

        bool HasNotifyTag(const AdvancedAnimationComponent& anim, std::uint64_t ownerTag)
        {
            if (ownerTag == 0)
                return false;

            for (const auto& kv : anim.notifies)
            {
                for (const auto& notify : kv.second)
                {
                    if (notify.ownerTag == ownerTag)
                        return true;
                }
            }
            return false;
        }

        bool HasAnyEnabledClip(const AttackDriverComponent& driver, const AdvancedAnimationComponent& anim)
        {
            for (const auto& clip : driver.clips)
            {
                if (!clip.enabled)
                    continue;

                const std::string resolved = ResolveClipName(clip, anim);
                if (!resolved.empty())
                    return true;
            }
            return false;
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

        bool TryGetClipTime(const AttackDriverClip& clip, const AdvancedAnimationComponent& anim, float& outTime)
        {
            const std::string resolved = ResolveClipName(clip, anim);
            if (resolved.empty())
                return false;

            if (clip.source == AttackDriverClipSource::Explicit)
            {
                if (anim.base.clipA == resolved) { outTime = anim.base.timeA; return true; }
                if (anim.base.clipB == resolved) { outTime = anim.base.timeB; return true; }
                if (anim.upper.clipA == resolved) { outTime = anim.upper.timeA; return true; }
                if (anim.upper.clipB == resolved) { outTime = anim.upper.timeB; return true; }
                if (anim.additive.clip == resolved) { outTime = anim.additive.time; return true; }
                return false;
            }

            switch (clip.source)
            {
            case AttackDriverClipSource::BaseA: outTime = anim.base.timeA; return true;
            case AttackDriverClipSource::BaseB: outTime = anim.base.timeB; return true;
            case AttackDriverClipSource::UpperA: outTime = anim.upper.timeA; return true;
            case AttackDriverClipSource::UpperB: outTime = anim.upper.timeB; return true;
            case AttackDriverClipSource::Additive: outTime = anim.additive.time; return true;
            case AttackDriverClipSource::Explicit:
            default:
                return false;
            }
        }

        bool IsWithinAnyWindow(const AttackDriverComponent& driver, const AdvancedAnimationComponent& anim)
        {
            for (const auto& clip : driver.clips)
            {
                if (!clip.enabled)
                    continue;

                float startTime = 0.0f;
                float endTime = 0.0f;
                SanitizeTimes(clip, startTime, endTime);

                if (clip.source == AttackDriverClipSource::Explicit)
                {
                    const std::string& name = clip.clipName;
                    if (name.empty())
                        continue;

                    const bool matchA = (anim.base.clipA == name);
                    const bool matchB = (anim.base.clipB == name);
                    const bool matchUpperA = (anim.upper.clipA == name);
                    const bool matchUpperB = (anim.upper.clipB == name);
                    const bool matchAdd = (anim.additive.clip == name);

                    if (matchA && anim.base.timeA >= startTime && anim.base.timeA <= endTime)
                        return true;
                    if (matchB && anim.base.timeB >= startTime && anim.base.timeB <= endTime)
                        return true;
                    if (matchUpperA && anim.upper.timeA >= startTime && anim.upper.timeA <= endTime)
                        return true;
                    if (matchUpperB && anim.upper.timeB >= startTime && anim.upper.timeB <= endTime)
                        return true;
                    if (matchAdd && anim.additive.time >= startTime && anim.additive.time <= endTime)
                        return true;

                    continue;
                }

                float currTime = 0.0f;
                if (!TryGetClipTime(clip, anim, currTime))
                    continue;

                if (currTime >= startTime && currTime <= endTime)
                    return true;
            }

            return false;
        }

        bool TryResolveSkinnedClipName(const SkinnedMeshRegistry* registry,
            const SkinnedMeshComponent* skinned,
            int clipIndex,
            std::string& outName)
        {
            if (!registry || !skinned || skinned->meshAssetPath.empty())
                return false;

            auto mesh = registry->Find(skinned->meshAssetPath);
            if (!mesh || !mesh->sourceModel)
                return false;

            if (clipIndex < 0)
                return false;

            const auto& names = mesh->sourceModel->GetAnimationNames();
            if (static_cast<size_t>(clipIndex) < names.size() && !names[static_cast<size_t>(clipIndex)].empty())
            {
                outName = names[static_cast<size_t>(clipIndex)];
                return true;
            }

            const aiScene* scene = mesh->sourceModel->GetScenePtr();
            if (scene && static_cast<unsigned>(clipIndex) < scene->mNumAnimations)
            {
                const aiAnimation* a = scene->mAnimations[clipIndex];
                if (a && a->mName.length > 0)
                {
                    outName = a->mName.C_Str();
                    return true;
                }
                outName = "Anim" + std::to_string(clipIndex);
                return true;
            }

            return false;
        }

        bool IsWithinAnyWindowSkinned(const AttackDriverComponent& driver,
            const std::string& currentClipName,
            float currentTimeSec)
        {
            if (currentClipName.empty())
                return false;

            for (const auto& clip : driver.clips)
            {
                if (!clip.enabled)
                    continue;

                const std::string targetName =
                    (clip.source == AttackDriverClipSource::Explicit) ? clip.clipName : currentClipName;

                if (targetName.empty() || targetName != currentClipName)
                    continue;

                float startTime = 0.0f;
                float endTime = 0.0f;
                SanitizeTimes(clip, startTime, endTime);

                if (currentTimeSec >= startTime && currentTimeSec <= endTime)
                    return true;
            }

            return false;
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
            EntityId traceId = ResolveTraceEntity(world, driver, entityId);
            if (!anim)
            {
                auto* skinnedAnim = world.GetComponent<SkinnedAnimationComponent>(entityId);
                if (!skinnedAnim || !skinnedAnim->playing)
                {
                    DeactivateTrace(world, traceId);
                    continue;
                }

                const auto* skinnedMesh = world.GetComponent<SkinnedMeshComponent>(entityId);
                std::string currentClipName;
                if (!TryResolveSkinnedClipName(m_registry, skinnedMesh, skinnedAnim->clipIndex, currentClipName))
                {
                    DeactivateTrace(world, traceId);
                    continue;
                }

                const bool withinWindow = IsWithinAnyWindowSkinned(
                    driver,
                    currentClipName,
                    static_cast<float>(skinnedAnim->timeSec));

                auto* trace = world.GetComponent<WeaponTraceComponent>(traceId);
                if (withinWindow)
                {
                    if (trace && !trace->active)
                        ActivateTrace(world, traceId);
                }
                else
                {
                    if (trace && trace->active)
                        DeactivateTrace(world, traceId);
                }
                continue;
            }

            if (!anim || !anim->enabled || !anim->playing)
            {
                DeactivateTrace(world, traceId);
                continue;
            }

            const std::uint32_t gen = world.GetEntityGeneration(entityId);
            if (driver.notifyTag == 0)
            {
                driver.notifyTag = static_cast<std::uint64_t>(entityId);
            }

            const std::uint64_t currentHash = HashClipList(driver.clips, *anim);
            const bool wantsNotifies = HasAnyEnabledClip(driver, *anim);
            const bool missingNotifies = wantsNotifies && !HasNotifyTag(*anim, driver.notifyTag);
            const bool needsRebuild = missingNotifies || (currentHash != driver.registeredHash);

            if (needsRebuild)
            {
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
                    DeactivateTrace(world, traceId);
                }

                driver.registeredHash = currentHash;
            }

            const bool withinWindow = IsWithinAnyWindow(driver, *anim);
            auto* trace = world.GetComponent<WeaponTraceComponent>(traceId);
            if (withinWindow)
            {
                if (trace && !trace->active)
                    ActivateTrace(world, traceId);
            }
            else
            {
                if (trace && trace->active)
                    DeactivateTrace(world, traceId);
            }
        }
    }
}
