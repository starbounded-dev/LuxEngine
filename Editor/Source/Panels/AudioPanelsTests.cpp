// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioMixerPanel.h"
#include "AudioDebugPanel.h"
#include "SoundBankEditorPanel.h"
#include "SoundEditor/SoundEditorPanel.h"

#ifndef LUX_DIST
#include "Lux/Asset/AssetImporter.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Asset/SoundSerializer.h"
#include "Lux/Audio/AudioEngine.h"
#include "Lux/Core/Events/KeyEvent.h"
#include "Lux/ImGui/ImGuiScaling.h"
#include "Lux/ImGui/PanelControls.h"
#include "Lux/Project/Project.h"
#include "Lux/Scene/SceneSerializer.h"
#include "Lux/Serialization/Yaml.h"
#include "Lux/Utilities/FileSystem.h"
#include "Lux/Utilities/StringUtils.h"
#include <imgui/imgui_internal.h>
#include <stdexcept>

namespace Lux
{
	namespace
	{
		struct AudioPanelTestContext
		{
			ImGuiContext* Previous = ImGui::GetCurrentContext();
			ImGuiContext* Context = ImGui::CreateContext();
			AudioPanelTestContext()
			{
				ImGui::SetCurrentContext(Context);
				auto& io = ImGui::GetIO();
				io.IniFilename = nullptr;
				io.DisplaySize = {1920.0f, 1080.0f};
				io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard;
				io.ConfigDebugHighlightIdConflicts = true;
				io.Fonts->AddFontDefault();
				unsigned char* pixels = nullptr;
				int width = 0, height = 0;
				io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
			}
			~AudioPanelTestContext()
			{
				ImGui::DestroyContext(Context);
				ImGui::SetCurrentContext(Previous);
			}
		};
		void Require(bool value, const char* reason)
		{
			if (!value)
				throw std::runtime_error(reason);
		}
		void Frame(EditorPanel& panel, const char* name, float width)
		{
			ImGui::SetWindowSize(name, {width, 750.0f});
			ImGui::NewFrame();
			ImGui::SetNextWindowPos({0.0f, 0.0f});
			ImGui::SetNextWindowSize({width, 750.0f});
			bool open = true;
			panel.OnImGuiRender(open);
			ImGui::Render();
		}
		void CheckWidth(const char* name)
		{
			const auto* root = ImGui::FindWindowByName(name);
			Require(root, "Audio panel root missing");
			for (const auto* window : GImGui->Windows)
			{
				if (!window->Active)
					continue;
				const auto* parent = window;
				while (parent->ParentWindow)
					parent = parent->ParentWindow;
				const float submitted = window->DC.CursorMaxPos.x - window->DC.CursorStartPos.x;
				if (parent == root && submitted > window->Size.x + 1.0f)
					throw std::runtime_error(std::format("Audio reflow: {} submitted {} width {}",
														 window->Name, submitted, window->Size.x));
			}
		}
		struct FailedSaveFixture
		{
			std::filesystem::path Path, Backup;
			explicit FailedSaveFixture(std::filesystem::path path) : Path(std::move(path)), Backup(Path)
			{
				Backup += ".before-failed-save";
				Require(FileSystem::Rename(Path, Backup), "Cannot preserve private audio save fixture");
				Require(FileSystem::CreateDirectory(Path), "Cannot construct private audio save failure");
			}
			~FailedSaveFixture()
			{
				std::error_code error;
				const bool removed = std::filesystem::remove(Path, error);
				if (removed)
					std::filesystem::rename(Backup, Path, error);
				if (!removed || error)
					LUX_CORE_ERROR_TAG("Editor", "Cannot restore private audio save fixture '{}'",
									   Utils::PathToUTF8(Path));
			}
		};
		AssetHandle CreateSound(const std::filesystem::path& path)
		{
			const auto sound = Sound::CreateForClip(0);
			Require(
				FileSystem::WriteFileAtomically(Project::GetActiveAssetDirectory() / path, sound->ToYAML()),
				"Cannot write private Sound fixture");
			return Project::GetEditorAssetManager()->ImportAsset(path);
		}
		std::string ReadSource(const std::filesystem::path& path)
		{
			ScopedBuffer bytes(FileSystem::ReadBytes(path));
			Require(static_cast<bool>(bytes), "Cannot read the private authored audio result");
			return {reinterpret_cast<const char*>(bytes.Data()), static_cast<size_t>(bytes.Size())};
		}
		AssetHandle CreateBank()
		{
			const std::filesystem::path path(u8"Audio/\u00e9##Bank.lbank");
			SoundBank bank;
			Require(FileSystem::WriteFileAtomically(Project::GetActiveAssetDirectory() / path, bank.ToYAML()),
					"Cannot write private Sound Bank fixture");
			return Project::GetEditorAssetManager()->ImportAsset(path);
		}
		template <typename TSerializer, typename TAsset>
		void CheckPack(TSerializer& serializer, AssetHandle handle, const std::filesystem::path& path)
		{
			AssetSerializationInfo info;
			{
				FileStreamWriter writer(path);
				Require(serializer.SerializeToAssetPack(handle, writer, info) && writer.Flush(),
						"Authored audio pack write failed");
			}
			FileStreamReader reader(path);
			const AssetPackFile::AssetInfo entry{info.Offset, info.Size, 0, 0};
			const auto restored = serializer.DeserializeFromAssetPack(reader, entry).template As<TAsset>();
			const auto source = AssetManager::GetAsset<TAsset>(handle);
			Require(restored && source && restored->ToYAML() == source->ToYAML(),
					"Packed audio result lost authored data");
		}
	} // namespace

	bool SoundBankEditorPanel::RunSelfTests(std::vector<std::string>& failures)
	{
		AudioPanelTestContext context;
		try
		{
			Require(Project::GetActive() != nullptr, "Audio panel tests require the private runner project");
			const auto assets = Project::GetActiveAssetDirectory();
			Require(FileSystem::CreateDirectory(assets / "Audio"),
					"Cannot create private audio asset folder");
			const AssetHandle sound = CreateSound("Audio/One.lsound");
			const AssetHandle second = CreateSound("Audio/Two.lsound");
			const AssetHandle other = CreateSound("Audio/Three.lsound");
			const AssetHandle handle = CreateBank();
			Require(sound && second && other && handle, "Audio panel fixture imports failed");
			auto bank = AssetManager::GetAsset<SoundBank>(handle);
			Require(bank != nullptr, "Sound Bank fixture did not load");
			SoundBankEditorPanel panel;
			std::vector<std::function<void()>> undo, redo;
			panel.SetUndoCallback(
				[&](const std::string&, std::function<void()> before, std::function<void()> after)
				{
					undo.push_back(std::move(before));
					redo.push_back(std::move(after));
				});
			panel.OpenBank(handle);
			SoundBank next;
			next.Sounds = {sound, second};
			Require(panel.Commit(next, "Bank test") && undo.size() == 1 && bank->Contains(sound),
					"Bank edit was not saved/undoable");
			auto manager = Project::GetEditorAssetManager();
			Require(manager->GetDependencies(handle) == std::unordered_set<AssetHandle>{sound, second},
					"Bank save lost its Sound dependencies");
			Require(manager->ReloadData(handle), "Bank dependency reload failed");
			bank = AssetManager::GetAsset<SoundBank>(handle);
			Require(bank &&
						manager->GetDependencies(handle) == std::unordered_set<AssetHandle>{sound, second},
					"Bank load miscast the asset or erased serializer dependencies");
			undo.back()();
			Require(bank->Sounds.empty(), "Bank undo did not restore membership");
			redo.back()();
			Require(bank->Contains(sound), "Bank redo lost its member");
			const auto path = Project::GetEditorAssetManager()->GetFileSystemPath(handle);
			const std::string saved = ReadSource(path);
			{
				FailedSaveFixture failure(path);
				next.Resident = true;
				Require(!panel.Commit(next, "Failed bank test") && !bank->Resident && undo.size() == 1 &&
							!panel.m_Error.empty(),
						"A failed bank save published data or pushed undo");
			}
			Require(ReadSource(path) == saved, "Failed bank save damaged its original file");
			Require(panel.Commit(next, "Retry bank test") && bank->Resident,
					"Bank save retry did not apply the change");
			next.Resident = false;
			Require(panel.Commit(next, "Resident reset"), "Could not restore private bank residency");
			ImGuiEx::DpiStyle dpi;
			dpi.Capture(ImGui::GetStyle());
			for (float scale : {1.0f, 1.5f})
			{
				dpi.Apply(ImGui::GetStyle(), scale);
				ImGui::GetStyle().FontScaleMain = scale;
				for (float width : {1200.0f, 500.0f, 280.0f, 190.0f, 1200.0f})
				{
					Frame(panel, "Sound Bank Editor", width);
					Frame(panel, "Sound Bank Editor", width);
					CheckWidth("Sound Bank Editor");
					Require(bank->Sounds == next.Sounds && !bank->Resident,
							"Bank reflow changed authored data");
				}
			}
			Require(!panel.m_MemoryDirty && panel.m_EstimatedSounds == bank->Sounds,
					"Bank memory cache did not settle");
			ImGui::NewFrame();
			ImGui::Begin("Bank inventory");
			ImGui::SetNextItemOpen(true);
			panel.UI_Unbanked(*bank);
			ImGui::End();
			ImGui::Render();
			Require(!panel.m_UnbankedDirty && std::ranges::any_of(panel.m_Unbanked, [&](const auto& item)
																  { return item.second == other; }),
					"Bank inventory missed the unbanked Sound");
			SoundBankSerializer serializer;
			CheckPack<SoundBankSerializer, SoundBank>(
				serializer, handle, Project::GetActive()->GetProjectDirectory() / "Bank.pack");
			panel.m_IsFocused = true;
			ImGui::GetIO().WantTextInput = true;
			KeyPressedEvent typing(Key::D, 0);
			panel.OnEvent(typing);
			Require(typing.Handled, "Bank picker leaked a scene shortcut");
			panel.OnProjectChanged(nullptr);
			Require(!panel.m_Bank && !panel.m_IsFocused && panel.m_Unbanked.empty(),
					"Bank project change retained context");
			Frame(panel, "Sound Bank Editor", 190.0f);
			CheckWidth("Sound Bank Editor");
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		return failures.empty();
	}

	bool SoundEditorPanel::RunPersistenceSelfTests(std::vector<std::string>& failures)
	{
		AudioPanelTestContext context;
		try
		{
			const auto handle =
				Project::GetEditorAssetManager()->GetAssetHandleFromFilePath("Audio/One.lsound");
			auto sound = AssetManager::GetAsset<Sound>(handle);
			Require(sound != nullptr, "Sound save fixture missing");
			SoundEditorPanel panel;
			panel.OpenSound(handle);
			auto* document = panel.GetActiveDocument();
			Require(document, "Sound document did not open");
			const auto path = Project::GetEditorAssetManager()->GetFileSystemPath(handle);
			const auto saved = document->Saved;
			sound->Volume = 0.35f;
			{
				FailedSaveFixture failure(path);
				Require(!panel.SaveDocument(*document, *sound) && document->Saved == saved &&
							sound->Volume == 0.35f,
						"Failed Sound save cleared dirty work");
			}
			Require(panel.SaveDocument(*document, *sound) && document->Saved.Volume == 0.35f,
					"Sound save retry did not commit its saved snapshot");
			Require(ReadSource(path) == sound->ToYAML(),
					"Sound source bytes diverged from the bounded packed YAML");
			Sound restored;
			Require(restored.FromYAML(ReadSource(path)) && restored.Volume == 0.35f,
					"Sound saved result lost authored volume");
			SoundSerializer serializer;
			CheckPack<SoundSerializer, Sound>(serializer, handle,
											  Project::GetActive()->GetProjectDirectory() / "Sound.pack");
			panel.OnProjectChanged(nullptr);
			Require(panel.m_Documents.empty() && !panel.m_Audition,
					"Sound save test retained document/audition context");
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		return failures.empty();
	}

	bool SoundEditorPanel::RunEditorSelfTests(std::vector<std::string>& failures)
	{
		AudioPanelTestContext context;
		try
		{
			auto manager = Project::GetEditorAssetManager();
			const auto clip = manager->GetOrImportAsset("Tone.wav");
			Require(clip && AssetManager::GetAssetType(clip) == AssetType::Audio,
					"Real waveform fixture missing");
			auto authored = Sound::CreateForClip(clip);
			authored->Volume =
				0.0f; // Exercise real playback without making the developer's speakers audible.
			authored->WrapNode(0, SoundNodeType::Layer);
			for (auto type : {SoundNodeType::Random, SoundNodeType::Sequence, SoundNodeType::Switch})
			{
				const auto container = authored->AddNode(0, type);
				if (type == SoundNodeType::Switch)
					authored->Nodes[container].SwitchParameter = "Surface";
				for (int child = 0; child < 2; ++child)
					authored->Nodes[authored->AddNode(container, SoundNodeType::Clip)].Clip = clip;
			}
			authored->Parameters = {{"Intensity", 0.0f, 1.0f, 0.25f, {}},
									{"Surface", 0.0f, 1.0f, 0.0f, {"Stone", "Wood"}}};
			authored->Nodes[1].Name = std::string(40, 'N') + " ## 100%";
			authored->Nodes[1].Loop = authored->Nodes[1].HasLoopRegion = true;
			authored->Nodes[1].LoopStart = 0.01f;
			authored->Nodes[1].LoopEnd = 0.2f;
			authored->Nodes[1].Curves = {
				{"Intensity", SoundCurveTarget::Volume, {{0.0f, 0.2f}, {1.0f, 1.0f}}}};
			authored->Tempo.BPM = 120.0f;
			authored->Markers = {{"Section:Start", 0.05f}};
			Require(authored->Validate(), "Sound Editor authored fixture is invalid");
			const std::filesystem::path relative(u8"Audio/\u00e9##Editor.lsound");
			Require(FileSystem::WriteFileAtomically(Project::GetActiveAssetDirectory() / relative,
													authored->ToYAML()),
					"Cannot write Sound Editor fixture");
			const auto handle = manager->ImportAsset(relative);
			auto sound = AssetManager::GetAsset<Sound>(handle);
			Require(sound && manager->GetDependencies(handle).contains(clip),
					"Sound load lost clip dependencies");
			SoundEditorPanel panel;
			std::vector<std::function<void()>> undo, redo;
			panel.SetUndoCallback(
				[&](const std::string&, auto before, auto after)
				{
					undo.push_back(std::move(before));
					redo.push_back(std::move(after));
				});
			panel.OpenSound(handle);
			panel.GetActiveDocument()->SelectedNode = 1;
			panel.LoadState(Yaml::Load("Section: Node\nTreeFraction: 0.3\nShowTree: true\n"));
			const auto original = SoundState::Capture(*sound);
			ImGuiEx::DpiStyle dpi;
			dpi.Capture(ImGui::GetStyle());
			for (float scale : {1.0f, 1.5f})
			{
				dpi.Apply(ImGui::GetStyle(), scale);
				ImGui::GetStyle().FontScaleMain = scale;
				for (uint32_t section = 0; section < 4; ++section)
				{
					panel.m_Section = section;
					for (float width : {1500.0f, 650.0f, 280.0f, 190.0f, 1500.0f})
					{
						Frame(panel, "Sound Editor", width);
						Frame(panel, "Sound Editor", width);
						try
						{
							CheckWidth("Sound Editor");
						}
						catch (const std::exception& error)
						{
							throw std::runtime_error(std::format("Section {} width {} scale {}: {}", section,
																 width, scale, error.what()));
						}
						Require(SoundState::Capture(*sound) == original && panel.m_TreeFraction == 0.3f,
								"Sound reflow changed authored data or the user's split");
					}
				}
				panel.m_NarrowTree = true;
				Frame(panel, "Sound Editor", 190.0f);
				Frame(panel, "Sound Editor", 190.0f);
				CheckWidth("Sound Editor");
				panel.m_NarrowTree = false;
				// Music and marker controls, usually collapsed, must also reflow.
				ImGui::NewFrame();
				ImGui::SetNextWindowSize({190.0f, 750.0f});
				ImGui::Begin("Music controls");
				ImGui::GetStateStorage()->SetInt(ImGui::GetID("Music"), 1);
				bool edited = false;
				panel.UI_SoundProperties(*sound, edited);
				ImGui::End();
				ImGui::Render();
				CheckWidth("Music controls");
				Require(!edited, "Viewing music controls changed the Sound");
			}
			Require(!panel.GetWaveform(clip).Peaks.empty(), "Real clip waveform was not decoded");
			// Drive the production canvases with actual ImGui input and the owner's undo grouping.
			auto& doc = *panel.GetActiveDocument();
			auto& node = sound->Nodes[1];
			ImRect canvas;
			const auto canvasFrame = [&](int kind)
			{
				ImGui::NewFrame();
				ImGui::SetNextWindowPos({0.0f, 0.0f});
				ImGui::SetNextWindowSize({650.0f, 750.0f});
				ImGui::Begin("Sound canvas input");
				bool edited = false;
				if (kind == 0)
					panel.UI_Clip(node, edited);
				else if (kind == 1)
					panel.UI_MusicTimeline(*sound, edited);
				else
					edited = panel.UI_CurveEditor(doc, *sound, node.Curves.front(), 4096);
				canvas = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
				panel.TrackEdit(doc, *sound, edited);
				ImGui::End();
				ImGui::Render();
			};
			auto& io = ImGui::GetIO();
			canvasFrame(0);
			const auto beforeWave = undo.size();
			io.AddMousePosEvent(canvas.Min.x + canvas.GetWidth() * 0.6f, canvas.GetCenter().y);
			canvasFrame(0);
			io.AddMouseButtonEvent(0, true);
			canvasFrame(0);
			io.AddMouseButtonEvent(0, false);
			canvasFrame(0);
			Require(node.StartOffset > 0.14f && node.StartOffset < 0.16f && undo.size() == beforeWave + 1,
					"Waveform input lost seconds or its undo transaction");
			canvasFrame(1);
			const auto beforeMarker = undo.size();
			io.AddMousePosEvent(canvas.Min.x + canvas.GetWidth() * 0.2f, canvas.GetCenter().y);
			canvasFrame(1);
			io.AddMouseButtonEvent(0, true);
			canvasFrame(1);
			io.AddMousePosEvent(canvas.Min.x + canvas.GetWidth() * 0.4f, canvas.GetCenter().y);
			canvasFrame(1);
			io.AddMouseButtonEvent(0, false);
			canvasFrame(1);
			Require(sound->Markers.front().Seconds > 0.09f && sound->Markers.front().Seconds < 0.11f &&
						undo.size() == beforeMarker + 1,
					"Music marker drag lost seconds or its undo transaction");
			canvasFrame(2);
			const auto beforeCurve = undo.size();
			io.AddMousePosEvent(canvas.GetCenter().x, canvas.GetCenter().y);
			canvasFrame(2);
			io.AddMouseButtonEvent(0, true);
			canvasFrame(2);
			io.AddMouseButtonEvent(0, false);
			canvasFrame(2);
			io.AddMouseButtonEvent(0, true);
			canvasFrame(2);
			io.AddMouseButtonEvent(0, false);
			canvasFrame(2);
			Require(node.Curves.front().Points.size() == 3 && undo.size() == beforeCurve + 1 &&
						sound->Validate(),
					"Curve point input lost data or its undo transaction");
			// Hot-reloaded clip bytes must invalidate the waveform rather than displaying stale peaks.
			const auto tonePath = Project::GetActiveAssetDirectory() / "Tone.wav";
			ScopedBuffer tone(FileSystem::ReadBytes(tonePath));
			Require(tone && tone.Size() > 44, "Cannot read private PCM fixture");
			std::memset(static_cast<uint8_t*>(tone.Data()) + 44, 0, tone.Size() - 44);
			Require(FileSystem::WriteBytes(tonePath, Buffer(tone.Data(), tone.Size())) &&
						manager->ReloadData(clip),
					"Could not reload changed private clip bytes");
			const auto& refreshed = panel.GetWaveform(clip);
			Require(!refreshed.Peaks.empty() &&
						std::ranges::all_of(refreshed.Peaks, [](float peak) { return peak == 0.0f; }),
					"Clip reload retained stale waveform peaks");
			panel.LoadState(Yaml::Load("TreeFraction: .nan\n"));
			Require(panel.m_TreeFraction == 0.3f, "Invalid Sound split replaced user preference");
			Yaml::Writer state;
			state << Yaml::BeginMap;
			panel.SaveState(state);
			state << Yaml::EndMap;
			SoundEditorPanel restored;
			restored.LoadState(Yaml::Load(state.c_str()));
			Require(restored.m_TreeFraction == panel.m_TreeFraction &&
						restored.m_Section == panel.m_Section && restored.m_ShowTree == panel.m_ShowTree,
					"Sound view state did not round trip");
			const auto count = undo.size();
			sound->Pitch = 1.25f;
			panel.GetActiveDocument()->EditInProgress = true;
			panel.OnClose();
			Require(undo.size() == count + 1 && !panel.GetActiveDocument()->EditInProgress,
					"Hiding Sound lost the current edit transaction");
			undo.back()();
			Require(sound->Pitch == 1.0f, "Sound hide undo failed");
			redo.back()();
			Require(sound->Pitch == 1.25f, "Sound hide redo failed");
			const auto bankHandle =
				manager->GetAssetHandleFromFilePath(std::filesystem::path(u8"Audio/\u00e9##Bank.lbank"));
			auto bank = AssetManager::GetAsset<SoundBank>(bankHandle);
			const auto members = bank->Sounds;
			const auto beforeAdd = undo.size();
			{
				FailedSaveFixture fail(manager->GetFileSystemPath(bankHandle));
				Require(!panel.AddToBank(bankHandle, handle) && bank->Sounds == members &&
							undo.size() == beforeAdd && !panel.m_BankError.empty(),
						"Sound's failed bank edit published membership or undo");
			}
			Require(panel.AddToBank(bankHandle, handle) && bank->Contains(handle) &&
						undo.size() == beforeAdd + 1,
					"Sound bank retry failed");
			undo.back()();
			Require(bank->Sounds == members, "Sound bank undo lost membership");
			redo.back()();
			Require(bank->Contains(handle), "Sound bank redo failed");
			undo.back()(); // Keep the Mixer scene-bank fixture silent and unchanged.
			Sound invalid;
			invalid.Nodes.clear();
			panel.StartAudition(*panel.GetActiveDocument(), invalid);
			Require(!panel.m_Audition && !panel.m_AuditionError.empty(), "Failed Sound audition was silent");
			panel.m_Section = 0;
			Frame(panel, "Sound Editor", 190.0f);
			Frame(panel, "Sound Editor", 190.0f);
			CheckWidth("Sound Editor");
			panel.StartAudition(*panel.GetActiveDocument(), *sound);
			Require(panel.m_Audition && panel.m_Audition->IsPlaying(), "Real Sound audition did not start");
			auto held = panel.m_Audition;
			panel.OpenSound(manager->GetAssetHandleFromFilePath("Audio/Two.lsound"));
			Require(!panel.m_Audition && !held->IsPlaying(), "Switching Sound retained the old audition");
			panel.OpenSound(handle);
			panel.RequestCloseDocument(panel.m_ActiveDocument);
			Require(panel.m_PendingCloseIndex >= 0, "Dirty Sound closed without a decision");
			Frame(panel, "Sound Editor", 650.0f);
			Require(panel.m_ClosePopupID &&
						ImGui::IsPopupOpen(panel.m_ClosePopupID, ImGuiPopupFlags_AnyPopupLevel),
					"Sound close decision was not shown");
			panel.OnClose();
			Require(!ImGui::IsPopupOpen(panel.m_ClosePopupID, ImGuiPopupFlags_AnyPopupLevel) &&
						panel.m_OpenCloseConfirm,
					"Hidden Sound retained a modal or lost its pending decision");
			Frame(panel, "Sound Editor", 650.0f);
			Require(ImGui::IsPopupOpen(panel.m_ClosePopupID, ImGuiPopupFlags_AnyPopupLevel),
					"Sound close decision did not resume");
			panel.OnProjectChanged(nullptr);
			Require(panel.m_Documents.empty() && !panel.m_OpenCloseConfirm && !panel.m_KeyboardFocus &&
						!panel.m_RequestFocus,
					"Sound project change retained document/focus/modal state");
			Frame(panel, "Sound Editor", 190.0f);
			CheckWidth("Sound Editor");
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		return failures.empty();
	}

	bool AudioDebugPanel::RunSelfTests(std::vector<std::string>& failures)
	{
		AudioPanelTestContext context;
		try
		{
			auto scene = Ref<Scene>::Create();
			const auto sound =
				Project::GetEditorAssetManager()->GetAssetHandleFromFilePath("Audio/One.lsound");
			UUID selected = 0;
			for (int index = 0; index < 180; ++index)
			{
				auto entity = scene->CreateEntity(index == 0 ? "Long ## Source 100% " + std::string(100, 'S')
															 : std::format("Source {}", index));
				auto& source = entity.AddComponent<AudioSourceComponent>();
				source.Enabled = false;
				source.Event.Sound = sound;
				if (index == 0)
				{
					selected = entity.GetUUID();
					entity.Transform().Translation = {3.0f, 4.0f, 0.0f};
				}
			}
			scene->CreateEntity("Listener").AddComponent<AudioListenerComponent>();
			scene->OnRuntimeStart();
			AudioDebugPanel panel;
			const auto check = [&]()
			{
				try
				{
					CheckWidth("Audio Debugger");
				}
				catch (const std::exception& error)
				{
					throw std::runtime_error(std::format("Diagnostic state page {} engine {} acoustic {}: {}",
														 panel.m_Page, panel.m_Engine.Initialized,
														 panel.m_Acoustics.Ready, error.what()));
				}
			};
			panel.SetSceneContext(scene);
			panel.CaptureReadings();
			scene->OnRuntimeStop();
			Require(
				SceneSerializer(scene).Serialize(Project::GetActiveAssetDirectory() / "Diagnostics.luxscene"),
				"Cannot save private diagnostics scene");
			Require(panel.m_Sources.size() == 180 && panel.m_FilteredSources.size() == 180 &&
						panel.m_HaveListener,
					"Audio diagnostics missed captured sources/listener");
			const auto found = std::ranges::find(panel.m_Sources, selected, &SourceRow::Entity);
			Require(found != panel.m_Sources.end() && found->Distance && *found->Distance == 5.0f &&
						!found->Simulated,
					"Captured distance or missing-acoustics semantics changed");
			const auto ownedName = found->Name;
			scene->DestroyEntity(scene->GetEntityWithUUID(selected));
			const auto sceneBeforeViews = SceneSerializer(scene).SerializeToString();
			panel.m_Paused = true;
			panel.m_SelectedSource = selected;
			// Exercise available and unavailable readings without fabricating engine instrumentation.
			// These are owned presentation fixtures; CaptureReadings above uses the real scene.
			found->Simulated = true;
			found->GainLF = 0.25f;
			found->GainHF = 0.125f;
			found->Paths = "1R 2O x2 !";
			panel.m_Acoustics.Ready = true;
			panel.m_Acoustics.StaticTriangles = 12;
			panel.m_Reverb = Soundwave::ReverbResult{};
			panel.m_Reverb->DecaySeconds = 0.75f;
			panel.m_ReverbSend = 0.5f;
			panel.m_Engine.HasMixerStats = true;
			panel.m_Engine.ChannelsPlaying = 3;
			panel.m_Engine.RealChannelsPlaying = 2;
			panel.m_Performance.MixerAvailable = panel.m_Performance.MemoryAvailable = true;
			panel.m_Performance.RealVoices = 3;
			panel.m_Performance.MemoryMiB = 2.5;
			panel.m_Meters = {{"bus:/" + std::string(120, 'M'), 8, 3, 2, 0.5f, 0.25f, true, false}};
			panel.m_Banks = {{"Audio/" + std::string(100, 'B') + "##Bank.lbank", {UUID(), 2, true, true}}};
			panel.m_SelectedBank = panel.m_Banks.front().second.Bank;
			panel.m_Clips = {{std::filesystem::path(u8"Audio/\u00e9##Tone.wav"), 1, 2, 0, 4096}};
			panel.m_SelectedClip = panel.m_Clips.front().File;
			panel.m_HasValidation = true;
			panel.m_Validation.Issues.push_back({AudioValidationSeverity::Warning, std::string(180, 'L'),
												 "A long diagnostic message to wrap at narrow widths."});
			// A new floating window must be usable without a test forcing its size.
			ImGui::NewFrame();
			bool firstOpen = true;
			panel.OnImGuiRender(firstOpen);
			ImGui::Render();
			Require(ImGui::FindWindowByName("Audio Debugger")->Size.x >= ImGui::GetFontSize() * 24.0f,
					"First-open Audio Debugger auto-fit collapsed around wrapped content");
			ImGuiEx::DpiStyle dpi;
			dpi.Capture(ImGui::GetStyle());
			for (float scale : {1.0f, 1.5f})
			{
				dpi.Apply(ImGui::GetStyle(), scale);
				ImGui::GetStyle().FontScaleMain = scale;
				for (uint32_t page = 0; page < 6; ++page)
				{
					panel.m_Page = page;
					panel.m_ResetScroll = true;
					for (float width : {1800.0f, 650.0f, 280.0f, 190.0f, 1800.0f})
					{
						Frame(panel, "Audio Debugger", width);
						Frame(panel, "Audio Debugger", width);
						try
						{
							check();
						}
						catch (const std::exception& error)
						{
							throw std::runtime_error(
								std::format("Audio diagnostics page {} scale {} width {}: {}", page, scale,
											width, error.what()));
						}
						Require(panel.m_Sources.size() == 180 && found->Name == ownedName,
								"Paused diagnostics read freed scene data or refreshed unexpectedly");
					}
				}
			}
			Require(SceneSerializer(scene).SerializeToString() == sceneBeforeViews,
					"Viewing audio diagnostics modified authored scene data");
			panel.m_SourceSearch = "One.lsound";
			panel.FilterSources();
			Require(panel.m_FilteredSources.size() == 180, "Diagnostics search missed Sound paths");
			panel.m_SourceSearch = std::to_string(static_cast<uint64_t>(selected));
			panel.FilterSources();
			Require(panel.m_FilteredSources.size() == 1, "Diagnostics UUID search lost source identity");
			panel.m_SourceSearch = "missing source";
			panel.FilterSources();
			Require(panel.m_FilteredSources.empty() && panel.m_SelectedSource == 0,
					"Diagnostics filter retained an excluded selection");
			panel.m_Page = 3;
			Frame(panel, "Audio Debugger", 190.0f);
			Frame(panel, "Audio Debugger", 190.0f);
			check();
			panel.m_SourceSearch.clear();
			panel.m_SelectedSource = selected;
			panel.CaptureReadings();
			Require(panel.m_Sources.size() == 179 && panel.m_SelectedSource == 0 && !panel.m_HaveListener,
					"Refresh retained a removed source or stopped listener");
			const auto captured = panel.m_CapturedAt;
			Frame(panel, "Audio Debugger", 650.0f);
			Require(panel.m_CapturedAt == captured, "Paused readings refreshed implicitly");
			panel.m_Paused = false;
			GImGui->Time += 1.0;
			Frame(panel, "Audio Debugger", 650.0f);
			Require(panel.m_CapturedAt > captured, "Resumed diagnostics did not advance");
			panel.m_IsFocused = true;
			ImGui::GetIO().WantTextInput = true;
			KeyPressedEvent typing(Key::D, 0);
			panel.OnEvent(typing);
			Require(typing.Handled, "Audio diagnostic search leaked scene shortcuts");
			panel.OnClose();
			Require(!panel.m_IsFocused, "Hidden diagnostics retained keyboard focus");
			const auto beforeHidden = panel.m_CapturedAt;
			GImGui->Time += 1.0;
			bool closed = false;
			panel.OnImGuiRender(closed);
			Require(panel.m_CapturedAt == beforeHidden, "Closed diagnostics sampled hidden data");
			panel.LoadState(Yaml::Load("Page: Sources\n"));
			Yaml::Writer state;
			state << Yaml::BeginMap;
			panel.SaveState(state);
			state << Yaml::EndMap;
			AudioDebugPanel restored;
			restored.LoadState(Yaml::Load(state.c_str()));
			Require(restored.m_Page == 3 && !restored.m_Paused,
					"Diagnostic view state lost its page or persisted stale pause");
			panel.SetSeeTheSoundEnabled(true);
			Require(panel.m_Visualisation.ShowOcclusion, "Visualization preset failed");
			panel.SetSeeTheSoundEnabled(false);
			Require(!panel.m_Visualisation.ShowOcclusion,
					"Visualization preset did not restore previous settings");
			panel.m_Paused = true;
			panel.m_Page = 0;
			panel.m_Engine.Initialized = false;
			Frame(panel, "Audio Debugger", 190.0f);
			Frame(panel, "Audio Debugger", 190.0f);
			check();
			panel.m_Page = 2;
			panel.m_Acoustics.Ready = false;
			panel.m_Acoustics.GeometryFailed = true;
			Frame(panel, "Audio Debugger", 190.0f);
			Frame(panel, "Audio Debugger", 190.0f);
			check();
			panel.m_Page = 4;
			panel.m_HasValidation = true;
			panel.m_Validation.Issues.clear();
			Frame(panel, "Audio Debugger", 190.0f);
			Frame(panel, "Audio Debugger", 190.0f);
			check();
			panel.m_Page = 5;
			panel.SetSeeTheSoundEnabled(true);
			Frame(panel, "Audio Debugger", 190.0f);
			Frame(panel, "Audio Debugger", 190.0f);
			check();
			panel.SetSeeTheSoundEnabled(false);
			panel.OnProjectChanged(Project::GetActive());
			Require(panel.m_Context == scene, "Project notification erased the already-installed new scene");
			panel.OnProjectChanged(nullptr);
			Require(!panel.m_Context && !panel.m_HasReadings && panel.m_Sources.empty() &&
						!panel.m_HasValidation && panel.m_SourceSearch.empty(),
					"Diagnostics project teardown retained stale readings or validation");
			Frame(panel, "Audio Debugger", 190.0f);
			Frame(panel, "Audio Debugger", 190.0f);
			check();
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		return failures.empty();
	}

	bool AudioMixerPanel::RunSelfTests(std::vector<std::string>& failures)
	{
		AudioPanelTestContext context;
		auto project = Project::GetActive();
		const auto original = project->GetConfig().Audio.Mixer;
		try
		{
			AudioMixerPanel panel;
			panel.OnProjectChanged(project);
			for (int frame = 0; frame < 3; ++frame)
			{
				ImGui::NewFrame();
				bool open = true;
				panel.OnImGuiRender(open);
				ImGui::Render();
			}
			Require(ImGui::FindWindowByName("Audio Mixer")->Size.x >= 200.0f,
				"Audio Mixer first floating open collapsed to unusable width");
			auto scene = Ref<Scene>::Create();
			const auto bank = Project::GetEditorAssetManager()->GetAssetHandleFromFilePath(
				std::filesystem::path(u8"Audio/\u00e9##Bank.lbank"));
			Require(bank != 0, "Mixer scene-bank fixture missing");
			scene->SetSoundBanks({bank});
			panel.SetSceneContext(scene);
			auto authored = original;
			auto& bus = authored.Buses.front();
			for (int type = 0; type < 5; ++type)
			{
				AudioMixerEffect effect;
				effect.Type = static_cast<AudioMixerEffectType>(type);
				bus.Effects.push_back(effect);
			}
			AudioMixerSnapshot snapshot;
			snapshot.ID = UUID();
			snapshot.Name = std::string(150, 'S') + " ## literal 100%";
			snapshot.Buses.push_back({bus.Path, 0.5f});
			authored.Snapshots.push_back(snapshot);
			authored.VCAs.push_back({"vca:/" + std::string(120, 'V'), {bus.Path}});
			Require(authored.Validate(), "Private authored mixer fixture invalid");
			std::vector<std::function<void()>> undo, redo;
			panel.SetUndoCallback(
				[&](const std::string&, std::function<void()> before, std::function<void()> after)
				{
					undo.push_back(std::move(before));
					redo.push_back(std::move(after));
				});
			panel.SetMixer(authored);
			panel.m_Selected = bus.Path;
			panel.m_SelectedSnapshot = snapshot.ID;
			panel.LoadState(Yaml::Load("Page: Snapshots\nBusFraction: 0.4\n"));
			const float fraction = panel.m_BusFraction;
			ImGuiEx::DpiStyle dpi;
			dpi.Capture(ImGui::GetStyle());
			for (float scale : {1.0f, 1.5f})
			{
				dpi.Apply(ImGui::GetStyle(), scale);
				ImGui::GetStyle().FontScaleMain = scale;
				for (uint32_t page = 0; page < 5; ++page)
				{
					panel.m_Page = page;
					panel.m_OpenPage = true;
					for (float width : {1500.0f, 650.0f, 280.0f, 190.0f, 1500.0f})
					{
						Frame(panel, "Audio Mixer", width);
						Frame(panel, "Audio Mixer", width);
						CheckWidth("Audio Mixer");
						Require(panel.m_BusFraction == fraction &&
									project->GetConfig().Audio.Mixer == authored,
								"Mixer reflow changed authored data or preferred split");
					}
				}
				panel.m_Page = 0;
				panel.m_NarrowDetails = true;
				Frame(panel, "Audio Mixer", 190.0f);
				Frame(panel, "Audio Mixer", 190.0f);
				CheckWidth("Audio Mixer");
			}
			panel.m_Page = 2;
			panel.m_OpenPage = true;
			std::strcpy(panel.m_SnapshotNameBuffer, "New snapshot");
			Frame(panel, "Audio Mixer", 1500.0f);
			Frame(panel, "Audio Mixer", 1500.0f);
			Frame(panel, "Audio Mixer", 1500.0f);
			ImGuiWindow* actions = nullptr;
			for (auto* window : GImGui->Windows)
				if (window->Active && std::strstr(window->Name, "##snapshot_actions"))
					actions = window;
			Require(actions, "Snapshot action bar missing");
			auto& io = ImGui::GetIO();
			io.AddMousePosEvent(actions->WorkRect.Min.x + 10.0f, actions->WorkRect.Min.y + 10.0f);
			Frame(panel, "Audio Mixer", 1500.0f);
			io.AddMouseButtonEvent(0, true);
			Frame(panel, "Audio Mixer", 1500.0f);
			io.AddMouseButtonEvent(0, false);
			Frame(panel, "Audio Mixer", 1500.0f);
			Frame(panel, "Audio Mixer", 1500.0f);
			Require(project->GetConfig().Audio.Mixer.Snapshots.size() == authored.Snapshots.size() + 1 &&
						panel.m_SelectedSnapshot != snapshot.ID &&
						project->GetConfig().Audio.Mixer.Validate(),
					"Adding a snapshot did not preserve selection identity/valid mixer state");
			const auto undoBeforeVolume = undo.size();
			// Hiding during a drag must finish the owned edit transaction and stop preview.
			project->GetConfig().Audio.Mixer.Buses.front().Volume = 0.25f;
			panel.OnClose();
			Require(undo.size() == undoBeforeVolume + 1 && panel.m_Dirty,
					"Closing Mixer lost the in-progress edit transaction");
			undo.back()();
			Require(project->GetConfig().Audio.Mixer.Buses.front().Volume == 1.0f,
					"Mixer undo lost the prior volume");
			redo.back()();
			Require(project->GetConfig().Audio.Mixer.Buses.front().Volume == 0.25f,
					"Mixer redo lost the edited volume");
			const auto path = project->GetProjectFilePath();
			{
				FailedSaveFixture failure(path);
				Require(!panel.SaveProject() && panel.m_Dirty, "Failed mixer save cleared dirty state");
			}
			Require(panel.SaveProject() && !panel.m_Dirty, "Mixer save retry failed");
			AudioMixerSettings decoded;
			Require(decoded.DeserializeYAML(Yaml::LoadFile(path)["Project"]["Audio"]["Mixer"]) &&
						decoded == project->GetConfig().Audio.Mixer,
					"Saved mixer lost its effects, snapshots, VCAs or authored volumes");
			Require(panel.ApplyChanges() && !panel.m_NeedsRestart,
					"Mixer Apply did not restart with authored settings");
			panel.m_Preview = AudioEventInstance::Create(std::to_string(static_cast<uint64_t>(snapshot.ID)));
			Require(panel.m_Preview && panel.m_Preview->Start(), "Snapshot preview fixture could not start");
			auto held = panel.m_Preview;
			panel.m_PreviewIntensity = 0.5f;
			panel.OnClose();
			Require(!panel.m_Preview && !held->IsPlaying() && panel.m_PreviewIntensity == 0.0f,
					"Hidden Mixer retained snapshot audition");
			panel.m_Preview = AudioEventInstance::Create(std::to_string(static_cast<uint64_t>(snapshot.ID)));
			Require(panel.m_Preview && panel.m_Preview->Start(), "Snapshot edit preview could not start");
			auto editedPreview = panel.m_Preview;
			auto changed = project->GetConfig().Audio.Mixer;
			changed.Snapshots.front().Priority += 1;
			panel.SetMixer(changed);
			Require(!panel.m_Preview && !editedPreview->IsPlaying() && panel.m_NeedsRestart,
					"Structural mixer edit retained an audition of the old snapshot");
			scene->OnRuntimeStart();
			panel.SetSceneContext(scene);
			panel.m_NeedsRestart = true;
			const bool appliedWhilePlaying = panel.ApplyChanges();
			panel.m_Page = 4;
			panel.m_OpenPage = true;
			Frame(panel, "Audio Mixer", 1500.0f);
			Frame(panel, "Audio Mixer", 1500.0f);
			ImGuiWindow* bankActions = nullptr;
			for (auto* window : GImGui->Windows)
				if (window->Active && std::strstr(window->Name, "##scene_bank_actions"))
					bankActions = window;
			Require(bankActions, "Scene-bank remove action missing");
			io.AddMousePosEvent(bankActions->WorkRect.Min.x + 10.0f, bankActions->WorkRect.Min.y + 10.0f);
			Frame(panel, "Audio Mixer", 1500.0f);
			io.AddMouseButtonEvent(0, true);
			Frame(panel, "Audio Mixer", 1500.0f);
			io.AddMouseButtonEvent(0, false);
			Frame(panel, "Audio Mixer", 1500.0f);
			Require(scene->GetSoundBanks() == std::vector<AssetHandle>{bank},
					"Scene bank changed during Play");
			scene->OnRuntimeStop();
			Require(!appliedWhilePlaying && panel.m_NeedsRestart,
					"Mixer applied structural changes during Play");
			panel.SetSceneContext(nullptr);
			Frame(panel, "Audio Mixer", 190.0f);
			Frame(panel, "Audio Mixer", 190.0f);
			CheckWidth("Audio Mixer");
			panel.m_IsFocused = true;
			ImGui::GetIO().WantTextInput = true;
			KeyPressedEvent typing(Key::D, 0);
			panel.OnEvent(typing);
			Require(typing.Handled, "Mixer naming leaked a scene shortcut");
			std::strcpy(panel.m_NameBuffer, "Old project draft");
			std::strcpy(panel.m_VCANameBuffer, "Old VCA draft");
			std::strcpy(panel.m_SnapshotNameBuffer, "Old snapshot draft");
			panel.OnProjectChanged(nullptr);
			Require(!panel.m_Project && !panel.m_Context && !panel.m_Preview && !panel.m_IsFocused,
					"Mixer project switch retained editor/audio context");
			Require(!panel.m_NameBuffer[0] && !panel.m_VCANameBuffer[0] && !panel.m_SnapshotNameBuffer[0],
					"Mixer project switch retained another project's naming drafts");
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		project->GetConfig().Audio.Mixer = original;
		AudioEngine::Shutdown();
		AudioEngine::Init();
		return failures.empty();
	}
} // namespace Lux
#endif
