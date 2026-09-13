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
	UnregisterTypes();
	Factory::sInstance = nullptr;
	std::cout << "PASS: real Jolt multi-worker impacts, masses, estimated impulse, persistent contacts, removal/sleep "
				 "and sensor exclusion\n";
}
