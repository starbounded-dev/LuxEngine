// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Core/Ref.h"
#include "Lux/Renderer/FrameRenderPacket.h"
#include "Lux/VR/LuxOpticConfig.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Lux {

	class KeyPressedEvent;
	class LuxOpticTracker;
	class Scene;
	class SceneRenderer;

	// A VR view of a scene for one host: Lux-Runtime with --vr today, the editor's Game View later
	// (docs/VR_PLAN.md). Owns the headset settings and the tracker, and renders the scene's primary
	// camera with the head applied (StereoRig). For now it draws one centre eye into the host's
	// renderer; stereo comes with the StereoRenderer.
	//
	// Without the tracker the head is still there to develop with: holding Alt and moving the mouse
	// turns it. Main thread.
	class VRSession : public RefCounted
	{
	public:
		struct Stats
		{
			bool TrackerConnected = false;
			uint32_t TrackerReportsPerSecond = 0;
			bool MouseHead = false;                    // the mouse is turning the head this frame
			glm::vec3 HeadYawPitchRoll = glm::vec3(0.0f);   // degrees
		};

		// Reads LuxOpticConfig::GetDefaultPath(), writing the defaults there first when there is no
		// file, so there is one to edit. Starts the tracker.
		VRSession();
		~VRSession() override;

		// Once a frame, before Render: follows the tracker and the mouse fallback.
		void OnUpdate();
		// Ctrl+F5 recenter, Ctrl+F6 show/hide the stats, Ctrl+F8 reload the settings file. True when
		// the key was one of these.
		bool OnKeyPressed(const KeyPressedEvent& event);

		// Builds the scene's runtime frame packet, puts the camera at the head, and submits it to
		// `renderer`, whose output is `targetSize` pixels. Draws nothing, like Scene::OnRenderRuntime,
		// when the scene has no primary camera or the renderer is not ready.
		void Render(Ref<Scene> scene, Ref<SceneRenderer> renderer, const glm::uvec2& targetSize);

		const LuxOpticConfig& GetConfig() const { return m_Config; }
		bool IsStatsVisible() const { return m_ShowStats; }
		Stats GetStats() const;

	private:
		glm::quat GetHeadOrientation() const;
		void Recenter();
		void ReloadConfig();

	private:
		LuxOpticConfig m_Config;
		Scope<LuxOpticTracker> m_Tracker;

		// Mouse fallback, in radians. On losing the tracker it starts from the tracker's last pose,
		// so the view holds still instead of snapping forward.
		float m_MouseYaw = 0.0f;
		float m_MousePitch = 0.0f;
		bool m_MouseHeadActive = false;
		bool m_HasMousePosition = false;
		glm::vec2 m_LastMousePosition = glm::vec2(0.0f);
		// The head source for this frame, sampled once in OnUpdate.
		bool m_TrackerConnected = false;
		glm::quat m_LastTrackerOrientation = { 1.0f, 0.0f, 0.0f, 0.0f };

		bool m_ShowStats = true;

		// Reused every frame so its lists keep their capacity; reset after each submit so it holds
		// no scene resources between frames.
		FrameRenderPacket m_Packet;
	};

}
