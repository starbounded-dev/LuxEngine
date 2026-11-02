#include "AudioTestHost.h"
#include "Lux/Audio/AudioGeometrySystem.h"
#include "Lux/Audio/AudioZoneSystem.h"
#include "Lux/Scene/Components.h"
#include <glm/gtc/matrix_transform.hpp>

int main()
{
	// Shared shutter maths used by the Soundwave capture and the raycast comparison.
	AcousticGeometry cube;
	AudioGeometrySystem::BuildPortal(cube);
	assert(cube.Triangles.size() == 36);
	AudioGeometryInput shutter;
	shutter.Entity = 9;
	shutter.Portal = true;
	shutter.Mode = AcousticGeometryMode::Dynamic;
	shutter.HalfExtents = { 1, 1, 0.1f };
	assert(AudioGeometrySystem::Validate(shutter));
	shutter.Open = 0.5f;
	const glm::vec4 edge = AudioGeometrySystem::PortalTransform(shutter) * glm::vec4(1, 0, 0, 1);
	Near(edge.x, 0.0f); // half open: the closing edge sits at the frame's centre
	shutter.Open = std::numeric_limits<float>::quiet_NaN();
	assert(!AudioGeometrySystem::Validate(shutter));
	shutter.Open = 0;
	shutter.Transform[0][0] = std::numeric_limits<float>::quiet_NaN();
	assert(!AudioGeometrySystem::Validate(shutter));

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
	std::cout << "PASS: shutter geometry and validation, normalized room leakage\n";
}
