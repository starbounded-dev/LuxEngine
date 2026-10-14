// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <string>

namespace Lux {

	// The loading splash shown while an application starts (board 06 of the Lux brand sheet): a
	// 960x540 card with the Lux mark over the dot grid, the LuxEngine wordmark, the version, and a
	// progress bar with the current step. The editor's welcome card (SplashScreen) is separate.
	//
	// Create one at the start of a layer's OnAttach, Report() between its steps, destroy it when the
	// layer is ready. Each Report() draws one frame through Application::RenderStartupFrame, so the
	// splash updates when startup code reports and not in between. With ImGui platform viewports
	// (Windows) it is a borderless OS window of its own and the main window stays hidden until the
	// splash is destroyed (create the application with DeferWindowShow); without them (Linux) it
	// draws centred in the main window. Main thread only, one at a time.
	class StartupSplash
	{
	public:
		StartupSplash();
		~StartupSplash();   // shows the main window; the splash's own window closes next frame

		StartupSplash(const StartupSplash&) = delete;
		StartupSplash& operator=(const StartupSplash&) = delete;

		// Shows `status`, the step that is starting, at `progress` (0..1) on the splash and draws a
		// frame. Does nothing when no splash is up, so any startup code may call it.
		static void Report(float progress, std::string status);

	private:
		void Draw() const;

	private:
		std::string m_Status;
		float m_Progress = 0.0f;
		bool m_OwnWindow = false;

		inline static StartupSplash* s_Instance = nullptr;
	};

}
