#pragma once

#include "Lux/Core/Ref.h"
#include "AcousticMaterial.h"
#include "Lux/Core/Timestep.h"
#include "Lux/Core/UUID.h"

#include <vector>

namespace Lux {

	class Scene;

	struct AcousticGeometry
	{
		AcousticMaterial Material = AcousticMaterial::Default;
		std::vector<glm::vec3> Triangles;
	};

	// Per-source result of the ray-traced acoustic simulation, meant to drive a playback backend's
	// per-voice filter and reverb send. Stays at "no effect" defaults until raytracing has produced
	// at least one result for that source.
	//
	// Occlusion is two-band because that is what the simulation measures: a wall passes low
	// frequencies far better than high ones, so a single scalar would throw away the part that makes
	// occlusion audible as *muffling* rather than as a volume drop.
	struct RaytracedAudioResult
	{
		bool Valid = false;

		// Direct-path occlusion from the listener toward this source (1 = unoccluded).
		float OcclusionGainLF = 1.0f;
		float OcclusionGainHF = 1.0f;
	};

	// The full EAX / I3DL2 reverb parameter set the simulation derives from its echogram, in the
	// SDK's own units: seconds, Hz, and linear gains. Converting to a backend's units (FMOD wants
	// milliseconds, percentages and decibels) is the backend's job, not this struct's.
	//
	// Defaults are a dry, neutral room, so a backend that applies this before the first result
	// lands does not colour the mix.
	struct RaytracedAudioReverb
	{
		bool Valid = false;

		float Gain = 0.0f;                 // overall linear wet gain (0-1)
		float GainLF = 1.0f;               // low-frequency reverb gain (0-1)
		float GainHF = 1.0f;               // high-frequency reverb gain (0-1)

		float DecayTime = 1.5f;            // mid-frequency decay, seconds (0.1-20)
		float DecayLFRatio = 1.0f;         // LF decay / mid decay (0.1-2)
		float DecayHFRatio = 1.0f;         // HF decay / mid decay (0.1-2)

		float ReflectionsGain = 0.0f;      // early reflections, linear (0-3.16)
		float ReflectionsDelay = 0.0f;     // seconds (0-0.3)
		float LateReverbGain = 0.0f;       // late reverberation, linear (0-10)
		float LateReverbDelay = 0.0f;      // seconds after early reflections (0-0.1)

		float Density = 1.0f;              // modal density (0-1)
		float Diffusion = 1.0f;            // echo diffusion (0-1)

		float EchoTime = 0.25f;            // seconds (0.075-0.25)
		float EchoDepth = 0.0f;            // (0-1)
		float ModulationTime = 0.25f;      // seconds (0.04-4)
		float ModulationDepth = 0.0f;      // (0-1)

		float AirAbsorptionGainHF = 1.0f;  // linear gain per meter (0.892-1)
		float HFReference = 5000.0f;       // Hz (1000-20000)
		float LFReference = 250.0f;        // Hz (20-1000)
		float RoomRolloffFactor = 0.0f;    // (0-10)
		bool DecayHFLimit = false;
	};

	// Listener-global acoustics: the reverb of the space the listener occupies and how enclosed that
	// space is. Produced by the listener emitter's reverb and ambient ray types.
	//
	// Reverb is a property of the space, not of any one source, which is why it lives here rather
	// than in RaytracedAudioResult. Sources feed this reverb through their individual send levels.
	struct RaytracedAudioAmbience
	{
		bool Valid = false;

		// Non-directional muffling of the space itself, from ambient occlusion/permeation rays.
		float AmbientGainLF = 1.0f;
		float AmbientGainHF = 1.0f;

		// Measured energy split from the echogram.
		float ReturnedPercent = 0.0f;      // energy that came back to the listener
		float OutsidePercent = 0.0f;       // energy that escaped the world (outdoors-ness)
		float MeasuredDecayTimeLF = 0.0f;  // seconds, RT20/RT30/RT60 from the echogram
		float MeasuredDecayTimeHF = 0.0f;
		float MaterialRoughness = 0.0f;    // average scattering of surfaces the rays hit
		float MaterialAbsorptionLF = 0.0f;
		float MaterialAbsorptionHF = 0.0f;

		RaytracedAudioReverb Reverb;
	};

	// One bounce of a visualisation ray, in world space. Visualisation rays are cast purely for
	// debug rendering and feed nothing back into the audio result.
	struct RaytracedAudioBounce
	{
		glm::vec3 Position{ 0.0f };
		glm::vec3 Normal{ 0.0f };   // surface normal at the bounce, already normalised by the SDK

		// False when the ray hit nothing and this slot is a miss placeholder. The SDK fills those
		// with a large out-of-world sentinel rather than shortening the array, so a renderer that
		// trusts every slot draws lines off to infinity. A miss also ends its ray: nothing after it
		// in that ray is meaningful.
		bool Hit = false;
	};

	// A snapshot of the most recent visualisation rays, laid out ray-major: bounce b of ray r is at
	// Bounces[r * BounceCount + b]. Origin is where every ray started (the listener), so a ray's
	// polyline is Origin followed by its BounceCount bounces, stopping at the first Hit == false.
	struct RaytracedAudioVisualisation
	{
		std::vector<RaytracedAudioBounce> Bounces;
		glm::vec3 Origin{ 0.0f };
		int RayCount = 0;
		int BounceCount = 0;
	};

	// World-level snapshot of the acoustics simulation for editor tooling. Everything here comes
	// from the SDK's own getters, so it costs nothing to collect and reports zeroes (Running =
	// false) when the feature isn't compiled in or Start() hasn't run.
	struct RaytracedAudioStats
	{
		bool Available = false;   // Core/vendor/VA_RAY compiled in
		bool Running = false;     // Start() has created a world

		int VersionMajor = 0;
		int VersionMinor = 0;
		int VersionPatch = 0;
		bool ProductionBuild = false;

		bool ThreadsRunning = false;
		int EmitterCount = 0;          // as the SDK counts it (sources + our listener emitter)
		int SourceEmitterCount = 0;    // emitters we own, one per audio-emitting entity
		int StaticPrimitiveCount = 0;
		int StaticTriangleCount = 0;
		int DynamicPrimitiveCount = 0;
		int DynamicTriangleCount = 0;
		int RaysCastThisFrame = 0;
		int WorkItemCount = 0;
		int MaximumConcurrencyLevel = 0;
		int GroupedEAXCount = 0;       // reverb zones the SDK is currently blending

		bool VisualisationEnabled = false;
		int VisualisationRayCount = 0;
		int VisualisationBounceCount = 0;

		// Milliseconds, averaged by the SDK.
		double MainThreadTimeMs = 0.0;
		double RaytracingTimeMs = 0.0;
		double PreparationTimeMs = 0.0;
		double AnalysisTimeMs = 0.0;
		double LatencyMs = 0.0;

		glm::vec3 WorldMin{ 0.0f };
		glm::vec3 WorldSize{ 0.0f };
		glm::vec3 ListenerPosition{ 0.0f };
	};

	// Owns the ray-traced acoustics simulation (Vercidium Audio, Core/vendor/VA_RAY) for one Scene:
	// mirrors static geometry, tracks one listener and one emitter per audio-emitting entity, and
	// exposes the occlusion, reverb and ambience results for a playback backend to apply.
	//
	// **Topology.** The listener emitter is the one that casts rays — reverb, occlusion, permeation,
	// ambient occlusion and ambient permeation — and every audio source is registered as one of its
	// targets. Sources themselves cast nothing. This is the arrangement Vercidium's documentation
	// describes, and it means the expensive ray budget is paid once per listener rather than once
	// per source: adding a hundred sources adds a hundred *targets*, not a hundred ray casters.
	//
	// Vercidium Audio is a required build dependency; scene simulation can be disabled by settings.
	class RaytracedAudioScene : public RefCounted
	{
	public:
		// SDK availability, independent of whether this scene has started its simulation.
		static bool IsAvailable();
		bool IsRunning() const;

		explicit RaytracedAudioScene(Scene* scene);
		~RaytracedAudioScene();

		void Start(const AcousticMaterialSettings& materials = {});
		void Stop();

		// The simulation is asynchronous, so a frame has two halves and the order matters:
		//
		//   WaitForResults();  // joins the batch OnUpdate kicked last frame
		//   ... move the listener and emitters, read GetResult / GetAmbience ...
		//   OnUpdate(ts);      // kicks the next batch
		//
		// Everything between the two runs while no Vercidium worker is touching the world, so
		// mutations and result reads are both safe there. Reading results without WaitForResults
		// races the workers that are still writing them.
		void WaitForResults();
		void OnUpdate(Timestep ts);

		// Each batch is one collider in world space, with three positions per triangle.
		// Call on an idle world; material boundaries are preserved as separate primitives.
		bool SetStaticGeometry(const std::vector<AcousticGeometry>& geometry);

		// Local-space geometry, keyed by a scene-owned runtime ID. Mutate only after WaitForResults.
		bool SetGeometry(UUID id, const AcousticGeometry& geometry, const glm::mat4& transform, bool dynamic);
		bool UpdateGeometry(UUID id, const glm::mat4& transform, AcousticMaterial material, bool dynamic);
		bool RemoveGeometry(UUID id);
		bool GetGeometryTransform(UUID id, glm::mat4& transform) const;

		void SetListener(const glm::vec3& position, const glm::vec3& forward);

		void CreateEmitter(UUID entityID);
		void DestroyEmitter(UUID entityID);
		void SetEmitterPosition(UUID entityID, const glm::vec3& position);

		// The loudest linear volume the source is ever played at. The simulation needs it to scale
		// the echogram correctly; leaving it at the default makes reverb energy wrong for sources
		// that are quiet by design.
		void SetEmitterMaxVolume(UUID entityID, float maxVolume);

		RaytracedAudioResult GetResult(UUID entityID) const;

		// Listener-global reverb + ambience. Read once per frame, not once per source.
		RaytracedAudioAmbience GetAmbience() const;

		// Visualisation rays are off by default — they cost real raytracing work and feed nothing
		// back into the audio. Enabling with rayCount or bounceCount <= 0 disables them.
		// updateIntervalMs throttles how often the SDK recasts them.
		void SetVisualisationEnabled(bool enabled, int rayCount, int bounceCount, int updateIntervalMs);

		// Copies the most recent visualisation snapshot. The SDK delivers bounces on its own worker
		// threads, so this hands back a stable copy rather than a view into live data.
		void GetVisualisation(RaytracedAudioVisualisation& outVisualisation) const;

		// Pure readout for the editor's Audio Debugger; changes no simulation state.
		RaytracedAudioStats GetStats() const;

	private:
		Scene* m_Scene = nullptr;

		struct Impl;
		Scope<Impl> m_Impl;
	};

}
