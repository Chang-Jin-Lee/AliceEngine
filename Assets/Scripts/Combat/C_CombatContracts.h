#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <variant>

#include <DirectXMath.h>

#include "Core/Entity.h"
#include "Game/CombatHitEvent.h"

namespace Alice::Combat
{
    using EntityId = Alice::EntityId;
    static constexpr EntityId InvalidEntityId = Alice::InvalidEntityId;

    struct Vec2
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    using Vec3 = DirectX::XMFLOAT3;

    enum class Team : uint8_t
    {
        Player = 0,
        Enemy = 1,
        Neutral = 2,
    };

    enum class ActionState : uint8_t
    {
        Idle,
        Move,
        Attack,
        Dodge,
        Guard,
        Hitstun,
        Dead,
    };

    struct ActionFlags
    {
        bool hitActive = false;
        bool guardActive = false;
        bool parryWindowActive = false;
        bool invulnActive = false;
        bool canBeInterrupted = true;
    };

    struct Intent
    {
        Vec2 move{};
        bool attackPressed = false;
        bool guardHeld = false;
        bool dodgePressed = false;
        bool lockOnToggle = false;
    };

    struct Sensors
    {
        float dt = 0.0f;
        float hp = 100.0f;
        float stamina = 100.0f;

        bool grounded = true;
        bool blocked = false;

        EntityId targetId = InvalidEntityId;
        float distToTarget = 9999.0f;
        float angleToTargetDeg = 0.0f;
        bool targetInFront = true;

        bool attackWindowActive = false;
        bool guardWindowActive = false;
        bool dodgeWindowActive = false;
        bool invulnActive = false;

        float moveSpeed = 5.0f;
    };

    using HitEvent = Alice::CombatHitEvent;

    enum class CombatEventType : uint8_t
    {
        OnHit,
        OnGuarded,
        OnParried,
        OnGuardBreak,
        OnDeath
    };

    struct CombatEvent
    {
        CombatEventType type{};
        EntityId subject = InvalidEntityId;
        EntityId other = InvalidEntityId;
        uint32_t attackInstanceId = 0;
        float value = 0.0f;
    };

    enum class CommandType : uint8_t
    {
        ApplyDamage,
        ConsumeStamina,
        EnterHitstun,
        ForceCancelAttack,
        DisableTrace,
        EnableTrace,
        PlayAnim,
        RequestMove
    };

    struct CmdApplyDamage { EntityId target = InvalidEntityId; float amount = 0.0f; };
    struct CmdConsumeStamina { EntityId target = InvalidEntityId; float amount = 0.0f; };
    struct CmdEnterHitstun { EntityId target = InvalidEntityId; float durationSec = 0.0f; };
    struct CmdForceCancelAttack { EntityId target = InvalidEntityId; };
    struct CmdDisableTrace { EntityId weaponOrOwner = InvalidEntityId; };
    struct CmdEnableTrace { EntityId weaponOrOwner = InvalidEntityId; };
    struct CmdPlayAnim
    {
        EntityId target = InvalidEntityId;
        std::string clip;
        bool immediate = true;
        bool loop = false;
    };
    struct CmdRequestMove
    {
        EntityId target = InvalidEntityId;
        Vec2 move{};
        float speed = 5.0f;
        bool useCameraRelative = true;
        bool faceMove = true;
    };

    using CommandPayload = std::variant<
        CmdApplyDamage,
        CmdConsumeStamina,
        CmdEnterHitstun,
        CmdForceCancelAttack,
        CmdDisableTrace,
        CmdEnableTrace,
        CmdPlayAnim,
        CmdRequestMove>;

    struct Command
    {
        CommandType type{};
        CommandPayload payload{};
    };

    struct FsmOutput
    {
        ActionState state = ActionState::Idle;
        ActionFlags flags{};
        std::vector<Command> commands;
    };

    struct ResolveOutput
    {
        std::vector<Command> immediate;
        std::vector<CombatEvent> deferred;
    };
}
