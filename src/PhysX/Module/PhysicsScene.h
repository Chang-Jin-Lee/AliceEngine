#pragma once

#include <memory>
#include <vector>

#include "../IPhysicsWorld.h"

// ============================================================
// PhysicsScene
// - Scene-owned wrapper around IPhysicsWorld
// - Enforces a sane per-step order:
//     world.Step(dt)
//     world.DrainActiveTransforms(...)
//     world.DrainEvents(...)
//
// Your ECS/scene can:
// - push kinematic targets BEFORE calling Step()
// - consume transforms/events AFTER calling Step()
// ============================================================

class PhysicsScene
{
public:
    PhysicsScene() = default;
    explicit PhysicsScene(std::shared_ptr<IPhysicsWorld> world);

    bool IsValid() const noexcept { return (m_world != nullptr); }

    IPhysicsWorld& World();
    const IPhysicsWorld& World() const;

    // Step simulation and capture outputs internally.
    void Step(float fixedDt);

    // Move out captured results (no extra allocations if you reuse the vectors).
    void ConsumeActiveTransforms(std::vector<ActiveTransform>& out);
    void ConsumeEvents(std::vector<PhysicsEvent>& out);

private:
    std::shared_ptr<IPhysicsWorld> m_world;
    std::vector<ActiveTransform> m_active;
    std::vector<PhysicsEvent> m_events;
};
