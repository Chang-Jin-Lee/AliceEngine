// PhysXWorld.h
#pragma once

#include "IPhysicsWorld.h"
#include "PhysXContext.h"

#include <memory>

class PhysXWorld final : public IPhysicsWorld
{
public:
	struct Desc
	{
		Vec3 gravity = { 0.0f, -9.81f, 0.0f };

		// If true, query/body methods will use PxScene::lockRead/lockWrite.
		// Recommended if you do scene queries from worker threads.
		bool enableSceneLocks = true;

		// If true, the backend will collect "moved body" poses per Step() using
		// PxSimulationEventCallback::onAdvance (requires eENABLE_POSE_INTEGRATION_PREVIEW).
		// You can retrieve them via IPhysicsWorld::DrainActiveTransforms().
		bool enableActiveTransforms = true;


		// Contact/trigger events
		bool enableContactEvents = true;   // begin/end
		bool enableContactPoints = false;  // store one contact point (extra cost)
		bool enableContactModify = false;  // enables PxContactModifyCallback + eMODIFY_CONTACTS (extra cost)
		bool enableCCD = false; // scene-wide CCD. must be set at scene creation

	};

	struct Impl;

	explicit PhysXWorld(PhysXContext& ctx, const Desc& desc = Desc{});
	~PhysXWorld();

	PhysXWorld(const PhysXWorld&) = delete;
	PhysXWorld& operator=(const PhysXWorld&) = delete;

	// IPhysicsWorld
	void Flush() override;
	void Step(float fixedDt) override;

	void SetGravity(const Vec3& g) override;
	Vec3 GetGravity() const override;

	std::unique_ptr<IRigidBody> CreateDynamicBox(
		const Vec3& pos,
		const Quat& rot,
		const RigidBodyDesc& rb,
		const BoxColliderDesc& box) override;

	std::unique_ptr<IRigidBody> CreateDynamicSphere(
		const Vec3& pos,
		const Quat& rot,
		const RigidBodyDesc& rb,
		const SphereColliderDesc& sphere) override;

	std::unique_ptr<IRigidBody> CreateDynamicCapsule(
		const Vec3& pos,
		const Quat& rot,
		const RigidBodyDesc& rb,
		const CapsuleColliderDesc& capsule) override;

	std::unique_ptr<IPhysicsActor> CreateStaticEmpty(
		const Vec3&,
		const Quat&,
		void*) override;

	std::unique_ptr<IRigidBody> CreateDynamicEmpty(
		const Vec3& pos,
		const Quat& rot,
		const RigidBodyDesc& rb) override;

	void CreateStaticPlane(
		float staticFriction,
		float dynamicFriction,
		float restitution,
		const FilterDesc& filter) override;

	std::unique_ptr<IPhysicsActor> CreateStaticPlaneActor(
		float staticFriction,
		float dynamicFriction,
		float restitution,
		const FilterDesc& filter) override;

	std::unique_ptr<IPhysicsActor> CreateStaticBox(
		const Vec3& pos,
		const Quat& rot,
		const BoxColliderDesc& box) override;

	std::unique_ptr<IPhysicsActor> CreateStaticSphere(
		const Vec3& pos,
		const Quat& rot,
		const SphereColliderDesc& sphere) override;

	std::unique_ptr<IPhysicsActor> CreateStaticCapsule(
		const Vec3& pos,
		const Quat& rot,
		const CapsuleColliderDesc& capsule) override;


	std::unique_ptr<IPhysicsActor> CreateStaticTriangleMesh(
		const Vec3& pos,
		const Quat& rot,
		const TriangleMeshColliderDesc& mesh) override;

	std::unique_ptr<IPhysicsActor> CreateStaticConvexMesh(
		const Vec3& pos,
		const Quat& rot,
		const ConvexMeshColliderDesc& mesh) override;

	std::unique_ptr<IRigidBody> CreateDynamicConvexMesh(
		const Vec3& pos,
		const Quat& rot,
		const RigidBodyDesc& rb,
		const ConvexMeshColliderDesc& mesh) override;

	bool SupportsCharacterControllers() const override;
	std::unique_ptr<ICharacterController> CreateCharacterController(
		const CharacterControllerDesc& desc) override;

	std::unique_ptr<IPhysicsJoint> CreateFixedJoint(
		const IPhysicsActor& a,
		const IPhysicsActor& b,
		const FixedJointDesc& desc) override;

	std::unique_ptr<IPhysicsJoint> CreateRevoluteJoint(
		const IPhysicsActor& a,
		const IPhysicsActor& b,
		const RevoluteJointDesc& desc) override;

	std::unique_ptr<IPhysicsJoint> CreatePrismaticJoint(
		const IPhysicsActor& a,
		const IPhysicsActor& b,
		const PrismaticJointDesc& desc) override;

	std::unique_ptr<IPhysicsJoint> CreateDistanceJoint(
		const IPhysicsActor& a,
		const IPhysicsActor& b,
		const DistanceJointDesc& desc) override;

	std::unique_ptr<IPhysicsJoint> CreateSphericalJoint(
		const IPhysicsActor& a,
		const IPhysicsActor& b,
		const SphericalJointDesc& desc) override;

	std::unique_ptr<IPhysicsJoint> CreateD6Joint(
		const IPhysicsActor& a,
		const IPhysicsActor& b,
		const D6JointDesc& desc) override;

	bool SupportsMeshCooking() const override;
	void ClearMeshCaches() override;

	bool Raycast(
		const Vec3& origin,
		const Vec3& dir,
		float maxDist,
		RaycastHit& outHit,
		uint32_t layerMask = 0xFFFFFFFFu,
		bool hitTriggers = false) const override;

	bool RaycastEx(
		const Vec3& origin,
		const Vec3& dir,
		float maxDist,
		RaycastHit& outHit,
		uint32_t layerMask,
		uint32_t queryMask,
		bool hitTriggers = false) const override;

	uint32_t RaycastAll(
		const Vec3& origin,
		const Vec3& dir,
		float maxDist,
		std::vector<RaycastHit>& outHits,
		uint32_t layerMask = 0xFFFFFFFFu,
		uint32_t queryMask = 0xFFFFFFFFu,
		bool hitTriggers = false,
		uint32_t maxHits = 64) const override;

	uint32_t OverlapBox(
		const Vec3& center,
		const Quat& rot,
		const Vec3& halfExtents,
		std::vector<OverlapHit>& outHits,
		uint32_t layerMask = 0xFFFFFFFFu,
		uint32_t queryMask = 0xFFFFFFFFu,
		bool hitTriggers = false,
		uint32_t maxHits = 64) const override;

	uint32_t OverlapSphere(
		const Vec3& center,
		float radius,
		std::vector<OverlapHit>& outHits,
		uint32_t layerMask = 0xFFFFFFFFu,
		uint32_t queryMask = 0xFFFFFFFFu,
		bool hitTriggers = false,
		uint32_t maxHits = 64) const override;

	uint32_t OverlapCapsule(
		const Vec3& center,
		const Quat& rot,
		float radius,
		float halfHeight,
		std::vector<OverlapHit>& outHits,
		uint32_t layerMask = 0xFFFFFFFFu,
		uint32_t queryMask = 0xFFFFFFFFu,
		bool hitTriggers = false,
		uint32_t maxHits = 64,
		bool alignYAxis = true) const override;

	bool SweepBox(
		const Vec3& origin,
		const Quat& rot,
		const Vec3& halfExtents,
		const Vec3& dir,
		float maxDist,
		SweepHit& outHit,
		uint32_t layerMask = 0xFFFFFFFFu,
		uint32_t queryMask = 0xFFFFFFFFu,
		bool hitTriggers = false) const override;

	bool SweepSphere(
		const Vec3& origin,
		float radius,
		const Vec3& dir,
		float maxDist,
		SweepHit& outHit,
		uint32_t layerMask = 0xFFFFFFFFu,
		uint32_t queryMask = 0xFFFFFFFFu,
		bool hitTriggers = false) const override;

	bool SweepCapsule(
		const Vec3& origin,
		const Quat& rot,
		float radius,
		float halfHeight,
		const Vec3& dir,
		float maxDist,
		SweepHit& outHit,
		uint32_t layerMask = 0xFFFFFFFFu,
		uint32_t queryMask = 0xFFFFFFFFu,
		bool hitTriggers = false,
		bool alignYAxis = true) const override;

	void DrainActiveTransforms(std::vector<ActiveTransform>& outTransforms) override;

	void DrainEvents(std::vector<PhysicsEvent>& outEvents) override;
	void SetContactModifyCallback(ContactModifyCallback cb, void* userContext = nullptr) override;

private:
	PhysXContext& ctx;
	std::shared_ptr<Impl> impl;
};
