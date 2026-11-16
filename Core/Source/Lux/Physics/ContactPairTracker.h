// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Physics/PhysicsContactEvent.h"

#include <algorithm>
#include <array>
#include <map>
#include <utility>

namespace Lux
{
	// Scripts get one OnCollisionBegin/End per entity pair, while Jolt reports every sub-shape pair (and
	// a character twice: its own sweep and its inner body). Each live contact key counts towards its
	// pair; onChange(entityA, entityB, begin) runs when a pair's count goes 0 -> 1 or 1 -> 0.
	class ContactPairTracker
	{
	public:
		template<typename OnChange>
		void Apply(const PhysicsContactEvent& event, OnChange&& onChange)
		{
			if (event.State == PhysicsContactEvent::Type::End)
			{
				const auto key = m_Keys.find(event.Key);
				if (key == m_Keys.end())
					return;
				const auto pair = key->second;
				m_Keys.erase(key);
				const auto count = m_Pairs.find(pair);
				if (count != m_Pairs.end() && --count->second == 0)
				{
					m_Pairs.erase(count);
					onChange(UUID(pair.first), UUID(pair.second), false);
				}
				return;
			}

			// Begin, or a Persist whose Begin was not seen (it came before Play started).
			if (!event.Entity1 || !event.Entity2 || event.Entity1 == event.Entity2 || m_Keys.contains(event.Key))
				return;
			const uint64_t a = event.Entity1, b = event.Entity2;
			const std::pair<uint64_t, uint64_t> pair{ std::min(a, b), std::max(a, b) };
			m_Keys.emplace(event.Key, pair);
			if (++m_Pairs[pair] == 1)
				onChange(UUID(pair.first), UUID(pair.second), true);
		}

		void Clear()
		{
			m_Keys.clear();
			m_Pairs.clear();
		}

	private:
		std::map<std::array<uint32_t, 4>, std::pair<uint64_t, uint64_t>> m_Keys;
		std::map<std::pair<uint64_t, uint64_t>, uint32_t> m_Pairs;
	};
}
