#include "Core/AdvancedAnimSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

#include <DirectXMath.h>
#include <assimp/scene.h>

#include "Components/AdvancedAnimator.h"
#include "Components/AdvancedAnimationComponent.h"
#include "Components/SkinnedAnimationComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/TransformComponent.h"
#include "Core/World.h"
#include "Rendering/SkinnedMeshRegistry.h"
#include "3Dmodel/FbxModel.h"

namespace Alice
{
    namespace
    {
        DirectX::XMMATRIX BuildWorldMatrix(const TransformComponent& t)
        {
            using namespace DirectX;
            XMMATRIX S = XMMatrixScaling(t.scale.x, t.scale.y, t.scale.z);
            XMMATRIX R = XMMatrixRotationRollPitchYaw(t.rotation.x, t.rotation.y, t.rotation.z);
            XMMATRIX T = XMMatrixTranslation(t.position.x, t.position.y, t.position.z);
            return S * R * T;
        }

        bool TryParseIndex(const std::string& key, int& outIdx)
        {
            if (key.empty()) return false;
            for (char c : key)
            {
                if (!std::isdigit(static_cast<unsigned char>(c)))
                    return false;
            }
            outIdx = std::atoi(key.c_str());
            return true;
        }
    }

    AdvancedAnimSystem::Runtime::Runtime()
        : animator(new AdvancedAnimator())
    {
    }

    AdvancedAnimSystem::Runtime::Runtime(Runtime&& other) noexcept
    {
        meshKey = std::move(other.meshKey);
        mesh = std::move(other.mesh);
        clipIndexByName = std::move(other.clipIndexByName);
        animator = other.animator;
        initialized = other.initialized;
        other.animator = nullptr;
        other.initialized = false;
    }

    AdvancedAnimSystem::Runtime& AdvancedAnimSystem::Runtime::operator=(Runtime&& other) noexcept
    {
        if (this == &other) return *this;
        delete animator;
        meshKey = std::move(other.meshKey);
        mesh = std::move(other.mesh);
        clipIndexByName = std::move(other.clipIndexByName);
        animator = other.animator;
        initialized = other.initialized;
        other.animator = nullptr;
        other.initialized = false;
        return *this;
    }

    AdvancedAnimSystem::Runtime::~Runtime()
    {
        delete animator;
        animator = nullptr;
    }

    AdvancedAnimSystem::AdvancedAnimSystem(SkinnedMeshRegistry& registry)
        : m_registry(registry)
    {
    }

    void AdvancedAnimSystem::Update(World& world, double dtSec)
    {
        // ------------------------------
        // 1) Advanced animation path
        // ------------------------------
        for (auto [entityId, animComp] : world.GetComponents<AdvancedAnimationComponent>())
        {
            auto* skinned = world.GetComponent<SkinnedMeshComponent>(entityId);
            if (!skinned || skinned->meshAssetPath.empty())
                continue;

            auto mesh = m_registry.Find(skinned->meshAssetPath);
            if (!mesh || !mesh->sourceModel)
                continue;

            ProcessAdvanced(entityId, world, animComp, *skinned, mesh, dtSec);
        }

        // ------------------------------
        // 2) Simple animation fallback
        // ------------------------------
        for (auto [entityId, animComp] : world.GetComponents<SkinnedAnimationComponent>())
        {
            if (world.GetComponent<AdvancedAnimationComponent>(entityId))
                continue;

            auto* skinned = world.GetComponent<SkinnedMeshComponent>(entityId);
            if (!skinned || skinned->meshAssetPath.empty())
                continue;

            auto mesh = m_registry.Find(skinned->meshAssetPath);
            if (!mesh || !mesh->sourceModel)
                continue;

            ProcessSimple(entityId, world, animComp, *skinned, mesh, dtSec);
        }
    }

    bool AdvancedAnimSystem::EnsureRuntime(Runtime& rt,
                                           const SkinnedMeshComponent& skinned,
                                           const std::shared_ptr<SkinnedMeshGPU>& mesh)
    {
        if (!mesh || !mesh->sourceModel)
            return false;

        if (rt.initialized && rt.meshKey == skinned.meshAssetPath)
            return true;

        rt.meshKey = skinned.meshAssetPath;
        rt.mesh = mesh;
        rt.clipIndexByName.clear();

        const auto& names = mesh->sourceModel->GetAnimationNames();
        const aiScene* scene = mesh->sourceModel->GetScenePtr();

        const size_t clipCount = scene ? scene->mNumAnimations : names.size();
        for (size_t i = 0; i < clipCount; ++i)
        {
            std::string key;
            if (i < names.size())
                key = names[i];
            if (key.empty() && scene && i < scene->mNumAnimations)
            {
                const aiAnimation* a = scene->mAnimations[i];
                key = (a && a->mName.length > 0) ? std::string(a->mName.C_Str())
                                                 : ("Anim" + std::to_string(i));
            }
            if (key.empty())
                key = "Anim" + std::to_string(i);

            rt.clipIndexByName[key] = static_cast<int>(i);
        }

        if (rt.animator)
        {
            rt.animator->Initialize(
                mesh->sourceModel->GetScenePtr(),
                mesh->sourceModel->GetNodeIndexOfName(),
                mesh->sourceModel->GetGlobalInverse(),
                mesh->sourceModel->GetBoneNames(),
                mesh->sourceModel->GetBoneOffsets());
        }

        rt.initialized = true;
        return true;
    }

    const aiAnimation* AdvancedAnimSystem::ResolveClip(const Runtime& rt, const std::string& key) const
    {
        if (!rt.mesh || !rt.mesh->sourceModel)
            return nullptr;

        if (key.empty())
            return nullptr;

        const aiScene* scene = rt.mesh->sourceModel->GetScenePtr();
        if (!scene)
            return nullptr;

        if (auto it = rt.clipIndexByName.find(key); it != rt.clipIndexByName.end())
        {
            const int idx = it->second;
            if (idx >= 0 && (unsigned)idx < scene->mNumAnimations)
                return scene->mAnimations[idx];
        }

        int idx = -1;
        if (TryParseIndex(key, idx))
        {
            if (idx >= 0 && (unsigned)idx < scene->mNumAnimations)
                return scene->mAnimations[idx];
        }

        return nullptr;
    }

    float AdvancedAnimSystem::GetClipDurationSec(const aiAnimation* anim) const
    {
        if (!anim)
            return 0.0f;
        const double tps = (anim->mTicksPerSecond != 0.0) ? anim->mTicksPerSecond : 25.0;
        if (tps <= 0.0)
            return 0.0f;
        return static_cast<float>(anim->mDuration / tps);
    }

    void AdvancedAnimSystem::AdvanceTime(float& timeSec,
                                         float dtSec,
                                         float speed,
                                         float durationSec,
                                         bool loop) const
    {
        if (durationSec <= 0.0f)
            return;

        timeSec += dtSec * speed;
        if (loop)
        {
            timeSec = std::fmod(timeSec, durationSec);
            if (timeSec < 0.0f)
                timeSec += durationSec;
        }
        else
        {
            timeSec = std::clamp(timeSec, 0.0f, durationSec);
        }
    }

    void AdvancedAnimSystem::ProcessAdvanced(EntityId id,
                                             World& world,
                                             AdvancedAnimationComponent& animComp,
                                             SkinnedMeshComponent& skinned,
                                             const std::shared_ptr<SkinnedMeshGPU>& mesh,
                                             double dtSec)
    {
        Runtime& rt = m_runtime[id];
        if (!EnsureRuntime(rt, skinned, mesh))
            return;

        const aiAnimation* baseA = ResolveClip(rt, animComp.base.clipA);
        const aiAnimation* baseB = ResolveClip(rt, animComp.base.clipB);
        const aiAnimation* upperA = ResolveClip(rt, animComp.upper.clipA);
        const aiAnimation* upperB = ResolveClip(rt, animComp.upper.clipB);
        const aiAnimation* additiveA = ResolveClip(rt, animComp.additive.clip);
        const aiAnimation* additiveRef = ResolveClip(rt, animComp.additive.refClip);

        // ------------------------------
        // Time advance (if enabled)
        // ------------------------------
        if (animComp.playing)
        {
            if (animComp.base.autoAdvance && baseA)
            {
                const float dur = GetClipDurationSec(baseA);
                AdvanceTime(animComp.base.timeA, (float)dtSec, animComp.base.speedA, dur, animComp.base.loopA);
            }

            if (animComp.base.autoAdvance && baseB)
            {
                const float dur = GetClipDurationSec(baseB);
                AdvanceTime(animComp.base.timeB, (float)dtSec, animComp.base.speedB, dur, animComp.base.loopB);
            }

            if (animComp.upper.autoAdvance && upperA)
            {
                const float dur = GetClipDurationSec(upperA);
                AdvanceTime(animComp.upper.timeA, (float)dtSec, animComp.upper.speedA, dur, animComp.upper.loopA);
            }

            if (animComp.upper.autoAdvance && upperB)
            {
                const float dur = GetClipDurationSec(upperB);
                AdvanceTime(animComp.upper.timeB, (float)dtSec, animComp.upper.speedB, dur, animComp.upper.loopB);
            }

            if (animComp.additive.autoAdvance && additiveA)
            {
                const float dur = GetClipDurationSec(additiveA);
                AdvanceTime(animComp.additive.time, (float)dtSec, animComp.additive.speed, dur, animComp.additive.loop);
            }

            if (animComp.procedural.strength > 0.0f)
                animComp.procedural.timeSec += (float)dtSec;
        }

        if (animComp.base.clipB.empty() || animComp.base.clipB == animComp.base.clipA)
            animComp.base.timeB = animComp.base.timeA;

        if (animComp.upper.clipB.empty() || animComp.upper.clipB == animComp.upper.clipA)
            animComp.upper.timeB = animComp.upper.timeA;

        // ------------------------------
        // Build update desc
        // ------------------------------
        AdvancedAnimator::UpdateDesc d{};
        d.dt = (float)dtSec;

        // Always evaluate base to keep bind pose when channels are missing
        d.base.enabled = true;
        d.base.animA = baseA;
        d.base.timeA = animComp.base.timeA;
        d.base.animB = baseB;
        d.base.timeB = animComp.base.timeB;
        d.base.blend01 = animComp.base.blend01;
        d.base.layerAlpha = 1.0f;

        d.upper.enabled = animComp.upper.enabled;
        d.upper.animA = upperA;
        d.upper.timeA = animComp.upper.timeA;
        d.upper.animB = upperB;
        d.upper.timeB = animComp.upper.timeB;
        d.upper.blend01 = animComp.upper.blend01;
        d.upper.layerAlpha = animComp.upper.layerAlpha;

        d.additive.enabled = animComp.additive.enabled;
        d.additive.anim = additiveA;
        d.additive.time = animComp.additive.time;
        d.additive.ref = additiveRef;
        d.additive.alpha = animComp.additive.alpha;

        d.procedural.strength = animComp.procedural.strength;
        d.procedural.seed = animComp.procedural.seed;
        d.procedural.timeSec = animComp.procedural.timeSec;

        d.ik.enabled = animComp.ik.enabled;
        d.ik.tipBone = animComp.ik.tipBone.empty() ? nullptr : animComp.ik.tipBone.c_str();
        d.ik.chainLen = animComp.ik.chainLength;
        d.ik.targetMS = DirectX::XMLoadFloat3(&animComp.ik.targetMS);
        d.ik.weight = animComp.ik.weight;

        d.aim.enabled = animComp.aim.enabled;
        d.aim.yawRad = animComp.aim.yawRad;
        d.aim.weight = animComp.aim.weight;

        // ------------------------------
        // Socket definitions (push to runtime)
        // ------------------------------
        for (const auto& s : animComp.sockets)
        {
            rt.animator->SetSocketSRT(s.name, s.parentBone, s.pos, s.rotDeg, s.scale);
        }

        // ------------------------------
        // Evaluate
        // ------------------------------
        rt.animator->Update(d);

        // ------------------------------
        // Palette output
        // ------------------------------
        const auto& finals = rt.animator->GetFinalTransforms();
        animComp.palette.resize(finals.size());
        for (size_t i = 0; i < finals.size(); ++i)
            DirectX::XMStoreFloat4x4(&animComp.palette[i], finals[i]);

        skinned.boneMatrices = animComp.palette.empty() ? nullptr : animComp.palette.data();
        skinned.boneCount = static_cast<std::uint32_t>(animComp.palette.size());

        // ------------------------------
        // Socket world outputs
        // ------------------------------
        DirectX::XMMATRIX charWorld = DirectX::XMMatrixIdentity();
        if (const auto* t = world.GetComponent<TransformComponent>(id))
            charWorld = BuildWorldMatrix(*t);

        for (auto& s : animComp.sockets)
        {
            DirectX::XMMATRIX socketWorld = rt.animator->GetSocketWorldMatrix(s.name, charWorld);
            DirectX::XMStoreFloat4x4(&s.worldMatrix, socketWorld);
        }
    }

    void AdvancedAnimSystem::ProcessSimple(EntityId id,
                                           World& world,
                                           SkinnedAnimationComponent& animComp,
                                           SkinnedMeshComponent& skinned,
                                           const std::shared_ptr<SkinnedMeshGPU>& mesh,
                                           double dtSec)
    {
        (void)world;
        Runtime& rt = m_runtime[id];
        if (!EnsureRuntime(rt, skinned, mesh))
            return;

        const aiScene* scene = mesh->sourceModel->GetScenePtr();
        if (!scene || scene->mNumAnimations == 0)
            return;

        int clipIdx = animComp.clipIndex;
        if (clipIdx < 0)
            clipIdx = 0;
        if ((unsigned)clipIdx >= scene->mNumAnimations)
            clipIdx = (int)scene->mNumAnimations - 1;
        if (clipIdx != animComp.clipIndex)
            animComp.clipIndex = clipIdx;

        const aiAnimation* anim = scene->mAnimations[clipIdx];
        if (animComp.playing)
        {
            const float dur = GetClipDurationSec(anim);
            float time = static_cast<float>(animComp.timeSec);
            AdvanceTime(time, (float)dtSec, animComp.speed, dur, true);
            animComp.timeSec = static_cast<double>(time);
        }

        AdvancedAnimator::UpdateDesc d{};
        d.dt = (float)dtSec;
        d.base.enabled = true;
        d.base.animA = anim;
        d.base.timeA = (float)animComp.timeSec;
        d.base.animB = anim;
        d.base.timeB = (float)animComp.timeSec;
        d.base.blend01 = 0.0f;

        rt.animator->Update(d);

        const auto& finals = rt.animator->GetFinalTransforms();
        animComp.palette.resize(finals.size());
        for (size_t i = 0; i < finals.size(); ++i)
            DirectX::XMStoreFloat4x4(&animComp.palette[i], finals[i]);

        skinned.boneMatrices = animComp.palette.empty() ? nullptr : animComp.palette.data();
        skinned.boneCount = static_cast<std::uint32_t>(animComp.palette.size());
    }
}

