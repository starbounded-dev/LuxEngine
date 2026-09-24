// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "RaytracedAudioScene.h"
#include "AudioGeometrySettings.h"
#include <deque>
#include <functional>
#include <map>
#include <span>

namespace Lux
{
	struct AudioGeometryInput
	{
		UUID Entity = 0;
		bool Portal = false;
		AcousticGeometryMode Mode = AcousticGeometryMode::Static;
		AcousticMaterial Material = AcousticMaterial::Default;
		glm::mat4 Transform{ 1.0f };
		uint64_t Mesh = 0;
		uint32_t Submesh = 0;
		glm::vec3 HalfExtents{ 0.5f, 1.0f, 0.05f };
		float Open = 0.0f;
		bool operator==(const AudioGeometryInput& other) const;
	};

	// Main-thread queue. Coalesces edits and keeps transform-only changes out of mesh construction.
	class AudioGeometrySystem
	{
	public:
		using Builder = std::function<bool(const AudioGeometryInput&, AcousticGeometry&)>;
		void Sync(std::span<const AudioGeometryInput> inputs, RaytracedAudioScene& world, const Builder& builder,
			size_t primitiveBudget = 8, size_t vertexBudget = 65536);
		void Clear(); // The scene stops its VA world immediately afterward.
		size_t GetPendingCount() const { return m_Pending.size(); }
		float GetPortalOpen(UUID entity) const;
		bool GetStaticTransform(UUID entity, glm::mat4& transform) const;
		static bool Validate(const AudioGeometryInput& input);
		static glm::mat4 PortalTransform(const AudioGeometryInput& input);
		static void BuildPortal(AcousticGeometry& geometry);
	private:
		using Key = std::pair<uint64_t, bool>;
		struct Entry
		{
			UUID Primitive;
			AudioGeometryInput Desired, Applied;
			size_t Vertices = 0;
			bool Seen = false, Queued = false, Exists = false, HasApplied = false;
		};
		std::map<Key, Entry> m_Entries;
		std::deque<Key> m_Pending;
	};
}
