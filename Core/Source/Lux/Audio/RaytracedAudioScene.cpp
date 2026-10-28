// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Audio/RaytracedAudioScene.h"

#include "Lux/Audio/AsterwaveMaterials.h"
#include "Lux/Core/Timer.h"
#include <asterwave/simulation.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace Lux
{
	namespace
	{
		constexpr float kWorldBoundsPadding = 50.0f;
		constexpr float kDefaultWorldHalfExtent = 500.0f;
		constexpr int kReverbRayCount = 128;
		constexpr int kReverbBounceCount = 64;
		constexpr int kOcclusionRayCount = 512;
		constexpr int kOcclusionBounceCount = 8;
		constexpr int kPermeationRayCount = 128;
		constexpr int kPermeationBounceCount = 3;
		constexpr int kAmbientOcclusionRayCount = 512;
		constexpr int kAmbientOcclusionBounceCount = 8;
		constexpr int kAmbientPermeationRayCount = 128;
		constexpr int kAmbientPermeationBounceCount = 4;
		constexpr int kDefaultVisualisationIntervalMs = 100;
		constexpr int kCustomMaterialBase = 1000;
		// Keep 30 / metres representable without changing the authored or serialized value.
		const float kMinimumNativeTransmission = std::nextafter(30.0f / std::numeric_limits<float>::max(),
			std::numeric_limits<float>::infinity());

		asterwave::Vec3 ToAsterwave(const glm::vec3& value)
		{
			return { value.x, value.y, value.z };
		}

		glm::vec3 FromAsterwave(const asterwave::Vec3& value)
		{
			return { value.x, value.y, value.z };
		}

		asterwave::Mat4 ToAsterwave(const glm::mat4& value)
		{
			// GLM's column-major storage is the transpose needed by Asterwave's row vectors.
			asterwave::Mat4 result;
			std::copy_n(glm::value_ptr(value), result.m.size(), result.m.begin());
			return result;
		}

		bool Finite(const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool ValidGeometryTransform(const glm::mat4& transform)
		{
			for (int column = 0; column < 4; ++column)
				for (int row = 0; row < 4; ++row)
					if (!std::isfinite(transform[column][row]))
						return false;
			const double determinant = glm::determinant(glm::dmat3(transform));
			if (!std::isfinite(determinant) || determinant == 0 || transform[0].w != 0 ||
				transform[1].w != 0 || transform[2].w != 0 || transform[3].w != 1)
				return false;
			try
			{
				// A valid double determinant can still underflow in the native float inverse.
				// Reject it before replacing a working node or submitting it to worker threads.
				const auto inverse = ToAsterwave(transform).inverse();
				return std::all_of(inverse.m.begin(), inverse.m.end(), [](float value) { return std::isfinite(value); });
			}
			catch (const std::domain_error&)
			{
				return false;
			}
		}

		bool SetBounds(asterwave::WorldSettings& settings, const glm::dvec3& minimum, const glm::dvec3& maximum)
		{
			const glm::dvec3 size = maximum - minimum;
			for (int axis = 0; axis < 3; ++axis)
			{
				const double limit = std::numeric_limits<float>::max();
				if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) || !std::isfinite(size[axis]) ||
					std::abs(minimum[axis]) > limit || std::abs(maximum[axis]) > limit || size[axis] > limit || size[axis] <= 0)
				{
					LUX_CORE_ERROR_TAG("Audio", "Acoustic geometry exceeds representable Asterwave world bounds");
					return false;
				}
			}
			settings.Bounds = { ToAsterwave(glm::vec3(minimum)), ToAsterwave(glm::vec3(maximum)) };
			// Float rounding can collapse a small extent at a very large position.
			if (settings.Bounds.lo.greater_equal_any(settings.Bounds.hi))
			{
				LUX_CORE_ERROR_TAG("Audio", "Acoustic world bounds collapse at float precision");
				return false;
			}
			return true;
		}

		bool ExpandGeometryBounds(asterwave::WorldSettings& settings, const glm::mat4& transform,
			const glm::vec3& localMin, const glm::vec3& localMax)
		{
			glm::dvec3 minimum = FromAsterwave(settings.Bounds.lo);
			glm::dvec3 maximum = FromAsterwave(settings.Bounds.hi);
			for (int i = 0; i < 8; ++i)
			{
				const glm::dvec3 local{ i & 1 ? localMax.x : localMin.x, i & 2 ? localMax.y : localMin.y, i & 4 ? localMax.z : localMin.z };
				const glm::dvec3 point(glm::dmat4(transform) * glm::dvec4(local, 1));
				minimum = glm::min(minimum, point - glm::dvec3(kWorldBoundsPadding));
				maximum = glm::max(maximum, point + glm::dvec3(kWorldBoundsPadding));
			}
			return SetBounds(settings, minimum, maximum);
		}

		int StatisticCount(uint64_t value)
		{
			return static_cast<int>(std::min(value, static_cast<uint64_t>(std::numeric_limits<int>::max())));
		}
	}

	int AcousticMaterialID(AcousticMaterial material)
	{
		return kCustomMaterialBase + (IsValidAcousticMaterial(material) ? static_cast<int>(material) : 0);
	}

	bool ConfigureAcousticMaterials(asterwave::World& world, const AcousticMaterialSettings& settings)
	{
		if (!settings.Validate())
			return false;
		for (size_t i = 0; i < AcousticMaterialCount; ++i)
		{
			const auto material = static_cast<AcousticMaterial>(i);
			const auto properties = settings.Overrides[i].Enabled ? settings.Overrides[i].Properties : GetDefaultAcousticMaterialProperties(material);
			try
			{
				// Legacy projects permit flat loss above one. Native transmission multiplies by
				// (1 - loss), so saturate complete loss instead of generating negative energy.
				world.set_material(AcousticMaterialID(material), asterwave::MaterialProperties(
					properties.AbsorptionLF, properties.AbsorptionHF, properties.Scattering,
					std::max(properties.TransmissionLF, kMinimumNativeTransmission),
					std::max(properties.TransmissionHF, kMinimumNativeTransmission),
					std::min(properties.FlatTransmissionLF, 1.0f), std::min(properties.FlatTransmissionHF, 1.0f)));
			}
			catch (const std::exception& error)
			{
				LUX_CORE_ERROR_TAG("Audio", "Cannot configure Asterwave material {0}: {1}", AcousticMaterialNames[i], error.what());
				return false;
			}
		}
		return true;
	}

	struct RaytracedAudioScene::Impl
	{
		// Asterwave's API retains native objects for worker snapshots and reverb tails. Shared
		// ownership is confined to this vendor boundary; Lux owners remain Scope/Ref objects.
		struct Geometry
		{
			std::shared_ptr<asterwave::MeshPrimitive> Primitive;
			glm::vec3 Min{ 0 }, Max{ 0 };
			glm::mat4 Transform{ 1.0f };
			int Triangles = 0;
			bool Dynamic = false;
		};
		Scope<asterwave::World> World;
		std::shared_ptr<asterwave::Emitter> Listener;
		std::unordered_map<UUID, std::shared_ptr<asterwave::Emitter>> SourceEmitters;
		std::unordered_map<UUID, Geometry> GeometryNodes;
		std::vector<Geometry> StaticPrimitives;
		int StaticTriangleCount = 0;
		bool BatchPending = false;
		asterwave::SimulationStatistics CompletedStatistics;
		int GroupedEAXCount = 0;
		double SubmissionTimeMs = 0.0;
		double MainThreadTimeMs = 0.0;
		glm::vec3 ListenerPosition{ 0.0f };
		glm::vec3 BatchListenerPosition{ 0.0f };
		RaytracedAudioVisualisation Visualisation;
		asterwave::RaySettings DesiredVisualisation;
		int VisualisationIntervalMs = kDefaultVisualisationIntervalMs;

		static std::shared_ptr<asterwave::Emitter> CreateNativeEmitter()
		{
			return std::make_shared<asterwave::Emitter>();
		}

		static Geometry CreateGeometry(const AcousticGeometry& geometry, const glm::mat4& transform, bool dynamic)
		{
			if (!IsValidAcousticMaterial(geometry.Material) || geometry.Triangles.empty() ||
				geometry.Triangles.size() % 3 || geometry.Triangles.size() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
				!ValidGeometryTransform(transform))
				throw std::invalid_argument("invalid material, triangle count or transform");
			Geometry node;
			node.Min = glm::vec3(std::numeric_limits<float>::max());
			node.Max = glm::vec3(std::numeric_limits<float>::lowest());
			std::vector<asterwave::Vec3> vertices;
			vertices.reserve(geometry.Triangles.size());
			for (size_t triangle = 0; triangle < geometry.Triangles.size(); triangle += 3)
			{
				// Asterwave's one-sided queries treat clockwise as the outside, the reverse of engine
				// meshes. Unreversed, every solid is inside out: no bounce sees its target and walls
				// are measured from their far faces.
				for (const size_t corner : { triangle, triangle + 2, triangle + 1 })
				{
					const glm::vec3& position = geometry.Triangles[corner];
					if (!Finite(position))
						throw std::invalid_argument("non-finite acoustic vertex");
					node.Min = glm::min(node.Min, position);
					node.Max = glm::max(node.Max, position);
					vertices.push_back(ToAsterwave(position));
				}
			}
			auto mesh = std::make_shared<asterwave::MeshGeometry>(vertices, asterwave::AABB{ ToAsterwave(node.Min), ToAsterwave(node.Max) });
			node.Primitive = std::make_shared<asterwave::MeshPrimitive>(std::move(mesh));
			node.Primitive->transform = ToAsterwave(transform);
			node.Primitive->material_id = AcousticMaterialID(geometry.Material);
			node.Primitive->validate();
			node.Transform = transform;
			node.Triangles = static_cast<int>(vertices.size() / 3);
			node.Dynamic = dynamic;
			return node;
		}

		void SetWorldSettings(const asterwave::WorldSettings& settings)
		{
			const auto previous = World->settings();
			if (previous.Bounds.lo != settings.Bounds.lo || previous.Bounds.hi != settings.Bounds.hi)
				World->set_settings(settings);
		}

		void CaptureVisualisation(std::span<const asterwave::VisualisationData> data)
		{
			// wait() publishes on the main thread. Overlay requests cannot change the active stride.
			if (!DesiredVisualisation.enabled())
				return;
			const int bounces = Listener->Visualisation.Bounces;
			if (bounces <= 0 || data.size() % static_cast<size_t>(bounces))
			{
				LUX_CORE_ERROR_TAG("Audio", "Asterwave returned an invalid visualisation ray layout");
				Visualisation = {};
				return;
			}
			Visualisation.Bounces.resize(data.size());
			for (size_t i = 0; i < data.size(); ++i)
			{
				auto& bounce = Visualisation.Bounces[i];
				bounce = { FromAsterwave(data[i].Position), FromAsterwave(data[i].Normal), data[i].Hit };
				// Mesh hits report the triangle normal, which points along the arriving ray. Overlays
				// need the side the ray arrived on, whichever way the surface is wound.
				const glm::vec3 from = i % static_cast<size_t>(bounces) != 0 ? Visualisation.Bounces[i - 1].Position : BatchListenerPosition;
				if (glm::dot(bounce.Normal, bounce.Position - from) > 0.0f)
					bounce.Normal = -bounce.Normal;
			}
			Visualisation.Origin = BatchListenerPosition;
			Visualisation.BounceCount = bounces;
			Visualisation.RayCount = static_cast<int>(data.size() / static_cast<size_t>(bounces));
		}
	};

	bool RaytracedAudioScene::IsAvailable() { return true; }

	RaytracedAudioScene::RaytracedAudioScene(Scene* scene)
		: m_Scene(scene), m_Impl(CreateScope<Impl>())
	{
	}

	RaytracedAudioScene::~RaytracedAudioScene()
	{
		Stop();
	}

	bool RaytracedAudioScene::IsRunning() const
	{
		return m_Impl->World != nullptr;
	}

	void RaytracedAudioScene::Start(const AcousticMaterialSettings& materials)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (m_Impl->World)
			return;
		try
		{
			m_Impl->World = CreateScope<asterwave::World>();
			// A throwing callback is caught by Asterwave and reported here instead of stderr
			m_Impl->World->Name = "Asterwave";
			m_Impl->World->LogError = [](const std::string& message) { LUX_CORE_ERROR_TAG("Audio", "{0}", message); };
			if (!ConfigureAcousticMaterials(*m_Impl->World, materials))
			{
				Stop();
				return;
			}
			auto settings = m_Impl->World->settings();
			settings.Bounds = { asterwave::Vec3(-kDefaultWorldHalfExtent), asterwave::Vec3(kDefaultWorldHalfExtent) };
			m_Impl->World->set_settings(settings);
			m_Impl->Listener = Impl::CreateNativeEmitter();
			auto& listener = *m_Impl->Listener;
			listener.Name = "LuxListener";
			listener.HasRelativeReverb = true;
			listener.AffectsGroupedEAX = false;
			listener.Reverb = { kReverbRayCount, kReverbBounceCount };
			listener.Occlusion = { kOcclusionRayCount, kOcclusionBounceCount };
			listener.Permeation = { kPermeationRayCount, kPermeationBounceCount };
			listener.AmbientOcclusion = { kAmbientOcclusionRayCount, kAmbientOcclusionBounceCount };
			listener.AmbientPermeation = { kAmbientPermeationRayCount, kAmbientPermeationBounceCount };
			listener.OnVisualisation = [impl = m_Impl.get()](std::span<const asterwave::VisualisationData> data)
			{
				impl->CaptureVisualisation(data);
			};
			m_Impl->World->add_emitter(m_Impl->Listener);
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot start Asterwave scene: {0}", error.what());
			Stop();
		}
	}

	void RaytracedAudioScene::Stop()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Impl->DesiredVisualisation = {};
		if (m_Impl->World)
		{
			try
			{
				// Native shutdown joins the outstanding batch before detaching every retained object.
				m_Impl->World->shutdown();
			}
			catch (const std::exception& error)
			{
				LUX_CORE_ERROR_TAG("Audio", "Asterwave batch failed during shutdown: {0}", error.what());
			}
			m_Impl->World.reset();
		}
		m_Impl->Listener.reset();
		m_Impl->SourceEmitters.clear();
		m_Impl->GeometryNodes.clear();
		m_Impl->StaticPrimitives.clear();
		m_Impl->StaticTriangleCount = 0;
		m_Impl->BatchPending = false;
		m_Impl->CompletedStatistics = {};
		m_Impl->GroupedEAXCount = 0;
		m_Impl->SubmissionTimeMs = m_Impl->MainThreadTimeMs = 0.0;
		m_Impl->ListenerPosition = m_Impl->BatchListenerPosition = glm::vec3(0.0f);
		m_Impl->Visualisation = {};
		m_Impl->VisualisationIntervalMs = kDefaultVisualisationIntervalMs;
	}

	double RaytracedAudioScene::GetRaytracingTimeMilliseconds() const
	{
		return m_Impl->CompletedStatistics.RaytracingMs;
	}

	void RaytracedAudioScene::WaitForResults()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!m_Impl->World || !m_Impl->BatchPending)
			return;
		Timer timer;
		try
		{
			m_Impl->World->wait();
			m_Impl->CompletedStatistics = m_Impl->World->Statistics;
			m_Impl->GroupedEAXCount = StatisticCount(m_Impl->World->GroupedEAX.size());
			m_Impl->BatchPending = false;
			m_Impl->MainThreadTimeMs = m_Impl->SubmissionTimeMs + timer.ElapsedMillis();
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR_TAG("Audio", "Asterwave simulation failed; stopping this scene: {0}", error.what());
			Stop();
		}
	}

	void RaytracedAudioScene::OnUpdate(Timestep ts)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		(void)ts;
		WaitForResults();
		if (!m_Impl->World)
			return;
		Timer timer;
		try
		{
			m_Impl->Listener->Visualisation = m_Impl->DesiredVisualisation;
			m_Impl->Listener->VisualisationUpdateFrequency = m_Impl->VisualisationIntervalMs;
			const auto settings = m_Impl->World->settings();
			m_Impl->BatchListenerPosition = glm::clamp(m_Impl->ListenerPosition,
				FromAsterwave(settings.Bounds.lo), FromAsterwave(settings.Bounds.hi));
			m_Impl->World->update();
			m_Impl->BatchPending = true;
			m_Impl->SubmissionTimeMs = timer.ElapsedMillis();
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot submit Asterwave simulation; stopping this scene: {0}", error.what());
			Stop();
		}
	}

	bool RaytracedAudioScene::SetStaticGeometry(const std::vector<AcousticGeometry>& geometry)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		WaitForResults();
		if (!m_Impl->World)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot mirror acoustic geometry: Asterwave world is not running");
			return false;
		}
		std::vector<Impl::Geometry> pending;
		size_t attached = 0;
		try
		{
			pending.reserve(geometry.size());
			glm::dvec3 minimum(std::numeric_limits<double>::max());
			glm::dvec3 maximum(std::numeric_limits<double>::lowest());
			uint64_t triangleCount = 0;
			for (const auto& batch : geometry)
			{
				if (!IsValidAcousticMaterial(batch.Material))
					throw std::invalid_argument("invalid acoustic material");
				if (batch.Triangles.empty())
					continue;
				auto node = Impl::CreateGeometry(batch, glm::mat4(1.0f), false);
				minimum = glm::min(minimum, glm::dvec3(node.Min));
				maximum = glm::max(maximum, glm::dvec3(node.Max));
				triangleCount += node.Triangles;
				if (triangleCount > static_cast<uint64_t>(std::numeric_limits<int>::max()))
					throw std::invalid_argument("acoustic triangle count exceeds the scene limit");
				pending.push_back(std::move(node));
			}
			auto settings = m_Impl->World->settings();
			if (triangleCount)
			{
				if (!SetBounds(settings, minimum - glm::dvec3(kWorldBoundsPadding), maximum + glm::dvec3(kWorldBoundsPadding)))
					return false;
			}
			else
				settings.Bounds = { asterwave::Vec3(-kDefaultWorldHalfExtent), asterwave::Vec3(kDefaultWorldHalfExtent) };
			for (const auto& [id, node] : m_Impl->GeometryNodes)
				if (!ExpandGeometryBounds(settings, node.Transform, node.Min, node.Max))
					return false;
			for (const auto& node : pending)
			{
				m_Impl->World->add_primitive(node.Primitive);
				++attached;
			}
			m_Impl->SetWorldSettings(settings);
			for (const auto& node : m_Impl->StaticPrimitives)
				m_Impl->World->remove_primitive(node.Primitive->id);
			m_Impl->StaticPrimitives = std::move(pending);
			m_Impl->StaticTriangleCount = static_cast<int>(triangleCount);
			return true;
		}
		catch (const std::exception& error)
		{
			for (size_t i = 0; i < attached; ++i)
				m_Impl->World->remove_primitive(pending[i].Primitive->id);
			LUX_CORE_ERROR_TAG("Audio", "Cannot replace Asterwave static geometry: {0}", error.what());
			return false;
		}
	}

	bool RaytracedAudioScene::SetGeometry(UUID id, const AcousticGeometry& geometry, const glm::mat4& transform, bool dynamic)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		WaitForResults();
		if (!m_Impl->World || id == 0)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot set acoustic geometry {0}: world is stopped or ID is invalid", static_cast<uint64_t>(id));
			return false;
		}
		int attachedID = -1;
		try
		{
			auto node = Impl::CreateGeometry(geometry, transform, dynamic);
			auto settings = m_Impl->World->settings();
			if (!ExpandGeometryBounds(settings, transform, node.Min, node.Max))
				return false;
			m_Impl->World->add_primitive(node.Primitive);
			attachedID = node.Primitive->id;
			m_Impl->SetWorldSettings(settings);
			auto found = m_Impl->GeometryNodes.find(id);
			if (found == m_Impl->GeometryNodes.end())
				m_Impl->GeometryNodes.emplace(id, std::move(node));
			else
			{
				m_Impl->World->remove_primitive(found->second.Primitive->id);
				found->second = std::move(node);
			}
			return true;
		}
		catch (const std::exception& error)
		{
			if (attachedID >= 0)
				m_Impl->World->remove_primitive(attachedID);
			LUX_CORE_ERROR_TAG("Audio", "Cannot replace acoustic geometry {0}: {1}", static_cast<uint64_t>(id), error.what());
			return false;
		}
	}

	bool RaytracedAudioScene::UpdateGeometry(UUID id, const glm::mat4& transform, AcousticMaterial material, bool dynamic)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		WaitForResults();
		const auto found = m_Impl->GeometryNodes.find(id);
		if (!m_Impl->World || found == m_Impl->GeometryNodes.end() || !IsValidAcousticMaterial(material) || !ValidGeometryTransform(transform))
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot update missing or invalid acoustic geometry {0}", static_cast<uint64_t>(id));
			return false;
		}
		auto& node = found->second;
		try
		{
			auto settings = m_Impl->World->settings();
			if (!ExpandGeometryBounds(settings, transform, node.Min, node.Max))
				return false;
			m_Impl->SetWorldSettings(settings);
			node.Primitive->transform = ToAsterwave(transform);
			node.Primitive->material_id = AcousticMaterialID(material);
			node.Transform = transform;
			node.Dynamic = dynamic;
			m_Impl->World->geometry_changed();
			return true;
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot update acoustic geometry {0}: {1}", static_cast<uint64_t>(id), error.what());
			return false;
		}
	}

	bool RaytracedAudioScene::RemoveGeometry(UUID id)
	{
		WaitForResults();
		const auto found = m_Impl->GeometryNodes.find(id);
		if (found == m_Impl->GeometryNodes.end())
			return true;
		m_Impl->World->remove_primitive(found->second.Primitive->id);
		m_Impl->GeometryNodes.erase(found);
		return true;
	}

	bool RaytracedAudioScene::GetGeometryTransform(UUID id, glm::mat4& transform) const
	{
		const auto found = m_Impl->GeometryNodes.find(id);
		if (found == m_Impl->GeometryNodes.end())
			return false;
		transform = found->second.Transform;
		return true;
	}

	void RaytracedAudioScene::SetListener(const glm::vec3& position, const glm::vec3& forward)
	{
		WaitForResults();
		if (!m_Impl->Listener)
			return;
		if (!Finite(position))
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot position acoustic listener: coordinates must be finite");
			return;
		}
		// Acoustic emission is omnidirectional. The engine owns the overlay camera separately.
		(void)forward;
		m_Impl->Listener->Position = ToAsterwave(position);
		m_Impl->ListenerPosition = position;
	}

	void RaytracedAudioScene::CreateEmitter(UUID entityID)
	{
		WaitForResults();
		if (!m_Impl->World || m_Impl->SourceEmitters.contains(entityID))
			return;
		try
		{
			auto entry = m_Impl->SourceEmitters.emplace(entityID, Impl::CreateNativeEmitter()).first;
			try
			{
				// Sources keep their zero ray budgets. The listener traces every target in one batch.
				m_Impl->World->add_emitter(entry->second);
				m_Impl->Listener->add_target(entry->second);
			}
			catch (...)
			{
				// add_emitter validates before attaching; a failure can precede world membership.
				for (const auto& attached : m_Impl->World->emitters())
					if (attached == entry->second)
						m_Impl->World->remove_emitter(entry->second);
				m_Impl->SourceEmitters.erase(entry);
				throw;
			}
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot create acoustic emitter for entity {0}: {1}", static_cast<uint64_t>(entityID), error.what());
		}
	}

	void RaytracedAudioScene::DestroyEmitter(UUID entityID)
	{
		// Entity destruction can run after OnUpdate in the scene's post-update queue.
		WaitForResults();
		const auto found = m_Impl->SourceEmitters.find(entityID);
		if (found == m_Impl->SourceEmitters.end())
			return;
		m_Impl->Listener->remove_target(found->second->id());
		m_Impl->World->remove_emitter(found->second);
		m_Impl->SourceEmitters.erase(found);
	}

	void RaytracedAudioScene::SetEmitterMaxVolume(UUID entityID, float maxVolume)
	{
		WaitForResults();
		const auto found = m_Impl->SourceEmitters.find(entityID);
		if (found == m_Impl->SourceEmitters.end())
			return;
		if (!std::isfinite(maxVolume) || maxVolume < 0.0f)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot set acoustic emitter {0} volume: value must be finite and nonnegative", static_cast<uint64_t>(entityID));
			return;
		}
		// FMOD accepts authored boosts above unity; the acoustic model stores a bounded energy fraction.
		found->second->MaxVolume = std::min(maxVolume, 1.0f);
	}

	void RaytracedAudioScene::SetEmitterPosition(UUID entityID, const glm::vec3& position)
	{
		WaitForResults();
		const auto found = m_Impl->SourceEmitters.find(entityID);
		if (found == m_Impl->SourceEmitters.end())
			return;
		if (!Finite(position))
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot position acoustic emitter {0}: coordinates must be finite", static_cast<uint64_t>(entityID));
			return;
		}
		found->second->Position = ToAsterwave(position);
	}

	RaytracedAudioResult RaytracedAudioScene::GetResult(UUID entityID) const
	{
		RaytracedAudioResult result;
		const auto found = m_Impl->SourceEmitters.find(entityID);
		if (found == m_Impl->SourceEmitters.end() || !m_Impl->Listener || found->second->Initialising ||
			found->second->PendingRemoval || m_Impl->Listener->Initialising)
			return result;
		if (const auto filter = m_Impl->Listener->get_target_filter(found->second->id()))
		{
			result.OcclusionGainLF = filter->GainLF;
			result.OcclusionGainHF = filter->GainHF;
			result.Valid = true;
		}
		return result;
	}

	RaytracedAudioAmbience RaytracedAudioScene::GetAmbience() const
	{
		RaytracedAudioAmbience ambience;
		if (!m_Impl->Listener || m_Impl->Listener->Initialising)
			return ambience;
		if (const auto& filter = m_Impl->Listener->AmbientFilter)
		{
			ambience.AmbientGainLF = filter->GainLF;
			ambience.AmbientGainHF = filter->GainHF;
			ambience.Valid = true;
		}
		if (const auto& processed = m_Impl->Listener->Processed)
		{
			ambience.ReturnedPercent = processed->ReturnedPercent;
			ambience.OutsidePercent = processed->OutsidePercent;
			ambience.MeasuredDecayTimeLF = processed->MeasuredDecayTimeLF;
			ambience.MeasuredDecayTimeHF = processed->MeasuredDecayTimeHF;
			ambience.MaterialRoughness = processed->MaterialRoughness;
			ambience.MaterialAbsorptionLF = processed->MaterialAbsorptionLF;
			ambience.MaterialAbsorptionHF = processed->MaterialAbsorptionHF;
			ambience.Valid = true;
		}
		if (const auto& eax = m_Impl->Listener->EAX)
		{
			auto& reverb = ambience.Reverb;
			reverb.Valid = true;
			reverb.Gain = eax->Gain;
			reverb.GainLF = eax->GainLF;
			reverb.GainHF = eax->GainHF;
			reverb.DecayTime = eax->DecayTime;
			reverb.DecayLFRatio = eax->DecayLFRatio;
			reverb.DecayHFRatio = eax->DecayHFRatio;
			reverb.ReflectionsGain = eax->ReflectionsGain;
			reverb.ReflectionsDelay = eax->ReflectionsDelay;
			reverb.LateReverbGain = eax->LateReverbGain;
			reverb.LateReverbDelay = eax->LateReverbDelay;
			reverb.Density = eax->Density;
			reverb.Diffusion = eax->Diffusion;
			reverb.EchoTime = eax->EchoTime;
			reverb.EchoDepth = eax->EchoDepth;
			reverb.ModulationTime = eax->ModulationTime;
			reverb.ModulationDepth = eax->ModulationDepth;
			reverb.AirAbsorptionGainHF = eax->AirAbsorptionGainHF;
			reverb.HFReference = eax->HFReference;
			reverb.LFReference = eax->LFReference;
			reverb.RoomRolloffFactor = eax->RoomRolloffFactor;
			reverb.DecayHFLimit = eax->DecayHFLimit != 0;
			ambience.Valid = true;
		}
		return ambience;
	}

	void RaytracedAudioScene::SetVisualisationEnabled(bool enabled, int rayCount, int bounceCount, int updateIntervalMs)
	{
		if (!m_Impl->Listener)
			return;
		const bool on = enabled && rayCount > 0 && bounceCount > 0;
		if (on && static_cast<uint64_t>(rayCount) * static_cast<uint64_t>(bounceCount) > static_cast<uint64_t>(std::numeric_limits<int>::max()))
		{
			LUX_CORE_ERROR_TAG("Audio", "Acoustic visualisation exceeds the supported bounce count");
			return;
		}
		// Overlay calls may arrive after submission. Apply these fields only before the next batch.
		m_Impl->DesiredVisualisation = on ? asterwave::RaySettings{ rayCount, bounceCount } : asterwave::RaySettings{};
		if (updateIntervalMs > 0)
			m_Impl->VisualisationIntervalMs = updateIntervalMs;
		if (!on)
			m_Impl->Visualisation = {};
	}

	void RaytracedAudioScene::GetVisualisation(RaytracedAudioVisualisation& visualisation) const
	{
		visualisation = m_Impl->Visualisation;
	}

	RaytracedAudioStats RaytracedAudioScene::GetStats() const
	{
		RaytracedAudioStats stats;
		stats.Available = true;
		stats.VersionMajor = 0;
		stats.VersionMinor = 2;
		stats.VersionPatch = 0;
#if defined(LUX_DIST)
		stats.ProductionBuild = true;
#endif
		if (!m_Impl->World)
			return stats;
		stats.Running = true;
		stats.ThreadsRunning = m_Impl->World->threads_running();
		stats.SourceEmitterCount = StatisticCount(m_Impl->SourceEmitters.size());
		stats.EmitterCount = stats.SourceEmitterCount + 1;
		stats.StaticPrimitiveCount = StatisticCount(m_Impl->StaticPrimitives.size());
		stats.StaticTriangleCount = m_Impl->StaticTriangleCount;
		for (const auto& [id, node] : m_Impl->GeometryNodes)
		{
			if (node.Dynamic)
			{
				++stats.DynamicPrimitiveCount;
				stats.DynamicTriangleCount = StatisticCount(static_cast<uint64_t>(stats.DynamicTriangleCount) + node.Triangles);
			}
			else
			{
				++stats.StaticPrimitiveCount;
				stats.StaticTriangleCount = StatisticCount(static_cast<uint64_t>(stats.StaticTriangleCount) + node.Triangles);
			}
		}
		const auto settings = m_Impl->World->settings();
		const auto& completed = m_Impl->CompletedStatistics;
		stats.RaysCastThisFrame = StatisticCount(completed.RaysCast);
		stats.WorkItemCount = StatisticCount(completed.WorkItems);
		stats.MaximumConcurrencyLevel = settings.MaximumConcurrencyLevel;
		stats.GroupedEAXCount = m_Impl->GroupedEAXCount;
		stats.VisualisationEnabled = m_Impl->DesiredVisualisation.enabled();
		stats.VisualisationRayCount = m_Impl->DesiredVisualisation.Count;
		stats.VisualisationBounceCount = m_Impl->DesiredVisualisation.Bounces;
		stats.MainThreadTimeMs = m_Impl->MainThreadTimeMs;
		stats.RaytracingTimeMs = completed.RaytracingMs;
		stats.PreparationTimeMs = completed.PreparationMs;
		stats.AnalysisTimeMs = completed.AnalysisMs;
		stats.LatencyMs = completed.LatencyMs;
		stats.WorldMin = FromAsterwave(settings.Bounds.lo);
		stats.WorldSize = FromAsterwave(settings.Bounds.hi - settings.Bounds.lo);
		stats.ListenerPosition = m_Impl->ListenerPosition;
		return stats;
	}
}
