#include "PhysTestComp.h"
#include "Core/ScriptFactory.h"
#include "Core/Logger.h"
#include "Core/GameObject.h"
#include "Core/World.h"

#include "PhysX/Components/Phy_RigidBodyComponent.h"
#include "PhysX/Components/Phy_ColliderComponent.h"
#include "PhysX/Components/Phy_SettingsComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(PhysicsTest);

    void PhysicsTest::Awake()
    {
        auto* world = GetWorld();
        if (!world) return;

        // 씬에 Phy_SettingsComponent가 있는지 확인
        const auto& settingsMap = world->GetComponents<Phy_SettingsComponent>();
        if (settingsMap.empty())
        {
            // 새 엔티티를 만들어서 Phy_SettingsComponent 추가
            // (어떤 엔티티든 상관없지만, 빈 엔티티가 깔끔함)
            EntityId physicsSettingsEntity = world->CreateEntity();
            auto& settings = world->AddComponent<Phy_SettingsComponent>(physicsSettingsEntity);
            settings.enablePhysics = true;
            settings.gravity = { 0.0f, -9.81f, 0.0f };
            settings.fixedDt = 1.0f / 60.0f;
            settings.maxSubsteps = 4;

            ALICE_LOG_INFO("[PhysicsTest] Phy_SettingsComponent added to new entity (%llu)",
                (unsigned long long)physicsSettingsEntity);

            // 씬 전환 시 RefreshPhysicsForCurrentWorld가 호출되지만,
            // 지금 당장은 Start()에서 확인하도록 함
        }
        else
        {
            const auto& settings = settingsMap.begin()->second;
            ALICE_LOG_INFO("[PhysicsTest] Physics settings: enablePhysics=%d, gravity=(%.2f, %.2f, %.2f)",
                settings.enablePhysics,
                settings.gravity.x, settings.gravity.y, settings.gravity.z);
        }

        auto go = gameObject();
        if (!go.IsValid())
        {
            ALICE_LOG_ERRORF("[PhysicsTest] GameObject is invalid!");
            return;
        }

        // TransformComponent 확인 및 추가 (필수!)
        auto* transform = go.GetComponent<TransformComponent>();
        if (!transform)
        {
            ALICE_LOG_WARN("[PhysicsTest] TransformComponent not found. Adding it.");
            auto& tr = go.AddComponent<TransformComponent>();
            tr.position = { 0.0f, 5.0f, 0.0f };  // 초기 위치 설정 (높이 5)
            tr.rotation = { 0.0f, 0.0f, 0.0f };
            tr.scale = { 1.0f, 1.0f, 1.0f };
        }
        else
        {
            // Transform이 이미 있으면 위치 확인
            ALICE_LOG_INFO("[PhysicsTest] Transform 위치: (%.2f, %.2f, %.2f)",
                transform->position.x, transform->position.y, transform->position.z);
        }

        // RigidBody 추가 및 설정
        auto& rb = go.AddComponent<Phy_RigidBodyComponent>();
        rb.density = 1.0f;
        rb.gravityEnabled = true;  // 중력 활성화
        rb.startAwake = true;      // 시작 시 깨어있음
        rb.isKinematic = false;    // Kinematic 아님 (물리 영향 받음)

        // Collider 추가 및 설정 (Box)
        // 렌더링 큐브 메시가 -1~1 범위이므로 기본 크기가 2입니다.
        // 콜라이더 halfExtents를 1.0으로 설정하면 scale=1일 때 크기가 2가 되어 렌더링 큐브와 일치합니다.
        auto& col = go.AddComponent<Phy_ColliderComponent>();
        col.type = ColliderType::Box;
        col.halfExtents = { 1.0f, 1.0f, 1.0f };  // 렌더링 큐브 크기와 일치하도록 변경
        col.isTrigger = false;
        col.staticFriction = 0.5f;
        col.dynamicFriction = 0.3f;

        ALICE_LOG_INFO("[PhysicsTest] Physics components added (density=%.2f, gravityEnabled=%d)",
            rb.density, rb.gravityEnabled);
    }

    void PhysicsTest::Start()
    {
        // Start()는 모든 Awake()가 호출된 후 실행됨
        // 물리 월드가 제대로 생성되었는지 확인
        auto* world = GetWorld();
        if (!world) return;

        auto* physicsWorld = world->GetPhysicsWorld();
        if (physicsWorld)
        {
            ALICE_LOG_INFO("[PhysicsTest] Physics world created successfully!");
        }
        else
        {
            ALICE_LOG_ERRORF("[PhysicsTest] Physics world not created! Check Phy_SettingsComponent.");
        }

        auto go = gameObject();
        if (!go.IsValid()) return;

        auto* rb = go.GetComponent<Phy_RigidBodyComponent>();
        if (rb)
        {
            ALICE_LOG_INFO("[PhysicsTest] RigidBody component: physicsActorHandle = %p (at Start)", rb->physicsActorHandle);
        }
    }

    void PhysicsTest::Update(float deltaTime)
    {
        auto go = gameObject();
        if (!go.IsValid()) return;

        auto* transform = go.GetComponent<TransformComponent>();
        if (transform)
        {
            // 물리 액터가 생성되었는지 확인
            auto* rb = go.GetComponent<Phy_RigidBodyComponent>();
            if (rb && rb->physicsActorHandle)
            {
                // 물리 액터가 생성되었으면 Y 위치를 주기적으로 로그
                static float logTimer = 0.0f;
                logTimer += deltaTime;
                if (logTimer >= 1.0f)  // 1초마다
                {
                    logTimer = 0.0f;
                    ALICE_LOG_INFO("[PhysicsTest] Position: Y = %.2f (physics actor exists)", transform->position.y);
                }
            }
            else if (rb)
            {
                // RigidBody는 있지만 아직 물리 액터가 생성되지 않음
                static float logTimer = 0.0f;
                logTimer += deltaTime;
                if (logTimer >= 2.0f)  // 2초마다
                {
                    logTimer = 0.0f;
                    ALICE_LOG_WARN("[PhysicsTest] RigidBody exists but physics actor not created yet. Transform position: Y = %.2f",
                        transform->position.y);
                }
            }
        }

        auto* input = Input();
        if (!input) return;

        // 스페이스바로 점프 (실제로는 RigidBody에 힘을 가하는 방식 사용)
        if (input->GetKeyDown(KeyCode::Space))
        {
            auto* rb = go.GetComponent<Phy_RigidBodyComponent>();
            if (rb)
            {
                // 주의: 실제 힘 적용은 IRigidBody 인터페이스를 통해야 함
                // 여기서는 컴포넌트 예시만 보여줌
                ALICE_LOG_INFO("[PhysicsTest] 점프! (물리 액터 핸들: %p)", rb->physicsActorHandle);
            }
        }
    }
}