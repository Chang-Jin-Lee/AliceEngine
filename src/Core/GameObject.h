#pragma once

#include <string>

#include "Core/Entity.h"
#include "Core/ScriptAPI.h"
#include "Core/World.h"
#include "Core/Logger.h"
#include "Rendering/SkinnedMeshRegistry.h"

// FbxModel은 전역 네임스페이스에 있습니다.
#include "3Dmodel/FbxModel.h"

namespace Alice
{
    /// Unity 느낌의 엔티티 래퍼 (스크립트에서 GetComponent<> 사용)
    class GameObject
    {
    public:
        GameObject() = default;
        GameObject(World* world, EntityId id, ScriptServices* services)
            : m_world(world), m_id(id), m_services(services)
        {
        }

        bool IsValid() const { return m_world && m_id != InvalidEntityId; }
        EntityId id() const { return m_id; }

        template <typename T>
        T* GetComponent() const
        {
            if (!m_world || m_id == InvalidEntityId)
                return nullptr;

            if constexpr (std::is_same_v<T, TransformComponent>)
                return m_world->GetTransform(m_id);
            else if constexpr (std::is_same_v<T, MaterialComponent>)
                return m_world->GetMaterial(m_id);
            else if constexpr (std::is_same_v<T, SkinnedMeshComponent>)
                return m_world->GetSkinnedMesh(m_id);
            else if constexpr (std::is_same_v<T, SkinnedAnimationComponent>)
                return m_world->GetSkinnedAnimation(m_id);
            else if constexpr (std::is_same_v<T, CameraComponent>)
                return m_world->GetCamera(m_id);
            else
            {
                static_assert(sizeof(T) == 0, "GetComponent<T>: unsupported component type.");
                return nullptr;
            }
        }

        /// Animator 핸들(엔티티 단위)
        class Animator
        {
        public:
            Animator() = default;
            Animator(World* world, EntityId id, ScriptServices* services)
                : m_world(world), m_id(id), m_services(services)
            {
            }

            bool IsValid() const
            {
                if (!m_world || m_id == InvalidEntityId || !m_services || !m_services->skinnedRegistry)
                    return false;

                auto* skinned = m_world->GetSkinnedMesh(m_id);
                if (!skinned || skinned->meshAssetPath.empty())
                    return false;

                auto mesh = m_services->skinnedRegistry->Find(skinned->meshAssetPath);
                if (!mesh || !mesh->sourceModel)
                    return false;

                return !mesh->sourceModel->GetAnimationNames().empty();
            }

            int ClipCount() const
            {
                auto model = SourceModel();
                return model ? (int)model->GetAnimationNames().size() : 0;
            }

            const char* ClipName(int idx) const
            {
                auto model = SourceModel();
                if (!model) return "";
                const auto& n = model->GetAnimationNames();
                if (idx < 0 || idx >= (int)n.size()) return "";
                return n[(size_t)idx].c_str();
            }

            int GetClip() const
            {
                auto* a = AnimComp(false);
                return a ? a->clipIndex : 0;
            }

            void SetClip(int idx)
            {
                auto* a = AnimComp(true);
                if (!a) return;

                const int count = ClipCount();
                if (count <= 0) return;
                if (idx < 0) idx = 0;
                if (idx >= count) idx = count - 1;
                a->clipIndex = idx;
                a->timeSec = 0.0;
            }

            bool IsPlaying() const
            {
                auto* a = AnimComp(false);
                return a ? a->playing : false;
            }

            void Play()
            {
                auto* a = AnimComp(true);
                if (a) a->playing = true;
            }

            void Play(int idx, bool forceRestart = false)
            {
                auto* a = AnimComp(true);
                if (!a) return;

                // 이미 재생 중이고, 요청한 클립이 현재 클립과 같으며, 강제 재시작이 아니라면 아무것도 하지 않고 리턴 (애니메이션 끊김 방지)
                if (a->playing && a->clipIndex == idx && !forceRestart) return;
                SetClip(idx);
                a->playing = true;
            }

            void Stop()
            {
                auto* a = AnimComp(true);
                if (!a) return;
                a->playing = false;
                a->timeSec = 0.0;
            }

            float GetSpeed() const
            {
                auto* a = AnimComp(false);
                return a ? a->speed : 1.0f;
            }

            void SetSpeed(float s)
            {
                auto* a = AnimComp(true);
                if (!a) return;
                if (s < 0.0f) s = 0.0f;
                a->speed = s;
            }

            double GetTime() const
            {
                auto* a = AnimComp(false);
                return a ? a->timeSec : 0.0;
            }

            void SetTime(double t)
            {
                auto* a = AnimComp(true);
                if (!a) return;
                a->timeSec = (t < 0.0) ? 0.0 : t;
            }

            double ClipDurationSec(int idx) const
            {
                auto model = SourceModel();
                return model ? model->GetClipDurationSec(idx) : 0.0;
            }

        private:
            std::shared_ptr<FbxModel> SourceModel() const
            {
                if (!m_world || !m_services || !m_services->skinnedRegistry)
                    return nullptr;
                auto* skinned = m_world->GetSkinnedMesh(m_id);
                if (!skinned) return nullptr;
                auto mesh = m_services->skinnedRegistry->Find(skinned->meshAssetPath);
                if (!mesh) return nullptr;
                return mesh->sourceModel;
            }

            SkinnedAnimationComponent* AnimComp(bool create) const
            {
                if (!m_world) return nullptr;
                if (auto* a = m_world->GetSkinnedAnimation(m_id))
                    return a;
                return create ? &m_world->AddSkinnedAnimation(m_id) : nullptr;
            }

            World* m_world = nullptr;
            EntityId m_id = InvalidEntityId;
            ScriptServices* m_services = nullptr;
        };

        Animator GetAnimator() const { return Animator(m_world, m_id, m_services); }

        /// 씬에서 "첫번째 SkinnedMesh" 엔티티를 찾습니다. (캐릭터 1인 게임용 간단 유틸)
        GameObject FindFirstSkinnedMesh() const
        {
            if (!m_world)
                return {};

            for (const auto& [id, comp] : m_world->GetSkinnedMeshes())
            {
                if (id == InvalidEntityId) continue;
                if (comp.meshAssetPath.empty()) continue;
                return GameObject(m_world, id, m_services);
            }
            return {};
        }

    private:
        World* m_world = nullptr;
        EntityId m_id = InvalidEntityId;
        ScriptServices* m_services = nullptr;
    };
}


