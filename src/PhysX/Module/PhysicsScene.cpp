#include "PhysicsScene.h"

#include <utility>

PhysicsScene::PhysicsScene(std::shared_ptr<IPhysicsWorld> world)
    : m_world(std::move(world))
{
}

IPhysicsWorld& PhysicsScene::World()
{
    return *m_world;
}

const IPhysicsWorld& PhysicsScene::World() const
{
    return *m_world;
}

void PhysicsScene::Step(float fixedDt)
{
    if (!m_world) return;

    m_world->Step(fixedDt);

    // Cache outputs for the scene to consume.
    m_active.clear();
    m_events.clear();

    m_world->DrainActiveTransforms(m_active);
    m_world->DrainEvents(m_events);
}

void PhysicsScene::ConsumeActiveTransforms(std::vector<ActiveTransform>& out)
{
    out = std::move(m_active);
    m_active.clear();
}

void PhysicsScene::ConsumeEvents(std::vector<PhysicsEvent>& out)
{
    out = std::move(m_events);
    m_events.clear();
}
