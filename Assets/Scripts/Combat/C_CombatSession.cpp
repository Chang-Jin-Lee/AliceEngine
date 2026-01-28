#include "C_CombatSession.h"

#include <unordered_map>

#include "Core/ScriptFactory.h"
#include "Core/Logger.h"
#include "Core/World.h"
#include "Components/IDComponent.h"
#include "Components/HealthComponent.h"
#include "Components/AttackDriverComponent.h"
#include "Components/WeaponTraceComponent.h"

#include "C_CombatContracts.h"
#include "C_CombatEventBus.h"
#include "C_ActionData.h"
#include "C_ActionFsm.h"
#include "C_Fighter.h"
#include "C_CombatResolver.h"
#include "C_CombatApply.h"
#include "C_CombatAnimBridge.h"
#include "C_CombatMovementBridge.h"
#include "C_CombatTraceBridge.h"
#include "C_PlayerInputSource.h"
#include "C_BossBrain.h"

namespace Alice
{
    struct C_CombatSession::SessionState
    {
        Combat::ActionDatabase db = Combat::ActionDatabase::MakeDefault();
        Combat::Fighter player{};
        Combat::Fighter boss{};
        Combat::ActionFsm playerFsm{ &db };
        Combat::ActionFsm bossFsm{ &db };
        Combat::CombatEventBus bus{};
        Combat::CombatResolver resolver{};
        Combat::CombatApply apply{};
        std::unordered_map<EntityId, Combat::Fighter*> fighterMap;

        void Init()
        {
            fighterMap.clear();
            player = Combat::Fighter{};
            boss = Combat::Fighter{};
            playerFsm.Reset();
            bossFsm.Reset();
            bus.ClearAll();
        }
    };

    REGISTER_SCRIPT(C_CombatSession);

    static IScript* FindScriptOnEntity(World& world, EntityId entityId, const char* name)
    {
        auto* scripts = world.GetScripts(entityId);
        if (!scripts)
            return nullptr;
        for (auto& sc : *scripts)
        {
            if (!sc.instance)
                continue;
            if (sc.scriptName == name)
                return sc.instance.get();
        }
        return nullptr;
    }

    static bool HasDeferredEvent(const Combat::ResolveOutput& resolved, Combat::CombatEventType type)
    {
        for (const auto& ev : resolved.deferred)
        {
            if (ev.type == type)
                return true;
        }
        return false;
    }

    static void UpdateHealthHitInfo(World& world,
                                    const Combat::HitEvent& hit,
                                    const Combat::ResolveOutput& resolved,
                                    const Combat::FighterSnapshot& victim)
    {
        auto* hc = world.GetComponent<HealthComponent>(hit.victimOwner);
        if (!hc)
            return;

        hc->lastHitAttacker = hit.attackerOwner;
        hc->lastHitPart = hit.part;
        hc->lastHitPosWS = hit.hitPosWS;
        hc->lastHitNormalWS = hit.hitNormalWS;

        const bool wasHit = HasDeferredEvent(resolved, Combat::CombatEventType::OnHit);
        const bool wasGuard = HasDeferredEvent(resolved, Combat::CombatEventType::OnGuarded);
        const bool wasParry = HasDeferredEvent(resolved, Combat::CombatEventType::OnParried);

        if (wasHit || wasGuard || wasParry)
            hc->hitThisFrame = true;

        if (wasGuard || wasParry)
            hc->guardHitThisFrame = true;

        if (wasHit)
            hc->lastHitDamage = hit.damage;
        else
            hc->lastHitDamage = 0.0f;

        if (!wasHit && !wasGuard && !wasParry && victim.flags.invulnActive)
            hc->dodgeAvoidedThisFrame = true;
    }

    EntityId C_CombatSession::ResolveEntity(uint64_t guid) const
    {
        if (guid == 0)
            return InvalidEntityId;
        if (!GetWorld())
            return InvalidEntityId;
        return GetWorld()->FindEntityByGuid(guid);
    }

    EntityId C_CombatSession::ResolveTraceEntity(EntityId ownerId) const
    {
        if (!GetWorld())
            return ownerId;
        auto* driver = GetWorld()->GetComponent<AttackDriverComponent>(ownerId);
        if (!driver || driver->traceGuid == 0)
            return ownerId;
        EntityId resolved = GetWorld()->FindEntityByGuid(driver->traceGuid);
        return (resolved != InvalidEntityId) ? resolved : ownerId;
    }

    void C_CombatSession::OverrideWeaponTraceDamage(EntityId ownerId) const
    {
        if (!m_overrideWeaponTraceDamage || !GetWorld())
            return;
        EntityId traceId = ResolveTraceEntity(ownerId);
        if (auto* trace = GetWorld()->GetComponent<WeaponTraceComponent>(traceId))
        {
            trace->baseDamage = 0.0f;
        }
    }

    void C_CombatSession::Start()
    {
        if (!m_state)
            m_state = std::make_unique<SessionState>();
        m_state->Init();

        if (auto* world = GetWorld())
            world->SetScriptCombatEnabled(true);
    }

    void C_CombatSession::OnEnable()
    {
        if (!m_state)
            m_state = std::make_unique<SessionState>();

        if (auto* world = GetWorld())
            world->SetScriptCombatEnabled(true);
    }

    void C_CombatSession::OnDisable()
    {
        if (m_state)
            m_state->Init();

        if (auto* world = GetWorld())
            world->SetScriptCombatEnabled(false);
    }

    void C_CombatSession::ForceReset()
    {
        if (m_state)
            m_state->Init();
    }

    void C_CombatSession::Update(float /*deltaTime*/)
    {
    }

    void C_CombatSession::PostCombatUpdate(float deltaTime)
    {
        if (!m_state || !GetWorld())
            return;

        World& world = *GetWorld();
        const EntityId playerId = ResolveEntity(m_playerGuid);
        const EntityId bossId = ResolveEntity(m_bossGuid);
        if (playerId == InvalidEntityId || bossId == InvalidEntityId)
            return;

        OverrideWeaponTraceDamage(playerId);
        OverrideWeaponTraceDamage(bossId);

        m_state->player.id = playerId;
        m_state->player.team = Combat::Team::Player;
        m_state->boss.id = bossId;
        m_state->boss.team = Combat::Team::Enemy;

        m_state->fighterMap.clear();
        m_state->fighterMap[playerId] = &m_state->player;
        m_state->fighterMap[bossId] = &m_state->boss;

        m_state->bus.ClearFrame();

        Combat::Intent playerIntent{};
        if (auto* script = FindScriptOnEntity(world, playerId, "C_PlayerInputSource"))
        {
            if (auto* input = dynamic_cast<C_PlayerInputSource*>(script))
                playerIntent = input->GetIntent(deltaTime);
        }

        Combat::Intent bossIntent{};
        if (auto* script = FindScriptOnEntity(world, bossId, "C_BossBrain"))
        {
            if (auto* brain = dynamic_cast<C_BossBrain*>(script))
                bossIntent = brain->Think(deltaTime, playerId);
        }

        Combat::Sensors sPlayer = m_state->player.BuildSensors(world, bossId, deltaTime);
        Combat::Sensors sBoss = m_state->boss.BuildSensors(world, playerId, deltaTime);

        m_state->player.hp = sPlayer.hp;
        m_state->boss.hp = sBoss.hp;

        const auto& ePlayer = m_state->bus.PeekDeferred(playerId);
        const auto& eBoss = m_state->bus.PeekDeferred(bossId);

        auto outPlayer = m_state->playerFsm.Update(playerId, playerIntent, sPlayer, ePlayer, deltaTime);
        auto outBoss = m_state->bossFsm.Update(bossId, bossIntent, sBoss, eBoss, deltaTime);

        m_state->player.state = outPlayer.state;
        m_state->player.flags = outPlayer.flags;
        m_state->boss.state = outBoss.state;
        m_state->boss.flags = outBoss.flags;

        m_state->bus.ClearDeferred(playerId);
        m_state->bus.ClearDeferred(bossId);

        if (auto* script = FindScriptOnEntity(world, GetOwnerId(), "C_CombatAnimBridge"))
        {
            if (auto* animBridge = dynamic_cast<C_CombatAnimBridge*>(script))
            {
                animBridge->Dispatch(outPlayer.commands);
                animBridge->Dispatch(outBoss.commands);
            }
        }

        if (auto* script = FindScriptOnEntity(world, GetOwnerId(), "C_CombatMovementBridge"))
        {
            if (auto* moveBridge = dynamic_cast<C_CombatMovementBridge*>(script))
            {
                moveBridge->Dispatch(outPlayer.commands);
                moveBridge->Dispatch(outBoss.commands);
            }
        }

        if (auto* script = FindScriptOnEntity(world, GetOwnerId(), "C_CombatTraceBridge"))
        {
            if (auto* traceBridge = dynamic_cast<C_CombatTraceBridge*>(script))
            {
                traceBridge->Dispatch(outPlayer.commands);
                traceBridge->Dispatch(outBoss.commands);
                const bool overrideDamage = m_overrideWeaponTraceDamage;
                traceBridge->DrainHits(world, m_state->fighterMap, m_state->bus, m_defaultDamage, overrideDamage);
            }
        }

        for (const auto& hit : m_state->bus.Hits())
        {
            Combat::FighterSnapshot attacker = (hit.attackerOwner == playerId)
                ? m_state->player.Snapshot()
                : m_state->boss.Snapshot();
            Combat::FighterSnapshot victim = (hit.victimOwner == playerId)
                ? m_state->player.Snapshot()
                : m_state->boss.Snapshot();

            auto resolved = m_state->resolver.ResolveOne(hit, attacker, victim);

            if (world.IsScriptCombatEnabled())
                UpdateHealthHitInfo(world, hit, resolved, victim);

            const bool skipDamage = !m_applyDamageInScript;
            m_state->apply.ApplyImmediate(world, m_state->fighterMap, m_state->bus, resolved.immediate, skipDamage);

            for (const auto& ev : resolved.deferred)
                m_state->bus.PushDeferred(ev);
        }
    }
}
