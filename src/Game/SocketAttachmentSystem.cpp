#include "Game/SocketAttachmentSystem.h"

#include <DirectXMath.h>

#include "Core/World.h"
#include "Components/TransformComponent.h"
#include "Components/IDComponent.h"
#include "Components/SocketAttachmentComponent.h"
#include "Components/AdvancedAnimationComponent.h"
#include "Components/SocketComponent.h"

namespace Alice
{
    namespace
    {
        EntityId ResolveOwner(World& world, SocketAttachmentComponent& att)
        {
            if (att.ownerCached != InvalidEntityId)
            {
                if (att.ownerGuid == 0)
                    return att.ownerCached;

                if (const auto* idc = world.GetComponent<IDComponent>(att.ownerCached))
                {
                    if (idc->guid == att.ownerGuid)
                        return att.ownerCached;
                }
            }

            if (att.ownerGuid == 0)
                return InvalidEntityId;

            EntityId resolved = world.FindEntityByGuid(att.ownerGuid);
            if (resolved == InvalidEntityId)
                return InvalidEntityId;

            att.ownerCached = resolved;
            return resolved;
        }

        bool TryGetSocketWorldMatrix(World& world, EntityId owner, const std::string& socketName, DirectX::XMMATRIX& out)
        {
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
            }

            return false;
        }
    }

    void SocketAttachmentSystem::Update(World& world)
    {
        auto&& attachments = world.GetComponents<SocketAttachmentComponent>(); // & -> &&·Î ¹Ù²Þ
        if (attachments.empty())
            return;

        using namespace DirectX;

        for (auto&& [eid, att] : attachments)
        {
            auto* tr = world.GetComponent<TransformComponent>(eid);
            if (!tr || !tr->enabled)
                continue;

            const EntityId owner = ResolveOwner(world, att);
            if (owner == InvalidEntityId || att.socketName.empty())
                continue;

            XMMATRIX socketWorld = XMMatrixIdentity();
            if (!TryGetSocketWorldMatrix(world, owner, att.socketName, socketWorld))
                continue;

            const XMMATRIX extra =
                XMMatrixScaling(att.extraScale.x, att.extraScale.y, att.extraScale.z) *
                XMMatrixRotationRollPitchYaw(att.extraRotRad.x, att.extraRotRad.y, att.extraRotRad.z) *
                XMMatrixTranslation(att.extraPos.x, att.extraPos.y, att.extraPos.z);

            const XMMATRIX finalM = extra * socketWorld;

            XMVECTOR S, R, T;
            if (!XMMatrixDecompose(&S, &R, &T, finalM))
                continue;

            XMStoreFloat3(&tr->position, T);

            XMFLOAT4 q;
            XMStoreFloat4(&q, R);
            tr->SetRotation(q);

            if (att.followScale)
            {
                XMStoreFloat3(&tr->scale, S);
            }

            tr->parent = InvalidEntityId;
            world.MarkTransformDirty(eid);
        }
    }
}
