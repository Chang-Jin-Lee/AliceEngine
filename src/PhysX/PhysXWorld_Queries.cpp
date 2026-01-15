// PhysXWorld_Queries.cpp
#include "PhysXWorld_Internal.h"

// ============================================================
//  Queries
// ============================================================

static inline void FillRaycastHit(const PxRaycastHit& h, RaycastHit& out)
{
	out.position = FromPx(h.position);
	out.normal = FromPx(h.normal);
	out.distance = h.distance;
	out.faceIndex = HasHitFlag(h.flags, PxHitFlag::eFACE_INDEX) ? h.faceIndex : 0xFFFFFFFFu;
	if (HasHitFlag(h.flags, PxHitFlag::eUV))
	{
		out.baryUV = Vec2(h.u, h.v);
		const float w = 1.0f - h.u - h.v;
		out.barycentric = Vec3(w, h.u, h.v);
	}
	else
	{
		out.baryUV = Vec2::Zero;
		out.barycentric = Vec3::Zero;
	}
	out.actorHandle = h.actor ? static_cast<uint64_t>(reinterpret_cast<uintptr_t>(h.actor)) : 0ull;
	out.shapeHandle = h.shape ? static_cast<uint64_t>(reinterpret_cast<uintptr_t>(h.shape)) : 0ull;
	out.userData = h.actor ? h.actor->userData : nullptr;
	out.nativeActor = h.actor;
	out.nativeShape = h.shape;
}

bool PhysXWorld::Raycast(const Vec3& origin, const Vec3& dir, float maxDist, RaycastHit& outHit, uint32_t layerMask, bool hitTriggers) const
{
	return RaycastEx(origin, dir, maxDist, outHit, layerMask, 0xFFFFFFFFu, hitTriggers);
}

bool PhysXWorld::RaycastEx(const Vec3& origin, const Vec3& dir, float maxDist, RaycastHit& outHit, uint32_t layerMask, uint32_t queryMask, bool hitTriggers) const
{
	if (!impl || !impl->scene) return false;

	const Vec3 nd = dir; // assume normalized by caller; PhysX will normalize internally for rays.

	PxRaycastBuffer buf;
	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(layerMask, queryMask, hitTriggers, QueryHitMode::Block);

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const PxHitFlags hitFlags = PxHitFlag::ePOSITION | PxHitFlag::eNORMAL | PxHitFlag::eFACE_INDEX | PxHitFlag::eUV;
	const bool hit = impl->scene->raycast(ToPx(origin), ToPx(nd), maxDist, buf, hitFlags, qfd, &cb);
	if (!hit || !buf.hasBlock) return false;

	FillRaycastHit(buf.block, outHit);
	return true;
}

uint32_t PhysXWorld::RaycastAll(const Vec3& origin, const Vec3& dir, float maxDist, std::vector<RaycastHit>& outHits, uint32_t layerMask, uint32_t queryMask, bool hitTriggers, uint32_t maxHits) const
{
	outHits.clear();
	if (!impl || !impl->scene || maxHits == 0) return 0;

	std::vector<PxRaycastHit> hits(maxHits);
	PxRaycastBuffer buf(hits.data(), static_cast<PxU32>(hits.size()));

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(layerMask, queryMask, hitTriggers, QueryHitMode::Touch);

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const PxHitFlags hitFlags = PxHitFlag::ePOSITION | PxHitFlag::eNORMAL | PxHitFlag::eFACE_INDEX | PxHitFlag::eUV;
	const bool ok = impl->scene->raycast(ToPx(origin), ToPx(dir), maxDist, buf,
		hitFlags,
		qfd, &cb);

	if (!ok) return 0;

	const PxU32 n = buf.getNbTouches();
	outHits.reserve(n);
	for (PxU32 i = 0; i < n; ++i)
	{
		RaycastHit rh;
		FillRaycastHit(buf.getTouch(i), rh);
		outHits.push_back(rh);
	}

	return static_cast<uint32_t>(outHits.size());
}

static inline void FillOverlapHit(const PxOverlapHit& h, OverlapHit& out)
{
	out.userData = h.actor ? h.actor->userData : nullptr;
	out.nativeActor = h.actor;
	out.nativeShape = h.shape;
}

uint32_t PhysXWorld::OverlapBox(const Vec3& center, const Quat& rot, const Vec3& halfExtents, std::vector<OverlapHit>& outHits, uint32_t layerMask, uint32_t queryMask, bool hitTriggers, uint32_t maxHits) const
{
	outHits.clear();
	if (!impl || !impl->scene || maxHits == 0) return 0;

	std::vector<PxOverlapHit> hits(maxHits);
	PxOverlapBuffer buf(hits.data(), static_cast<PxU32>(hits.size()));

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(layerMask, queryMask, hitTriggers, QueryHitMode::Touch);

	const PxBoxGeometry geom(ToPx(halfExtents));
	const PxTransform pose = ToPxTransform(center, rot);

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->overlap(geom, pose, buf, qfd, &cb);
	if (!ok) return 0;

	const PxU32 n = buf.getNbTouches();
	outHits.reserve(n);
	for (PxU32 i = 0; i < n; ++i)
	{
		OverlapHit oh;
		FillOverlapHit(buf.getTouch(i), oh);
		outHits.push_back(oh);
	}

	return static_cast<uint32_t>(outHits.size());
}

uint32_t PhysXWorld::OverlapSphere(const Vec3& center, float radius, std::vector<OverlapHit>& outHits, uint32_t layerMask, uint32_t queryMask, bool hitTriggers, uint32_t maxHits) const
{
	outHits.clear();
	if (!impl || !impl->scene || maxHits == 0) return 0;

	std::vector<PxOverlapHit> hits(maxHits);
	PxOverlapBuffer buf(hits.data(), static_cast<PxU32>(hits.size()));

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(layerMask, queryMask, hitTriggers, QueryHitMode::Touch);

	const PxSphereGeometry geom(radius);
	const PxTransform pose(ToPx(center));

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->overlap(geom, pose, buf, qfd, &cb);
	if (!ok) return 0;

	const PxU32 n = buf.getNbTouches();
	outHits.reserve(n);
	for (PxU32 i = 0; i < n; ++i)
	{
		OverlapHit oh;
		FillOverlapHit(buf.getTouch(i), oh);
		outHits.push_back(oh);
	}

	return static_cast<uint32_t>(outHits.size());
}

uint32_t PhysXWorld::OverlapCapsule(const Vec3& center, const Quat& rot, float radius, float halfHeight, std::vector<OverlapHit>& outHits, uint32_t layerMask, uint32_t queryMask, bool hitTriggers, uint32_t maxHits, bool alignYAxis) const
{
	outHits.clear();
	if (!impl || !impl->scene || maxHits == 0) return 0;

	std::vector<PxOverlapHit> hits(maxHits);
	PxOverlapBuffer buf(hits.data(), static_cast<PxU32>(hits.size()));

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(layerMask, queryMask, hitTriggers, QueryHitMode::Touch);

	const PxCapsuleGeometry geom(radius, halfHeight);

	Quat q = rot;
	if (alignYAxis)
	{
		Quat align = FromPx(CapsuleAlignQuatPx());
		q = q * align; // world rot * align (shape local)
	}

	const PxTransform pose = ToPxTransform(center, q);

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->overlap(geom, pose, buf, qfd, &cb);
	if (!ok) return 0;

	const PxU32 n = buf.getNbTouches();
	outHits.reserve(n);
	for (PxU32 i = 0; i < n; ++i)
	{
		OverlapHit oh;
		FillOverlapHit(buf.getTouch(i), oh);
		outHits.push_back(oh);
	}

	return static_cast<uint32_t>(outHits.size());
}

static inline void FillSweepHit(const PxSweepHit& h, SweepHit& out)
{
	out.position = FromPx(h.position);
	out.normal = FromPx(h.normal);
	out.distance = h.distance;
	out.userData = h.actor ? h.actor->userData : nullptr;
	out.nativeActor = h.actor;
	out.nativeShape = h.shape;
}

bool PhysXWorld::SweepBox(const Vec3& origin, const Quat& rot, const Vec3& halfExtents, const Vec3& dir, float maxDist, SweepHit& outHit, uint32_t layerMask, uint32_t queryMask, bool hitTriggers) const
{
	if (!impl || !impl->scene) return false;

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(layerMask, queryMask, hitTriggers, QueryHitMode::Block);

	const PxBoxGeometry geom(ToPx(halfExtents));
	const PxTransform pose = ToPxTransform(origin, rot);

	PxSweepBuffer buf;

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->sweep(geom, pose, ToPx(dir), maxDist, buf,
		PxHitFlag::ePOSITION | PxHitFlag::eNORMAL,
		qfd, &cb);

	if (!ok || !buf.hasBlock) return false;

	FillSweepHit(buf.block, outHit);
	return true;
}

bool PhysXWorld::SweepSphere(const Vec3& origin, float radius, const Vec3& dir, float maxDist, SweepHit& outHit, uint32_t layerMask, uint32_t queryMask, bool hitTriggers) const
{
	if (!impl || !impl->scene) return false;

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(layerMask, queryMask, hitTriggers, QueryHitMode::Block);

	const PxSphereGeometry geom(radius);
	const PxTransform pose(ToPx(origin));

	PxSweepBuffer buf;

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->sweep(geom, pose, ToPx(dir), maxDist, buf,
		PxHitFlag::ePOSITION | PxHitFlag::eNORMAL,
		qfd, &cb);

	if (!ok || !buf.hasBlock) return false;

	FillSweepHit(buf.block, outHit);
	return true;
}

bool PhysXWorld::SweepCapsule(const Vec3& origin, const Quat& rot, float radius, float halfHeight, const Vec3& dir, float maxDist, SweepHit& outHit, uint32_t layerMask, uint32_t queryMask, bool hitTriggers, bool alignYAxis) const
{
	if (!impl || !impl->scene) return false;

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(layerMask, queryMask, hitTriggers, QueryHitMode::Block);

	const PxCapsuleGeometry geom(radius, halfHeight);

	Quat q = rot;
	if (alignYAxis)
	{
		Quat align = FromPx(CapsuleAlignQuatPx());
		q = q * align;
	}

	const PxTransform pose = ToPxTransform(origin, q);

	PxSweepBuffer buf;

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->sweep(geom, pose, ToPx(dir), maxDist, buf,
		PxHitFlag::ePOSITION | PxHitFlag::eNORMAL,
		qfd, &cb);

	if (!ok || !buf.hasBlock) return false;

	FillSweepHit(buf.block, outHit);
	return true;
}

