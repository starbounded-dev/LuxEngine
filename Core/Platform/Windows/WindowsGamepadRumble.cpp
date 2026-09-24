// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Core/GamepadRumble.h"

#include <Windows.h>
#include <Xinput.h>

#pragma comment(lib, "xinput.lib")

namespace Lux::PlatformRumble {

	namespace {

		std::array<bool, XUSER_MAX_COUNT> s_Rumbling{};

		void SetUser(DWORD user, float low, float high)
		{
			XINPUT_VIBRATION vibration{};
			vibration.wLeftMotorSpeed = (WORD)std::lround(std::clamp(low, 0.0f, 1.0f) * 65535.0f);
			vibration.wRightMotorSpeed = (WORD)std::lround(std::clamp(high, 0.0f, 1.0f) * 65535.0f);
			if (XInputSetState(user, &vibration) == ERROR_SUCCESS)
				s_Rumbling[user] = vibration.wLeftMotorSpeed != 0 || vibration.wRightMotorSpeed != 0;
		}

		// GLFW does not expose a pad's XInput user index, but it adds XInput pads to joystick slots
		// in user-index order, so the k-th Xbox slot maps to the k-th connected user index.
		int FindUserIndex(const Controller& controller)
		{
			int ordinal = 0;
			for (const auto& [id, other] : Input::GetControllers())
			{
				if (id == controller.ID)
					break;
				if (other.Family == GamepadFamily::Xbox)
					ordinal++;
			}

			for (DWORD user = 0; user < XUSER_MAX_COUNT; user++)
			{
				XINPUT_STATE state;
				if (XInputGetState(user, &state) != ERROR_SUCCESS)
					continue;

				if (ordinal-- == 0)
					return (int)user;
			}
			return -1;
		}

	}

	void OnControllersChanged()
	{
	}

	bool Supports(const Controller& controller)
	{
		return controller.Family == GamepadFamily::Xbox;
	}

	void Set(const Controller& controller, float low, float high)
	{
		if (!Supports(controller))
			return;

		const int user = FindUserIndex(controller);
		if (user >= 0)
			SetUser((DWORD)user, low, high);
	}

	void Shutdown()
	{
		for (DWORD user = 0; user < XUSER_MAX_COUNT; user++)
		{
			if (s_Rumbling[user])
				SetUser(user, 0.0f, 0.0f);
		}
	}

}
