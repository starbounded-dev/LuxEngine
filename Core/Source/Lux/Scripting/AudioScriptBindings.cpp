#include "lpch.h"
#include "Lux/Audio/AudioAccessibility.h"
#include "AudioScriptBindings.h"
#include "ScriptEngine.h"

#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/AudioEventInstance.h"
#include "Lux/Core/Application.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Project/Project.h"

#include <Coral/Assembly.hpp>
#include <Coral/String.hpp>
#include <Coral/Type.hpp>
#include <cmath>

namespace Lux
{

	namespace
	{
		struct ScriptEvent
		{
			Ref<AudioEventInstance> Instance;
			bool OneShot = false;
		};
		std::unordered_map<uint64_t, ScriptEvent> s_Instances;
		uint64_t s_NextHandle = 1; // Never recycled, including across scene/assembly/bank reloads.
		Coral::Type* s_AudioType = nullptr;
		Coral::Type* s_MusicType = nullptr;
		Coral::Type* s_DialogueType = nullptr;
		Coral::Type* s_AccessibilityType = nullptr;

		bool OnMainThread()
		{
			if (Application::IsMainThread())
				return true;
			LUX_CORE_ERROR_TAG("Audio", "The scripting audio API must be called on the main thread");
			return false;
		}

		Scene* AudioScene()
		{
			if (!OnMainThread())
				return nullptr;
			auto scene = ScriptEngine::GetInstance().GetCurrentScene();
			if (scene && scene->IsRunning())
				return scene.Raw();
			LUX_CORE_ERROR_TAG("Audio", "Gameplay audio requires a running scene");
			return nullptr;
		}

		Entity AudioEntity(uint64_t id)
		{
			Scene* scene = AudioScene();
			Entity entity = scene ? scene->TryGetEntityWithUUID(id) : Entity{};
			if (!entity)
				LUX_CORE_ERROR_TAG("Audio", "Audio operation references unavailable entity {0}", id);
			return entity;
		}

		Ref<AudioEventInstance> Instance(uint64_t handle)
		{
			if (!OnMainThread())
				return nullptr;
			auto it = s_Instances.find(handle);
			return it != s_Instances.end() && it->second.Instance->IsValid() ? it->second.Instance : nullptr;
		}

		AudioSourceComponent* Source(uint64_t id)
		{
			Entity entity = AudioEntity(id);
			auto* component = entity ? entity.TryGetComponent<AudioSourceComponent>() : nullptr;
			if (entity && !component)
				LUX_CORE_ERROR_TAG("Audio", "Entity {0} no longer has AudioSourceComponent", id);
			return component;
		}

		Ref<AudioEventInstance> SourceEvent(uint64_t id)
		{
			auto* source = Source(id);
			return source && source->Event.IsValid() ? AudioScene()->GetRuntimeEventInstance(id) : nullptr;
		}
		int32_t Audio_SourceGetPriority(uint64_t id)
		{
			const auto* source = Source(id);
			return source ? source->Priority : 128;
		}
		Coral::Bool32 Audio_SourceSetPriority(uint64_t id, int32_t priority)
		{
			auto* source = Source(id);
			if (!source || priority < 0 || priority > 256)
				return false;
			source->Priority = priority;
			return true;
		}
		Coral::Bool32 Audio_SourceGetCulling(uint64_t id)
		{
			const auto* source = Source(id);
			return source && source->DistanceCulling;
		}
		void Audio_SourceSetCulling(uint64_t id, Coral::Bool32 value)
		{
			if (auto* source = Source(id))
				source->DistanceCulling = value;
		}
		Coral::Bool32 Audio_SourceIsCulled(uint64_t id)
		{
			return Source(id) && AudioScene()->IsAudioSourceCulled(id);
		}

		uint64_t Audio_CreateInstance(Coral::String reference, Coral::Bool32 oneShot, const glm::vec3* position)
		{
			Scene* scene = AudioScene();
			if (!scene)
				return 0;
			Ref<AudioEventInstance> event = AudioEventInstance::Create(static_cast<std::string>(reference));
			if (!event)
				return 0;
			if (oneShot && !event->IsOneShot())
			{
				LUX_CORE_ERROR_TAG("Audio", "PlayOneShot requires a finite one-shot event; use CreateInstance for looping events");
				return 0;
			}
			if (s_NextHandle == 0)
			{
				LUX_CORE_ERROR_TAG("Audio", "Script audio handle space exhausted");
				return 0;
			}
			const uint64_t handle = s_NextHandle++;
			if (!event->SetCallbackHandle(handle))
				return 0;
			event->Set3DAttributes(position ? *position : glm::vec3(0), glm::vec3(0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
			event->SetScenePaused(scene->IsPaused());
			s_Instances.emplace(handle, ScriptEvent{event, oneShot != 0});
			if (oneShot)
				event->Start();
			return handle;
		}

		Coral::Bool32 Audio_SetSnapshotIntensity(uint64_t handle, float value)
		{
			auto event = Instance(handle);
			return event && event->SetSnapshotIntensity(value);
		}

		AudioPortalComponent* Portal(uint64_t id)
		{
			Entity entity = AudioEntity(id);
			auto* component = entity ? entity.TryGetComponent<AudioPortalComponent>() : nullptr;
			if (entity && !component)
				LUX_CORE_ERROR_TAG("Audio", "Entity {} no longer has AudioPortalComponent", id);
			return component;
		}

		float Audio_PortalGetScalar(uint64_t id, int32_t field)
		{
			const auto* portal = Portal(id);
			if (!portal)
				return 0;
			switch (field)
			{
				case 0: return portal->Enabled;
				case 1: return portal->Open;
				case 2: return portal->BlendDistance;
				case 3: return static_cast<float>(portal->Material);
				default: return 0;
			}
		}

		Coral::Bool32 Audio_PortalSetScalar(uint64_t id, int32_t field, float value)
		{
			auto* portal = Portal(id);
			if (!portal || !std::isfinite(value))
				return false;
			auto candidate = *portal;
			switch (field)
			{
				case 0:
					if (value != 0 && value != 1)
						return false;
					candidate.Enabled = value != 0;
					break;
				case 1: candidate.Open = value; break;
				case 2: candidate.BlendDistance = value; break;
				case 3:
					if (value < 0 || value >= AcousticMaterialCount || std::trunc(value) != value)
						return false;
					candidate.Material = static_cast<AcousticMaterial>(value);
					break;
				default: return false;
			}
			if (!AudioZoneSystem::Validate(candidate))
				return false;
			*portal = candidate;
			return true;
		}

		void Audio_PortalGetExtents(uint64_t id, glm::vec3* result)
		{
			const auto* portal = Portal(id);
			if (result)
				*result = portal ? portal->HalfExtents : glm::vec3(0);
		}

		Coral::Bool32 Audio_PortalSetExtents(uint64_t id, glm::vec3* value)
		{
			auto* portal = Portal(id);
			if (!portal || !value)
				return false;
			auto candidate = *portal;
			candidate.HalfExtents = *value;
			if (!AudioZoneSystem::Validate(candidate))
				return false;
			*portal = candidate;
			return true;
		}

		uint64_t Audio_PortalGetRoom(uint64_t id, Coral::Bool32 second)
		{
			const auto* portal = Portal(id);
			return portal ? static_cast<uint64_t>(second ? portal->ZoneB : portal->ZoneA) : 0;
		}

		Coral::Bool32 Audio_PortalSetRoom(uint64_t id, Coral::Bool32 second, uint64_t room)
		{
			auto* portal = Portal(id);
			if (!portal)
				return false;
			if (room)
			{
				Entity entity = AudioEntity(room);
				if (!entity || !entity.HasComponent<AudioZoneComponent>() || room == (second ? portal->ZoneA : portal->ZoneB))
					return false;
			}
			(second ? portal->ZoneB : portal->ZoneA) = room;
			return true;
		}

		int32_t Audio_MeshGetMotion(uint64_t id)
		{
			Entity entity = AudioEntity(id);
			const auto* collider = entity ? entity.TryGetComponent<MeshColliderComponent>() : nullptr;
			return collider ? static_cast<int32_t>(collider->AcousticMotion) : 0;
		}

		Coral::Bool32 Audio_MeshSetMotion(uint64_t id, int32_t mode)
		{
			Entity entity = AudioEntity(id);
			auto* collider = entity ? entity.TryGetComponent<MeshColliderComponent>() : nullptr;
			if (!collider || mode < 0 || mode > static_cast<int32_t>(AcousticGeometryMode::Disabled))
				return false;
			collider->AcousticMotion = static_cast<AcousticGeometryMode>(mode);
			return true;
		}

		AudioZoneComponent* Zone(uint64_t id)
		{
			Entity entity = AudioEntity(id);
			auto* component = entity ? entity.TryGetComponent<AudioZoneComponent>() : nullptr;
			if (entity && !component)
				LUX_CORE_ERROR_TAG("Audio", "Entity {0} no longer has AudioZoneComponent", id);
			return component;
		}

		// IDs are private to the managed bridge; public C# exposes named properties.
		float Audio_ZoneGetScalar(uint64_t id, int32_t field)
		{
			auto* zone = Zone(id);
			if (!zone)
				return 0.0f;
			switch (field)
			{
				case 0: return zone->Enabled ? 1.0f : 0.0f;
				case 1: return static_cast<float>(zone->Shape);
				case 2: return zone->Priority;
				case 3: return zone->BlendDistance;
				case 4: return zone->FadeTime;
				case 5: return zone->Volume;
				case 6: return zone->Radius;
				case 7: return AudioScene()->GetAudioZoneWeight(id);
				default:
					LUX_CORE_ERROR_TAG("Audio", "Invalid zone scalar field {0}", field);
					return 0.0f;
			}
		}

		Coral::Bool32 Audio_ZoneSetScalar(uint64_t id, int32_t field, float value)
		{
			auto* zone = Zone(id);
			if (!zone || !std::isfinite(value))
				return false;
			AudioZoneComponent candidate = *zone;
			switch (field)
			{
				case 0: candidate.Enabled = value != 0.0f; break;
				case 1:
					if (value < 0.0f || value > 2.0f || std::floor(value) != value)
						return false;
					candidate.Shape = static_cast<AudioZoneShape>(static_cast<uint8_t>(value));
					break;
				case 2: candidate.Priority = value; break;
				case 3: candidate.BlendDistance = value; break;
				case 4: candidate.FadeTime = value; break;
				case 5: candidate.Volume = value; break;
				case 6: candidate.Radius = value; break;
				default: return false;
			}
			if (!AudioZoneSystem::Validate(candidate))
				return false;
			*zone = std::move(candidate);
			return true;
		}

		void Audio_ZoneGetVector(uint64_t id, Coral::Bool32 extents, glm::vec3* value)
		{
			if (!value)
				return;
			auto* zone = Zone(id);
			*value = zone ? (extents ? zone->HalfExtents : zone->Offset) : glm::vec3(0.0f);
		}

		Coral::Bool32 Audio_ZoneSetVector(uint64_t id, Coral::Bool32 extents, const glm::vec3* value)
		{
			auto* zone = Zone(id);
			if (!zone || !value)
				return false;
			AudioZoneComponent candidate = *zone;
			(extents ? candidate.HalfExtents : candidate.Offset) = *value;
			if (!AudioZoneSystem::Validate(candidate))
				return false;
			*zone = std::move(candidate);
			return true;
		}

		Coral::Bool32 Audio_ZoneSetEvent(uint64_t id, Coral::Bool32 snapshot, Coral::String reference)
		{
			auto* zone = Zone(id);
			if (!zone)
				return false;
			const std::string text = reference;
			auto& target = snapshot ? zone->Snapshot : zone->AmbienceEvent;
			if (text.empty())
			{
				target = {};
				return true;
			}
			auto event = AudioEventInstance::Create(text);
			if (!event || (snapshot ? !event->SetSnapshotIntensity(0.0f) : event->IsSnapshot() || event->IsOneShot()))
				return false;
			target = { event->GetReference(), {}, {} };
			return true;
		}

		Coral::Bool32 Audio_IsMainThread()
		{
			return Application::IsMainThread();
		}

		Coral::Bool32 Audio_IsValid(uint64_t handle)
		{
			return Instance(handle) != nullptr;
		}
		Coral::Bool32 Audio_IsPlaying(uint64_t handle)
		{
			auto event = Instance(handle);
			return event && event->IsPlaying();
		}
		void Audio_Start(uint64_t handle)
		{
			if (auto event = Instance(handle))
				event->Start();
		}
		void Audio_Stop(uint64_t handle, Coral::Bool32 fade)
		{
			if (auto event = Instance(handle))
				event->Stop(fade);
		}
		void Audio_Dispose(uint64_t handle)
		{
			if (OnMainThread())
				s_Instances.erase(handle);
		}
		void Audio_SetVolume(uint64_t handle, float value)
		{
			if (auto event = Instance(handle))
				event->SetVolume(value);
		}
		void Audio_SetPitch(uint64_t handle, float value)
		{
			if (auto event = Instance(handle))
				event->SetPitch(value);
		}
		void Audio_SetParameter(uint64_t handle, Coral::String name, float value)
		{
			if (auto event = Instance(handle))
				event->SetParameter(name, value);
		}
		void Audio_Set3DAttributes(uint64_t handle, const glm::vec3* position, const glm::vec3* velocity, const glm::vec3* forward,
								   const glm::vec3* up)
		{
			if (auto event = Instance(handle); event && position && velocity && forward && up)
				event->Set3DAttributes(*position, *velocity, *forward, *up);
		}

		Coral::Bool32 Audio_PlayFootstep(uint64_t id, float speed, float weight, float probeDistance)
		{
			Entity entity = AudioEntity(id);
			return entity && entity.GetScene()->PlayFootstep(id, speed, weight, probeDistance);
		}

		int32_t Audio_GetSurfaceMaterial(uint64_t id)
		{
			Entity entity = AudioEntity(id);
			if (!entity)
				return static_cast<int32_t>(AcousticMaterial::Default);
			if (const auto* surface = entity.TryGetComponent<AudioSurfaceComponent>())
				return static_cast<int32_t>(surface->Material);
			if (const auto* collider = entity.TryGetComponent<MeshColliderComponent>())
				return static_cast<int32_t>(collider->Acoustic);
			return static_cast<int32_t>(AcousticMaterial::Default);
		}

		void Audio_SourcePlay(uint64_t id)
		{
			auto* source = Source(id);
			if (!source)
				return;
			if (!source->Event.IsValid())
			{
				LUX_CORE_ERROR_TAG("Audio", "Entity {0} has no Studio event assigned", id);
				return;
			}
			source->ScriptPaused = false;
			if (auto* playback = AudioScene()->GetAudioSourcePlayback(id))
				playback->Play();
		}

		void Audio_SourceStop(uint64_t id, Coral::Bool32 fade)
		{
			if (auto* source = Source(id))
			{
				if (auto* playback = AudioScene()->GetAudioSourcePlayback(id))
					playback->Stop(fade);
				source->ScriptPaused = false;
			}
		}

		Coral::Bool32 Audio_SourceIsPlaying(uint64_t id)
		{
			auto* playback = Source(id) ? AudioScene()->GetAudioSourcePlayback(id) : nullptr;
			return playback && playback->IsPlaying();
		}

		Coral::Bool32 Audio_SourceIsPaused(uint64_t id)
		{
			auto* source = Source(id);
			return source && (source->ScriptPaused || AudioScene()->IsPaused());
		}

		void Audio_SourceSetPaused(uint64_t id, Coral::Bool32 paused)
		{
			auto* source = Source(id);
			if (!source)
				return;
			source->ScriptPaused = paused;
			if (auto event = SourceEvent(id))
				event->SetPaused(paused);
		}

		float Audio_SourceGetVolume(uint64_t id)
		{
			auto* source = Source(id);
			return source ? source->Config.VolumeMultiplier : 0.0f;
		}
		float Audio_SourceGetPitch(uint64_t id)
		{
			auto* source = Source(id);
			return source ? source->Config.PitchMultiplier : 0.0f;
		}
		void Audio_SourceSetVolume(uint64_t id, float value)
		{
			if (auto* source = Source(id); source && std::isfinite(value))
			{
				source->Config.VolumeMultiplier = std::max(0.0f, value);
				if (auto event = SourceEvent(id))
					event->SetVolume(value);
			}
		}
		void Audio_SourceSetPitch(uint64_t id, float value)
		{
			if (auto* source = Source(id); source && std::isfinite(value))
			{
				source->Config.PitchMultiplier = std::max(0.0f, value);
				if (auto event = SourceEvent(id))
					event->SetPitch(value);
			}
		}
		Coral::Bool32 Audio_SourceHasEvent(uint64_t id)
		{
			auto* source = Source(id);
			return source && source->Event.IsValid();
		}
		void Audio_SourceSetParameter(uint64_t id, Coral::String name, float value)
		{
			if (auto* playback = Source(id) ? AudioScene()->GetAudioSourcePlayback(id) : nullptr)
				playback->SetParameter(name, value);
		}
		float Audio_SourceGetParameter(uint64_t id, Coral::String name)
		{
			auto* playback = Source(id) ? AudioScene()->GetAudioSourcePlayback(id) : nullptr;
			return playback ? playback->GetParameter(name) : 0.0f;
		}
		void Audio_SourceSetParameterLabel(uint64_t id, Coral::String name, Coral::String label)
		{
			if (auto* playback = Source(id) ? AudioScene()->GetAudioSourcePlayback(id) : nullptr)
				playback->SetParameterLabel(name, label);
		}
		int32_t Audio_SourceGetTimeline(uint64_t id)
		{
			auto* playback = Source(id) ? AudioScene()->GetAudioSourcePlayback(id) : nullptr;
			return playback ? playback->GetTimelinePosition() : 0;
		}
		void Audio_SourceSetTimeline(uint64_t id, int32_t value)
		{
			if (auto* playback = Source(id) ? AudioScene()->GetAudioSourcePlayback(id) : nullptr)
				playback->SetTimelinePosition(value);
		}
		void Audio_SourceSetEvent(uint64_t id, Coral::String reference)
		{
			auto* source = Source(id);
			if (!source)
				return;
			auto probe = AudioEventInstance::Create(reference);
			if (!probe)
				return;
			source->Event = {probe->GetReference(), {}, {}};
			AudioScene()->GetAudioSourcePlayback(id);
		}

		AudioListenerComponent* Listener(uint64_t id)
		{
			Entity entity = AudioEntity(id);
			auto* component = entity ? entity.TryGetComponent<AudioListenerComponent>() : nullptr;
			if (entity && !component)
				LUX_CORE_ERROR_TAG("Audio", "Entity {0} no longer has AudioListenerComponent", id);
			return component;
		}
		Coral::Bool32 Audio_ListenerGetActive(uint64_t id)
		{
			auto* c = Listener(id);
			return c && c->Active;
		}
		void Audio_ListenerSetActive(uint64_t id, Coral::Bool32 value)
		{
			if (auto* c = Listener(id))
				c->Active = value;
		}
		int32_t Audio_ListenerGetIndex(uint64_t id)
		{
			auto* c = Listener(id);
			return c ? c->ListenerIndex : 0;
		}
		void Audio_ListenerSetIndex(uint64_t id, int32_t value)
		{
			if (auto* c = Listener(id))
				c->ListenerIndex = std::clamp(value, 0, AudioListener::MaxListeners - 1);
		}
		float Audio_ListenerGetWeight(uint64_t id)
		{
			auto* c = Listener(id);
			return c ? c->Weight : 0.0f;
		}
		void Audio_ListenerSetWeight(uint64_t id, float value)
		{
			if (auto* c = Listener(id); c && std::isfinite(value))
				c->Weight = std::clamp(value, 0.0f, 1.0f);
		}
		Coral::Bool32 Audio_ListenerGetUseTarget(uint64_t id)
		{
			auto* c = Listener(id);
			return c && c->UseAttenuationTarget;
		}
		void Audio_ListenerSetUseTarget(uint64_t id, Coral::Bool32 value)
		{
			if (auto* c = Listener(id))
				c->UseAttenuationTarget = value;
		}
		uint64_t Audio_ListenerGetTarget(uint64_t id)
		{
			auto* c = Listener(id);
			return c ? static_cast<uint64_t>(c->AttenuationTarget) : 0;
		}
		void Audio_ListenerSetTarget(uint64_t id, uint64_t target)
		{
			if (auto* c = Listener(id))
				c->AttenuationTarget = target;
		}

		float Accessibility_GetOption(int32_t option)
		{
			if (!AudioScene())
				return 0;
			const auto& p = AudioAccessibility::GetPreferences();
			switch (option)
			{
			case 0: return p.Subtitles;
			case 1: return p.Captions;
			case 2: return p.VisualCues;
			case 3: return p.SpeakerNames;
			case 4: return p.DirectionIndicators;
			case 5: return p.Mono;
			case 6: return p.AudioDescriptions;
			case 7: return static_cast<float>(p.DynamicRange);
			case 8: return p.TextSize;
			case 9: return p.BackgroundOpacity;
			case 10: return p.DurationMultiplier;
			case 11: return static_cast<float>(p.MaxLines);
			case 12: return p.DialogueBoost;
			default: return option >= 13 && option < 19 ? p.Volumes[option - 13] : 0;
			}
		}
		Coral::Bool32 Accessibility_SetOption(int32_t option, float value)
		{
			if (!AudioScene() || !std::isfinite(value) || option < 0 || option >= 19 ||
				(option <= 6 && value != 0 && value != 1) || ((option == 7 || option == 11) && std::trunc(value) != value))
				return false;
			auto p = AudioAccessibility::GetPreferences();
			switch (option)
			{
			case 0: p.Subtitles = value != 0; break;
			case 1: p.Captions = value != 0; break;
			case 2: p.VisualCues = value != 0; break;
			case 3: p.SpeakerNames = value != 0; break;
			case 4: p.DirectionIndicators = value != 0; break;
			case 5: p.Mono = value != 0; break;
			case 6: p.AudioDescriptions = value != 0; break;
			case 7:
				if (value < 0 || value > 2)
					return false;
				p.DynamicRange = static_cast<AudioDynamicRange>(value); break;
			case 8: p.TextSize = value; break;
			case 9: p.BackgroundOpacity = value; break;
			case 10: p.DurationMultiplier = value; break;
			case 11:
				if (value < 1 || value > 10)
					return false;
				p.MaxLines = static_cast<uint32_t>(value); break;
			case 12: p.DialogueBoost = value; break;
			default: p.Volumes[option - 13] = value; break;
			}
			return AudioAccessibility::ApplyPreferences(p);
		}
		Coral::Bool32 Accessibility_Save()
		{
			return AudioScene() && AudioAccessibility::SavePreferences();
		}
		Coral::Bool32 Accessibility_HasBus(int32_t category)
		{
			return AudioScene() && category >= 0 && category < 6 && AudioAccessibility::HasBus(static_cast<AudioCategory>(category));
		}
		void Accessibility_GetSpeakerColor(Coral::String name, glm::vec4* color)
		{
			if (!color)
				return;
			*color = glm::vec4(1.0f);
			if (!AudioScene())
				return;
			const auto& colors = AudioAccessibility::GetConfig().SpeakerColors;
			if (auto found = colors.find(static_cast<std::string>(name)); found != colors.end())
				*color = found->second;
		}
		struct ManagedSoundCue
		{
			uint64_t Handle;
			int32_t Active, Category;
			glm::vec3 Position, Direction;
			float Intensity;
		};
		static_assert(sizeof(ManagedSoundCue) == 48, "SoundCueData must match the managed sequential layout");
		int32_t Accessibility_GetCueCount()
		{
			return AudioScene() ? static_cast<int32_t>(AudioAccessibility::GetSoundCues().size()) : 0;
		}
		Coral::Bool32 Accessibility_GetCue(int32_t index, ManagedSoundCue* result)
		{
			if (!AudioScene() || !result || index < 0 || static_cast<size_t>(index) >= AudioAccessibility::GetSoundCues().size())
				return false;
			const auto& cue = AudioAccessibility::GetSoundCues()[index];
			*result = { cue.Handle, cue.Active ? 1 : 0, static_cast<int32_t>(cue.Category), cue.Position, cue.Direction, cue.Intensity };
			return true;
		}
		uint64_t Dialogue_Describe(Coral::String key)
		{
			auto scene = AudioScene();
			return scene ? scene->GetDialogueDirector().Describe(key) : 0;
		}

		uint64_t Dialogue_Speak(Coral::String key, uint64_t speaker, Coral::Bool32 bark)
		{
			auto scene = AudioScene();
			if (!scene)
				return 0;
			return bark ? scene->GetDialogueDirector().Bark(key, speaker) : scene->GetDialogueDirector().Speak(key, speaker);
		}
		void Dialogue_Stop(uint64_t handle, Coral::Bool32 fade)
		{
			if (auto scene = AudioScene())
				scene->GetDialogueDirector().Stop(handle, fade);
		}
		void Dialogue_StopAll()
		{
			if (auto scene = AudioScene())
				scene->GetDialogueDirector().StopAll();
		}
		Coral::Bool32 Dialogue_Query(uint64_t value, Coral::Bool32 speaker)
		{
			auto scene = AudioScene();
			return scene && (speaker ? scene->GetDialogueDirector().IsSpeaking(value) : scene->GetDialogueDirector().IsActive(value));
		}
		int32_t Dialogue_GetQueueLength()
		{
			auto scene = AudioScene();
			return scene ? static_cast<int32_t>(scene->GetDialogueDirector().GetQueueLength()) : 0;
		}
		Coral::Bool32 Dialogue_SetLanguage(Coral::String language)
		{
			auto scene = AudioScene();
			return scene && scene->GetDialogueDirector().SetLanguage(language);
		}
		Coral::Bool32 Dialogue_SetQueueMode(int32_t mode)
		{
			auto scene = AudioScene();
			return scene && mode >= 0 && mode <= 2 && scene->GetDialogueDirector().SetQueueMode(static_cast<DialogueQueueMode>(mode));
		}

		Coral::Bool32 Music_Play(Coral::String reference)
		{
			auto* scene = AudioScene();
			return scene && scene->GetMusicDirector().Play(reference);
		}
		void Music_Stop(Coral::Bool32 fade)
		{
			if (auto* scene = AudioScene())
				scene->GetMusicDirector().Stop(fade);
		}
		Coral::Bool32 Music_SetState(Coral::String state)
		{
			auto* scene = AudioScene();
			return scene && scene->GetMusicDirector().SetState(state);
		}
		Coral::Bool32 Music_SetIntensity(float value)
		{
			auto* scene = AudioScene();
			return scene && scene->GetMusicDirector().SetIntensity(value);
		}
		float Music_GetIntensity()
		{
			auto* scene = AudioScene();
			return scene ? scene->GetMusicDirector().GetIntensity() : 0.0f;
		}
		Coral::Bool32 Music_SetLayerEnabled(Coral::String layer, Coral::Bool32 enabled)
		{
			auto* scene = AudioScene();
			return scene && scene->GetMusicDirector().SetLayerEnabled(layer, enabled);
		}
		Coral::Bool32 Music_PlayStinger(Coral::String reference)
		{
			auto* scene = AudioScene();
			return scene && scene->GetMusicDirector().PlayStinger(reference);
		}
		Coral::Bool32 Music_QueueTransition(Coral::String reference, int32_t sync)
		{
			auto* scene = AudioScene();
			return scene && scene->GetMusicDirector().QueueTransition(reference, static_cast<MusicSync>(sync));
		}
		int32_t Music_GetBeat(Coral::Bool32 bar)
		{
			auto* scene = AudioScene();
			if (!scene)
				return 0;
			return bar ? scene->GetMusicDirector().GetCurrentBar() : scene->GetMusicDirector().GetCurrentBeat();
		}
		Coral::Bool32 Music_IsPlaying()
		{
			auto* scene = AudioScene();
			return scene && scene->GetMusicDirector().IsPlaying();
		}

		bool MixerAvailable()
		{
			if (!AudioScene())
				return false;
			if (AudioEngine::GetStudioSystem())
				return true;
			LUX_CORE_ERROR_TAG("Audio", "Mixer controls require an initialized FMOD Studio backend");
			return false;
		}

		Coral::Bool32 Audio_LoadBank(Coral::String file)
		{
			if (!MixerAvailable())
				return false;
			std::filesystem::path path = static_cast<std::string>(file);
			if (path.is_relative())
			{
				auto project = Project::GetActive();
				if (!project)
				{
					LUX_CORE_ERROR_TAG("Audio", "Relative bank paths require an active project");
					return false;
				}
				path = project->GetAssetDirectory() / path;
			}
			return AudioEngine::LoadBank(path);
		}

		void Audio_SetBusVolume(Coral::String name, float value)
		{
			if (MixerAvailable())
				AudioEngine::SetBusVolume(name, value);
		}
		float Audio_GetBusVolume(Coral::String name)
		{
			return MixerAvailable() ? AudioEngine::GetBusVolume(name) : 0.0f;
		}
		void Audio_SetBusMuted(Coral::String name, Coral::Bool32 value)
		{
			if (MixerAvailable())
				AudioEngine::SetBusMuted(name, value);
		}
		void Audio_SetVCAVolume(Coral::String name, float value)
		{
			if (MixerAvailable())
				AudioEngine::SetVCAVolume(name, value);
		}
		void Audio_SetGlobalParameter(Coral::String name, float value)
		{
			if (MixerAvailable())
				AudioEngine::SetGlobalParameter(name, value);
		}
		float Audio_GetGlobalParameter(Coral::String name)
		{
			return MixerAvailable() ? AudioEngine::GetGlobalParameter(name) : 0.0f;
		}
	} // namespace

	void AudioScriptBindings::Reset()
	{
		s_Instances.clear();
		AudioEventInstance::DrainNotifications();
		if (s_AudioType)
			s_AudioType->InvokeStaticMethod("Reset");
		if (s_MusicType)
			s_MusicType->InvokeStaticMethod("Reset");
		if (s_DialogueType)
			s_DialogueType->InvokeStaticMethod("Reset");
		if (s_AccessibilityType)
			s_AccessibilityType->InvokeStaticMethod("Reset");
	}

	void AudioScriptBindings::Shutdown()
	{
		Reset();
		s_AudioType = nullptr;
		s_MusicType = nullptr;
		s_DialogueType = nullptr;
		s_AccessibilityType = nullptr;
	}

	void AudioScriptBindings::Update(bool paused)
	{
		if (auto scene = ScriptEngine::GetInstance().GetCurrentScene(); scene && scene->IsRunning())
		{
			AudioAccessibility::SetScriptSoundCallback([](const SoundEvent& cue)
			{
				if (s_AccessibilityType)
					s_AccessibilityType->InvokeStaticMethod("DispatchSound", cue.Handle, static_cast<int32_t>(cue.Active), static_cast<int32_t>(cue.Category),
						cue.Position.x, cue.Position.y, cue.Position.z, cue.Direction.x, cue.Direction.y, cue.Direction.z, cue.Intensity);
			});
			scene->GetDialogueDirector().m_ScriptSubtitleCallback = [](const SubtitleEvent& event)
			{
				if (!s_DialogueType)
					return;
				Coral::ScopedString text = Coral::String::New(event.Text);
				Coral::ScopedString name = Coral::String::New(event.SpeakerName);
				Coral::ScopedString key = Coral::String::New(event.Key);
				Coral::ScopedString language = Coral::String::New(event.Language);
				s_DialogueType->InvokeStaticMethod("Dispatch", event.Handle, static_cast<int32_t>(event.Shown),
					static_cast<Coral::String>(text), static_cast<Coral::String>(name), static_cast<uint64_t>(event.SpeakerEntity),
					event.SpeakerPosition.x, event.SpeakerPosition.y, event.SpeakerPosition.z, event.Duration,
					static_cast<int32_t>(event.IsOffScreen), static_cast<Coral::String>(key), static_cast<Coral::String>(language), static_cast<int32_t>(event.IsCaption), static_cast<int32_t>(event.IsDescription));
			};
			scene->GetMusicDirector().m_ScriptTempoCallback = [](int bar, int beat)
			{
				if (s_MusicType)
					s_MusicType->InvokeStaticMethod("DispatchBeat", bar, beat);
			};
			scene->GetMusicDirector().m_ScriptMarkerCallback = [](const std::string& name)
			{
				if (s_MusicType)
				{
					Coral::ScopedString marker = Coral::String::New(name);
					s_MusicType->InvokeStaticMethod("DispatchMarker", static_cast<Coral::String>(marker));
				}
			};
		}
		for (auto& [handle, entry] : s_Instances)
			entry.Instance->SetScenePaused(paused);
		for (const auto& notification : AudioEventInstance::DrainNotifications())
		{
			if (!s_AudioType || !Instance(notification.Handle))
				continue;
			Coral::ScopedString marker = Coral::String::New(notification.Marker);
			s_AudioType->InvokeStaticMethod("Dispatch", notification.Handle, static_cast<int32_t>(notification.Stopped ? 0 : 1),
											static_cast<Coral::String>(marker));
		}
		std::erase_if(s_Instances, [](const auto& item) {
			return !item.second.Instance->IsValid() || (item.second.OneShot && !item.second.Instance->IsPlaying());
		});
	}

	void AudioScriptBindings::Register(Coral::ManagedAssembly& assembly)
	{
		s_AudioType = &assembly.GetLocalType("Lux.Audio");
		s_MusicType = &assembly.GetLocalType("Lux.Music");
		s_DialogueType = &assembly.GetLocalType("Lux.Dialogue");
		s_AccessibilityType = &assembly.GetLocalType("Lux.Accessibility");
		assembly.AddInternalCall("Lux.InternalCalls", "Accessibility_GetOption", reinterpret_cast<void*>(&Accessibility_GetOption));
		assembly.AddInternalCall("Lux.InternalCalls", "Accessibility_SetOption", reinterpret_cast<void*>(&Accessibility_SetOption));
		assembly.AddInternalCall("Lux.InternalCalls", "Accessibility_Save", reinterpret_cast<void*>(&Accessibility_Save));
		assembly.AddInternalCall("Lux.InternalCalls", "Accessibility_HasBus", reinterpret_cast<void*>(&Accessibility_HasBus));
		assembly.AddInternalCall("Lux.InternalCalls", "Accessibility_GetSpeakerColor", reinterpret_cast<void*>(&Accessibility_GetSpeakerColor));
		assembly.AddInternalCall("Lux.InternalCalls", "Accessibility_GetCueCount", reinterpret_cast<void*>(&Accessibility_GetCueCount));
		assembly.AddInternalCall("Lux.InternalCalls", "Accessibility_GetCue", reinterpret_cast<void*>(&Accessibility_GetCue));
		assembly.AddInternalCall("Lux.InternalCalls", "Dialogue_Describe", reinterpret_cast<void*>(&Dialogue_Describe));
		assembly.AddInternalCall("Lux.InternalCalls", "Dialogue_Speak", reinterpret_cast<void*>(&Dialogue_Speak));
		assembly.AddInternalCall("Lux.InternalCalls", "Dialogue_Stop", reinterpret_cast<void*>(&Dialogue_Stop));
		assembly.AddInternalCall("Lux.InternalCalls", "Dialogue_StopAll", reinterpret_cast<void*>(&Dialogue_StopAll));
		assembly.AddInternalCall("Lux.InternalCalls", "Dialogue_Query", reinterpret_cast<void*>(&Dialogue_Query));
		assembly.AddInternalCall("Lux.InternalCalls", "Dialogue_GetQueueLength", reinterpret_cast<void*>(&Dialogue_GetQueueLength));
		assembly.AddInternalCall("Lux.InternalCalls", "Dialogue_SetLanguage", reinterpret_cast<void*>(&Dialogue_SetLanguage));
		assembly.AddInternalCall("Lux.InternalCalls", "Dialogue_SetQueueMode", reinterpret_cast<void*>(&Dialogue_SetQueueMode));

		assembly.AddInternalCall("Lux.InternalCalls", "Music_Play", reinterpret_cast<void*>(&Music_Play));
		assembly.AddInternalCall("Lux.InternalCalls", "Music_Stop", reinterpret_cast<void*>(&Music_Stop));
		assembly.AddInternalCall("Lux.InternalCalls", "Music_SetState", reinterpret_cast<void*>(&Music_SetState));
		assembly.AddInternalCall("Lux.InternalCalls", "Music_SetIntensity", reinterpret_cast<void*>(&Music_SetIntensity));
		assembly.AddInternalCall("Lux.InternalCalls", "Music_GetIntensity", reinterpret_cast<void*>(&Music_GetIntensity));
		assembly.AddInternalCall("Lux.InternalCalls", "Music_SetLayerEnabled", reinterpret_cast<void*>(&Music_SetLayerEnabled));
		assembly.AddInternalCall("Lux.InternalCalls", "Music_PlayStinger", reinterpret_cast<void*>(&Music_PlayStinger));
		assembly.AddInternalCall("Lux.InternalCalls", "Music_QueueTransition", reinterpret_cast<void*>(&Music_QueueTransition));
		assembly.AddInternalCall("Lux.InternalCalls", "Music_GetBeat", reinterpret_cast<void*>(&Music_GetBeat));
		assembly.AddInternalCall("Lux.InternalCalls", "Music_IsPlaying", reinterpret_cast<void*>(&Music_IsPlaying));

		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SetSnapshotIntensity", reinterpret_cast<void*>(&Audio_SetSnapshotIntensity));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_PortalGetScalar", reinterpret_cast<void*>(&Audio_PortalGetScalar));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_PortalSetScalar", reinterpret_cast<void*>(&Audio_PortalSetScalar));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_PortalGetExtents", reinterpret_cast<void*>(&Audio_PortalGetExtents));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_PortalSetExtents", reinterpret_cast<void*>(&Audio_PortalSetExtents));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_PortalGetRoom", reinterpret_cast<void*>(&Audio_PortalGetRoom));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_PortalSetRoom", reinterpret_cast<void*>(&Audio_PortalSetRoom));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_MeshGetMotion", reinterpret_cast<void*>(&Audio_MeshGetMotion));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_MeshSetMotion", reinterpret_cast<void*>(&Audio_MeshSetMotion));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_ZoneGetScalar", reinterpret_cast<void*>(&Audio_ZoneGetScalar));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_ZoneSetScalar", reinterpret_cast<void*>(&Audio_ZoneSetScalar));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_ZoneGetVector", reinterpret_cast<void*>(&Audio_ZoneGetVector));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_ZoneSetVector", reinterpret_cast<void*>(&Audio_ZoneSetVector));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_ZoneSetEvent", reinterpret_cast<void*>(&Audio_ZoneSetEvent));

		assembly.AddInternalCall("Lux.InternalCalls", "Audio_PlayFootstep", reinterpret_cast<void*>(&Audio_PlayFootstep));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_GetSurfaceMaterial", reinterpret_cast<void*>(&Audio_GetSurfaceMaterial));
		// Registrations below are kept one-to-one with InternalCalls.cs.
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_LoadBank", reinterpret_cast<void*>(&Audio_LoadBank));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_IsMainThread", reinterpret_cast<void*>(&Audio_IsMainThread));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_CreateInstance", reinterpret_cast<void*>(&Audio_CreateInstance));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_IsValid", reinterpret_cast<void*>(&Audio_IsValid));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_IsPlaying", reinterpret_cast<void*>(&Audio_IsPlaying));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_Start", reinterpret_cast<void*>(&Audio_Start));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_Stop", reinterpret_cast<void*>(&Audio_Stop));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_Dispose", reinterpret_cast<void*>(&Audio_Dispose));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SetVolume", reinterpret_cast<void*>(&Audio_SetVolume));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SetPitch", reinterpret_cast<void*>(&Audio_SetPitch));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SetParameter", reinterpret_cast<void*>(&Audio_SetParameter));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_Set3DAttributes", reinterpret_cast<void*>(&Audio_Set3DAttributes));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SourceGetPriority", reinterpret_cast<void*>(&Audio_SourceGetPriority));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SourceSetPriority", reinterpret_cast<void*>(&Audio_SourceSetPriority));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SourceGetCulling", reinterpret_cast<void*>(&Audio_SourceGetCulling));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SourceSetCulling", reinterpret_cast<void*>(&Audio_SourceSetCulling));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SourceIsCulled", reinterpret_cast<void*>(&Audio_SourceIsCulled));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SourcePlay", reinterpret_cast<void*>(&Audio_SourcePlay));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SourceStop", reinterpret_cast<void*>(&Audio_SourceStop));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SourceIsPlaying", reinterpret_cast<void*>(&Audio_SourceIsPlaying));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SourceIsPaused", reinterpret_cast<void*>(&Audio_SourceIsPaused));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SourceSetPaused", reinterpret_cast<void*>(&Audio_SourceSetPaused));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SourceGetVolume", reinterpret_cast<void*>(&Audio_SourceGetVolume));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SourceGetPitch", reinterpret_cast<void*>(&Audio_SourceGetPitch));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SourceSetVolume", reinterpret_cast<void*>(&Audio_SourceSetVolume));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SourceSetPitch", reinterpret_cast<void*>(&Audio_SourceSetPitch));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SourceHasEvent", reinterpret_cast<void*>(&Audio_SourceHasEvent));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SourceSetParameter", reinterpret_cast<void*>(&Audio_SourceSetParameter));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SourceGetParameter", reinterpret_cast<void*>(&Audio_SourceGetParameter));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SourceSetParameterLabel", reinterpret_cast<void*>(&Audio_SourceSetParameterLabel));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SourceGetTimeline", reinterpret_cast<void*>(&Audio_SourceGetTimeline));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SourceSetTimeline", reinterpret_cast<void*>(&Audio_SourceSetTimeline));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SourceSetEvent", reinterpret_cast<void*>(&Audio_SourceSetEvent));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_ListenerGetActive", reinterpret_cast<void*>(&Audio_ListenerGetActive));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_ListenerSetActive", reinterpret_cast<void*>(&Audio_ListenerSetActive));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_ListenerGetIndex", reinterpret_cast<void*>(&Audio_ListenerGetIndex));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_ListenerSetIndex", reinterpret_cast<void*>(&Audio_ListenerSetIndex));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_ListenerGetWeight", reinterpret_cast<void*>(&Audio_ListenerGetWeight));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_ListenerSetWeight", reinterpret_cast<void*>(&Audio_ListenerSetWeight));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_ListenerGetUseTarget", reinterpret_cast<void*>(&Audio_ListenerGetUseTarget));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_ListenerSetUseTarget", reinterpret_cast<void*>(&Audio_ListenerSetUseTarget));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_ListenerGetTarget", reinterpret_cast<void*>(&Audio_ListenerGetTarget));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_ListenerSetTarget", reinterpret_cast<void*>(&Audio_ListenerSetTarget));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SetBusVolume", reinterpret_cast<void*>(&Audio_SetBusVolume));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_GetBusVolume", reinterpret_cast<void*>(&Audio_GetBusVolume));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SetBusMuted", reinterpret_cast<void*>(&Audio_SetBusMuted));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SetVCAVolume", reinterpret_cast<void*>(&Audio_SetVCAVolume));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_SetGlobalParameter", reinterpret_cast<void*>(&Audio_SetGlobalParameter));
		assembly.AddInternalCall("Lux.InternalCalls", "Audio_GetGlobalParameter", reinterpret_cast<void*>(&Audio_GetGlobalParameter));
	}
} // namespace Lux
