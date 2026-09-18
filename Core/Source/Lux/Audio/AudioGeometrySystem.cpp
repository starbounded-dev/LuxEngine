#include "lpch.h"
#include "AudioGeometrySystem.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace Lux
{
	bool AudioGeometryInput::operator==(const AudioGeometryInput& other) const
	{
		// Stable invalid authoring values must not flood the queue and log every frame.
		const auto equal = [](float a, float b) { return a == b || (std::isnan(a) && std::isnan(b)); };
		if (Entity != other.Entity || Portal != other.Portal || Mode != other.Mode || Material != other.Material ||
			Mesh != other.Mesh || Submesh != other.Submesh || !equal(Open, other.Open))
			return false;
		for (int axis = 0; axis < 3; ++axis)
			if (!equal(HalfExtents[axis], other.HalfExtents[axis]))
				return false;
		for (int c = 0; c < 4; ++c)
			for (int r = 0; r < 4; ++r)
				if (!equal(Transform[c][r], other.Transform[c][r]))
					return false;
		return true;
	}

	bool AudioGeometrySystem::Validate(const AudioGeometryInput& input)
	{
		if (input.Entity == 0 || input.Mode > AcousticGeometryMode::Disabled || !IsValidAcousticMaterial(input.Material))
			return false;
		for (int c = 0; c < 4; ++c)
			for (int r = 0; r < 4; ++r)
				if (!std::isfinite(input.Transform[c][r]))
					return false;
		const double determinant = glm::determinant(glm::dmat3(input.Transform));
		if (input.Transform[0].w != 0 || input.Transform[1].w != 0 || input.Transform[2].w != 0 || input.Transform[3].w != 1 ||
			!std::isfinite(determinant) || determinant == 0)
			return false;
		return !input.Portal || (std::isfinite(input.Open) && input.Open >= 0 && input.Open <= 1 &&
			std::isfinite(input.HalfExtents.x) && std::isfinite(input.HalfExtents.y) && std::isfinite(input.HalfExtents.z) &&
			glm::all(glm::greaterThan(input.HalfExtents, glm::vec3(0))));
	}

	glm::mat4 AudioGeometrySystem::PortalTransform(const AudioGeometryInput& input)
	{
		// A shutter retracts toward local -X; Open=1 removes the primitive entirely.
		return glm::scale(glm::translate(input.Transform, glm::vec3(-input.HalfExtents.x * input.Open, 0, 0)),
			input.HalfExtents * glm::vec3(1.0f - input.Open, 1, 1));
	}

	void AudioGeometrySystem::BuildPortal(AcousticGeometry& geometry)
	{
		constexpr int indices[] = { 0,2,3, 0,3,1, 4,5,7, 4,7,6, 0,4,6, 0,6,2,
			1,3,7, 1,7,5, 0,1,5, 0,5,4, 2,6,7, 2,7,3 };
		geometry.Triangles.reserve(36);
		for (int i : indices)
			geometry.Triangles.emplace_back(i & 1 ? 1 : -1, i & 2 ? 1 : -1, i & 4 ? 1 : -1);
	}

	void AudioGeometrySystem::Sync(std::span<const AudioGeometryInput> inputs, RaytracedAudioScene& world,
		const Builder& builder, size_t primitiveBudget, size_t vertexBudget)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		for (auto& [key, entry] : m_Entries)
			entry.Seen = false;
		for (auto input : inputs)
		{
			const Key key{ static_cast<uint64_t>(input.Entity), input.Portal };
			auto [it, inserted] = m_Entries.try_emplace(key);
			auto& entry = it->second;
			entry.Seen = true;
			// Static geometry keeps its original transform; changing mode opts into live movement.
			if (!inserted && Validate(entry.Desired) && input.Mode == AcousticGeometryMode::Static && entry.Desired.Mode == input.Mode)
				input.Transform = entry.Desired.Transform;
			if (inserted || !(input == entry.Desired))
			{
				entry.Desired = input;
				if (!entry.Queued)
				{
					m_Pending.push_back(key);
					entry.Queued = true;
				}
			}
		}
		// Deletion is immediate so destroyed walls cannot remain as acoustic ghosts behind a queue.
		std::erase_if(m_Entries, [&](auto& pair)
		{
			auto& entry = pair.second;
			return !entry.Seen && (!entry.Exists || world.RemoveGeometry(entry.Primitive));
		});
		std::erase_if(m_Pending, [&](const Key& key) { return !m_Entries.contains(key); });
		size_t work = 0, vertices = 0;
		while (!m_Pending.empty() && work < primitiveBudget && (vertices < vertexBudget || work == 0))
		{
			const Key key = m_Pending.front();
			m_Pending.pop_front();
			const auto found = m_Entries.find(key);
			if (found == m_Entries.end())
				continue;
			auto& entry = found->second;
			entry.Queued = false;
			const auto& input = entry.Desired;
			++work;
			if (!Validate(input))
			{
				LUX_CORE_ERROR_TAG("Audio", "Invalid acoustic geometry on entity {}: check transform, mode, material and portal dimensions", key.first);
				if (entry.Exists && world.RemoveGeometry(entry.Primitive))
					entry.Exists = false;
				entry.HasApplied = false;
				continue;
			}
			if (input.Mode == AcousticGeometryMode::Disabled || (input.Portal && input.Open == 1))
			{
				if (entry.Exists && !world.RemoveGeometry(entry.Primitive))
					continue;
				entry.Exists = false;
			}
			else
			{
				const bool rebuild = !entry.Exists || input.Mesh != entry.Applied.Mesh || input.Submesh != entry.Applied.Submesh;
				const auto transform = input.Portal ? PortalTransform(input) : input.Transform;
				if (rebuild)
				{
					AcousticGeometry geometry;
					geometry.Material = input.Material;
					if (input.Portal)
						BuildPortal(geometry);
					else if (!builder(input, geometry))
					{
						LUX_CORE_ERROR_TAG("Audio", "Cannot build acoustic mesh on entity {} (asset {})", key.first, input.Mesh);
						continue;
					}
					if (!world.SetGeometry(entry.Primitive, geometry, transform, input.Mode == AcousticGeometryMode::Dynamic))
						continue;
					entry.Vertices = geometry.Triangles.size();
					entry.Exists = true;
				}
				else if (!world.UpdateGeometry(entry.Primitive, transform, input.Material, input.Mode == AcousticGeometryMode::Dynamic))
					continue;
				vertices += entry.Vertices;
			}
			entry.Applied = input;
			entry.HasApplied = true;
		}
	}

	float AudioGeometrySystem::GetPortalOpen(UUID entity) const
	{
		const auto found = m_Entries.find({ static_cast<uint64_t>(entity), true });
		return found != m_Entries.end() && found->second.HasApplied ? found->second.Applied.Open : 0.0f;
	}

	bool AudioGeometrySystem::GetStaticTransform(UUID entity, glm::mat4& transform) const
	{
		const auto found = m_Entries.find({ static_cast<uint64_t>(entity), false });
		if (found == m_Entries.end() || found->second.Desired.Mode != AcousticGeometryMode::Static || !Validate(found->second.Desired))
			return false;
		transform = found->second.Desired.Transform;
		return true;
	}

	void AudioGeometrySystem::Clear()
	{
		m_Entries.clear();
		m_Pending.clear();
	}
}
