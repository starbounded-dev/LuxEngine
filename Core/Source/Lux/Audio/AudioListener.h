#pragma once

#include "Lux/Core/UUID.h"

#include <array>
#include <glm/glm.hpp>

namespace Lux {

	struct AudioListenerState
	{
		UUID EntityID = 0;
		float Weight = 0.0f;
		glm::vec3 Position{ 0.0f };
		glm::vec3 Velocity{ 0.0f };
		glm::vec3 Forward{ 0.0f, 0.0f, -1.0f };
		glm::vec3 Up{ 0.0f, 1.0f, 0.0f };
		bool UseAttenuationPosition = false;
		glm::vec3 AttenuationPosition{ 0.0f };
	};

	// Main-thread bridge: Scene supplies the complete set each frame, including empty slots.
	class AudioListener
	{
	public:
		static constexpr int MaxListeners = 8;
		using States = std::array<AudioListenerState, MaxListeners>;

		static bool SetTransform(AudioListenerState& listener, const glm::mat4& transform);
		static int GetPrimaryIndex(const States& listeners);
		static void Apply(const States& listeners);
	};
}
