#include "Core/ComponentRegistry.h"
#include "Core/World.h"
#include "Logger.h"

#include <rttr/registration>
#include <DirectXMath.h>

// 물리 컴포넌트 헤더
#include "PhysX/Components/RigidBodyComponent.h"
#include "PhysX/Components/ColliderComponent.h"
#include "PhysX/IPhysicsWorld.h"

using namespace DirectX;

namespace Alice
{
	void LinkComponentRegistry() {
        ALICE_LOG_INFO("[리플렉션] rttr Success");
    }

    RTTR_REGISTRATION
    {
        // === DirectX 타입 등록 ===
        rttr::registration::class_<XMFLOAT3>("XMFLOAT3")
            .constructor<>()
            .property("x", &XMFLOAT3::x)
            .property("y", &XMFLOAT3::y)
            .property("z", &XMFLOAT3::z);

        // XMFLOAT4X4는 4x4 행렬을 나타내는 타입
        // 렌더링할 때 4x4 행렬을 렌더링하기 위해 등록
        rttr::registration::class_<XMFLOAT4X4>("XMFLOAT4X4")
            .constructor<>()
            .property("_11", &XMFLOAT4X4::_11)
            .property("_12", &XMFLOAT4X4::_12)
            .property("_13", &XMFLOAT4X4::_13)
            .property("_14", &XMFLOAT4X4::_14)
            .property("_21", &XMFLOAT4X4::_21)
            .property("_22", &XMFLOAT4X4::_22)
            .property("_23", &XMFLOAT4X4::_23)
            .property("_24", &XMFLOAT4X4::_24)
            .property("_31", &XMFLOAT4X4::_31)
            .property("_32", &XMFLOAT4X4::_32)
            .property("_33", &XMFLOAT4X4::_33)
            .property("_34", &XMFLOAT4X4::_34)
            .property("_41", &XMFLOAT4X4::_41)
            .property("_42", &XMFLOAT4X4::_42)
            .property("_43", &XMFLOAT4X4::_43)
            .property("_44", &XMFLOAT4X4::_44);

        // === TransformComponent 등록 ===
        rttr::registration::class_<TransformComponent>("TransformComponent")
            .constructor<>()
            .property("position", &TransformComponent::position)
            .property("rotation", &TransformComponent::rotation)
            .property("scale", &TransformComponent::scale);

        // === MaterialComponent 등록 ===
        rttr::registration::class_<MaterialComponent>("MaterialComponent")
            .constructor<>()
            .property("color", &MaterialComponent::color)
            .property("roughness", &MaterialComponent::roughness)
            .property("metalness", &MaterialComponent::metalness)
            .property("assetPath", &MaterialComponent::assetPath)
            .property("albedoTexturePath", &MaterialComponent::albedoTexturePath);

        // === SkinnedMeshComponent 등록 ===
        // boneMatrices는 뼈 행렬을 나타내는 프로퍼티
        rttr::registration::class_<SkinnedMeshComponent>("SkinnedMeshComponent")
            .constructor<>()
            .property("meshAssetPath", &SkinnedMeshComponent::meshAssetPath)
            .property("instanceAssetPath", &SkinnedMeshComponent::instanceAssetPath)
            .property("boneCount", &SkinnedMeshComponent::boneCount);

        // === SkinnedAnimationComponent 등록 ===
        rttr::registration::class_<SkinnedAnimationComponent>("SkinnedAnimationComponent")
            .constructor<>()
            .property("clipIndex", &SkinnedAnimationComponent::clipIndex)
            .property("playing", &SkinnedAnimationComponent::playing)
            .property("speed", &SkinnedAnimationComponent::speed)
            .property("timeSec", &SkinnedAnimationComponent::timeSec);
        
        // palette는 팔레트를 나타내는 프로퍼티

        // === CameraComponent 등록 ===
        rttr::registration::class_<CameraComponent>("CameraComponent")
            .constructor<>()
            .property("primary", &CameraComponent::primary)
            .property("fovYRad", &CameraComponent::fovYRad)
            .property("nearPlane", &CameraComponent::nearPlane)
            .property("farPlane", &CameraComponent::farPlane);

        rttr::registration::class_<PhysicsSceneSettingsComponent>("PhysicsSceneSettingsComponent")
            .constructor<>()
            .property("enablePhysics", &PhysicsSceneSettingsComponent::enablePhysics)
            .property("gravity", &PhysicsSceneSettingsComponent::gravity)
            .property("fixedDt", &PhysicsSceneSettingsComponent::fixedDt)
            .property("maxSubsteps", &PhysicsSceneSettingsComponent::maxSubsteps);

        // === ColliderType enum 등록 ===
        rttr::registration::enumeration<ColliderType>("ColliderType")
        (
            rttr::value("Box", ColliderType::Box),
            rttr::value("Sphere", ColliderType::Sphere),
            rttr::value("Capsule", ColliderType::Capsule)
        );

        // === RigidBodyLockFlags enum 등록 ===
        rttr::registration::enumeration<RigidBodyLockFlags>("RigidBodyLockFlags")
        (
            rttr::value("None", RigidBodyLockFlags::None),
            rttr::value("LockLinearX", RigidBodyLockFlags::LockLinearX),
            rttr::value("LockLinearY", RigidBodyLockFlags::LockLinearY),
            rttr::value("LockLinearZ", RigidBodyLockFlags::LockLinearZ),
            rttr::value("LockAngularX", RigidBodyLockFlags::LockAngularX),
            rttr::value("LockAngularY", RigidBodyLockFlags::LockAngularY),
            rttr::value("LockAngularZ", RigidBodyLockFlags::LockAngularZ)
        );

        // === RigidBodyComponent 등록 (physicsActorHandle는 내부용이므로 등록하지 않음) ===
        rttr::registration::class_<RigidBodyComponent>("RigidBodyComponent")
            .constructor<>()
            .property("density", &RigidBodyComponent::density)
            .property("massOverride", &RigidBodyComponent::massOverride)
            .property("isKinematic", &RigidBodyComponent::isKinematic)
            .property("gravityEnabled", &RigidBodyComponent::gravityEnabled)
            .property("startAwake", &RigidBodyComponent::startAwake)
            .property("enableCCD", &RigidBodyComponent::enableCCD)
            .property("enableSpeculativeCCD", &RigidBodyComponent::enableSpeculativeCCD)
            .property("lockFlags", &RigidBodyComponent::lockFlags)
            .property("linearDamping", &RigidBodyComponent::linearDamping)
            .property("angularDamping", &RigidBodyComponent::angularDamping)
            .property("maxLinearVelocity", &RigidBodyComponent::maxLinearVelocity)
            .property("maxAngularVelocity", &RigidBodyComponent::maxAngularVelocity)
            .property("solverPositionIterations", &RigidBodyComponent::solverPositionIterations)
            .property("solverVelocityIterations", &RigidBodyComponent::solverVelocityIterations)
            .property("sleepThreshold", &RigidBodyComponent::sleepThreshold)
            .property("stabilizationThreshold", &RigidBodyComponent::stabilizationThreshold);

        // === ColliderComponent 등록 (physicsActorHandle는 내부용이므로 등록하지 않음) ===
        rttr::registration::class_<ColliderComponent>("ColliderComponent")
            .constructor<>()
            .property("type", &ColliderComponent::type)
            .property("halfExtents", &ColliderComponent::halfExtents)
            .property("radius", &ColliderComponent::radius)
            .property("capsuleRadius", &ColliderComponent::capsuleRadius)
            .property("capsuleHalfHeight", &ColliderComponent::capsuleHalfHeight)
            .property("capsuleAlignYAxis", &ColliderComponent::capsuleAlignYAxis)
            .property("staticFriction", &ColliderComponent::staticFriction)
            .property("dynamicFriction", &ColliderComponent::dynamicFriction)
            .property("restitution", &ColliderComponent::restitution)
            .property("layerBits", &ColliderComponent::layerBits)
            .property("collideMask", &ColliderComponent::collideMask)
            .property("queryMask", &ColliderComponent::queryMask)
            .property("isTrigger", &ColliderComponent::isTrigger);

        rttr::registration::class_<IScript>("IScript")
            .constructor<>();
    }
}
