#include "Core/ComponentRegistry.h"
#include "Core/World.h"
#include "Logger.h"

#include <rttr/registration>
#include <DirectXMath.h>

// 물리 컴포넌트 헤더
#include "PhysX/Components/Phy_RigidBodyComponent.h"
#include "PhysX/Components/Phy_ColliderComponent.h"
#include "PhysX/Components/Phy_TerrainHeightFieldComponent.h"
#include "PhysX/Components/Phy_CCTComponent.h"
#include "PhysX/Components/Phy_SettingsComponent.h"
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
            .property("farPlane", &CameraComponent::farPlane)
            .property("useAspectOverride", &CameraComponent::useAspectOverride)
            .property("aspectOverride", &CameraComponent::aspectOverride);

        // === CameraFollowComponent 등록 ===
        rttr::registration::class_<CameraFollowComponent>("CameraFollowComponent")
            .constructor<>()
            .property("enabled", &CameraFollowComponent::enabled)
            .property("targetName", &CameraFollowComponent::targetName)
            .property("heightOffset", &CameraFollowComponent::heightOffset)
            .property("shoulderOffset", &CameraFollowComponent::shoulderOffset)
            .property("shoulderSide", &CameraFollowComponent::shoulderSide)
            .property("enableInput", &CameraFollowComponent::enableInput)
            .property("sensitivity", &CameraFollowComponent::sensitivity)
            .property("yawDeg", &CameraFollowComponent::yawDeg)
            .property("pitchDeg", &CameraFollowComponent::pitchDeg)
            .property("pitchMinDeg", &CameraFollowComponent::pitchMinDeg)
            .property("pitchMaxDeg", &CameraFollowComponent::pitchMaxDeg)
            .property("baseDistance", &CameraFollowComponent::baseDistance)
            .property("minDistance", &CameraFollowComponent::minDistance)
            .property("maxDistance", &CameraFollowComponent::maxDistance)
            .property("positionDamping", &CameraFollowComponent::positionDamping)
            .property("rotationDamping", &CameraFollowComponent::rotationDamping)
            .property("fastTurnYawThresholdDeg", &CameraFollowComponent::fastTurnYawThresholdDeg)
            .property("fastTurnMultiplier", &CameraFollowComponent::fastTurnMultiplier)
            .property("mode", &CameraFollowComponent::mode)
            .property("exploreDistance", &CameraFollowComponent::exploreDistance)
            .property("combatDistance", &CameraFollowComponent::combatDistance)
            .property("lockOnDistance", &CameraFollowComponent::lockOnDistance)
            .property("aimDistance", &CameraFollowComponent::aimDistance)
            .property("bossIntroDistance", &CameraFollowComponent::bossIntroDistance)
            .property("deathDistance", &CameraFollowComponent::deathDistance)
            .property("exploreFovDeg", &CameraFollowComponent::exploreFovDeg)
            .property("combatFovDeg", &CameraFollowComponent::combatFovDeg)
            .property("lockOnFovDeg", &CameraFollowComponent::lockOnFovDeg)
            .property("aimFovDeg", &CameraFollowComponent::aimFovDeg)
            .property("bossIntroFovDeg", &CameraFollowComponent::bossIntroFovDeg)
            .property("deathFovDeg", &CameraFollowComponent::deathFovDeg)
            .property("fovDamping", &CameraFollowComponent::fovDamping)
            .property("enableLockOn", &CameraFollowComponent::enableLockOn)
            .property("lockOnMaxDistance", &CameraFollowComponent::lockOnMaxDistance)
            .property("lockOnMaxAngleDeg", &CameraFollowComponent::lockOnMaxAngleDeg)
            .property("lockOnSwitchAngleDeg", &CameraFollowComponent::lockOnSwitchAngleDeg)
            .property("lockOnAngleWeight", &CameraFollowComponent::lockOnAngleWeight)
            .property("lockOnRotationDamping", &CameraFollowComponent::lockOnRotationDamping)
            .property("allowManualOrbitInLockOn", &CameraFollowComponent::allowManualOrbitInLockOn)
            .property("cameraTimeScale", &CameraFollowComponent::cameraTimeScale);

        // === CameraSpringArmComponent 등록 ===
        rttr::registration::class_<CameraSpringArmComponent>("CameraSpringArmComponent")
            .constructor<>()
            .property("enabled", &CameraSpringArmComponent::enabled)
            .property("enableCollision", &CameraSpringArmComponent::enableCollision)
            .property("enableZoom", &CameraSpringArmComponent::enableZoom)
            .property("distance", &CameraSpringArmComponent::distance)
            .property("minDistance", &CameraSpringArmComponent::minDistance)
            .property("maxDistance", &CameraSpringArmComponent::maxDistance)
            .property("zoomSpeed", &CameraSpringArmComponent::zoomSpeed)
            .property("distanceDamping", &CameraSpringArmComponent::distanceDamping)
            .property("probeRadius", &CameraSpringArmComponent::probeRadius)
            .property("probePadding", &CameraSpringArmComponent::probePadding)
            .property("minHeight", &CameraSpringArmComponent::minHeight);

        // === CameraLookAtComponent 등록 ===
        rttr::registration::class_<CameraLookAtComponent>("CameraLookAtComponent")
            .constructor<>()
            .property("enabled", &CameraLookAtComponent::enabled)
            .property("targetName", &CameraLookAtComponent::targetName)
            .property("rotationDamping", &CameraLookAtComponent::rotationDamping);

        // === CameraShakeComponent 등록 ===
        rttr::registration::class_<CameraShakeComponent>("CameraShakeComponent")
            .constructor<>()
            .property("enabled", &CameraShakeComponent::enabled)
            .property("amplitude", &CameraShakeComponent::amplitude)
            .property("frequency", &CameraShakeComponent::frequency)
            .property("duration", &CameraShakeComponent::duration)
            .property("decay", &CameraShakeComponent::decay);

        // === CameraBlendComponent 등록 ===
        rttr::registration::class_<CameraBlendComponent>("CameraBlendComponent")
            .constructor<>()
            .property("targetName", &CameraBlendComponent::targetName)
            .property("duration", &CameraBlendComponent::duration)
            .property("useSmoothStep", &CameraBlendComponent::useSmoothStep)
            .property("slowTriggerT", &CameraBlendComponent::slowTriggerT)
            .property("slowDuration", &CameraBlendComponent::slowDuration)
            .property("slowTimeScale", &CameraBlendComponent::slowTimeScale);

        // === CameraInputComponent 등록 ===
        rttr::registration::class_<CameraInputComponent>("CameraInputComponent")
            .constructor<>()
            .property("enabled", &CameraInputComponent::enabled)
            .property("cameraListCsv", &CameraInputComponent::cameraListCsv)
            .property("blendTimeKey3", &CameraInputComponent::blendTimeKey3)
            .property("blendTimeKey4", &CameraInputComponent::blendTimeKey4)
            .property("blendTimeKey5", &CameraInputComponent::blendTimeKey5)
            .property("shakeAmplitudeKey4", &CameraInputComponent::shakeAmplitudeKey4)
            .property("shakeFrequencyKey4", &CameraInputComponent::shakeFrequencyKey4)
            .property("shakeDurationKey4", &CameraInputComponent::shakeDurationKey4)
            .property("shakeDecayKey4", &CameraInputComponent::shakeDecayKey4)
            .property("slowTriggerTKey5", &CameraInputComponent::slowTriggerTKey5)
            .property("slowDurationKey5", &CameraInputComponent::slowDurationKey5)
            .property("slowTimeScaleKey5", &CameraInputComponent::slowTimeScaleKey5)
            .property("lookAtTargetName", &CameraInputComponent::lookAtTargetName);

        // === PointLightComponent 등록 ===
        rttr::registration::class_<PointLightComponent>("PointLightComponent")
            .constructor<>()
            .property("color", &PointLightComponent::color)
            .property("intensity", &PointLightComponent::intensity)
            .property("range", &PointLightComponent::range)
            .property("enabled", &PointLightComponent::enabled);

        // === SpotLightComponent 등록 ===
        rttr::registration::class_<SpotLightComponent>("SpotLightComponent")
            .constructor<>()
            .property("color", &SpotLightComponent::color)
            .property("intensity", &SpotLightComponent::intensity)
            .property("range", &SpotLightComponent::range)
            .property("innerAngleDeg", &SpotLightComponent::innerAngleDeg)
            .property("outerAngleDeg", &SpotLightComponent::outerAngleDeg)
            .property("enabled", &SpotLightComponent::enabled);

        // === RectLightComponent 등록 ===
        rttr::registration::class_<RectLightComponent>("RectLightComponent")
            .constructor<>()
            .property("color", &RectLightComponent::color)
            .property("intensity", &RectLightComponent::intensity)
            .property("width", &RectLightComponent::width)
            .property("height", &RectLightComponent::height)
            .property("range", &RectLightComponent::range)
            .property("enabled", &RectLightComponent::enabled);

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

        // === Phy_RigidBodyComponent 등록 (physicsActorHandle는 내부용이므로 등록하지 않음) ===
        rttr::registration::class_<Phy_RigidBodyComponent>("Phy_RigidBodyComponent")
            .constructor<>()
            .property("density", &Phy_RigidBodyComponent::density)
            .property("massOverride", &Phy_RigidBodyComponent::massOverride)
            .property("isKinematic", &Phy_RigidBodyComponent::isKinematic)
            .property("gravityEnabled", &Phy_RigidBodyComponent::gravityEnabled)
            .property("startAwake", &Phy_RigidBodyComponent::startAwake)
            .property("enableCCD", &Phy_RigidBodyComponent::enableCCD)
            .property("enableSpeculativeCCD", &Phy_RigidBodyComponent::enableSpeculativeCCD)
            .property("lockFlags", &Phy_RigidBodyComponent::lockFlags)
            .property("linearDamping", &Phy_RigidBodyComponent::linearDamping)
            .property("angularDamping", &Phy_RigidBodyComponent::angularDamping)
            .property("maxLinearVelocity", &Phy_RigidBodyComponent::maxLinearVelocity)
            .property("maxAngularVelocity", &Phy_RigidBodyComponent::maxAngularVelocity)
            .property("solverPositionIterations", &Phy_RigidBodyComponent::solverPositionIterations)
            .property("solverVelocityIterations", &Phy_RigidBodyComponent::solverVelocityIterations)
            .property("sleepThreshold", &Phy_RigidBodyComponent::sleepThreshold)
            .property("stabilizationThreshold", &Phy_RigidBodyComponent::stabilizationThreshold)
            .property("teleport", &Phy_RigidBodyComponent::teleport)
            .property("resetVelocityOnTeleport", &Phy_RigidBodyComponent::resetVelocityOnTeleport);

        // === Phy_ColliderComponent 등록 (physicsActorHandle는 내부용이므로 등록하지 않음) ===
        rttr::registration::class_<Phy_ColliderComponent>("Phy_ColliderComponent")
            .constructor<>()
            .property("type", &Phy_ColliderComponent::type)
            .property("halfExtents", &Phy_ColliderComponent::halfExtents)
            .property("radius", &Phy_ColliderComponent::radius)
            .property("capsuleRadius", &Phy_ColliderComponent::capsuleRadius)
            .property("capsuleHalfHeight", &Phy_ColliderComponent::capsuleHalfHeight)
            .property("capsuleAlignYAxis", &Phy_ColliderComponent::capsuleAlignYAxis)
            .property("staticFriction", &Phy_ColliderComponent::staticFriction)
            .property("dynamicFriction", &Phy_ColliderComponent::dynamicFriction)
            .property("restitution", &Phy_ColliderComponent::restitution)
            .property("layerBits", &Phy_ColliderComponent::layerBits)
            .property("collideMask", &Phy_ColliderComponent::collideMask)
            .property("queryMask", &Phy_ColliderComponent::queryMask)
            .property("ignoreLayers", &Phy_ColliderComponent::ignoreLayers)
            .property("isTrigger", &Phy_ColliderComponent::isTrigger);

        // === Phy_TerrainHeightFieldComponent 등록 (physicsActorHandle는 내부용이므로 등록하지 않음) ===
        rttr::registration::class_<Phy_TerrainHeightFieldComponent>("Phy_TerrainHeightFieldComponent")
            .constructor<>()
            .property("numRows", &Phy_TerrainHeightFieldComponent::numRows)
            .property("numCols", &Phy_TerrainHeightFieldComponent::numCols)
            .property("heightSamples", &Phy_TerrainHeightFieldComponent::heightSamples)
            .property("rowScale", &Phy_TerrainHeightFieldComponent::rowScale)
            .property("colScale", &Phy_TerrainHeightFieldComponent::colScale)
            .property("heightScale", &Phy_TerrainHeightFieldComponent::heightScale)
            .property("centerPivot", &Phy_TerrainHeightFieldComponent::centerPivot)
            .property("doubleSidedQueries", &Phy_TerrainHeightFieldComponent::doubleSidedQueries)
            .property("staticFriction", &Phy_TerrainHeightFieldComponent::staticFriction)
            .property("dynamicFriction", &Phy_TerrainHeightFieldComponent::dynamicFriction)
            .property("restitution", &Phy_TerrainHeightFieldComponent::restitution)
            .property("layerBits", &Phy_TerrainHeightFieldComponent::layerBits)
            .property("collideMask", &Phy_TerrainHeightFieldComponent::collideMask)
            .property("queryMask", &Phy_TerrainHeightFieldComponent::queryMask)
            .property("ignoreLayers", &Phy_TerrainHeightFieldComponent::ignoreLayers);

        // === CCTNonWalkableMode enum 등록 ===
        rttr::registration::enumeration<CCTNonWalkableMode>("CCTNonWalkableMode")
            (
                rttr::value("PreventClimbing", CCTNonWalkableMode::PreventClimbing),
                rttr::value("PreventClimbingAndForceSliding", CCTNonWalkableMode::PreventClimbingAndForceSliding)
            );

        // === CCTCapsuleClimbingMode enum 등록 ===
        rttr::registration::enumeration<CCTCapsuleClimbingMode>("CCTCapsuleClimbingMode")
            (
                rttr::value("Easy", CCTCapsuleClimbingMode::Easy),
                rttr::value("Constrained", CCTCapsuleClimbingMode::Constrained)
            );

        // === Phy_CCTComponent 등록 (내부 핸들과 출력 값들은 제외) ===
        rttr::registration::class_<Phy_CCTComponent>("Phy_CCTComponent")
            .constructor<>()
            .property("radius", &Phy_CCTComponent::radius)
            .property("halfHeight", &Phy_CCTComponent::halfHeight)
            .property("stepOffset", &Phy_CCTComponent::stepOffset)
            .property("contactOffset", &Phy_CCTComponent::contactOffset)
            .property("slopeLimitRadians", &Phy_CCTComponent::slopeLimitRadians)
            .property("nonWalkableMode", &Phy_CCTComponent::nonWalkableMode)
            .property("climbingMode", &Phy_CCTComponent::climbingMode)
            .property("density", &Phy_CCTComponent::density)
            .property("enableQueries", &Phy_CCTComponent::enableQueries)
            .property("layerBits", &Phy_CCTComponent::layerBits)
            .property("collideMask", &Phy_CCTComponent::collideMask)
            .property("queryMask", &Phy_CCTComponent::queryMask)
            .property("ignoreLayers", &Phy_CCTComponent::ignoreLayers)
            .property("hitTriggers", &Phy_CCTComponent::hitTriggers)
            .property("desiredVelocity", &Phy_CCTComponent::desiredVelocity)
            .property("applyGravity", &Phy_CCTComponent::applyGravity)
            .property("gravity", &Phy_CCTComponent::gravity)
            .property("verticalVelocity", &Phy_CCTComponent::verticalVelocity)
            .property("jumpRequested", &Phy_CCTComponent::jumpRequested)
            .property("jumpSpeed", &Phy_CCTComponent::jumpSpeed)
            .property("teleport", &Phy_CCTComponent::teleport);

        // === Phy_SettingsComponent 등록 ===
        rttr::registration::class_<Phy_SettingsComponent>("Phy_SettingsComponent")
            .constructor<>()
            .property("enablePhysics", &Phy_SettingsComponent::enablePhysics)
            .property("gravity", &Phy_SettingsComponent::gravity)
            .property("fixedDt", &Phy_SettingsComponent::fixedDt)
            .property("maxSubsteps", &Phy_SettingsComponent::maxSubsteps)
            .property("layerCollideMatrix", &Phy_SettingsComponent::layerCollideMatrix)
            .property("layerQueryMatrix", &Phy_SettingsComponent::layerQueryMatrix)
            .property("layerNames", &Phy_SettingsComponent::layerNames)
            .property("filterRevision", &Phy_SettingsComponent::filterRevision);

        rttr::registration::class_<IScript>("IScript")
            .constructor<>();
    }
}
