#include "AudioTestHost.h"
#include "Lux/Audio/AudioGeometrySystem.h"
#include "Lux/Audio/AudioZoneSystem.h"
#include "Lux/Scene/Components.h"
#include <glm/gtc/matrix_transform.hpp>

int main()
{
	RaytracedAudioScene world(nullptr);
	world.Start();
	assert(world.IsRunning());
	AcousticGeometry cube;
	AudioGeometrySystem::BuildPortal(cube);
	const glm::mat4 transform = glm::scale(glm::rotate(glm::translate(glm::mat4(1), { 1200, 3, -2 }), 0.7f, glm::vec3(0, 1, 0)), { -2, 3, 1 });
	assert(world.SetGeometry(100, cube, transform, true));
	glm::mat4 actual;
	assert(world.GetGeometryTransform(100, actual) && actual == transform);
	assert(world.GetStats().DynamicTriangleCount == 12);
	assert(world.GetStats().WorldMin.x + world.GetStats().WorldSize.x > 1200);
	world.OnUpdate(0.01f);
	world.WaitForResults();
	assert(world.UpdateGeometry(100, glm::mat4(1), AcousticMaterial::Glass, false));
	assert(world.GetGeometryTransform(100, actual) && actual == glm::mat4(1));
	assert(world.GetStats().DynamicPrimitiveCount == 0 && world.GetStats().StaticPrimitiveCount == 1);
	assert(world.RemoveGeometry(100));
	assert(!world.GetGeometryTransform(100, actual));

	AudioGeometrySystem geometry;
	int builds = 0;
	auto builder = [&](const AudioGeometryInput&, AcousticGeometry& output)
	{
		++builds;
		AudioGeometrySystem::BuildPortal(output);
		return true;
	};
	std::vector<AudioGeometryInput> inputs(3);
	for (size_t i = 0; i < inputs.size(); ++i)
	{
		inputs[i].Entity = i + 1;
		inputs[i].Mesh = 1;
	}
	geometry.Sync(inputs, world, builder, 1);
	assert(builds == 1 && geometry.GetPendingCount() == 2);
	inputs[0].Transform = glm::translate(glm::mat4(1), { 8, 0, 0 });
	geometry.Sync(inputs, world, builder, 1);
	assert(builds == 2);
	geometry.Sync(inputs, world, builder, 1);
	assert(builds == 3 && geometry.GetPendingCount() == 0);
	assert(geometry.GetStaticTransform(1, actual) && actual == glm::mat4(1));
	inputs[0].Mode = AcousticGeometryMode::Dynamic;
	geometry.Sync(inputs, world, builder, 1);
	assert(builds == 3 && world.GetStats().DynamicPrimitiveCount == 1);
	inputs[0].Material = AcousticMaterial::Wood;
	inputs[0].Transform = glm::mat4(1);
	geometry.Sync(inputs, world, builder, 1);
	assert(builds == 3);
	inputs[0].Mesh = 2;
	geometry.Sync(inputs, world, builder, 1);
	assert(builds == 4 && world.GetStats().StaticPrimitiveCount + world.GetStats().DynamicPrimitiveCount == 3);
	geometry.Sync({}, world, builder);
	assert(world.GetStats().StaticPrimitiveCount + world.GetStats().DynamicPrimitiveCount == 0);

	AudioGeometryInput shutter;
	shutter.Entity = 9;
	shutter.Portal = true;
	shutter.Mode = AcousticGeometryMode::Dynamic;
	inputs = { shutter };
	geometry.Sync(inputs, world, builder);
	assert(world.GetStats().DynamicPrimitiveCount == 1 && geometry.GetPortalOpen(9) == 0);
	inputs[0].Open = 1;
	geometry.Sync(inputs, world, builder, 0);
	assert(geometry.GetPortalOpen(9) == 0 && geometry.GetPendingCount() == 1);
	geometry.Sync(inputs, world, builder, 1);
	assert(geometry.GetPortalOpen(9) == 1 && world.GetStats().DynamicPrimitiveCount == 0);
	for (int i = 0; i < 10; ++i)
	{
		inputs[0].Open = i / 20.0f;
		geometry.Sync(inputs, world, builder, 0);
	}
	assert(geometry.GetPendingCount() == 1);
	geometry.Sync(inputs, world, builder, 1);
	Near(geometry.GetPortalOpen(9), 0.45f);
	assert(world.GetStats().DynamicPrimitiveCount == 1);
	inputs[0].Open = std::nextafter(1.0f, 0.0f);
	geometry.Sync(inputs, world, builder);
	assert(geometry.GetPortalOpen(9) == inputs[0].Open && world.GetStats().DynamicPrimitiveCount == 1);
	inputs[0].Open = std::numeric_limits<float>::quiet_NaN();
	geometry.Sync(inputs, world, builder);
	assert(world.GetStats().DynamicPrimitiveCount == 0);
	geometry.Sync({}, world, builder);
	inputs[0] = {};
	inputs[0].Entity = 12;
	inputs[0].Transform[0][0] = std::numeric_limits<float>::quiet_NaN();
	geometry.Sync(inputs, world, builder);
	geometry.Sync(inputs, world, builder, 0);
	assert(geometry.GetPendingCount() == 0);
	assert(!geometry.GetStaticTransform(12, actual));
	inputs[0].Transform = glm::mat4(1);
	geometry.Sync(inputs, world, builder);
	assert(geometry.GetStaticTransform(12, actual));
	inputs[0].Mesh = 100;
	geometry.Sync(inputs, world, builder, 0);
	assert(geometry.GetPendingCount() == 1);
	geometry.Sync({}, world, builder, 0);
	assert(geometry.GetPendingCount() == 0 && world.GetStats().StaticPrimitiveCount == 0);
	assert(world.SetGeometry(100, cube, glm::mat4(1), false));
	glm::mat4 invalid(1);
	invalid[0][0] = invalid[3][0] = std::numeric_limits<float>::max();
	assert(!world.UpdateGeometry(100, invalid, AcousticMaterial::Default, false));
	assert(world.GetGeometryTransform(100, actual) && actual == glm::mat4(1));
	geometry.Clear();
	world.Stop();
	assert(!world.IsRunning());

	AudioZoneComponent a, b;
	a.BlendDistance = b.BlendDistance = 0;
	AudioZoneInput zones[2];
	zones[0].ID = 10; zones[0].Component = &a;
	zones[1].ID = 11; zones[1].Component = &b;
	zones[0].Volume.Transform = glm::translate(glm::mat4(1), { 0, 0, -2 });
	zones[1].Volume.Transform = glm::translate(glm::mat4(1), { 0, 0, 2 });
	zones[0].Volume.HalfExtents = zones[1].Volume.HalfExtents = { 2, 2, 2 };
	AudioListener::States listeners{};
	listeners[0].Weight = 1;
	listeners[0].Position = { 0, 0, -0.1f };
	AudioPortalInput portal;
	portal.ID = 20; portal.ZoneA = 10; portal.ZoneB = 11;
	portal.BlendDistance = 1;
	assert(AudioZoneSystem::ValidatePortal(portal));
	AudioZoneSystem::Blend(zones, listeners, { &portal, 1 });
	Near(zones[0].Target, 1); Near(zones[1].Target, 0);
	portal.Open = 0.5f;
	AudioZoneSystem::Blend(zones, listeners, { &portal, 1 });
	const float half = zones[1].Target;
	assert(half > 0 && half < 0.5f);
	portal.Open = 1;
	AudioZoneSystem::Blend(zones, listeners, { &portal, 1 });
	Near(zones[1].Target, half * 2);
	Near(zones[0].Target + zones[1].Target, 1);
	std::vector<AudioPortalInput> many(8, portal);
	AudioZoneSystem::Blend(zones, listeners, many);
	Near(zones[0].Target + zones[1].Target, 1);
	assert(zones[0].Target >= 0 && zones[1].Target <= 1.0f + 1.0e-6f);
	portal.ZoneB = 999;
	AudioZoneSystem::Blend(zones, listeners, { &portal, 1 });
	Near(zones[0].Target, 1); Near(zones[1].Target, 0);
	portal.ZoneB = portal.ZoneA;
	assert(!AudioZoneSystem::ValidatePortal(portal));
	std::cout << "PASS: real VA geometry transforms/materials/bounds, budget/coalescing/deletion, shutters and normalized room leakage\n";
}
