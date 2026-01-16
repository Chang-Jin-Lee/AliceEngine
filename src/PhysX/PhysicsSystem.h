#pragma once

#include "IPhysicsWorld.h"
#include "Components/RigidBodyComponent.h"
#include "Components/ColliderComponent.h"
#include <Core/World.h>
#include <DirectXMath.h>
#include <unordered_map>
#include <memory>

// PhysicsSystem: ECS 컴포넌트와 PhysX를 연결하는 브릿지
// - 컴포넌트 추가/제거 시 물리 액터 자동 생성/삭제
// - Game → Physics 동기화 (Transform 변경)
// - Physics → Game 동기화 (위치/회전)
// - 이벤트 라우팅
class PhysicsSystem
{
public:
    PhysicsSystem(Alice::World& world);
    ~PhysicsSystem();

    // 매 프레임 호출: 컴포넌트 변경 감지 및 동기화
    void Update(float deltaTime);

    // 물리 월드 설정 (씬 전환 시 호출)
    void SetPhysicsWorld(IPhysicsWorld* physicsWorld);

    // 이벤트 콜백 타입
    using EventCallback = void(*)(const PhysicsEvent& event, void* userData);
    void SetEventCallback(EventCallback callback, void* userData);

    // Physics → Game 동기화 (외부에서 호출 가능)
    void SyncPhysicsToGame(const ActiveTransform& transform);

    // 유틸리티: Quat → Euler 변환
    static DirectX::XMFLOAT3 ToEulerRadians(const Quat& q);

private:
    // 컴포넌트 → 물리 액터 생성
    void CreatePhysicsActor(Alice::EntityId entityId);
    void DestroyPhysicsActor(Alice::EntityId entityId);

    // Game → Physics 동기화
    void SyncGameToPhysics(Alice::EntityId entityId, const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& rotation);


    // Transform을 Vec3/Quat로 변환
    static Vec3 ToVec3(const DirectX::XMFLOAT3& v);
    static Quat ToQuat(const DirectX::XMFLOAT3& eulerRadians);
    static DirectX::XMFLOAT3 ToXMFLOAT3(const Vec3& v);

private:
    Alice::World& m_world;
    IPhysicsWorld* m_physicsWorld = nullptr;

    // EntityId → 물리 액터 매핑
    // unique_ptr을 소유하여 래퍼 객체의 생명주기를 안전하게 관리
    struct ActorHandle
    {
        std::unique_ptr<IPhysicsActor> owned;  // 소유권 유지! (래퍼 객체 delete 보장)
        IRigidBody* rigid = nullptr;           // owned.get()의 non-owning 캐시 (편의용)
        
        ActorHandle() = default;
        
        // unique_ptr<IPhysicsActor>로부터 생성 (Static Actor용)
        explicit ActorHandle(std::unique_ptr<IPhysicsActor> actor)
            : owned(std::move(actor))
            , rigid(nullptr)  // Static Actor는 IRigidBody가 아님
        {
        }
        
        // unique_ptr<IRigidBody>로부터 생성 (IRigidBody는 IPhysicsActor를 상속)
        explicit ActorHandle(std::unique_ptr<IRigidBody> body)
            : owned(std::move(body))
            , rigid(static_cast<IRigidBody*>(owned.get()))
        {
        }
        
        bool IsValid() const { return owned && owned->IsValid(); }
        
        IPhysicsActor* GetActor() const { return owned.get(); }
        IRigidBody* GetRigidBody() const { return rigid; }
        
        void Destroy()
        {
            if (owned)
            {
                owned->Destroy();  // native PxActor 정리 예약 (deferred-safe)
                owned.reset();     // 래퍼 객체 delete (누수 방지!)
            }
            rigid = nullptr;
        }
    };
    std::unordered_map<Alice::EntityId, ActorHandle> m_entityToActor;

    // 이전 프레임의 Transform 상태 (변경 감지용)
    struct TransformState
    {
        DirectX::XMFLOAT3 position{};
        DirectX::XMFLOAT3 rotation{};
        DirectX::XMFLOAT3 scale{};
    };
    std::unordered_map<Alice::EntityId, TransformState> m_lastTransforms;

    // 이벤트 콜백
    EventCallback m_eventCallback = nullptr;
    void* m_eventCallbackUserData = nullptr;
};
