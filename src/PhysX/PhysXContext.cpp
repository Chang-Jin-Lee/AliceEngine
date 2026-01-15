// PhysXContext.cpp
#include "PhysXContext.h"

#include <PhysX/PxPhysicsAPI.h>

#if PHYSXWRAP_ENABLE_COOKING && PHYSXWRAP_HAS_COOKING_HEADERS
#  if PHYSXWRAP_COOKING_INCLUDE_STYLE == 1
#    include <cooking/PxCooking.h>
#  elif PHYSXWRAP_COOKING_INCLUDE_STYLE == 2
#    include <physx/cooking/PxCooking.h>
#  endif
#endif

#include <stdexcept>

using namespace physx;

struct PhysXContext::Impl
{
	~Impl()
	{
		if (dispatcher) dispatcher->release();

		if (physics)
		{
			if (extensionsInited) PxCloseExtensions();
			physics->release();
		}

		if (pvd) pvd->release();

		if (pvdTransport)
		{
			// PxPvd::release() does NOT release transport.
			pvdTransport->release();
			pvdTransport = nullptr;
		}

		if (foundation) foundation->release();
	}

	PxDefaultAllocator allocator;
	PxDefaultErrorCallback errorCb;

	PxFoundation* foundation = nullptr;
	PxPhysics* physics = nullptr;
	PxPvd* pvd = nullptr;
	PxPvdTransport* pvdTransport = nullptr;

#if PHYSXWRAP_ENABLE_COOKING && PHYSXWRAP_HAS_COOKING_HEADERS
	PxCookingParams cookingParams{ PxTolerancesScale{} };
	bool cookingEnabled = false;
#endif

	PxDefaultCpuDispatcher* dispatcher = nullptr;

	bool extensionsInited = false;
	PxTolerancesScale scale{};
};

static PxPvd* CreatePvd(PxFoundation& foundation, PxPvdTransport*& outTransport,
	const char* host, int port, uint32_t timeoutMs)
{
	outTransport = PxDefaultPvdSocketTransportCreate(host, port, timeoutMs);
	if (!outTransport)
		throw std::runtime_error("PxDefaultPvdSocketTransportCreate failed");

	PxPvd* pvd = PxCreatePvd(foundation);
	if (!pvd)
		throw std::runtime_error("PxCreatePvd failed");

	if (!pvd->connect(*outTransport, PxPvdInstrumentationFlag::eALL))
		throw std::runtime_error("PxPvd::connect failed");

	return pvd;
}

PhysXContext::PhysXContext()
	: PhysXContext(PhysXContextDesc{})
{
}

PhysXContext::PhysXContext(const PhysXContextDesc& desc)
	: impl(std::make_unique<Impl>())
{
	impl->foundation = PxCreateFoundation(PX_PHYSICS_VERSION, impl->allocator, impl->errorCb);
	if (!impl->foundation)
		throw std::runtime_error("PxCreateFoundation failed");

	if (desc.enablePvd)
		impl->pvd = CreatePvd(*impl->foundation, impl->pvdTransport, desc.pvdHost, desc.pvdPort, desc.pvdTimeoutMs);

	impl->physics = PxCreatePhysics(PX_PHYSICS_VERSION, *impl->foundation, impl->scale, true, impl->pvd);
	if (!impl->physics)
		throw std::runtime_error("PxCreatePhysics failed");

	if (!PxInitExtensions(*impl->physics, impl->pvd))
		throw std::runtime_error("PxInitExtensions failed");
	impl->extensionsInited = true;

#if PHYSXWRAP_ENABLE_COOKING && PHYSXWRAP_HAS_COOKING_HEADERS
	if (desc.enableCooking)
	{
		impl->cookingParams = PxCookingParams(impl->physics->getTolerancesScale());

		// Keep mesh cleaning enabled by default for robustness.
		if (desc.weldVertices)
			impl->cookingParams.meshPreprocessParams |= PxMeshPreprocessingFlag::eWELD_VERTICES;
		impl->cookingParams.meshWeldTolerance = desc.meshWeldTolerance;

		// Saves memory if you don't need the remap table (common in engines).
		impl->cookingParams.suppressTriangleMeshRemapTable = desc.suppressTriangleMeshRemapTable;

		// Optional extras.
		impl->cookingParams.buildTriangleAdjacencies = desc.buildTriangleAdjacencies;
		impl->cookingParams.buildGPUData = desc.buildGPUData;

		impl->cookingEnabled = true;
	}
#endif

	impl->dispatcher = PxDefaultCpuDispatcherCreate(desc.dispatcherThreads);
	if (!impl->dispatcher)
		throw std::runtime_error("PxDefaultCpuDispatcherCreate failed");
}

PhysXContext::~PhysXContext() = default;

PxPhysics* PhysXContext::GetPhysics() const noexcept { return impl ? impl->physics : nullptr; }
PxFoundation* PhysXContext::GetFoundation() const noexcept { return impl ? impl->foundation : nullptr; }
PxCpuDispatcher* PhysXContext::GetDispatcher() const noexcept { return impl ? impl->dispatcher : nullptr; }
PxPvd* PhysXContext::GetPvd() const noexcept { return impl ? impl->pvd : nullptr; }

const PxCookingParams* PhysXContext::GetCookingParams() const noexcept
{
#if PHYSXWRAP_ENABLE_COOKING && PHYSXWRAP_HAS_COOKING_HEADERS
	return (impl && impl->cookingEnabled) ? &impl->cookingParams : nullptr;
#else
	return nullptr;
#endif
}

bool PhysXContext::IsCookingAvailable() const noexcept
{
#if PHYSXWRAP_ENABLE_COOKING && PHYSXWRAP_HAS_COOKING_HEADERS
	return impl && impl->cookingEnabled;
#else
	return false;
#endif
}
