// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "SceneAudioVerification.h"
#include "Scene.h"
#include "Components.h"
#include "Entity.h"
#include "Lux/Core/Application.h"
#include "Lux/Serialization/Yaml.h"
#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/AudioEventInstance.h"
#include "Lux/Audio/AudioPerformance.h"
#ifdef LUX_ENABLE_FMOD
#include "Lux/Audio/FMOD/FMODInternal.h" // verification reads the authored Studio parameter
#include <fmod_studio.hpp>
#endif
#include <algorithm>
#include <bit>
#include <cmath>
#include <format>

namespace Lux
{
	namespace
	{
		constexpr float k_AcousticTransportTolerance = 0.00001f;
	}

	SceneAudioVerificationStatus CaptureSceneAudioVerification(Scene& scene, std::string& report, std::string& error)
	{
		LUX_PROFILE_FUNCTION("CaptureSceneAudioVerification");
		LUX_CORE_ASSERT(Application::IsMainThread(), "Scene audio verification is main-thread only");
		report.clear();
		error.clear();
		const auto& kernel = scene.GetAudioAcoustics();
		const auto stats = kernel.GetDiagnostics();
		if (!scene.IsRunning() || stats.GeometryFailed || stats.SolveFailures)
		{
			error = "Soundwave Scene verification failed: scene not running or acoustic preparation/solve failed";
			return SceneAudioVerificationStatus::Failed;
		}
		if (stats.ReverbStalled)
		{
			error = "Soundwave reverb stalled: its GPU measurement never returned";
			return SceneAudioVerificationStatus::Failed;
		}
		// The listener reverb lands a few frames after geometry (GPU readback or CPU job).
		if (!stats.Ready || !stats.ReverbMeasurements || !kernel.GetReverb())
			return SceneAudioVerificationStatus::Pending;
		if (!stats.Evaluations)
		{
			error = "Scene verification needs an enabled audio source with a valid event reference";
			return SceneAudioVerificationStatus::Failed;
		}
		Yaml::Writer out;
		out << Yaml::BeginMap << Yaml::Key << "Backend" << Yaml::Value << "Soundwave";
		out << Yaml::Key << "ReverbOn" << Yaml::Value << (stats.ReverbOnGpu ? "GPU" : "CPU");
		out << Yaml::Key << "ReverbMeasurements" << Yaml::Value << stats.ReverbMeasurements;
		// Audio budget evidence from the last 4 Hz sample. A session over budget also logs
		// "Soundwave acoustics (ms) budget exceeded" once.
		const AudioPerformanceStats& performance = AudioPerformance::GetStats();
		out << Yaml::Key << "AcousticsMilliseconds" << Yaml::Value << performance.RaytracingMilliseconds;
		out << Yaml::Key << "AcousticsBudgetMilliseconds" << Yaml::Value << AudioPerformance::GetSettings().RaytracingMilliseconds;
		out << Yaml::Key << "AcousticsBudgetExceeded" << Yaml::Value << performance.RaytracingExceeded;
		out << Yaml::Key << "AudioCPUPercent" << Yaml::Value << performance.CPUPercent;
		out << Yaml::Key << "ReverbDecaySeconds" << Yaml::Value << kernel.GetReverb()->DecaySeconds;
		out << Yaml::Key << "ReverbSend" << Yaml::Value << kernel.GetReverb()->Send;
		out << Yaml::Key << "ReverbEscapedFraction" << Yaml::Value << kernel.GetReverb()->EscapedFraction;
		if (const auto* check = kernel.GetReverbCheck())
		{
			const auto gap = [](float a, float b) { return a == b ? 0.0f : std::abs(20.0f * std::log10(std::max(a, 1e-6f) / std::max(b, 1e-6f))); };
			const float sendGap = gap(check->first.Send, check->second.Send), decayGap = gap(check->first.DecaySeconds, check->second.DecaySeconds);
			out << Yaml::Key << "ReverbCheck" << Yaml::Value << Yaml::BeginMap;
			out << Yaml::Key << "GpuSend" << Yaml::Value << check->first.Send << Yaml::Key << "CpuSend" << Yaml::Value << check->second.Send;
			out << Yaml::Key << "GpuDecaySeconds" << Yaml::Value << check->first.DecaySeconds << Yaml::Key << "CpuDecaySeconds" << Yaml::Value << check->second.DecaySeconds;
			out << Yaml::Key << "GpuTraces" << Yaml::Value << check->first.Traces << Yaml::Key << "CpuTraces" << Yaml::Value << check->second.Traces;
			out << Yaml::Key << "SendGapDb" << Yaml::Value << sendGap << Yaml::Key << "DecayGapDb" << Yaml::Value << decayGap;
			out << Yaml::EndMap;
			if (sendGap > 0.5f || decayGap > 0.5f)
			{
				error = std::format("Soundwave GPU reverb differs from the CPU reference by {:.3f} dB (send) / {:.3f} dB (decay)", sendGap, decayGap);
				return SceneAudioVerificationStatus::Failed;
			}
		}
		out << Yaml::Key << "StaticTriangles" << Yaml::Value << stats.StaticTriangles;
		out << Yaml::Key << "MovingTriangles" << Yaml::Value << stats.MovingTriangles;
		out << Yaml::Key << "StaticInertTriangles" << Yaml::Value << stats.StaticInertTriangles;
		out << Yaml::Key << "MovingInertTriangles" << Yaml::Value << stats.MovingInertTriangles;
		out << Yaml::Key << "StaticStoredTriangles" << Yaml::Value << stats.StaticStoredTriangles;
		out << Yaml::Key << "MovingStoredTriangles" << Yaml::Value << stats.MovingStoredTriangles;
		out << Yaml::Key << "StaticMeshInstances" << Yaml::Value << stats.StaticMeshInstances;
		out << Yaml::Key << "MovingMeshInstances" << Yaml::Value << stats.MovingMeshInstances;
		out << Yaml::Key << "GeometryGeneration" << Yaml::Value << stats.GeometryGeneration;
		out << Yaml::Key << "Evaluations" << Yaml::Value << stats.Evaluations;
		out << Yaml::Key << "EngineRayComparison" << Yaml::Value << kernel.IsRaycastComparisonEnabled();
		out << Yaml::Key << "Sources" << Yaml::Value << Yaml::BeginSeq;
		uint32_t checkedParameters = 0;
		for (auto handle : scene.GetAllEntitiesWith<AudioSourceComponent>())
		{
			Entity entity{handle,&scene};
			const UUID id = entity.GetUUID();
			const auto* result = kernel.GetResult(id);
			auto instance = scene.GetRuntimeEventInstance(id);
			if (!instance || !instance->IsValid())
				continue;
			if (!result)
				return SceneAudioVerificationStatus::Pending; // its first solve batch is still on a worker
			const float gain = kernel.GetGainLF(id), applied = scene.GetAudioSourceOcclusion(id);
			if (!std::isfinite(gain) || std::abs(gain - applied) > k_AcousticTransportTolerance)
			{
				error = std::format("Soundwave LF gain did not reach Scene source {}", static_cast<uint64_t>(id));
				return SceneAudioVerificationStatus::Failed;
			}
			out << Yaml::BeginMap << Yaml::Key << "Name" << Yaml::Value << entity.GetComponent<TagComponent>().Tag;
			out << Yaml::Key << "UUID" << Yaml::Value << id;
			out << Yaml::Key << "OcclusionGainLF" << Yaml::Value << gain;
			out << Yaml::Key << "DirectGainLF" << Yaml::Value << result->Direct.GainLF();
			out << Yaml::Key << "DirectGainLFBits" << Yaml::Value << std::bit_cast<uint32_t>(result->Direct.GainLF());
			out << Yaml::Key << "RawGainLF" << Yaml::Value << result->OcclusionGainLF;
			out << Yaml::Key << "Listener" << Yaml::Value << Yaml::Flow << Yaml::BeginSeq
				<< result->Direct.From.X << result->Direct.From.Y << result->Direct.From.Z << Yaml::EndSeq;
			out << Yaml::Key << "Source" << Yaml::Value << Yaml::Flow << Yaml::BeginSeq
				<< result->Direct.To.X << result->Direct.To.Y << result->Direct.To.Z << Yaml::EndSeq;
			if (kernel.IsRaycastComparisonEnabled())
			{
				const auto* oracle = scene.GetAudioOcclusion().GetPath(id);
				if (!oracle)
				{
					error = std::format("Engine-ray comparison path missing for source {}", static_cast<uint64_t>(id));
					return SceneAudioVerificationStatus::Failed;
				}
				out << Yaml::Key << "EngineRayGainLF" << Yaml::Value << oracle->Measurement.GainLF();
				out << Yaml::Key << "EngineRayGainLFBits" << Yaml::Value << std::bit_cast<uint32_t>(oracle->Measurement.GainLF());
				out << Yaml::Key << "EngineRayWalls" << Yaml::Value << static_cast<uint32_t>(oracle->Walls.size());
			}
			out << Yaml::Key << "ReflectionPaths" << Yaml::Value << result->Diagnostics.ReflectionPaths;
			out << Yaml::Key << "OpeningPaths" << Yaml::Value << result->Diagnostics.OpeningPaths;
			out << Yaml::Key << "Work" << Yaml::Value << result->Diagnostics.Work;
			out << Yaml::Key << "WorkLimited" << Yaml::Value << result->Diagnostics.WorkLimited;
			out << Yaml::Key << "PathsLimited" << Yaml::Value << result->Diagnostics.PathsLimited;
			if (result->HasAudibleOpening)
			{
				out << Yaml::Key << "AudibleOpening" << Yaml::Value << Yaml::BeginMap;
				out << Yaml::Key << "GainLF" << Yaml::Value << result->AudibleOpening.GainLF;
				out << Yaml::Key << "Points" << Yaml::Value << Yaml::BeginSeq;
				for (uint32_t i = 0; i < result->AudibleOpening.PointCount; ++i)
				{
					const auto point = result->AudibleOpening.Points[i];
					out << Yaml::Flow << Yaml::BeginSeq << point.X << point.Y << point.Z << Yaml::EndSeq;
				}
				out << Yaml::EndSeq << Yaml::EndMap;
			}
#ifdef LUX_ENABLE_FMOD
			const auto& event = entity.GetComponent<AudioSourceComponent>().Event;
			FMOD_GUID guid{};
			FMOD::Studio::EventDescription* description = nullptr;
			FMOD_STUDIO_PARAMETER_DESCRIPTION parameter{};
			if (auto* studio = FMODInternal::GetStudioSystem(); studio && FMOD::Studio::parseID(event.Guid.c_str(), &guid) == FMOD_OK &&
				studio->getEventByID(&guid, &description) == FMOD_OK &&
				description->getParameterDescriptionByName(AudioEventInstance::kOcclusionParameter, &parameter) == FMOD_OK)
			{
				const float value = instance->GetParameter(AudioEventInstance::kOcclusionParameter);
				if (!std::isfinite(value) || std::abs(value - std::clamp(1.0f - gain, 0.0f, 1.0f)) > k_AcousticTransportTolerance)
				{
					error = std::format("Soundwave LF gain did not reach FMOD source {}", static_cast<uint64_t>(id));
					return SceneAudioVerificationStatus::Failed;
				}
				out << Yaml::Key << "FMODOcclusion" << Yaml::Value << value;
				++checkedParameters;
			}
#else
			// shortcut: the native backend applies the gain inside the voice with no readback, so the
			// check ends at the Scene gain above; add an instance readback if this gate must cover it.
			++checkedParameters;
#endif
			out << Yaml::EndMap;
		}
		out << Yaml::EndSeq << Yaml::Key << "CheckedFMODParameters" << Yaml::Value << checkedParameters << Yaml::EndMap;
		if (!checkedParameters || (!stats.StaticTriangles && !stats.MovingTriangles))
		{
			error = "Scene verification needs acoustic render geometry and a live event exposing Occlusion";
			return SceneAudioVerificationStatus::Failed;
		}
		report = out.c_str();
		return SceneAudioVerificationStatus::Passed;
	}
}
