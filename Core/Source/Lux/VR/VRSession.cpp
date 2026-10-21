// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "VRSession.h"

#include "Lux/Core/Events/KeyEvent.h"
#include "Lux/Core/Input.h"
#include "Lux/Debug/Profiler.h"
#include "Lux/Renderer/SceneRenderer.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Utilities/FileSystem.h"
#include "Lux/VR/LuxOpticTracker.h"
#include "Lux/VR/StereoRig.h"

namespace Lux {

	namespace {

		constexpr float kMouseRadiansPerPixel = 0.0025f;
		// Short of straight up/down, where the heading would flip.
		constexpr float kMaxMousePitch = glm::radians(85.0f);

		constexpr glm::vec3 kUp = { 0.0f, 1.0f, 0.0f };
		constexpr glm::vec3 kRight = { 1.0f, 0.0f, 0.0f };

	}

	VRSession::VRSession()
	{
		const std::filesystem::path configPath = LuxOpticConfig::GetDefaultPath();
		if (FileSystem::Exists(configPath))
			m_Config.Load(configPath);
		else if (m_Config.Save(configPath))
			LUX_CORE_INFO_TAG("VR", "Wrote the default headset settings to '{}'.", configPath.string());

		LUX_CORE_INFO_TAG("VR", "VR on: headset settings '{}' (IPD {} m, FOV {} deg).", configPath.string(), m_Config.IPD, m_Config.FOV);

		m_Tracker = CreateScope<LuxOpticTracker>();
	}

	VRSession::~VRSession() = default;

	void VRSession::OnUpdate()
	{
		// Decided once per frame: the tracker thread can connect or drop at any moment, and a source
		// switching between here and Render would show one frame of the other source's pose.
		const bool trackerConnected = m_Tracker->IsConnected();
		if (trackerConnected)
		{
			m_LastTrackerOrientation = m_Tracker->GetOrientation();
		}
		else if (m_TrackerConnected)
		{
			// Lost: the mouse head takes over from the last pose (its roll is dropped).
			const glm::vec3 yawPitchRoll = glm::radians(StereoRig::ToYawPitchRollDegrees(m_LastTrackerOrientation));
			m_MouseYaw = yawPitchRoll.x;
			m_MousePitch = glm::clamp(yawPitchRoll.y, -kMaxMousePitch, kMaxMousePitch);
		}
		m_TrackerConnected = trackerConnected;

		// Track the cursor every frame, so pressing Alt does not turn the head by everything the
		// mouse moved since the last time.
		const auto [mouseX, mouseY] = Input::GetMousePosition();
		const glm::vec2 mousePosition = { mouseX, mouseY };
		const glm::vec2 mouseDelta = m_HasMousePosition ? mousePosition - m_LastMousePosition : glm::vec2(0.0f);
		m_LastMousePosition = mousePosition;
		m_HasMousePosition = true;

		m_MouseHeadActive = !trackerConnected && (Input::IsKeyDown(KeyCode::LeftAlt) || Input::IsKeyDown(KeyCode::RightAlt));
		if (m_MouseHeadActive)
		{
			// Window y grows downwards: moving the mouse right turns right (negative yaw about +Y),
			// moving it down looks down.
			m_MouseYaw -= mouseDelta.x * kMouseRadiansPerPixel;
			m_MousePitch = glm::clamp(m_MousePitch - mouseDelta.y * kMouseRadiansPerPixel, -kMaxMousePitch, kMaxMousePitch);
		}
	}

	bool VRSession::OnKeyPressed(const KeyPressedEvent& event)
	{
		if (event.GetRepeatCount() != 0 || !(Input::IsKeyDown(KeyCode::LeftControl) || Input::IsKeyDown(KeyCode::RightControl)))
			return false;

		switch (event.GetKeyCode())
		{
			case KeyCode::F5:
				Recenter();
				return true;
			case KeyCode::F6:
				m_ShowStats = !m_ShowStats;
				return true;
			case KeyCode::F8:
				ReloadConfig();
				return true;
			default:
				return false;
		}
	}

	void VRSession::Render(Ref<Scene> scene, Ref<SceneRenderer> renderer, const glm::uvec2& targetSize)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!scene || !renderer || !renderer->IsReady())
			return;

		m_Packet.Reset();
		if (!scene->BuildRenderPacketRuntime(m_Packet, renderer))
			return;

		// The pose is read here, after the scene update and as late as the main thread can, so the
		// image starts from the newest head rotation.
		const glm::mat4 cameraWorld = glm::inverse(m_Packet.Camera.ViewMatrix);
		const glm::mat4 headWorld = StereoRig::ComputeHeadTransform(cameraWorld, GetHeadOrientation());
		StereoRig::ApplyEyeCamera(m_Packet.Camera, headWorld, StereoRig::Eye::Center, m_Config, targetSize);
		m_Packet.Overlay2DView = m_Packet.Camera.ViewMatrix;
		m_Packet.Overlay2DViewProjection = m_Packet.Camera.Camera.GetProjectionMatrix() * m_Packet.Camera.ViewMatrix;

		// Each eye would focus somewhere else, and comfort guides drop DOF in VR anyway.
		m_Packet.PostProcess.DOFEnabled = false;

		scene->SubmitRenderPacket(renderer, m_Packet);
		m_Packet.Reset();
	}

	VRSession::Stats VRSession::GetStats() const
	{
		Stats stats;
		stats.TrackerConnected = m_TrackerConnected;
		stats.TrackerReportsPerSecond = m_Tracker->GetReportsPerSecond();
		stats.MouseHead = m_MouseHeadActive;
		stats.HeadYawPitchRoll = StereoRig::ToYawPitchRollDegrees(GetHeadOrientation());
		return stats;
	}

	glm::quat VRSession::GetHeadOrientation() const
	{
		// A tracker that dropped since OnUpdate still returns its last pose.
		if (m_TrackerConnected)
			return m_Tracker->GetOrientation();

		return glm::angleAxis(m_MouseYaw, kUp) * glm::angleAxis(m_MousePitch, kRight);
	}

	void VRSession::Recenter()
	{
		if (m_TrackerConnected)
			m_Tracker->Recenter();
		else
			m_MouseYaw = 0.0f;

		LUX_CORE_INFO_TAG("VR", "Head recentered.");
	}

	void VRSession::ReloadConfig()
	{
		const std::filesystem::path configPath = LuxOpticConfig::GetDefaultPath();
		LuxOpticConfig config = m_Config;
		if (config.Load(configPath))
		{
			m_Config = config;
			LUX_CORE_INFO_TAG("VR", "Reloaded the headset settings from '{}'.", configPath.string());
		}
		else if (!FileSystem::Exists(configPath))
		{
			LUX_CORE_WARN_TAG("VR", "'{}' no longer exists; keeping the current headset settings.", configPath.string());
		}
	}

}
