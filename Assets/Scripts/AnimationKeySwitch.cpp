#include "AnimationKeySwitch.h"

#include "Core/GameObject.h"

namespace Alice
{
    REGISTER_SCRIPT(AnimationKeySwitch);

    void AnimationKeySwitch::Start()
    {
        auto go = gameObject();
        auto anim = go.GetAnimator();
        if (!anim.IsValid())
        {
            ALICE_LOG_WARN("[AnimationKeySwitch] Animator not valid. (Need SkinnedMesh + animations)");
            return;
        }

        ALICE_LOG_INFO("[AnimationKeySwitch] Ready. clips=%d (press 1/2/3)", anim.ClipCount());
        for (int i = 0; i < anim.ClipCount(); ++i)
            ALICE_LOG_INFO("  - [%d] %s", i, anim.ClipName(i));

        anim.Play();
    }

    void AnimationKeySwitch::Update(float /*deltaTime*/)
    {
        auto* input = Input();
        if (!input)
            return;

        auto go = gameObject();
        auto anim = go.GetAnimator();
        if (!anim.IsValid())
            return;

        if (input->GetKeyDown(KeyCode::Alpha1)) { anim.SetClip(0); anim.Play(); }
        if (input->GetKeyDown(KeyCode::Alpha2)) { anim.SetClip(1); anim.Play(); }
        if (input->GetKeyDown(KeyCode::Alpha3)) { anim.SetClip(2); anim.Play(); }
    }
}






