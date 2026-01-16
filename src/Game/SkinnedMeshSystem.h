#pragma once

#include <vector>

#include "Core/World.h"
#include "Core/Logger.h"
#include "Rendering/ForwardRenderSystem.h"
#include "Rendering/SkinnedMeshRegistry.h"

namespace Alice
{
    /// World ??SkinnedMeshComponent ?ㅼ쓣 ?묒뼱??
    /// ForwardRenderSystem ???댄빐?????덈뒗 SkinnedDrawCommand 由ъ뒪?몃? 留뚮뱶???쒖뒪?쒖엯?덈떎.
    /// - 寃뚯엫 濡쒖쭅/?좊땲硫붿씠??履쎌뿉??boneMatrices 瑜?梨꾩썙 ?ｌ쑝硫?
    ///   ???쒖뒪?쒖씠 洹멸쾬???뚮뜑 紐낅졊?쇰줈 蹂?섑빀?덈떎.
    class SkinnedMeshSystem
    {
    public:
        explicit SkinnedMeshSystem(SkinnedMeshRegistry& registry)
            : m_registry(registry)
        {
        }

        /// World + Registry 瑜?湲곕컲?쇰줈 ?ㅽ궎???쒕줈??紐낅졊 由ъ뒪?몃? 援ъ꽦?⑸땲??
        void BuildDrawList(const World& world,
            std::vector<SkinnedDrawCommand>& outCommands) const
        {
            outCommands.clear();

            const auto& skinnedMap = world.GetComponents<SkinnedMeshComponent>();
            if (skinnedMap.empty())
            {
                // ?뷀뤃???곹깭(?ㅽ궎??而댄룷?뚰듃媛 ?섎굹???놁쓣 ????濡쒓렇瑜?李띿? ?딆뒿?덈떎.
                return;
            }

            {
                //ALICE_LOG_INFO("[SkinnedMeshSystem] BuildDrawList: skinnedComponents=%zu",
                //               skinnedMap.size());
            }

            for (const auto& [entityId, comp] : skinnedMap)
            {
                if (!comp.boneMatrices || comp.boneCount == 0)
                {
                    //ALICE_LOG_INFO("[SkinnedMeshSystem]  - skip: entity=%u no bones",
                    //               static_cast<unsigned>(entityId));
                    continue;
                }

                auto mesh = m_registry.Find(comp.meshAssetPath);
                if (!mesh)
                {
                    //ALICE_LOG_INFO("[SkinnedMeshSystem]  - skip: mesh not found for key=\"%s\"",
                    //               comp.meshAssetPath.c_str());
                    continue;
                }

                const TransformComponent* t = world.GetComponent<TransformComponent>(entityId);
                if (!t)
                {
                    //ALICE_LOG_INFO("[SkinnedMeshSystem]  - skip: entity=%u no Transform",
                    //               static_cast<unsigned>(entityId));
                    continue;
                }

                // ?붾뱶 ?됰젹 援ъ꽦 (S * R * T)
                using namespace DirectX;
                XMMATRIX S = XMMatrixScaling(t->scale.x, t->scale.y, t->scale.z);
                XMMATRIX R = XMMatrixRotationRollPitchYaw(t->rotation.x, t->rotation.y, t->rotation.z);
                XMMATRIX Tm = XMMatrixTranslation(t->position.x, t->position.y, t->position.z);
                XMMATRIX worldM = S * R * Tm;

                SkinnedDrawCommand cmd = {};
                cmd.vertexBuffer = mesh->vertexBuffer.Get();
                cmd.indexBuffer = mesh->indexBuffer.Get();
                cmd.stride = mesh->stride;
                cmd.indexCount = mesh->indexCount;
                cmd.startIndex = mesh->startIndex;
                cmd.baseVertex = mesh->baseVertex;
                cmd.world = worldM;
                cmd.bones = comp.boneMatrices;
                cmd.boneCount = comp.boneCount;
                cmd.meshKey = comp.meshAssetPath;

                if (const MaterialComponent* mat = world.GetComponent<MaterialComponent>(entityId))
                {
                    cmd.color = mat->color;
                    cmd.roughness = mat->roughness;
                    cmd.metalness = mat->metalness;
                    cmd.albedoTexturePath = mat->albedoTexturePath;

                    if (!mat->albedoTexturePath.empty())
                    {
                        //ALICE_LOG_INFO("[SkinnedMeshSystem] entity=%u mesh=\"%s\" albedoTex=\"%s\"",
                        //               static_cast<unsigned>(entityId),
                        //               comp.meshAssetPath.c_str(),
                        //               mat->albedoTexturePath.c_str());
                    }
                }

                outCommands.push_back(cmd);
            }

            //if (!outCommands.empty())
            //{
            //    // ?ㅼ젣濡??쒕줈??而ㅻ㎤?쒓? ?앷꼈???뚮쭔 1??濡쒓렇瑜??④퉩?덈떎.
            //    ALICE_LOG_INFO("[SkinnedMeshSystem] BuildDrawList: commands=%zu",
            //                   outCommands.size());
            //}
        }

    private:
        SkinnedMeshRegistry& m_registry;
    };
}