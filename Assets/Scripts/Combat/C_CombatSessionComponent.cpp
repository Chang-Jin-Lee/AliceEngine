#include "C_CombatSessionComponent.h"

#include <unordered_map>

#include "Core/ScriptFactory.h"
#include "Core/Logger.h"
#include "Core/World.h"
#include "Components/IDComponent.h"
#include "Components/HealthComponent.h"
#include "Components/AttackDriverComponent.h"
#include "Components/AdvancedAnimationComponent.h"

#include "C_CombatContracts.h"
#include "C_CombatEventBus.h"
#include "C_ActionFsm.h"
#include "C_Fighter.h"
#include "C_CombatResolver.h"
#include "C_PlayerInputSourceComponent.h"
#include "C_BossBrainComponent.h"

namespace Alice
{
    struct C_CombatSessionComponent::SessionState
    {
        Combat::Fighter player{};
        Combat::Fighter boss{};
        Combat::ActionFsm playerFsm{};
        Combat::ActionFsm bossFsm{};
        Combat::CombatEventBus bus{};
        Combat::CombatResolver resolver{};
        std::unordered_map<EntityId, Combat::Fighter*> fighterMap;
        Combat::ActionState prevPlayerState = Combat::ActionState::Idle;
        Combat::ActionState prevBossState = Combat::ActionState::Idle;

        void Init()
        {
            fighterMap.clear();
            player = Combat::Fighter{};
            boss = Combat::Fighter{};
            playerFsm.Reset();
            bossFsm.Reset();
            bus.ClearFrame();
        }
    };

    REGISTER_SCRIPT(C_CombatSessionComponent);

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

    EntityId C_CombatSessionComponent::ResolveEntity(uint64_t guid) const
    {
        if (guid == 0)
            return InvalidEntityId;
        if (!GetWorld())
            return InvalidEntityId;
        return GetWorld()->FindEntityByGuid(guid);
    }

    void C_CombatSessionComponent::Start()
    {
        if (!m_state)
            m_state = std::make_unique<SessionState>();
        m_state->Init();

        if (auto* world = GetWorld())
            world->SetScriptCombatEnabled(false);
    }

    void C_CombatSessionComponent::OnEnable()
    {
        if (!m_state)
            m_state = std::make_unique<SessionState>();

        if (auto* world = GetWorld())
            world->SetScriptCombatEnabled(false);
    }

    void C_CombatSessionComponent::OnDisable()
    {
        if (m_state)
            m_state->Init();

        if (auto* world = GetWorld())
            world->SetScriptCombatEnabled(false);
    }

    void C_CombatSessionComponent::ForceReset()
    {
        if (m_state)
            m_state->Init();
    }

    void C_CombatSessionComponent::Update(float /*deltaTime*/)
    {
    }

    void C_CombatSessionComponent::PostCombatUpdate(float deltaTime)
    {
        if (!m_state || !GetWorld())
            return;

        World& world = *GetWorld();
        world.SetScriptCombatEnabled(false);
        const EntityId playerId = ResolveEntity(m_playerGuid);
        const EntityId bossId = ResolveEntity(m_bossGuid);
        if (playerId == InvalidEntityId || bossId == InvalidEntityId)
            return;

        m_state->player.id = playerId;
        m_state->player.team = Combat::Team::Player;
        m_state->boss.id = bossId;
        m_state->boss.team = Combat::Team::Enemy;

        m_state->fighterMap.clear();
        m_state->fighterMap[playerId] = &m_state->player;
        m_state->fighterMap[bossId] = &m_state->boss;

        m_state->bus.ClearFrame();
        if (world.HasFrameCombatHits())
        {
            for (const auto& hit : world.GetFrameCombatHits())
                m_state->bus.PushHit(hit);
        }

        Combat::Intent playerIntent{};
        if (auto* script = FindScriptOnEntity(world, playerId, "C_PlayerInputSourceComponent"))
        {
            if (auto* input = dynamic_cast<C_PlayerInputSourceComponent*>(script))
                playerIntent = input->GetIntent(deltaTime);
        }

        Combat::Intent bossIntent{};
        if (auto* script = FindScriptOnEntity(world, bossId, "C_BossBrainComponent"))
        {
            if (auto* brain = dynamic_cast<C_BossBrainComponent*>(script))
                bossIntent = brain->Think(deltaTime, playerId);
        }

        Combat::Sensors sPlayer = m_state->player.BuildSensors(world, bossId, deltaTime);
        Combat::Sensors sBoss = m_state->boss.BuildSensors(world, playerId, deltaTime);

        m_state->player.hp = sPlayer.hp;
        m_state->boss.hp = sBoss.hp;

        if (auto* driver = world.GetComponent<AttackDriverComponent>(playerId))
        {
            sPlayer.attackWindowActive = driver->attackActive;
            sPlayer.guardWindowActive = driver->guardActive;
            sPlayer.dodgeWindowActive = driver->dodgeActive;
        }
        if (auto* driver = world.GetComponent<AttackDriverComponent>(bossId))
        {
            sBoss.attackWindowActive = driver->attackActive;
            sBoss.guardWindowActive = driver->guardActive;
            sBoss.dodgeWindowActive = driver->dodgeActive;
        }

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
        auto ApplyAnimByState = [&](EntityId entityId, Combat::ActionState curr, Combat::ActionState& prev) {
            if (curr == prev)
                return;

            auto* anim = world.GetComponent<AdvancedAnimationComponent>(entityId);
            auto* driver = world.GetComponent<AttackDriverComponent>(entityId);
            if (!anim || !driver)
            {
                prev = curr;
                return;
            }

            auto resolveClipByType = [&](AttackDriverNotifyType type) -> std::string {
                for (const auto& clip : driver->clips)
                {
                    if (!clip.enabled || clip.type != type)
                        continue;

                    switch (clip.source)
                    {
                    case AttackDriverClipSource::BaseA: return anim->base.clipA;
                    case AttackDriverClipSource::BaseB: return anim->base.clipB;
                    case AttackDriverClipSource::UpperA: return anim->upper.clipA;
                    case AttackDriverClipSource::UpperB: return anim->upper.clipB;
                    case AttackDriverClipSource::Additive: return anim->additive.clip;
                    case AttackDriverClipSource::Explicit:
                    default: return clip.clipName;
                    }
                }
                return {};
            };

            std::string clipName;
            bool loop = false;
            bool immediate = true;

            if (curr == Combat::ActionState::Attack)
            {
                clipName = resolveClipByType(AttackDriverNotifyType::Attack);
            }
            else if (curr == Combat::ActionState::Dodge)
            {
                clipName = resolveClipByType(AttackDriverNotifyType::Dodge);
            }
            else if (curr == Combat::ActionState::Guard)
            {
                clipName = resolveClipByType(AttackDriverNotifyType::Guard);
                loop = true;
                immediate = false;
            }

            if (!clipName.empty())
            {
                anim->enabled = true;
                anim->playing = true;
                anim->base.clipA = clipName;
                anim->base.autoAdvance = true;
                anim->base.loopA = loop;
                if (immediate)
                    anim->base.timeA = 0.0f;
            }

            prev = curr;
        };

        ApplyAnimByState(playerId, outPlayer.state, m_state->prevPlayerState);
        ApplyAnimByState(bossId, outBoss.state, m_state->prevBossState);

        for (const auto& hit : m_state->bus.Hits())
        {
            Combat::FighterSnapshot attacker = (hit.attackerOwner == playerId)
                ? m_state->player.Snapshot()
                : m_state->boss.Snapshot();
            Combat::FighterSnapshot victim = (hit.victimOwner == playerId)
                ? m_state->player.Snapshot()
                : m_state->boss.Snapshot();

            auto resolved = m_state->resolver.ResolveOne(hit, attacker, victim);

            for (const auto& ev : resolved.deferred)
                m_state->bus.PushDeferred(ev);
        }
    }
}
