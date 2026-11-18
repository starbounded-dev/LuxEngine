// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "RuntimeSplash.h"

#ifdef LUX_PLATFORM_WINDOWS

#include "stb/include/stb_image.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace Lux::RuntimeSplash
{
	namespace
	{
		struct Splash
		{
			std::vector<uint8_t> Bgra; // over black, top-down
			int Width = 0, Height = 0; // image size
			int WindowWidth = 0, WindowHeight = 0;
		};

		std::thread s_Thread;
		std::atomic<bool> s_Stop = false;
		Splash s_Splash;

		LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
		{
			if (message == WM_PAINT)
			{
				PAINTSTRUCT paint;
				HDC dc = BeginPaint(window, &paint);
				BITMAPINFO info = {};
				info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				info.bmiHeader.biWidth = s_Splash.Width;
				info.bmiHeader.biHeight = -s_Splash.Height; // top-down
				info.bmiHeader.biPlanes = 1;
				info.bmiHeader.biBitCount = 32;
				info.bmiHeader.biCompression = BI_RGB;
				SetStretchBltMode(dc, HALFTONE);
				StretchDIBits(dc, 0, 0, s_Splash.WindowWidth, s_Splash.WindowHeight, 0, 0, s_Splash.Width, s_Splash.Height,
					s_Splash.Bgra.data(), &info, DIB_RGB_COLORS, SRCCOPY);
				EndPaint(window, &paint);
				return 0;
			}
			if (message == WM_ERASEBKGND)
				return 1;
			return DefWindowProcW(window, message, wParam, lParam);
		}

		void Run();

		// A std::thread still joinable at static destruction ends the process, so no exit path may skip Hide.
		struct Guard
		{
			~Guard() { Hide(); }
		};
		Guard s_Guard;

		void Run()
		{
			WNDCLASSW windowClass = {};
			windowClass.lpfnWndProc = Procedure;
			windowClass.hInstance = GetModuleHandleW(nullptr);
			windowClass.lpszClassName = WindowClass;
			windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
			RegisterClassW(&windowClass);

			const int screenWidth = GetSystemMetrics(SM_CXSCREEN), screenHeight = GetSystemMetrics(SM_CYSCREEN);
			HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, WindowClass, L"", WS_POPUP | WS_VISIBLE,
				(screenWidth - s_Splash.WindowWidth) / 2, (screenHeight - s_Splash.WindowHeight) / 2,
				s_Splash.WindowWidth, s_Splash.WindowHeight, nullptr, nullptr, windowClass.hInstance, nullptr);
			if (window)
			{
				UpdateWindow(window);
				MSG message;
				while (!s_Stop)
				{
					while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
					{
						TranslateMessage(&message);
						DispatchMessageW(&message);
					}
					Sleep(10);
				}
				DestroyWindow(window);
			}
			UnregisterClassW(WindowClass, windowClass.hInstance);
		}
	}

	void Show(const std::filesystem::path& image)
	{
		if (s_Thread.joinable() || image.empty())
			return;

		int width = 0, height = 0, channels = 0;
		stbi_uc* pixels = stbi_load(image.string().c_str(), &width, &height, &channels, 4);
		if (!pixels)
			return; // a missing or unreadable splash never stops the game
		s_Splash.Width = width;
		s_Splash.Height = height;
		s_Splash.Bgra.resize(static_cast<size_t>(width) * height * 4);
		for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i)
		{
			const uint32_t alpha = pixels[i * 4 + 3];
			s_Splash.Bgra[i * 4 + 0] = static_cast<uint8_t>(pixels[i * 4 + 2] * alpha / 255);
			s_Splash.Bgra[i * 4 + 1] = static_cast<uint8_t>(pixels[i * 4 + 1] * alpha / 255);
			s_Splash.Bgra[i * 4 + 2] = static_cast<uint8_t>(pixels[i * 4 + 0] * alpha / 255);
		}
		stbi_image_free(pixels);

		// At most 720 x 480, in the image's own proportions.
		const double scale = std::min({ 1.0, 720.0 / width, 480.0 / height });
		s_Splash.WindowWidth = std::max(1, static_cast<int>(width * scale));
		s_Splash.WindowHeight = std::max(1, static_cast<int>(height * scale));

		s_Stop = false;
		s_Thread = std::thread(Run);
	}

	void Hide()
	{
		if (!s_Thread.joinable())
			return;
		s_Stop = true;
		s_Thread.join();
	}
}

#else

namespace Lux::RuntimeSplash
{
	void Show(const std::filesystem::path&) {}
	void Hide() {}
}

#endif
