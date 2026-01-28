#include "C_CombatAnimBridge.h"

#include "Core/ScriptFactory.h"
#include "Core/World.h"
#include "Components/AdvancedAnimationComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(C_CombatAnimBridge);

    void C_CombatAnimBridge::Start()
    {
    }

    void C_CombatAnimBridge::Update(float /*deltaTime*/)
    {
    }

    void C_CombatAnimBridge::OnDisable()
    {
    }

    void C_CombatAnimBridge::Dispatch(const std::vector<Combat::Command>& cmds)
    {
        auto* world = GetWorld();
        if (!world)
            return;

        for (const auto& cmd : cmds)
        {
            if (cmd.type != Combat::CommandType::PlayAnim)
                continue;

            const auto payload = std::get<Combat::CmdPlayAnim>(cmd.payload);
            auto* anim = world->GetComponent<AdvancedAnimationComponent>(payload.target);
            if (!anim)
                continue;

            anim->enabled = true;
            anim->playing = true;
            anim->base.clipA = payload.clip;
            anim->base.autoAdvance = true;
            anim->base.loopA = payload.loop;
            if (payload.immediate)
                anim->base.timeA = 0.0f;
        }
    }
}
