#include "C_CombatMovementBridge.h"

#include <cmath>

#include "Core/ScriptFactory.h"
#include "Core/GameObject.h"
#include "Core/World.h"
#include "Components/TransformComponent.h"
#include "PhysX/Components/Phy_CCTComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(C_CombatMovementBridge);

    namespace
    {
        bool ComputeCameraRelative(World& world, const std::string& cameraName, EntityId target,
                                   const Combat::Vec2& input, float& outX, float& outZ)
        {
            outX = input.x;
            outZ = input.y;
            const float inputLen = std::sqrt(outX * outX + outZ * outZ);
            if (inputLen < 0.0001f)
                return false;

            auto* targetTr = world.GetComponent<TransformComponent>(target);
            if (!targetTr)
                return false;

            GameObject camObj = world.FindGameObject(cameraName.c_str());
            if (!camObj.IsValid())
                return true;

            auto* camTr = camObj.GetComponent<TransformComponent>();
            if (!camTr)
                return true;

            float fwdX = targetTr->position.x - camTr->position.x;
            float fwdZ = targetTr->position.z - camTr->position.z;
            float lenFwd = std::sqrt(fwdX * fwdX + fwdZ * fwdZ);
            if (lenFwd > 0.0001f)
            {
                fwdX /= lenFwd;
                fwdZ /= lenFwd;
            }

            float rightX = fwdZ;
            float rightZ = -fwdX;

            const float moveX = (fwdX * input.y) + (rightX * input.x);
            const float moveZ = (fwdZ * input.y) + (rightZ * input.x);
            outX = moveX;
            outZ = moveZ;
            return true;
        }
    }

    void C_CombatMovementBridge::Start()
    {
    }

    void C_CombatMovementBridge::Update(float /*deltaTime*/)
    {
    }

    void C_CombatMovementBridge::OnDisable()
    {
    }

    void C_CombatMovementBridge::Dispatch(const std::vector<Combat::Command>& cmds)
    {
        auto* world = GetWorld();
        if (!world)
            return;

        for (const auto& cmd : cmds)
        {
            if (cmd.type != Combat::CommandType::RequestMove)
                continue;

            const auto payload = std::get<Combat::CmdRequestMove>(cmd.payload);
            auto* cct = world->GetComponent<Phy_CCTComponent>(payload.target);
            auto* tr = world->GetComponent<TransformComponent>(payload.target);
            if (!cct || !tr)
                continue;

            float dirX = 0.0f;
            float dirZ = 0.0f;
            bool hasInput = false;

            if (payload.useCameraRelative)
            {
                hasInput = ComputeCameraRelative(*world, Get_m_cameraName(), payload.target, payload.move, dirX, dirZ);
            }
            else
            {
                dirX = payload.move.x;
                dirZ = payload.move.y;
                hasInput = (std::fabs(dirX) + std::fabs(dirZ)) > 0.0001f;
            }

            if (!hasInput)
            {
                cct->desiredVelocity.x = 0.0f;
                cct->desiredVelocity.z = 0.0f;
                continue;
            }

            float len = std::sqrt(dirX * dirX + dirZ * dirZ);
            if (len > 0.0001f)
            {
                dirX /= len;
                dirZ /= len;
            }

            cct->desiredVelocity.x = dirX * payload.speed;
            cct->desiredVelocity.z = dirZ * payload.speed;
            cct->desiredVelocity.y = 0.0f;

            if (payload.faceMove)
            {
                const float radian = std::atan2(dirX, dirZ);
                const float degree = radian * (180.0f / 3.14159265f);
                tr->SetRotation(0.0f, degree + Get_m_rotationOffsetDeg(), 0.0f);
            }
        }
    }
}
