#pragma once
#include "Core/IScript.h"
#include "Core/ScriptReflection.h"

namespace Alice
{
    class PhysicsTest : public IScript
    {
        ALICE_BODY(PhysicsTest);
    public:
        void Awake() override;
        void Start() override;
        void Update(float deltaTime) override;

    private:
        ALICE_PROPERTY(float, m_jumpForce, 5.0f);
    };
}