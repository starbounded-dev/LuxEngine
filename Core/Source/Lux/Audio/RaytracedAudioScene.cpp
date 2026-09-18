#include "lpch.h"
#include "Lux/Audio/RaytracedAudioScene.h"

#include "vaudio.h"
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <unordered_map>

namespace Lux {


	namespace {

		// Meters. Padded onto the mirrored geometry's AABB so emitters slightly above/beside it
		// (e.g. a bird sound over open terrain) are still inside the world and get raytraced.
		constexpr float kWorldBoundsPadding = 50.0f;
		// Meters. Used only until the first SetStaticGeometry() call gives us a real AABB.
		constexpr float kDefaultWorldHalfExtent = 500.0f;

		// The vendored libvaudionative.so exports only functions (EXPORT_API), not the header's
		// extern VAMatrix globals - VA_MATRIX_IDENTITY/VA_MATRIX_EMPTY link-fail on Linux. Identity
		// is the same under any row/column-major convention, so build it locally instead.
		constexpr VAMatrix kIdentityMatrix = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };

		// A freshly created VAEmitter casts ZERO rays of every type - vaEmitterGetCastsAnyRays()
		// returns false until both a ray count and a bounce count are set, and an emitter that
		// casts nothing never produces an occlusion filter or reverb result. These are the values
		// from Vercidium's own getting-started example; they are the starting point for tuning
		// quality against CPU cost, not a hard requirement.
		//
		// All of them are set on the *listener*, which is the only ray caster (see the class
		// comment). Reverb rays measure the space the listener is in; occlusion and permeation rays
		// are cast toward each target source; the ambient pair measures how enclosed the listener
		// is, independent of any source.
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

		// Upper bound on the vaWorldUpdate/vaWorldWait pumps used to drain Vercidium's worker
		// threads during Stop(); one is normally enough (see the comment there).
		constexpr int kShutdownDrainMaxSpins = 1000;

		VAVector ToVA(const glm::vec3& v)
		{
			return vaVectorCreate(v.x, v.y, v.z);
		}

		glm::vec3 FromVA(const VAVector& v)
		{
			return { v.x, v.y, v.z };
		}

		bool ValidGeometryTransform(const glm::mat4& transform)
		{
			for (int c = 0; c < 4; ++c)
				for (int r = 0; r < 4; ++r)
					if (!std::isfinite(transform[c][r]))
						return false;
			const double determinant = glm::determinant(glm::dmat3(transform));
			return std::isfinite(determinant) && determinant != 0 && transform[0].w == 0 &&
				transform[1].w == 0 && transform[2].w == 0 && transform[3].w == 1;
		}

		// Validate in double precision before passing any transformed coordinates to VA.
		bool ExpandGeometryBounds(VAWorld* world, const glm::mat4& transform, const glm::vec3& localMin, const glm::vec3& localMax)
		{
			glm::dvec3 minimum = FromVA(vaWorldGetPosition(world));
			glm::dvec3 maximum = minimum + glm::dvec3(FromVA(vaWorldGetSize(world)));
			for (int i = 0; i < 8; ++i)
			{
				const glm::dvec3 local{ i & 1 ? localMax.x : localMin.x, i & 2 ? localMax.y : localMin.y, i & 4 ? localMax.z : localMin.z };
				const glm::dvec3 point(glm::dmat4(transform) * glm::dvec4(local, 1));
				minimum = glm::min(minimum, point - glm::dvec3(kWorldBoundsPadding));
				maximum = glm::max(maximum, point + glm::dvec3(kWorldBoundsPadding));
			}
			const glm::dvec3 size = maximum - minimum;
			for (int axis = 0; axis < 3; ++axis)
			{
				const double limit = std::numeric_limits<float>::max();
				if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) || !std::isfinite(size[axis]) ||
					std::abs(minimum[axis]) > limit || std::abs(maximum[axis]) > limit || size[axis] > limit || size[axis] <= 0)
				{
					LUX_CORE_ERROR_TAG("Audio", "Acoustic geometry exceeds representable VA world bounds");
					return false;
				}
			}
			const auto positioned = vaWorldSetPosition(world, ToVA(glm::vec3(minimum)));
			const auto sized = vaWorldSetSize(world, ToVA(glm::vec3(size)));
			if ((positioned != VA_SUCCESS && positioned != VA_UNCHANGED) || (sized != VA_SUCCESS && sized != VA_UNCHANGED))
			{
				LUX_CORE_ERROR_TAG("Audio", "Cannot expand acoustic world bounds (position={}, size={})", positioned, sized);
				return false;
			}
			return true;
		}

	}

	bool RaytracedAudioScene::IsAvailable() { return true; }

	namespace {

		// Visualisation bounces are produced on Vercidium's worker threads and are only valid for
		// the duration of the callback, so they are copied out under this lock and copied again by
		// whoever renders them. File-local rather than a member of the private Impl because
		// vaEmitterSetVisualisationCallback takes a plain C function pointer with no user-data
		// parameter — the emitter's user data is the only route back, and it must point at
		// something this callback can name.
		struct VisualisationState
		{
			mutable std::mutex Mutex;
			std::vector<RaytracedAudioBounce> Bounces;
			glm::vec3 Origin{ 0.0f };
			int RayCount = 0;
			int BounceCount = 0;
			bool Enabled = false;
		};

		void VisualisationCallback(VAEmitter* emitter, VAVisualisationData* data, int count)
		{
			auto* state = (VisualisationState*)vaEmitterGetUserData(emitter);
			if (!state || !data || count <= 0)
				return;

			// The lock is held for a copy of a few thousand floats. Workers contend only with each
			// other and with one per-frame read; nothing here blocks on rendering.
			std::scoped_lock lock(state->Mutex);

			state->Bounces.resize((size_t)count);
			for (int i = 0; i < count; i++)
			{
				state->Bounces[i].Position = FromVA(data[i].position);
				state->Bounces[i].Normal = FromVA(data[i].normal);
			}
			state->Origin = FromVA(vaEmitterGetPosition(emitter));

			// The SDK hands back a flat array with no ray/bounce grouping. It is ray-major over
			// rayCount * bounceCount, so derive the stride from what actually arrived rather than
			// trusting the configured counts — a short delivery under the configured stride would
			// splice unrelated rays into one polyline.
			const int configuredBounces = vaEmitterGetVisualisationBounceCount(emitter);
			if (configuredBounces > 0 && count % configuredBounces == 0)
			{
				state->BounceCount = configuredBounces;
				state->RayCount = count / configuredBounces;
			}
			else
			{
				// Draw each bounce as its own segment from the origin instead of as a connected
				// path. Visibly different from a real ray path, but never misleading.
				state->BounceCount = 1;
				state->RayCount = count;
			}
		}

	}

	struct RaytracedAudioScene::Impl
	{
		struct Geometry
		{
			VAMeshPrimitive* Primitive = nullptr;
			glm::vec3 Min{ 0 }, Max{ 0 };
			int Triangles = 0;
			bool Dynamic = false;
		};
		std::unordered_map<UUID, Geometry> GeometryNodes;
		VAWorld* World = nullptr;
		VAEmitter* Listener = nullptr;
		std::unordered_map<UUID, VAEmitter*> SourceEmitters;
		std::vector<VAMeshPrimitive*> StaticPrimitives;
		// The SDK offers no way to read a primitive's triangle count back, so record what we
		// mirrored into it - the editor uses this to tell "no geometry" apart from "geometry the
		// simulation is ignoring".
		int StaticTriangleCount = 0;

		VisualisationState Visualisation;
	};

	RaytracedAudioScene::RaytracedAudioScene(Scene* scene)
		: m_Scene(scene), m_Impl(CreateScope<Impl>())
	{
	}

	bool RaytracedAudioScene::IsRunning() const
	{
		return m_Impl->World != nullptr;
	}

	RaytracedAudioScene::~RaytracedAudioScene()
	{
		Stop();
	}

	void RaytracedAudioScene::Start(const AcousticMaterialSettings& materials)
	{
		LUX_PROFILE_FUNCTION_AUTO;

		if (m_Impl->World)
			return;

		m_Impl->World = vaWorldCreate();
		if (!ConfigureAcousticMaterials(m_Impl->World, materials))
		{
			if (m_Impl->World)
				vaWorldDestroy(m_Impl->World);
			m_Impl->World = nullptr;
			return;
		}
		vaWorldSetPosition(m_Impl->World, vaVectorCreateUniform(-kDefaultWorldHalfExtent));
		vaWorldSetSize(m_Impl->World, vaVectorCreateUniform(kDefaultWorldHalfExtent * 2.0f));

		// The listener is the only ray caster in the world (see the class comment): reverb rays
		// measure the space it is in, occlusion and permeation rays are cast toward each target
		// source, and the ambient pair measures how enclosed it is regardless of any source.
		m_Impl->Listener = vaEmitterCreate();
		vaEmitterSetName(m_Impl->Listener, "LuxListener");
		vaEmitterSetHasRelativeReverb(m_Impl->Listener, true);
		// The listener hears the grouped reverb rather than contributing to it; blending its own
		// reverb back in would double-count the space.
		vaEmitterSetAffectsGroupedEAX(m_Impl->Listener, false);

		vaEmitterSetReverbRayCount(m_Impl->Listener, kReverbRayCount);
		vaEmitterSetReverbBounceCount(m_Impl->Listener, kReverbBounceCount);
		vaEmitterSetOcclusionRayCount(m_Impl->Listener, kOcclusionRayCount);
		vaEmitterSetOcclusionBounceCount(m_Impl->Listener, kOcclusionBounceCount);
		vaEmitterSetPermeationRayCount(m_Impl->Listener, kPermeationRayCount);
		vaEmitterSetPermeationBounceCount(m_Impl->Listener, kPermeationBounceCount);
		vaEmitterSetAmbientOcclusionRayCount(m_Impl->Listener, kAmbientOcclusionRayCount);
		vaEmitterSetAmbientOcclusionBounceCount(m_Impl->Listener, kAmbientOcclusionBounceCount);
		vaEmitterSetAmbientPermeationRayCount(m_Impl->Listener, kAmbientPermeationRayCount);
		vaEmitterSetAmbientPermeationBounceCount(m_Impl->Listener, kAmbientPermeationBounceCount);

		// Routes the visualisation callback back to our snapshot buffer. Set unconditionally so
		// SetVisualisationEnabled only has to touch ray counts.
		vaEmitterSetUserData(m_Impl->Listener, &m_Impl->Visualisation);
		vaEmitterSetVisualisationCallback(m_Impl->Listener, VisualisationCallback);

		if (VAResult result = vaWorldAddEmitter(m_Impl->World, m_Impl->Listener); result != VA_SUCCESS)
			LUX_CORE_ERROR_TAG("Audio", "RaytracedAudioScene::Start failed to add the listener emitter to the world; no source will produce occlusion (VAResult={0})", result);
	}

	void RaytracedAudioScene::Stop()
	{
		LUX_PROFILE_FUNCTION_AUTO;

		if (!m_Impl->World)
			return;

		// Stop the background raytracing threads submitting new work, then wait for the ones
		// already in flight to drain. vaudio.h states the precondition explicitly: "When
		// vaWorldGetThreadsRunning() returns false, it is safe to call vaWorldDestroy()".
		// Tearing down without waiting segfaults on a Vercidium worker thread (it jumps through
		// a function pointer belonging to the world we just freed); in a host process that
		// surfaces later as glibc heap corruption in an unrelated allocation. One update is
		// normally enough to drain - the bound only stops a wedged worker hanging the editor.
		// Stop new visualisation bounces being delivered before anything is torn down, so the drain
		// below is the only thing the workers are still finishing.
		if (m_Impl->Listener)
		{
			vaEmitterSetVisualisationRayCount(m_Impl->Listener, 0);
			vaEmitterSetVisualisationCallback(m_Impl->Listener, nullptr);
		}

		// Join whatever the last OnUpdate kicked off, unconditionally and before anything else.
		// OnUpdate only calls vaWorldUpdate and never waits - that is the point of an asynchronous
		// simulation - so on entry here a batch is typically still in flight or not yet picked up.
		// In the latter case vaWorldGetThreadsRunning() answers false, the drain loop below is
		// skipped entirely, and the world is destroyed out from under work that is about to start.
		vaWorldWait(m_Impl->World);

		vaWorldSetPendingShutdown(m_Impl->World, true);
		for (int spin = 0; spin < kShutdownDrainMaxSpins && vaWorldGetThreadsRunning(m_Impl->World); spin++)
		{
			vaWorldUpdate(m_Impl->World);
			vaWorldWait(m_Impl->World);
		}

		if (vaWorldGetThreadsRunning(m_Impl->World))
			LUX_CORE_ERROR_TAG("Audio", "RaytracedAudioScene::Stop: Vercidium worker threads still running after {0} drain attempts; destroying the world anyway may crash", kShutdownDrainMaxSpins);

		// Every source is a target of the listener, and the listener outlives them in this teardown
		// order. Destroying a source while the listener still lists it as a target leaves a dangling
		// entry that vaWorldDestroy walks - which segfaults inside the SDK, on the main thread, with
		// nothing in the trace pointing back here. Deregister first, exactly as DestroyEmitter does.
		//
		// vaWorldRemoveEmitter can then answer VA_PENDING_REMOVAL: the world keeps the emitter alive
		// until its reverb tail finishes. Destroying it here regardless leaves the world holding a
		// freed pointer, which it frees again from vaWorldDestroy below - a double free. Only
		// destroy what actually came back out; vaWorldDestroy releases whatever the world still
		// owns along with itself.
		for (auto& [entityID, emitter] : m_Impl->SourceEmitters)
		{
			if (m_Impl->Listener)
				vaEmitterRemoveTarget(m_Impl->Listener, emitter);

			if (vaWorldRemoveEmitter(m_Impl->World, emitter) == VA_SUCCESS)
				vaEmitterDestroy(emitter);
		}
		m_Impl->SourceEmitters.clear();

		for (auto& [id, node] : m_Impl->GeometryNodes)
		{
			const auto result = vaWorldRemovePrimitive_(m_Impl->World, node.Primitive);
			if (result == VA_SUCCESS)
			{
				if (const auto destroyed = vaMeshPrimitiveDestroy(node.Primitive); destroyed != VA_SUCCESS)
					LUX_CORE_ERROR_TAG("Audio", "Cannot release live acoustic geometry {} during teardown (VAResult={})", static_cast<uint64_t>(id), destroyed);
			}
			else
				LUX_CORE_ERROR_TAG("Audio", "Cannot remove live acoustic geometry {} during teardown (VAResult={})", static_cast<uint64_t>(id), result);
		}
		m_Impl->GeometryNodes.clear();
		for (VAMeshPrimitive* primitive : m_Impl->StaticPrimitives)
		{
			if (vaWorldRemovePrimitive_(m_Impl->World, primitive) == VA_SUCCESS)
				vaMeshPrimitiveDestroy(primitive);
		}
		m_Impl->StaticPrimitives.clear();
		m_Impl->StaticTriangleCount = 0;

		if (m_Impl->Listener)
		{
			if (vaWorldRemoveEmitter(m_Impl->World, m_Impl->Listener) == VA_SUCCESS)
				vaEmitterDestroy(m_Impl->Listener);
			m_Impl->Listener = nullptr;
		}

		// Waits for the background threads, then frees the world and anything still attached.
		vaWorldDestroy(m_Impl->World);
		m_Impl->World = nullptr;
	}

	double RaytracedAudioScene::GetRaytracingTimeMilliseconds() const
	{
		return m_Impl->World ? vaWorldGetRaytracingTime(m_Impl->World) : 0;
	}

	void RaytracedAudioScene::WaitForResults()
	{
		LUX_PROFILE_FUNCTION_AUTO;

		if (!m_Impl->World)
			return;

		// Joins the batch OnUpdate kicked last frame. Everything the caller does between this and
		// the next OnUpdate - moving emitters, reading filters and reverb - then runs with no
		// Vercidium worker touching the world. This is a wait on work that has had a full frame to
		// finish, not a synchronous raytrace: by the time the caller gets here the batch is
		// normally already complete and this returns immediately.
		vaWorldWait(m_Impl->World);
	}

	void RaytracedAudioScene::OnUpdate(Timestep ts)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		(void)ts;

		if (!m_Impl->World)
			return;

		// Kicks the next batch and returns without waiting - the results land by the next frame's
		// WaitForResults. Nothing may read results or move emitters after this point in the frame.
		vaWorldUpdate(m_Impl->World);
	}

	bool RaytracedAudioScene::SetStaticGeometry(const std::vector<AcousticGeometry>& geometry)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!m_Impl->World)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot mirror acoustic geometry: VA world is not running");
			return false;
		}

		glm::vec3 minBounds(std::numeric_limits<float>::max());
		glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
		size_t triangleCount = 0;
		for (const auto& batch : geometry)
		{
			if (!IsValidAcousticMaterial(batch.Material) || batch.Triangles.size() % 3 != 0
				|| batch.Triangles.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
			{
				LUX_CORE_ERROR_TAG("Audio", "Invalid acoustic geometry batch: check material ID and triangle count");
				return false;
			}
			triangleCount += batch.Triangles.size() / 3;
			for (const auto& vertex : batch.Triangles)
			{
				if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z))
				{
					LUX_CORE_ERROR_TAG("Audio", "Acoustic geometry contains a non-finite vertex");
					return false;
				}
				minBounds = glm::min(minBounds, vertex);
				maxBounds = glm::max(maxBounds, vertex);
			}
		}
		if (triangleCount > static_cast<size_t>(std::numeric_limits<int>::max()))
		{
			LUX_CORE_ERROR_TAG("Audio", "Acoustic geometry exceeds VA's triangle limit");
			return false;
		}

		std::vector<VAMeshPrimitive*> pending;
		const auto release = [this](auto& primitives)
		{
			for (auto* primitive : primitives)
			{
				const VAResult result = vaWorldRemovePrimitive_(m_Impl->World, primitive);
				if (result == VA_SUCCESS)
					vaMeshPrimitiveDestroy(primitive);
				else
					LUX_CORE_ERROR_TAG("Audio", "Cannot remove acoustic primitive (VAResult={0}); ownership remains with VA", result);
			}
			primitives.clear();
		};
		for (const auto& batch : geometry)
		{
			if (batch.Triangles.empty())
				continue;
			glm::vec3 batchMin(std::numeric_limits<float>::max());
			glm::vec3 batchMax(std::numeric_limits<float>::lowest());
			std::vector<VAVector> vertices;
			vertices.reserve(batch.Triangles.size());
			for (const auto& vertex : batch.Triangles)
			{
				vertices.push_back(ToVA(vertex));
				batchMin = glm::min(batchMin, vertex);
				batchMax = glm::max(batchMax, vertex);
			}
			VAMeshPrimitive* primitive = nullptr;
			const auto material = static_cast<VAMaterialType>(AcousticMaterialVAID(batch.Material));
			const VAResult created = vaMeshPrimitiveCreate(material, vertices.data(), static_cast<int>(vertices.size()),
				ToVA(batchMin), ToVA(batchMax), &kIdentityMatrix, &primitive);
			if (created != VA_SUCCESS || !primitive)
			{
				LUX_CORE_ERROR_TAG("Audio", "Cannot create acoustic mesh for {0} (VAResult={1})", AcousticMaterialName(batch.Material), created);
				release(pending);
				return false;
			}
			const VAResult added = vaWorldAddPrimitive_(m_Impl->World, primitive);
			if (added != VA_SUCCESS)
			{
				LUX_CORE_ERROR_TAG("Audio", "Cannot add acoustic mesh for {0} (VAResult={1})", AcousticMaterialName(batch.Material), added);
				vaMeshPrimitiveDestroy(primitive);
				release(pending);
				return false;
			}
			pending.push_back(primitive);
		}
		release(m_Impl->StaticPrimitives);
		m_Impl->StaticPrimitives = std::move(pending);
		m_Impl->StaticTriangleCount = static_cast<int>(triangleCount);
		const glm::vec3 paddedMin = triangleCount ? minBounds - kWorldBoundsPadding : glm::vec3(-kDefaultWorldHalfExtent);
		const glm::vec3 size = triangleCount ? maxBounds - minBounds + 2.0f * kWorldBoundsPadding : glm::vec3(2.0f * kDefaultWorldHalfExtent);
		const VAResult positioned = vaWorldSetPosition(m_Impl->World, ToVA(paddedMin));
		const VAResult resized = vaWorldSetSize(m_Impl->World, ToVA(size));
		if ((positioned != VA_SUCCESS && positioned != VA_UNCHANGED) || (resized != VA_SUCCESS && resized != VA_UNCHANGED))
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot update acoustic world bounds (position={0}, size={1})", positioned, resized);
			return false;
		}
		return true;
	}

	bool RaytracedAudioScene::SetGeometry(UUID id, const AcousticGeometry& geometry, const glm::mat4& transform, bool dynamic)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!m_Impl->World || id == 0 || !IsValidAcousticMaterial(geometry.Material) || geometry.Triangles.empty() ||
			geometry.Triangles.size() % 3 || geometry.Triangles.size() > static_cast<size_t>(INT_MAX))
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid live acoustic geometry {}", static_cast<uint64_t>(id));
			return false;
		}
		VAMatrix matrix;
		std::memcpy(&matrix, glm::value_ptr(transform), sizeof(matrix));
		if (!ValidGeometryTransform(transform))
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid acoustic geometry transform {}", static_cast<uint64_t>(id));
			return false;
		}
		Impl::Geometry node;
		node.Min = glm::vec3(std::numeric_limits<float>::max());
		node.Max = glm::vec3(std::numeric_limits<float>::lowest());
		std::vector<VAVector> vertices;
		vertices.reserve(geometry.Triangles.size());
		for (const auto& position : geometry.Triangles)
		{
			if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
			{
				LUX_CORE_ERROR_TAG("Audio", "Non-finite acoustic vertex on geometry {}", static_cast<uint64_t>(id));
				return false;
			}
			node.Min = glm::min(node.Min, position);
			node.Max = glm::max(node.Max, position);
			vertices.push_back(ToVA(position));
		}
		if (!ExpandGeometryBounds(m_Impl->World, transform, node.Min, node.Max))
			return false;
		node.Triangles = static_cast<int>(vertices.size() / 3);
		node.Dynamic = dynamic;
		auto result = vaMeshPrimitiveCreate(static_cast<VAMaterialType>(AcousticMaterialVAID(geometry.Material)), vertices.data(),
			static_cast<int>(vertices.size()), ToVA(node.Min), ToVA(node.Max), &matrix, &node.Primitive);
		if (result != VA_SUCCESS || !node.Primitive)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot create live acoustic geometry {} (VAResult={})", static_cast<uint64_t>(id), result);
			return false;
		}
		result = vaWorldAddPrimitive_(m_Impl->World, node.Primitive);
		if (result != VA_SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot attach live acoustic geometry {} (VAResult={})", static_cast<uint64_t>(id), result);
			if (const auto destroyed = vaMeshPrimitiveDestroy(node.Primitive); destroyed != VA_SUCCESS)
				LUX_CORE_ERROR_TAG("Audio", "Cannot release unattached acoustic geometry {} (VAResult={})", static_cast<uint64_t>(id), destroyed);
			return false;
		}
		if (!RemoveGeometry(id))
		{
			if (vaWorldRemovePrimitive_(m_Impl->World, node.Primitive) == VA_SUCCESS)
				if (const auto destroyed = vaMeshPrimitiveDestroy(node.Primitive); destroyed != VA_SUCCESS)
				LUX_CORE_ERROR_TAG("Audio", "Cannot release unattached acoustic geometry {} (VAResult={})", static_cast<uint64_t>(id), destroyed);
			return false;
		}
		m_Impl->GeometryNodes.emplace(id, node);
		return true;
	}

	bool RaytracedAudioScene::UpdateGeometry(UUID id, const glm::mat4& transform, AcousticMaterial material, bool dynamic)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		const auto found = m_Impl->GeometryNodes.find(id);
		if (!m_Impl->World || found == m_Impl->GeometryNodes.end() || !IsValidAcousticMaterial(material))
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot update missing or invalid acoustic geometry {}", static_cast<uint64_t>(id));
			return false;
		}
		VAMatrix matrix;
		std::memcpy(&matrix, glm::value_ptr(transform), sizeof(matrix));
		if (!ValidGeometryTransform(transform))
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid acoustic geometry transform {}", static_cast<uint64_t>(id));
			return false;
		}
		auto& node = found->second;
		if (!ExpandGeometryBounds(m_Impl->World, transform, node.Min, node.Max))
			return false;
		const auto moved = vaMeshPrimitiveSetTransform(node.Primitive, &matrix);
		const auto tagged = vaMeshPrimitiveSetMaterial(node.Primitive, static_cast<VAMaterialType>(AcousticMaterialVAID(material)));
		if ((moved != VA_SUCCESS && moved != VA_UNCHANGED) || (tagged != VA_SUCCESS && tagged != VA_UNCHANGED))
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot update acoustic geometry {} (transform={}, material={})", static_cast<uint64_t>(id), moved, tagged);
			return false;
		}
		node.Dynamic = dynamic;
		return true;
	}

	bool RaytracedAudioScene::RemoveGeometry(UUID id)
	{
		const auto found = m_Impl->GeometryNodes.find(id);
		if (found == m_Impl->GeometryNodes.end())
			return true;
		const auto removed = vaWorldRemovePrimitive_(m_Impl->World, found->second.Primitive);
		if (removed != VA_SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot remove acoustic geometry {} (VAResult={})", static_cast<uint64_t>(id), removed);
			return false;
		}
		const auto destroyed = vaMeshPrimitiveDestroy(found->second.Primitive);
		m_Impl->GeometryNodes.erase(found);
		if (destroyed != VA_SUCCESS)
			LUX_CORE_ERROR_TAG("Audio", "Cannot release detached acoustic geometry {} (VAResult={})", static_cast<uint64_t>(id), destroyed);
		return destroyed == VA_SUCCESS;
	}

	bool RaytracedAudioScene::GetGeometryTransform(UUID id, glm::mat4& transform) const
	{
		const auto found = m_Impl->GeometryNodes.find(id);
		if (found == m_Impl->GeometryNodes.end())
			return false;
		const auto* matrix = vaMeshPrimitiveGetTransform(found->second.Primitive);
		if (!matrix)
			return false;
		std::memcpy(glm::value_ptr(transform), matrix, sizeof(VAMatrix));
		return true;
	}

	void RaytracedAudioScene::SetListener(const glm::vec3& position, const glm::vec3& forward)
	{
		if (!m_Impl->World || !m_Impl->Listener)
			return;

		vaEmitterSetPosition(m_Impl->Listener, ToVA(position));

		const glm::vec3 dir = glm::normalize(forward);
		const float pitch = std::asin(std::clamp(dir.y, -1.0f, 1.0f));
		const float yaw = std::atan2(-dir.x, -dir.z);

		vaWorldSetCameraPosition(m_Impl->World, ToVA(position));
		vaWorldSetCameraPitch(m_Impl->World, pitch);
		vaWorldSetCameraYaw(m_Impl->World, yaw);
	}

	void RaytracedAudioScene::CreateEmitter(UUID entityID)
	{
		if (!m_Impl->World || m_Impl->SourceEmitters.contains(entityID))
			return;

		// A source is a pure target: it casts nothing itself and is raytraced *by* the listener, so
		// its ray counts stay at the SDK's zero defaults. Adding a source therefore costs one more
		// target for the listener's existing ray budget, not a second budget of its own.
		VAEmitter* emitter = vaEmitterCreate();

		if (VAResult result = vaWorldAddEmitter(m_Impl->World, emitter); result != VA_SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Audio", "RaytracedAudioScene::CreateEmitter failed to add emitter for entity {0} to the world (VAResult={1})", (uint64_t)entityID, result);
			vaEmitterDestroy(emitter);
			return;
		}

		// vaEmitterAddTarget requires the target to already be in the same world, which the call
		// above guarantees.
		if (m_Impl->Listener)
		{
			if (VAResult result = vaEmitterAddTarget(m_Impl->Listener, emitter); result != VA_SUCCESS)
				LUX_CORE_ERROR_TAG("Audio", "RaytracedAudioScene::CreateEmitter failed to target entity {0} from the listener; it will produce no occlusion (VAResult={1})", (uint64_t)entityID, result);
		}

		m_Impl->SourceEmitters[entityID] = emitter;
	}

	void RaytracedAudioScene::DestroyEmitter(UUID entityID)
	{
		auto it = m_Impl->SourceEmitters.find(entityID);
		if (it == m_Impl->SourceEmitters.end())
			return;

		// Drop the listener's target first: leaving it registered would have the listener keep
		// casting occlusion rays at an emitter that is on its way out of the world.
		if (m_Impl->Listener)
			vaEmitterRemoveTarget(m_Impl->Listener, it->second);

		// As in Stop(): a VA_PENDING_REMOVAL emitter is still owned by the world (its reverb tail
		// is still playing), so dropping our handle is all we may safely do - the world frees it.
		if (m_Impl->World && vaWorldRemoveEmitter(m_Impl->World, it->second) == VA_SUCCESS)
			vaEmitterDestroy(it->second);

		m_Impl->SourceEmitters.erase(it);
	}

	void RaytracedAudioScene::SetEmitterMaxVolume(UUID entityID, float maxVolume)
	{
		auto it = m_Impl->SourceEmitters.find(entityID);
		if (it == m_Impl->SourceEmitters.end())
			return;

		vaEmitterSetMaxVolume(it->second, maxVolume);
	}

	void RaytracedAudioScene::SetEmitterPosition(UUID entityID, const glm::vec3& position)
	{
		auto it = m_Impl->SourceEmitters.find(entityID);
		if (it == m_Impl->SourceEmitters.end())
			return;

		vaEmitterSetPosition(it->second, ToVA(position));
	}

	RaytracedAudioResult RaytracedAudioScene::GetResult(UUID entityID) const
	{
		RaytracedAudioResult result;

		auto it = m_Impl->SourceEmitters.find(entityID);
		if (it == m_Impl->SourceEmitters.end() || !m_Impl->Listener)
			return result;

		VAEmitter* emitter = it->second;

		// An emitter added to the world this frame has not been through a vaWorldUpdate yet, so
		// its result buffers do not exist and the accessors below dereference them regardless -
		// querying one that is still initialising segfaults inside the SDK rather than returning
		// NULL. Scene::OnUpdateRuntime creates emitters and reads results in the same pass, so
		// this is hit on the first frame of every newly-added source.
		if (vaEmitterGetInitialising(emitter) || vaEmitterGetPendingRemoval(emitter))
			return result;

		if (vaEmitterGetInitialising(m_Impl->Listener))
			return result;

		// The listener is the caster, so the filter is looked up caster-first: this is the filter
		// the listener computed for hearing that source.
		if (const VALowPassFilter* filter = vaEmitterGetTargetFilter(m_Impl->Listener, emitter))
		{
			result.OcclusionGainLF = filter->gainLF;
			result.OcclusionGainHF = filter->gainHF;
			result.Valid = true;
		}

		return result;
	}

	RaytracedAudioAmbience RaytracedAudioScene::GetAmbience() const
	{
		RaytracedAudioAmbience ambience;

		if (!m_Impl->World || !m_Impl->Listener || vaEmitterGetInitialising(m_Impl->Listener))
			return ambience;

		if (const VALowPassFilter* filter = vaEmitterGetAmbientFilter(m_Impl->Listener))
		{
			ambience.AmbientGainLF = filter->gainLF;
			ambience.AmbientGainHF = filter->gainHF;
			ambience.Valid = true;
		}

		if (const VAProcessedReverb* processed = vaEmitterGetProcessedReverb(m_Impl->Listener))
		{
			ambience.ReturnedPercent = processed->returnedPercent;
			ambience.OutsidePercent = processed->outsidePercent;
			ambience.MeasuredDecayTimeLF = processed->measuredDecayTimeLF;
			ambience.MeasuredDecayTimeHF = processed->measuredDecayTimeHF;
			ambience.MaterialRoughness = processed->materialRoughness;
			ambience.MaterialAbsorptionLF = processed->materialAbsorptionLF;
			ambience.MaterialAbsorptionHF = processed->materialAbsorptionHF;
			ambience.Valid = true;
		}

		// The whole EAX set, copied field for field. This is the simulation's actual output: a
		// backend that applies only one or two of these is leaving the reverb character on the
		// table.
		if (const VAEAXReverb* eax = vaEmitterGetEAX(m_Impl->Listener))
		{
			RaytracedAudioReverb& reverb = ambience.Reverb;
			reverb.Valid = true;
			reverb.Gain = eax->gain;
			reverb.GainLF = eax->gainLF;
			reverb.GainHF = eax->gainHF;
			reverb.DecayTime = eax->decayTime;
			reverb.DecayLFRatio = eax->decayLFRatio;
			reverb.DecayHFRatio = eax->decayHFRatio;
			reverb.ReflectionsGain = eax->reflectionsGain;
			reverb.ReflectionsDelay = eax->reflectionsDelay;
			reverb.LateReverbGain = eax->lateReverbGain;
			reverb.LateReverbDelay = eax->lateReverbDelay;
			reverb.Density = eax->density;
			reverb.Diffusion = eax->diffusion;
			reverb.EchoTime = eax->echoTime;
			reverb.EchoDepth = eax->echoDepth;
			reverb.ModulationTime = eax->modulationTime;
			reverb.ModulationDepth = eax->modulationDepth;
			reverb.AirAbsorptionGainHF = eax->airAbsorptionGainHF;
			reverb.HFReference = eax->hfReference;
			reverb.LFReference = eax->lfReference;
			reverb.RoomRolloffFactor = eax->roomRolloffFactor;
			reverb.DecayHFLimit = eax->decayHFLimit != 0;
			ambience.Valid = true;
		}

		return ambience;
	}

	void RaytracedAudioScene::SetVisualisationEnabled(bool enabled, int rayCount, int bounceCount, int updateIntervalMs)
	{
		if (!m_Impl->Listener)
			return;

		const bool on = enabled && rayCount > 0 && bounceCount > 0;

		vaEmitterSetVisualisationRayCount(m_Impl->Listener, on ? rayCount : 0);
		vaEmitterSetVisualisationBounceCount(m_Impl->Listener, on ? bounceCount : 0);
		if (on && updateIntervalMs > 0)
			vaEmitterSetVisualisationUpdateFrequency(m_Impl->Listener, updateIntervalMs);

		std::scoped_lock lock(m_Impl->Visualisation.Mutex);
		m_Impl->Visualisation.Enabled = on;
		if (!on)
		{
			// Drop the last snapshot so a disabled overlay stops drawing immediately rather than
			// leaving the final frame of rays frozen in the viewport.
			m_Impl->Visualisation.Bounces.clear();
			m_Impl->Visualisation.RayCount = 0;
			m_Impl->Visualisation.BounceCount = 0;
		}
	}

	void RaytracedAudioScene::GetVisualisation(RaytracedAudioVisualisation& outVisualisation) const
	{
		{
			std::scoped_lock lock(m_Impl->Visualisation.Mutex);
			outVisualisation.Bounces = m_Impl->Visualisation.Bounces;
			outVisualisation.Origin = m_Impl->Visualisation.Origin;
			outVisualisation.RayCount = m_Impl->Visualisation.RayCount;
			outVisualisation.BounceCount = m_Impl->Visualisation.BounceCount;
		}

		if (!m_Impl->World)
			return;

		// A ray that hit nothing still occupies its slot in the array, filled with a far
		// out-of-world sentinel (observed as -99999 per component, but the SDK documents no
		// specific value). Classifying by world containment rather than by that magic number keeps
		// this correct if the sentinel ever changes, and costs one AABB test per bounce.
		const glm::vec3 worldMin = FromVA(vaWorldGetPosition(m_Impl->World));
		const glm::vec3 worldMax = worldMin + FromVA(vaWorldGetSize(m_Impl->World));

		for (RaytracedAudioBounce& bounce : outVisualisation.Bounces)
		{
			bounce.Hit = glm::all(glm::greaterThanEqual(bounce.Position, worldMin))
				&& glm::all(glm::lessThanEqual(bounce.Position, worldMax));
		}
	}

	RaytracedAudioStats RaytracedAudioScene::GetStats() const
	{
		RaytracedAudioStats stats;
		stats.Available = true;
		vaGetVersion(&stats.VersionMajor, &stats.VersionMinor, &stats.VersionPatch);
		stats.ProductionBuild = vaIsProduction();

		if (!m_Impl->World)
			return stats;

		stats.Running = true;
		stats.ThreadsRunning = vaWorldGetThreadsRunning(m_Impl->World);
		stats.EmitterCount = vaWorldGetEmitterCount(m_Impl->World);
		stats.SourceEmitterCount = (int)m_Impl->SourceEmitters.size();
		stats.StaticPrimitiveCount = (int)m_Impl->StaticPrimitives.size();
		stats.StaticTriangleCount = m_Impl->StaticTriangleCount;
		for (const auto& [id, node] : m_Impl->GeometryNodes)
		{
			if (node.Dynamic)
			{
				++stats.DynamicPrimitiveCount;
				stats.DynamicTriangleCount += node.Triangles;
			}
			else
			{
				++stats.StaticPrimitiveCount;
				stats.StaticTriangleCount += node.Triangles;
			}
		}
		stats.RaysCastThisFrame = vaWorldGetRaysCastThisFrame(m_Impl->World);
		stats.WorkItemCount = vaWorldGetWorkItemCount(m_Impl->World);
		stats.MaximumConcurrencyLevel = vaWorldGetMaximumConcurrencyLevel(m_Impl->World);
		stats.GroupedEAXCount = vaWorldGetGroupedEAXCount(m_Impl->World);

		if (m_Impl->Listener)
		{
			stats.VisualisationEnabled = vaEmitterGetVisualisationEnabled(m_Impl->Listener);
			stats.VisualisationRayCount = vaEmitterGetVisualisationRayCount(m_Impl->Listener);
			stats.VisualisationBounceCount = vaEmitterGetVisualisationBounceCount(m_Impl->Listener);
		}

		stats.MainThreadTimeMs = vaWorldGetMainThreadTime(m_Impl->World);
		stats.RaytracingTimeMs = vaWorldGetRaytracingTime(m_Impl->World);
		stats.PreparationTimeMs = vaWorldGetPreparationTime(m_Impl->World);
		stats.AnalysisTimeMs = vaWorldGetAnalysisTime(m_Impl->World);
		stats.LatencyMs = vaWorldGetLatency(m_Impl->World);

		stats.WorldMin = FromVA(vaWorldGetPosition(m_Impl->World));
		stats.WorldSize = FromVA(vaWorldGetSize(m_Impl->World));
		stats.ListenerPosition = FromVA(vaWorldGetCameraPosition(m_Impl->World));

		return stats;
	}


}
