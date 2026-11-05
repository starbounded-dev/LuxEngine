// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioMixerPanel.h"
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

	bool AudioMixerPanel::RunSelfTests(std::vector<std::string>& failures)
	{
		AudioPanelTestContext context;
		auto project = Project::GetActive();
		const auto original = project->GetConfig().Audio.Mixer;
		try
		{
			AudioMixerPanel panel;
			panel.OnProjectChanged(project);
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
