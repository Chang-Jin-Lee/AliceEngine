#include "CharacterAnimatorComponent.h"

#include <algorithm>
#include <cmath>

#include "Core/ScriptFactory.h"
#include "Core/GameObject.h"
#include "Core/InputTypes.h"
#include "Components/AdvancedAnimationComponent.h"
#include "Components/TransformComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(CharacterAnimatorComponent);

    namespace
    {
        float SmoothApproach(float current, float target, float speed, float dt)
        {
            float t = std::clamp(speed * dt, 0.0f, 1.0f);
            return current + (target - current) * t;
        }
    }

    void CharacterAnimatorComponent::Update(float DeltaTime)
    {
        auto* input = Input();
        auto go = gameObject();
        if (!input || !go.IsValid())
            return;

        auto* t = go.GetComponent<TransformComponent>();
        if (!t)
            return;

        auto* anim = go.GetComponent<AdvancedAnimationComponent>();
        if (!anim)
            anim = &go.AddComponent<AdvancedAnimationComponent>();

        // ------------------------------------------------------------
        // 1) Input
        // ------------------------------------------------------------
        float inputX = 0.0f;
        float inputZ = 0.0f;
        if (input->GetKey(KeyCode::W)) inputZ += 1.0f;
        if (input->GetKey(KeyCode::S)) inputZ -= 1.0f;
        if (input->GetKey(KeyCode::D)) inputX += 1.0f;
        if (input->GetKey(KeyCode::A)) inputX -= 1.0f;

        const bool hasInput = (inputX != 0.0f || inputZ != 0.0f);
        const bool isRunning = input->GetKey(KeyCode::LeftShift);

        // ------------------------------------------------------------
        // 2) Camera-based movement (same logic as CharacterMovement)
        // ------------------------------------------------------------
        float moveX = 0.0f;
        float moveZ = 0.0f;

        auto mainCamObj = GetWorld()->FindGameObject("MainCamera");
        if (hasInput && mainCamObj.IsValid())
        {
            auto* camT = mainCamObj.GetComponent<TransformComponent>();
            if (camT)
            {
                float fwdX = t->position.x - camT->position.x;
                float fwdZ = t->position.z - camT->position.z;

                float lenFwd = std::sqrt(fwdX * fwdX + fwdZ * fwdZ);
                if (lenFwd > 0.0001f)
                {
                    fwdX /= lenFwd;
                    fwdZ /= lenFwd;
                }

                float rightX = fwdZ;
                float rightZ = -fwdX;

                moveX = (fwdX * inputZ) + (rightX * inputX);
                moveZ = (fwdZ * inputZ) + (rightZ * inputX);
            }
        }
        else if (hasInput)
        {
            moveX = inputX;
            moveZ = inputZ;
        }

        float moveLen = std::sqrt(moveX * moveX + moveZ * moveZ);
        if (moveLen > 0.0001f)
        {
            moveX /= moveLen;
            moveZ /= moveLen;

            const float speed = Get_m_moveSpeed() * (isRunning ? Get_m_runMultiplier() : 1.0f);
            t->position.x += moveX * speed * DeltaTime;
            t->position.z += moveZ * speed * DeltaTime;

            const float rad = std::atan2(moveX, moveZ);
            const float deg = rad * (180.0f / 3.14159265358979323846f);
            t->SetRotation(0.0f, deg + 180.0f, 0.0f);
        }

        // ------------------------------------------------------------
        // 3) Jump + gravity (keep existing behavior)
        // ------------------------------------------------------------
        const bool grounded = (t->position.y <= 0.0f);
        if (grounded)
        {
            t->position.y = 0.0f;
            if (m_velY < 0.0f)
                m_velY = 0.0f;

            if (input->GetKeyDown(KeyCode::Space))
                m_velY = Get_m_jumpSpeed();
        }

        m_velY -= Get_m_gravity() * DeltaTime;
        t->position.y += m_velY * DeltaTime;
        if (t->position.y < 0.0f)
            t->position.y = 0.0f;

        // ------------------------------------------------------------
        // 4) Base layer (Idle -> Walk/Run blend)
        // ------------------------------------------------------------
        const std::string moveClip = isRunning ? Get_m_runClip() : Get_m_walkClip();
        if (moveClip != m_lastMoveClip)
        {
            anim->base.timeB = 0.0f;
            m_lastMoveClip = moveClip;
        }

        const float targetBlend = hasInput ? 1.0f : 0.0f;
        m_moveBlend = SmoothApproach(m_moveBlend, targetBlend, Get_m_blendSpeed(), DeltaTime);

        anim->enabled = true;
        anim->playing = true;
        anim->base.enabled = true;
        anim->base.autoAdvance = true;
        anim->base.clipA = Get_m_idleClip();
        anim->base.clipB = moveClip;
        anim->base.blend01 = m_moveBlend;
        anim->base.layerAlpha = 1.0f;
        anim->base.speedA = 1.0f;
        anim->base.speedB = 1.0f;

        // ------------------------------------------------------------
        // 5) Upper layer (optional pose)
        // ------------------------------------------------------------
        anim->upper.enabled = Get_m_enableUpperLayer();
        anim->upper.autoAdvance = true;
        anim->upper.clipA = Get_m_upperClip();
        anim->upper.clipB.clear();
        anim->upper.blend01 = 0.0f;
        anim->upper.layerAlpha = anim->upper.enabled ? 1.0f : 0.0f;
        anim->upper.speedA = 1.0f;

        // ------------------------------------------------------------
        // 6) Additive (one-shot recoil on LMB)
        // ------------------------------------------------------------
        if (!Get_m_enableAdditive())
        {
            anim->additive.enabled = false;
            anim->additive.autoAdvance = true;
            m_additiveTimer = -1.0f;
        }
        else
        {
            if (input->GetMouseButtonDown(MouseCode::Left))
                m_additiveTimer = 0.0f;

            if (m_additiveTimer >= 0.0f)
            {
                anim->additive.enabled = true;
                anim->additive.autoAdvance = false;
                anim->additive.clip = Get_m_additiveClip();
                anim->additive.refClip = Get_m_additiveRefClip();
                anim->additive.alpha = 1.0f;
                anim->additive.time = m_additiveTimer;
                anim->additive.loop = false;

                m_additiveTimer += DeltaTime;
                if (m_additiveTimer > Get_m_additiveDuration())
                {
                    anim->additive.enabled = false;
                    m_additiveTimer = -1.0f;
                }
            }
            else
            {
                anim->additive.enabled = false;
            }
        }

        // ------------------------------------------------------------
        // 7) Socket setup (weapon attach point)
        // ------------------------------------------------------------
        if (!m_socketInitialized)
        {
            anim->SetSocketSRT(Get_m_socketName(),
                               Get_m_socketParentBone(),
                               Get_m_socketPos(),
                               Get_m_socketRotDeg(),
                               Get_m_socketScale());
            m_socketInitialized = true;
        }

        // ------------------------------------------------------------
        // 8) IK (optional)
        // ------------------------------------------------------------
        anim->ik.enabled = Get_m_enableIK();
        anim->ik.tipBone = Get_m_ikTipBone();
        anim->ik.chainLength = Get_m_ikChainLength();
        anim->ik.weight = Get_m_ikWeight();
        anim->ik.targetMS = Get_m_ikTargetLocal();

        // ------------------------------------------------------------
        // 9) Aim (optional)
        // ------------------------------------------------------------
        anim->aim.enabled = Get_m_enableAim();
        anim->aim.yawRad = DirectX::XMConvertToRadians(Get_m_aimYawDeg());
        anim->aim.weight = Get_m_aimWeight();
    }
}

