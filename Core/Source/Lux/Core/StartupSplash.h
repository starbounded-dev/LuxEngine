// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <cstdint>
#include <string>

namespace Lux {

	// The loading splash shown while an application starts (board 06 of the Lux brand sheet): a
	// 960x540 card with the Lux mark over the dot grid, the LuxEngine wordmark, the version, and a
	// progress bar with the current step. The editor's welcome card (SplashScreen) is separate.
	//
	// Create one at the start of a layer's OnAttach, Report() between its steps, and Finish() at its
	// end. Each Report() draws one frame through Application::RenderStartupFrame, so the splash
	// updates when startup code reports and not in between. After OnAttach, call DrawUntilPresented()
	// every frame from the layer's OnImGuiRender and destroy the splash when it returns false. Create
	// the application with DeferWindowShow: the splash places and shows the main window. With ImGui
	// platform viewports (Windows) the splash is a borderless OS window of its own and the main window
	// stays hidden until the splash is destroyed; without them (Linux) the splash can only draw in the
	// main window, so until Finish() that window is the card. Main thread only, one at a time.
	class StartupSplash
	{
	public:
		StartupSplash();
		~StartupSplash();   // shows the main window; the splash's own window closes next frame

		StartupSplash(const StartupSplash&) = delete;
		StartupSplash& operator=(const StartupSplash&) = delete;

		// Shows `status`, the step that is starting, at `progress` (0..1) on the splash and draws a
		// frame. Does nothing when no splash is up or startup is over, so any startup code may call it.
		static void Report(float progress, std::string status);

		// At the end of the owner's OnAttach, after its last Report(); Report() does nothing from
		// here on. Where the main window has been the card, puts it where the application asked
		// (WindowWidth x WindowHeight, maximized or centred) and draws until the window manager has
		// maximized it, so the owner's first frame is laid out at the final size.
		void Finish();

		// After startup, once per frame from the owner's OnImGuiRender (after its own windows). Keeps
		// the splash up over the still-hidden main window until the owner's first frames have been
		// presented into it, then returns false: destroy the splash, and the main window appears with
		// the UI already drawn instead of an empty frame covering the splash.
		bool DrawUntilPresented();

	private:
		void Draw() const;

	private:
		std::string m_Status;
		float m_Progress = 0.0f;
		bool m_OwnWindow = false;
		bool m_WindowIsCard = false;    // the main window is sized to the card until Finish()
		bool m_Finished = false;        // Finish() ran: startup is over
		uint32_t m_HandOffFrames = 0;   // frames drawn by DrawUntilPresented

		inline static StartupSplash* s_Instance = nullptr;
	};

}
