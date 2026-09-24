#include "lpch.h"
#include "Lux/Physics/JoltPhysics/JoltContactListener.h"
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <vector>
#include <cassert>
#include <iostream>
using namespace JPH;
namespace Layers
{
	constexpr ObjectLayer NON_MOVING = 0, MOVING = 1;
}
class BPLayerInterfaceImpl : public BroadPhaseLayerInterface
{
  public:
	uint GetNumBroadPhaseLayers() const override { return 2; }
	BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer layer) const override
	{
		return BroadPhaseLayer(static_cast<uint8>(layer));
	}
	const char* GetBroadPhaseLayerName(BroadPhaseLayer) const override { return "test"; }
};
class ObjectVsBroadPhaseLayerFilterImpl : public ObjectVsBroadPhaseLayerFilter
{
  public:
	bool ShouldCollide(ObjectLayer object, BroadPhaseLayer broad) const override
	{
		return object == 1 || broad == BroadPhaseLayer(1);
	}
};
class ObjectLayerPairFilterImpl : public ObjectLayerPairFilter
{
  public:
	bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override { return a == 1 || b == 1; }
};
int main()
{
	RegisterDefaultAllocator();
	Factory factory;
	Factory::sInstance = &factory;
	RegisterTypes();
	{
		BPLayerInterfaceImpl layers;
		ObjectVsBroadPhaseLayerFilterImpl broad;
		ObjectLayerPairFilterImpl pair;
		Lux::JoltContactListener listener;
		PhysicsSystem system;
		system.SetContactListener(&listener);
		system.Init(1024, 0, 2048, 2048, layers, broad, pair);
		TempAllocatorImpl temp(10 * 1024 * 1024);
		JobSystemThreadPool jobs(cMaxPhysicsJobs, cMaxPhysicsBarriers, 2);
		auto box = BoxShapeSettings(Vec3(5, 0.5f, 5)).Create().Get();
		BodyCreationSettings floor(box, RVec3(0, -0.5f, 0), Quat::sIdentity(), EMotionType::Static, Layers::NON_MOVING);
		floor.mUserData = 100;
		auto& bi = system.GetBodyInterface();
		auto ground = bi.CreateAndAddBody(floor, EActivation::DontActivate);
		auto sphere = SphereShapeSettings(0.5f).Create().Get();
		BodyCreationSettings ball(sphere, RVec3(0, 1, 0), Quat::sIdentity(), EMotionType::Dynamic, Layers::MOVING);
		ball.mUserData = 200;
		ball.mLinearVelocity = Vec3(1, -4, 0);
		ball.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
		ball.mMassPropertiesOverride.mMass = 2;
		ball.mRestitution = 0;
		auto moving = bi.CreateAndAddBody(ball, EActivation::Activate);
		std::vector<Lux::PhysicsContactEvent> events;
		int real = 0, removed = 0;
		float impulse = 0;
		for (int i = 0; i < 180; i++)
		{
			system.Update(1.0f / 60, 1, &temp, &jobs);
			listener.Drain(events);
			for (auto& e : events)
			{
				if (e.State == Lux::PhysicsContactEvent::Type::End)
				{
					removed++;
					continue;
				}
				assert(e.Entity1 == 100 && e.Entity2 == 200);
				assert(e.Mass1 == 0 && std::abs(e.Mass2 - 2) < 0.001f);
				assert(std::isfinite(e.SlipSpeed) && std::isfinite(e.RollSpeed));
				real++;
				impulse = std::max(impulse, e.EstimatedImpulse);
			}
		}
		assert(real > 0 && impulse > 5);
		// Converting a touching body into a sensor must end its audible contact.
		bi.SetPosition(moving, RVec3(0, 0.49f, 0), EActivation::Activate);
		bi.SetLinearVelocity(moving, Vec3(1, 0, 0));
		system.Update(1.0f / 60, 1, &temp, &jobs);
		listener.Drain(events);
		assert(std::any_of(events.begin(), events.end(), [](const auto& event)
		{
			return event.State != Lux::PhysicsContactEvent::Type::End;
		}));
		{
			BodyLockWrite lock(system.GetBodyLockInterface(), moving);
			assert(lock.Succeeded());
			lock.GetBody().SetIsSensor(true);
		}
		system.Update(1.0f / 60, 1, &temp, &jobs);
		listener.Drain(events);
		assert(std::any_of(events.begin(), events.end(), [](const auto& event)
		{
			return event.State == Lux::PhysicsContactEvent::Type::End;
		}));
		++removed;
		bi.RemoveBody(moving);
		bi.DestroyBody(moving);
		system.Update(1.0f / 60, 1, &temp, &jobs);
		listener.Drain(events);
		for (auto& e : events)
			if (e.State == Lux::PhysicsContactEvent::Type::End)
				removed++;
		assert(removed > 0);
		// Sensors do not produce sound-producing contacts.
		ball.mIsSensor = true;
		ball.mPosition = RVec3(0, 0.4f, 0);
		ball.mLinearVelocity = Vec3::sZero();
		auto sensor = bi.CreateAndAddBody(ball, EActivation::Activate);
		system.Update(1.0f / 60, 1, &temp, &jobs);
		listener.Drain(events);
		for (auto& e : events)
			assert(e.State == Lux::PhysicsContactEvent::Type::End);
		bi.RemoveBody(sensor);
		bi.DestroyBody(sensor);
		bi.RemoveBody(ground);
		bi.DestroyBody(ground);
	}
	{
		// Mirrors PhysicsScene::CastRayAll's settings: occlusion needs each solid's entry and exit.
		BPLayerInterfaceImpl layers;
		ObjectVsBroadPhaseLayerFilterImpl broad;
		ObjectLayerPairFilterImpl pair;
		PhysicsSystem system;
		system.Init(1024, 0, 2048, 2048, layers, broad, pair);
		auto& bi = system.GetBodyInterface();
		BodyCreationSettings convexWall(BoxShapeSettings(Vec3(0.15f, 2, 2)).Create().Get(), RVec3(3, 0, 0), Quat::sIdentity(), EMotionType::Static, Layers::NON_MOVING);
		convexWall.mUserData = 300;
		const auto convexID = bi.CreateAndAddBody(convexWall, EActivation::DontActivate);
		TriangleList triangles;
		const Float3 c[8] = { { -0.25f, -2, -2 }, { 0.25f, -2, -2 }, { -0.25f, 2, -2 }, { 0.25f, 2, -2 },
			{ -0.25f, -2, 2 }, { 0.25f, -2, 2 }, { -0.25f, 2, 2 }, { 0.25f, 2, 2 } };
		const int faces[12][3] = { { 0, 2, 3 }, { 0, 3, 1 }, { 4, 5, 7 }, { 4, 7, 6 }, { 0, 4, 6 }, { 0, 6, 2 },
			{ 1, 3, 7 }, { 1, 7, 5 }, { 0, 1, 5 }, { 0, 5, 4 }, { 2, 6, 7 }, { 2, 7, 3 } };
		for (const auto& face : faces)
			triangles.push_back(Triangle(c[face[0]], c[face[1]], c[face[2]]));
		BodyCreationSettings meshWall(MeshShapeSettings(triangles).Create().Get(), RVec3(6, 0, 0), Quat::sIdentity(), EMotionType::Static, Layers::NON_MOVING);
		meshWall.mUserData = 400;
		const auto meshID = bi.CreateAndAddBody(meshWall, EActivation::DontActivate);
		system.OptimizeBroadPhase();

		RRayCast ray(RVec3(0, 0.3f, 0.2f), Vec3(10, 0, 0));
		RayCastSettings settings;
		settings.SetBackFaceMode(EBackFaceMode::CollideWithBackFaces);
		settings.mTreatConvexAsSolid = false;
		AllHitCollisionCollector<CastRayCollector> collector;
		system.GetNarrowPhaseQuery().CastRay(ray, settings, collector);
		collector.Sort();
		struct Crossing { uint64 Entity; float Distance; bool Exit; };
		std::vector<Crossing> crossings;
		for (const RayCastResult& hit : collector.mHits)
		{
			BodyLockRead lock(system.GetBodyLockInterface(), hit.mBodyID);
			assert(lock.Succeeded());
			const RVec3 point = ray.GetPointOnRay(hit.mFraction);
			const Vec3 normal = lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point);
			crossings.push_back({ lock.GetBody().GetUserData(), hit.mFraction * 10, normal.Dot(ray.mDirection) > 0 });
		}
		// Winding of the hand-built mesh does not matter: the classification uses the surface normal.
		assert(crossings.size() == 4);
		assert(crossings[0].Entity == 300 && !crossings[0].Exit && std::abs(crossings[0].Distance - 2.85f) < 1e-3f);
		assert(crossings[1].Entity == 300 && crossings[1].Exit && std::abs(crossings[1].Distance - 3.15f) < 1e-3f);
		assert(crossings[2].Entity == 400 && std::abs(crossings[2].Distance - 5.75f) < 1e-3f);
		assert(crossings[3].Entity == 400 && std::abs(crossings[3].Distance - 6.25f) < 1e-3f);
		assert(crossings[2].Exit != crossings[3].Exit);
		for (const auto id : { convexID, meshID })
		{
			bi.RemoveBody(id);
			bi.DestroyBody(id);
		}
	}
	UnregisterTypes();
	Factory::sInstance = nullptr;
	std::cout << "PASS: real Jolt multi-worker impacts, masses, estimated impulse, persistent contacts, removal/sleep "
				 "and sensor exclusion; all-hit rays report solid entries and exits\n";
}
