#pragma once

#include <vector>

#include "Core/World.h"
#include "Core/Logger.h"
#include "Rendering/ForwardRenderSystem.h"
#include "Rendering/SkinnedMeshRegistry.h"

namespace Alice
{
    /// World 의 SkinnedMeshComponent 들을 훑어서,
    /// ForwardRenderSystem 이 이해할 수 있는 SkinnedDrawCommand 리스트를 만드는 시스템입니다.
    /// - 게임 로직/애니메이션 쪽에서 boneMatrices 를 채워 넣으면,
    ///   이 시스템이 그것을 렌더 명령으로 변환합니다.
    class SkinnedMeshSystem
    {
    public:
        explicit SkinnedMeshSystem(SkinnedMeshRegistry& registry)
            : m_registry(registry)
        {
        }

        /// World + Registry 를 기반으로 스키닝 드로우 명령 리스트를 구성합니다.
        void BuildDrawList(const World& world,
                           std::vector<ForwardRenderSystem::SkinnedDrawCommand>& outCommands) const
        {
            outCommands.clear();

            const auto& skinnedMap = world.GetSkinnedMeshes();
            if (skinnedMap.empty())
            {
                // 디폴트 상태(스키닝 컴포넌트가 하나도 없을 때)는 로그를 찍지 않습니다.
                return;
            }

            {
                ALICE_LOG_INFO("[SkinnedMeshSystem] BuildDrawList: skinnedComponents=%zu",
                               skinnedMap.size());
            }

            for (const auto& [entityId, comp] : skinnedMap)
            {
                if (!comp.boneMatrices || comp.boneCount == 0)
                {
                    ALICE_LOG_INFO("[SkinnedMeshSystem]  - skip: entity=%u no bones",
                                   static_cast<unsigned>(entityId));
                    continue;
                }

                auto mesh = m_registry.Find(comp.meshAssetPath);
                if (!mesh)
                {
                    ALICE_LOG_INFO("[SkinnedMeshSystem]  - skip: mesh not found for key=\"%s\"",
                                   comp.meshAssetPath.c_str());
                    continue;
                }

                const TransformComponent* t = world.GetTransform(entityId);
                if (!t)
                {
                    ALICE_LOG_INFO("[SkinnedMeshSystem]  - skip: entity=%u no Transform",
                                   static_cast<unsigned>(entityId));
                    continue;
                }

                // 월드 행렬 구성 (S * R * T)
                using namespace DirectX;
                XMMATRIX S = XMMatrixScaling(t->scale.x, t->scale.y, t->scale.z);
                XMMATRIX R = XMMatrixRotationRollPitchYaw(t->rotation.x, t->rotation.y, t->rotation.z);
                XMMATRIX Tm = XMMatrixTranslation(t->position.x, t->position.y, t->position.z);
                XMMATRIX worldM = S * R * Tm;

                ForwardRenderSystem::SkinnedDrawCommand cmd = {};
                cmd.vertexBuffer = mesh->vertexBuffer.Get();
                cmd.indexBuffer  = mesh->indexBuffer.Get();
                cmd.stride       = mesh->stride;
                cmd.indexCount   = mesh->indexCount;
                cmd.startIndex   = mesh->startIndex;
                cmd.baseVertex   = mesh->baseVertex;
                cmd.world        = worldM;
                cmd.bones        = comp.boneMatrices;
                cmd.boneCount    = comp.boneCount;
                cmd.meshKey      = comp.meshAssetPath;

                if (const MaterialComponent* mat = world.GetMaterial(entityId))
                {
                    cmd.color             = mat->color;
                    cmd.roughness         = mat->roughness;
                    cmd.metalness         = mat->metalness;
                    cmd.albedoTexturePath = mat->albedoTexturePath;

                    if (!mat->albedoTexturePath.empty())
                    {
                        ALICE_LOG_INFO("[SkinnedMeshSystem] entity=%u mesh=\"%s\" albedoTex=\"%s\"",
                                       static_cast<unsigned>(entityId),
                                       comp.meshAssetPath.c_str(),
                                       mat->albedoTexturePath.c_str());
                    }
                }

                outCommands.push_back(cmd);
            }

            if (!outCommands.empty())
            {
                // 실제로 드로우 커맨드가 생겼을 때만 1회 로그를 남깁니다.
                ALICE_LOG_INFO("[SkinnedMeshSystem] BuildDrawList: commands=%zu",
                               outCommands.size());
            }
        }

    private:
        SkinnedMeshRegistry& m_registry;
    };
}



