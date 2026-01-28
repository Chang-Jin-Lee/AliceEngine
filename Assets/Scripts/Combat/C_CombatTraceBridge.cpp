#include "C_CombatTraceBridge.h"

#include "Core/ScriptFactory.h"
#include "Core/World.h"
#include "Components/WeaponTraceComponent.h"
#include "Components/AttackDriverComponent.h"
#include "Components/HealthComponent.h"
#include "C_CombatEventBus.h"
#include "C_Fighter.h"

namespace Alice
{
    REGISTER_SCRIPT(C_CombatTraceBridge);

    namespace
    {
        EntityId ResolveTraceEntity(World& world, EntityId ownerOrWeapon)
        {
            if (world.GetComponent<WeaponTraceComponent>(ownerOrWeapon))
                return ownerOrWeapon;

            auto* driver = world.GetComponent<AttackDriverComponent>(ownerOrWeapon);
            if (!driver || driver->traceGuid == 0)
                return ownerOrWeapon;

            EntityId resolved = world.FindEntityByGuid(driver->traceGuid);
            return (resolved != InvalidEntityId) ? resolved : ownerOrWeapon;
        }
    }

    void C_CombatTraceBridge::Start()
    {
        m_frameHits.clear();
    }

    void C_CombatTraceBridge::Update(float /*deltaTime*/)
    {
    }

    void C_CombatTraceBridge::OnDisable()
    {
        m_frameHits.clear();
    }

    void C_CombatTraceBridge::Dispatch(const std::vector<Combat::Command>& cmds)
    {
        auto* world = GetWorld();
        if (!world)
            return;

        for (const auto& cmd : cmds)
        {
            if (cmd.type == Combat::CommandType::EnableTrace)
            {
                const auto payload = std::get<Combat::CmdEnableTrace>(cmd.payload);
                EntityId traceId = ResolveTraceEntity(*world, payload.weaponOrOwner);
                if (auto* trace = world->GetComponent<WeaponTraceComponent>(traceId))
                {
                    trace->active = true;
                }
            }
            else if (cmd.type == Combat::CommandType::DisableTrace)
            {
                const auto payload = std::get<Combat::CmdDisableTrace>(cmd.payload);
                EntityId traceId = ResolveTraceEntity(*world, payload.weaponOrOwner);
                if (auto* trace = world->GetComponent<WeaponTraceComponent>(traceId))
                {
                    trace->active = false;
                }
            }
        }
    }

    void C_CombatTraceBridge::DrainHits(World& world,
                                       const std::unordered_map<Combat::EntityId, Combat::Fighter*>& fighters,
                                       Combat::CombatEventBus& bus,
                                       float defaultDamage,
                                       bool overrideDamage)
    {
        if (world.HasFrameCombatHits())
        {
            for (const auto& hit : world.GetFrameCombatHits())
            {
                if (fighters.find(hit.victimOwner) == fighters.end())
                    continue;
                if (fighters.find(hit.attackerOwner) == fighters.end())
                    continue;

                Combat::HitEvent ev = hit;
                if (overrideDamage)
                    ev.damage = defaultDamage;
                bus.PushHit(ev);
            }
            return;
        }

        if (!m_useHealthComponentHits)
            return;

        for (const auto& kv : fighters)
        {
            const Combat::EntityId victimId = kv.first;
            auto* hc = world.GetComponent<HealthComponent>(victimId);
            if (!hc)
                continue;

            if (!hc->hitThisFrame && !hc->guardHitThisFrame && !hc->dodgeAvoidedThisFrame)
                continue;

            if (hc->lastHitAttacker == Combat::InvalidEntityId)
                continue;

            Combat::HitEvent ev{};
            ev.attackerOwner = hc->lastHitAttacker;
            ev.victimOwner = victimId;
            ev.hurtboxEntity = Combat::InvalidEntityId;
            ev.part = hc->lastHitPart;
            ev.attackInstanceId = 0; // TODO: pull attackInstanceId from trace
            ev.damage = overrideDamage ? defaultDamage : hc->lastHitDamage;
            ev.debugLog = false;
            ev.hitPosWS = hc->lastHitPosWS;
            ev.hitNormalWS = hc->lastHitNormalWS;
            bus.PushHit(ev);
        }
    }
}
