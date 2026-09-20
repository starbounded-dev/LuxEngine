// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

namespace Lux {

	class Timestep
	{
		public:
			Timestep(float time = 0.0f)
				: m_Time(time)
			{
			}
			operator float() const { return m_Time; }
			float GetSeconds() const { return m_Time; }
			float GetMilliseconds() const { return m_Time * 1000.0f; }
		private:
			float m_Time;
	};
}