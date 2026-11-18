// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ApplicationSettingsPanel.h"
#include "ContentBrowserPanel.h"
#include "HelpPanel.h"
#include "LightSettingsPanel.h"
#include "ProjectSettingsWindow.h"
#include "SceneRendererPanel.h"
#include "GameViewPanel.h"
#include "Viewport/Viewport.h"
#include "RuntimeExportUtils.h"
#include "RuntimeExportBranding.h"
#include "Lux/Serialization/FileStream.h"
#include "Lux/Serialization/Obfuscation.h"

#ifndef LUX_DIST
#include "EditorLayer.h"
#include "Lux/Editor/SceneHierarchyPanel.h"
#include "Lux/Core/Events/KeyEvent.h"
#include "Lux/Core/Events/ApplicationEvent.h"
#include "Lux/Core/Input.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Scene/SceneSerializer.h"
#include "Lux/ImGui/ImGuiScaling.h"
#include "Lux/ImGui/ImGuiWidgets.h"
#include "Lux/Serialization/Yaml.h"
#include "Lux/Utilities/StringUtils.h"
#include "Lux/Utilities/FileSystem.h"
#include <imgui/imgui_internal.h>
#include "ImGuizmo.h"
#include <stdexcept>

namespace Lux
{
	namespace
	{
		struct SettingsTestContext
		{
			ImGuiContext* Previous = ImGui::GetCurrentContext();
			ImGuiContext* Context = ImGui::CreateContext();
			SettingsTestContext()
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
			~SettingsTestContext()
			{
				ImGui::DestroyContext(Context);
				ImGui::SetCurrentContext(Previous);
			}
		};
		void Require(bool result, const char* reason)
		{
			if (!result)
				throw std::runtime_error(reason);
		}
		// What an export of a game with scripts and a branded executable relies on, with no editor window:
		// which .NET runtime gets bundled, and the bytes stamped on the executable.
		void CheckExportPackaging()
		{
			namespace fs = std::filesystem;
			const fs::path root = fs::temp_directory_path() / "lux-export-selftest";
			std::error_code ec;
			fs::remove_all(root, ec);
			const auto versions = [&](std::initializer_list<const char*> names)
			{
				fs::remove_all(root, ec);
				for (const char* name : names)
					fs::create_directories(root / name);
			};
			versions({ "9.0.99", "10.0.2", "10.0.12", "10.0.12-rc.1", "11.0.0-preview.1", "not-a-version" });
			Require(RuntimeExport::FindNewestVersionDirectory(root, 10).filename() == "10.0.12",
					"The bundled .NET runtime is not the newest stable one from major 10");
			versions({ "9.0.99", "11.0.0-preview.1", "11.0.0-rc.2" });
			Require(RuntimeExport::FindNewestVersionDirectory(root, 10).filename() == "11.0.0-rc.2",
					"With only previews installed the newest preview is bundled");
			versions({ "8.0.25", "9.0.17" });
			Require(RuntimeExport::FindNewestVersionDirectory(root, 10).empty(), "A runtime older than major 10 was offered for bundling");
			fs::remove_all(root, ec);

			// A 32 x 16 image: centred on a 32 x 32 canvas, never upscaled past 48 px.
			const std::vector<uint8_t> pixels(32 * 16 * 4, 255);
			const auto icons = RuntimeExport::BuildIconImages(pixels.data(), 32, 16);
			Require(icons.size() == 3 && icons[0].Size == 48 && icons[1].Size == 32 && icons[2].Size == 16,
					"Icon sizes are not 48, 32, 16 for a 32 px image");
			for (const auto& icon : icons)
			{
				const uint32_t maskRow = ((icon.Size + 31) / 32) * 4;
				Require(icon.Resource.size() == 40u + icon.Size * icon.Size * 4u + maskRow * icon.Size, "An icon image has the wrong byte length");
				uint32_t headerSize = 0, height = 0;
				std::memcpy(&headerSize, icon.Resource.data(), 4);
				std::memcpy(&height, icon.Resource.data() + 8, 4);
				Require(headerSize == 40 && height == icon.Size * 2, "An icon image has a bad bitmap header");
			}
			Require(RuntimeExport::BuildIconImages(pixels.data(), 0, 16).empty(), "An empty image produced icon images");

			const auto version = RuntimeExport::BuildVersionResource("My Game", "1.2.3", "MyGame.exe");
			uint16_t length = 0;
			uint32_t signature = 0, fileVersionMS = 0, fileVersionLS = 0;
			std::memcpy(&length, version.data(), 2);
			// wLength, wValueLength, wType, "VS_VERSION_INFO + terminator" (16 UTF-16 units), padding to 4, then VS_FIXEDFILEINFO.
			const size_t fixedInfo = 6 + 32 + 2;
			std::memcpy(&signature, version.data() + fixedInfo, 4);
			std::memcpy(&fileVersionMS, version.data() + fixedInfo + 8, 4);
			std::memcpy(&fileVersionLS, version.data() + fixedInfo + 12, 4);
			Require(length == version.size() && signature == 0xFEEF04BD, "The version resource is malformed");
			Require(fileVersionMS == ((1u << 16) | 2u) && fileVersionLS == (3u << 16), "The version resource carries the wrong numbers");
			std::string wide;
			for (const char c : std::string_view("My Game"))
			{
				wide += c;
				wide += static_cast<char>(0);
			}
			Require(std::string_view(reinterpret_cast<const char*>(version.data()), version.size()).find(wide) != std::string_view::npos,
					"The game's name is missing from the version resource");
		}
		// The scrambling container: random access, files, the wrong key, and the key block an export patches.
		void CheckObfuscation()
		{
			namespace fs = std::filesystem;
			std::error_code ec;
			const Obfuscation::Key key = Obfuscation::Key::Generate();
			Require(Obfuscation::Key::FromHex(key.ToHex()) == key && !Obfuscation::Key::FromHex("zz"), "A key does not survive its hex form");

			std::vector<uint8_t> plain(10000);
			for (size_t i = 0; i < plain.size(); ++i)
				plain[i] = static_cast<uint8_t>(i * 31 + 7);
			// Any range, from any position, is scrambled the same as inside the whole.
			std::vector<uint8_t> whole = plain;
			Obfuscation::Apply(key, 0, whole.data(), whole.size());
			Require(whole != plain, "Apply left the bytes unchanged");
			std::vector<uint8_t> piece(plain.begin() + 123, plain.begin() + 4321);
			Obfuscation::Apply(key, 123, piece.data(), piece.size());
			Require(std::equal(piece.begin(), piece.end(), whole.begin() + 123), "A range scrambled alone differs from the same range in the whole");
			Obfuscation::Apply(key, 0, whole.data(), whole.size());
			Require(whole == plain, "Applying twice did not restore the bytes");

			const fs::path path = fs::temp_directory_path() / "lux-obfuscation-selftest.bin";
			{
				FileStreamWriter writer(path, &key);
				writer.WriteRaw<uint32_t>(0xC0FFEE);
				writer.WriteZero(8);
				writer.WriteData(reinterpret_cast<const char*>(plain.data()), plain.size());
				writer.SetStreamPosition(4);
				writer.WriteRaw<uint64_t>(0x1122334455667788ull); // seek back and patch, as the pack index does
				Require(writer.Flush(), "The scrambled file could not be written");
			}
			Obfuscation::Header header;
			Require(Obfuscation::Peek(path, header) && header.KeyFingerprint == key.Fingerprint() && fs::file_size(path) == Obfuscation::HeaderSize + 12 + plain.size(),
					"The scrambled file has the wrong header or size");
			{
				std::ifstream raw(path, std::ios::binary);
				std::vector<char> bytes(fs::file_size(path));
				raw.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
				const std::string_view view(bytes.data(), bytes.size());
				Require(view.find(std::string_view(reinterpret_cast<const char*>(plain.data()) + 100, 64)) == std::string_view::npos,
						"The scrambled file holds readable bytes");
			}
			Obfuscation::SetActiveKey(key);
			{
				FileStreamReader reader(path);
				uint32_t magic = 0;
				uint64_t patched = 0;
				std::vector<uint8_t> back(plain.size());
				Require(reader.IsStreamGood() && reader.GetStreamSize() == 12 + plain.size(), "The scrambled file does not read as its original size");
				reader.ReadData(reinterpret_cast<char*>(&magic), 4);
				reader.ReadData(reinterpret_cast<char*>(&patched), 8);
				reader.SetStreamPosition(12 + 5000);
				reader.ReadData(reinterpret_cast<char*>(back.data()), 1000);
				Require(magic == 0xC0FFEE && patched == 0x1122334455667788ull && std::equal(back.begin(), back.begin() + 1000, plain.begin() + 5000),
						"The scrambled file did not read back what was written");
			}
			Obfuscation::SetActiveKey(Obfuscation::Key::Generate());
			{
				FileStreamReader wrongKey(path);
				Require(!wrongKey.IsStreamGood(), "A scrambled file opened with the wrong key");
			}
			Obfuscation::ClearActiveKey();
			{
				FileStreamReader noKey(path);
				Require(!noKey.IsStreamGood(), "A scrambled file opened with no key");
			}
			{
				const fs::path plainPath = fs::temp_directory_path() / "lux-obfuscation-selftest-plain.bin";
				{ FileStreamWriter writer(plainPath); writer.WriteRaw<uint32_t>(42); }
				{
					FileStreamReader reader(plainPath);
					uint32_t value = 0;
					reader.ReadData(reinterpret_cast<char*>(&value), 4);
					Require(reader.IsStreamGood() && value == 42, "A plain file no longer reads as before");
				}
				fs::remove(plainPath, ec);
			}
			fs::remove(path, ec);

			// The key block: found once by its magic and read back as the key it was patched with.
			std::vector<uint8_t> blockInit = { LUX_OBFUSCATION_KEY_BLOCK_INIT };
			Require(blockInit.size() == Obfuscation::KeyBlockSize && std::memcmp(blockInit.data(), Obfuscation::KeyBlockMagic, 16) == 0
						&& std::memcmp(blockInit.data() + 16, Obfuscation::KeyBlockUnpatched, 16) == 0,
					"The key block initializer disagrees with the magic");
			Require(!Obfuscation::ReadKeyBlock(blockInit.data()), "An unpatched key block read as patched");
			const fs::path exe = fs::temp_directory_path() / "lux-obfuscation-selftest.exe";
			{
				std::ofstream out(exe, std::ios::binary);
				const std::string filler(5000, 'x');
				out << filler;
				out.write(reinterpret_cast<const char*>(blockInit.data()), static_cast<std::streamsize>(blockInit.size()));
				out << filler;
			}
			std::string error;
			Require(Obfuscation::PatchExecutable(exe, key, error), error.c_str());
			{
				std::ifstream in(exe, std::ios::binary);
				std::vector<uint8_t> bytes(fs::file_size(exe));
				in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
				const auto patched = Obfuscation::ReadKeyBlock(bytes.data() + 5000);
				Require(patched && *patched == key && fs::file_size(exe) == 10000 + Obfuscation::KeyBlockSize, "The patched key block does not read back");
			}
			{
				std::ofstream out(exe, std::ios::binary | std::ios::trunc);
				out << "no key block here";
			}
			Require(!Obfuscation::PatchExecutable(exe, key, error), "Patching an executable with no key block succeeded");
			fs::remove(exe, ec);
		}
		void Frame(EditorPanel& panel, float width, float height = 720.0f, const char* name = "Help")
		{
			// A FirstUseEver default overrides pending NextWindowData even on later frames.
			// Resize the existing window as the OS/dock splitter would, then render its settled frame.
			ImGui::SetWindowSize(name, {width, height});
			ImGui::NewFrame();
			ImGui::SetNextWindowPos({0.0f, 0.0f});
			ImGui::SetNextWindowSize({width, height});
			bool open = true;
			panel.OnImGuiRender(open);
			ImGui::Render();
		}
		void CheckWidth(const char* rootName)
		{
			auto* root = ImGui::FindWindowByName(rootName);
			if (!root || root->ContentSize.x > root->Size.x)
				throw std::runtime_error(std::format("{} width {} content {}", rootName,
													 root ? root->Size.x : 0.0f,
													 root ? root->ContentSize.x : 0.0f));
		}
		Yaml::Node StateOf(const EditorPanel& panel)
		{
			Yaml::Writer out;
			out << Yaml::BeginMap;
			panel.SaveState(out);
			out << Yaml::EndMap;
			return Yaml::Load(out.str());
		}
	} // namespace

	bool EditorLayer::RunViewportOverlaySelfTests(std::vector<std::string>& failures)
	{
		ImGuiContext* previous = ImGui::GetCurrentContext();
		ImGuiContext* context = ImGui::CreateContext(previous->IO.Fonts);
		ImGui::SetCurrentContext(context);
		const auto originalViewport = m_EditorViewport;
		const auto originalScene = m_ActiveScene;
		const auto originalHierarchy = m_SceneHierarchyPanel;
		const auto originalSelection = SelectionManager::GetSelections(SelectionContext::PrefabEditor);
		const std::string originalBadgeName = m_SelectionBadgeName;
		const std::string originalBadgeLabel = m_SelectionBadgeLabel;
		const bool originalHUD = m_ShowViewportPerformanceHUD;
		const bool originalCategories = m_SceneRenderer->GetOptions().ShowDebugCategories;
		try
		{
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.DisplaySize = {1920.0f, 1080.0f};
			io.BackendFlags = previous->IO.BackendFlags & ImGuiBackendFlags_RendererHasTextures;
			m_EditorViewport = Ref<Viewport>::Create("Scene View");
			m_ActiveScene = Ref<Scene>::Create();
			m_SceneHierarchyPanel =
				Ref<SceneHierarchyPanel>::Create(m_ActiveScene, SelectionContext::PrefabEditor);
			Entity selected = m_ActiveScene->CreateEntity(std::string(300, 'N') + " ## 100%");
			m_SceneHierarchyPanel->SetSelectedEntity(selected);
			selected.AddComponent<MeshColliderComponent>();
			m_ShowViewportPerformanceHUD = true;
			m_SceneRenderer->GetOptions().ShowDebugCategories = true;
			ImGuiEx::DpiStyle dpi;
			dpi.Capture(ImGui::GetStyle());
			for (float scale : {1.0f, 1.5f})
			{
				dpi.Apply(ImGui::GetStyle(), scale);
				ImGui::GetStyle().FontScaleMain = scale;
				for (const ImVec2 size : {ImVec2{900.0f, 600.0f}, ImVec2{180.0f, 140.0f}})
				{
					for (int frame = 0; frame < 3; ++frame)
					{
						ImGui::NewFrame();
						ImGui::SetNextWindowPos({50.0f, 50.0f});
						ImGui::SetNextWindowSize({size.x * scale, size.y * scale});
						m_EditorViewport->BeginImGui();
						// Native floating windows bring their scene root forward when focused.
						// Exercise that transition after the overlay windows already exist.
						if (frame > 0)
							ImGui::FocusWindow(ImGui::GetCurrentWindow());
						UI_ViewportPerformanceHUD();
						UI_AcousticMaterialLegend();
						m_EditorViewport->EndImGui();
						UI_ViewportSettings();
						UI_ViewportOrientationGizmo();
						UI_ViewportSelectionBadge();
						ImGui::Render();
					}
					const auto* bounds = m_EditorViewport->GetBounds();
					auto* sceneWindow = ImGui::FindWindowByName("Scene View");
					Require(context->NavWindow == sceneWindow, "Scene View overlays stole keyboard focus");
					for (const char* name : {"##viewport_performance_hud", "##acoustic_material_legend",
											 "##viewport_settings_toolbar", "##viewport_orientation_gizmo",
											 "##viewport_selection_badge"})
					{
						auto* window = ImGui::FindWindowByName(name);
						Require(window && window->Active, "A Scene View overlay was not submitted");
						Require(ImGui::FindWindowDisplayIndex(sceneWindow->RootWindowDockTree) <
									ImGui::FindWindowDisplayIndex(window),
								"Focused Scene View covered its corner overlays");
						Require(window->Viewport->ID == m_EditorViewport->GetPlatformViewportID(),
								"A Scene View overlay targeted a different platform viewport");
						if (window->Pos.x < bounds[0].x - 1.0f || window->Pos.y < bounds[0].y - 1.0f ||
							window->Pos.x + window->Size.x > bounds[1].x + 1.0f ||
							window->Pos.y + window->Size.y > bounds[1].y + 1.0f)
							throw std::runtime_error(std::format("Scene overlay {} escaped {}x{} at scale {}",
																 name, size.x, size.y, scale));
					}
					const auto* hud = ImGui::FindWindowByName("##viewport_performance_hud");
					const auto* legend = ImGui::FindWindowByName("##acoustic_material_legend");
					if (size.x >= 900.0f)
						Require(hud->InnerRect.GetWidth() >= ImGui::CalcTextSize("FPS 60  16.67 ms").x,
								"Wide Scene View collapsed the HUD to a vertical text column");
					Require(legend->Pos.x + legend->Size.x <= hud->Pos.x,
							"Scene View diagnostic overlays overlapped");
				}
			}
			m_EditorViewport->Hide();
			Require(m_EditorViewport->GetPlatformViewportID() == 0 &&
						!m_EditorViewport->PrepareOverlay({0.0f, 0.0f}),
					"Hidden Scene View retained overlay submission state");
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		m_EditorViewport = originalViewport;
		m_ActiveScene = originalScene;
		m_SceneHierarchyPanel = originalHierarchy;
		m_SelectionBadgeName = originalBadgeName;
		m_SelectionBadgeLabel = originalBadgeLabel;
		SelectionManager::DeselectAll(SelectionContext::PrefabEditor);
		for (UUID id : originalSelection)
			SelectionManager::Select(SelectionContext::PrefabEditor, id);
		m_ShowViewportPerformanceHUD = originalHUD;
		m_SceneRenderer->GetOptions().ShowDebugCategories = originalCategories;
		ImGui::DestroyContext(context);
		ImGui::SetCurrentContext(previous);
		return failures.empty();
	}

	bool EditorLayer::RunTitlebarSelfTests(std::vector<std::string>& failures)
	{
		ImGuiContext* previous = ImGui::GetCurrentContext();
		ImGuiContext* context = ImGui::CreateContext(previous->IO.Fonts);
		ImGui::SetCurrentContext(context);
		const auto originalViewport = m_EditorViewport;
		const auto originalScenePath = m_EditorScenePath;
		auto project = Project::GetActive();
		const std::string originalProjectName = project->GetConfig().Name;
		const auto originalDragMin = m_TitleBarDragRectMin, originalDragMax = m_TitleBarDragRectMax;
		const auto originalTransportMin = m_TitleBarTransportRectMin;
		const auto originalTransportMax = m_TitleBarTransportRectMax;
		const auto originalColor = m_AnimatedTitlebarColor;
		const float originalHeight = m_TitlebarHeight;
		const int originalGizmo = m_GizmoType;
		try
		{
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.DisplaySize = {3840.0f, 2160.0f};
			io.BackendFlags = previous->IO.BackendFlags & ImGuiBackendFlags_RendererHasTextures;
			io.FontDefault = previous->IO.FontDefault;
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			io.AddMousePosEvent(-1000.0f, -1000.0f);
			project->GetConfig().Name = "Project ## 100% " + std::string(200, 'P');
			m_EditorScenePath = std::string(200, 'S') + ".lscene";
			m_EditorViewport = Ref<Viewport>::Create("Titlebar scene probe");
			ImGuiEx::DpiStyle dpi;
			dpi.Capture(ImGui::GetStyle());
			for (float scale : {1.0f, 1.3f, 1.5f})
			{
				dpi.Apply(ImGui::GetStyle(), scale);
				ImGui::GetStyle().FontScaleMain = scale;
				m_TitlebarHeight = 57.0f * scale;
				for (bool visible : {false, true})
				{
					for (float width : {640.0f, 958.0f, 1077.0f, 1920.0f})
					{
						for (int frame = 0; frame < 3; ++frame)
						{
							ImGui::NewFrame();
							if (visible)
							{
								m_EditorViewport->BeginImGui();
								m_EditorViewport->EndImGui();
							}
							else
								m_EditorViewport->Hide();
							ImGui::SetNextWindowPos({0.0f, 0.0f});
							ImGui::SetNextWindowSize({width * scale, 500.0f * scale});
							ImGui::Begin("Titlebar probe", nullptr, ImGuiWindowFlags_NoDecoration);
							UI_DrawTitlebar();
							ImGui::End();
							ImGui::Render();
						}
						auto* window = ImGui::FindWindowByName("Titlebar probe");
						bool foundBreadcrumb = false;
						for (const auto& command : window->DrawList->CmdBuffer)
						{
							if (command.ElemCount == 0 || command.ClipRect.x <= m_TitleBarDragRectMin.x ||
								std::abs(command.ClipRect.w - m_TitlebarHeight) > 1.0f)
								continue;
							foundBreadcrumb = true;
							Require(command.ClipRect.z <= m_TitleBarTransportRectMin.x,
									"Titlebar breadcrumb can draw over scene controls");
						}
						if (width >= 1920.0f)
							Require(foundBreadcrumb, "Wide titlebar lost project/scene identification");
						Require(m_TitleBarTransportRectMin.x >= m_TitleBarDragRectMin.x &&
									m_TitleBarTransportRectMax.x <= m_TitleBarDragRectMax.x,
								"Titlebar scene controls overlap menus or window controls");
						int hit = 0;
						WindowTitleBarHitTestEvent controlHit(
							static_cast<int>((m_TitleBarTransportRectMin.x + m_TitleBarTransportRectMax.x) *
											 0.5f),
							static_cast<int>(m_TitlebarHeight * 0.5f), hit);
						OnTitleBarHitTest(controlHit);
						Require(hit == 0, "Native titlebar dragging swallowed a scene-control hit");
					}
				}
				ImGuiID controlsID = 0, popupID = 0;
				auto transportFrame = [&](float right)
				{
					ImGui::NewFrame();
					ImGui::SetNextWindowPos({0.0f, 0.0f});
					ImGui::SetNextWindowSize({640.0f * scale, 500.0f * scale});
					ImGui::Begin("Titlebar overflow probe", nullptr, ImGuiWindowFlags_NoDecoration);
					controlsID = ImGui::GetID("Controls##titlebar_controls");
					popupID = ImGui::GetID("##titlebar_controls_popup");
					UI_TitlebarTransport(640.0f * scale, 350.0f * scale, right * scale);
					ImGui::End();
					ImGui::Render();
				};
				transportFrame(480.0f);
				ImGui::ActivateItemByID(controlsID);
				transportFrame(480.0f);
				transportFrame(480.0f);
				Require(ImGui::IsPopupOpen(popupID, ImGuiPopupFlags_AnyPopupLevel),
						"Compact titlebar controls could not open by keyboard activation");
				auto* popup = context->OpenPopupStack.back().Window;
				Require(popup && popup->Active, "Compact titlebar controls popup was not visible");
				m_GizmoType = -1;
				ImGui::ActivateItemByID(popup->GetID("Move"));
				transportFrame(480.0f);
				Require(m_GizmoType == static_cast<int>(ImGuizmo::TRANSLATE),
						"Compact titlebar controls could not select the Move tool");
				ImGui::ActivateItemByID(controlsID);
				transportFrame(480.0f);
				transportFrame(480.0f);
				KeyPressedEvent rotateKey(Key::R, 0);
				Require(OnKeyPressed(rotateKey) && m_GizmoType == static_cast<int>(ImGuizmo::TRANSLATE),
						"A titlebar popup key also changed the scene tool");
				// Pairwise discriminator for the popup guard: an unmodified key above returns true
				// (popup navigation owns it), a Ctrl chord must return false so the Save/Undo cases
				// stay live. Key::A is the harmless stand-in for a chord: no handler acts on it, so
				// the probe cannot mutate the fixture scene, and the state is released afterwards.
				Input::UpdateKeyState(Key::LeftControl, KeyState::Pressed);
				KeyPressedEvent ctrlChord(Key::A, 0);
				Require(!OnKeyPressed(ctrlChord),
						"An open titlebar popup swallowed a Ctrl chord");
				Require(m_GizmoType == static_cast<int>(ImGuizmo::TRANSLATE),
						"The Ctrl chord probe leaked a scene tool change");
				Input::UpdateKeyState(Key::LeftControl, KeyState::Released);
				transportFrame(640.0f);
				Require(!ImGui::IsPopupOpen(popupID, ImGuiPopupFlags_AnyPopupLevel),
						"Widening the titlebar left an invisible controls popup open");
				OnKeyPressed(rotateKey);
				Require(m_GizmoType == static_cast<int>(ImGuizmo::SCALE),
						"Closing titlebar controls did not restore scene shortcuts");
			}
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		m_EditorViewport = originalViewport;
		m_EditorScenePath = originalScenePath;
		project->GetConfig().Name = originalProjectName;
		m_TitleBarDragRectMin = originalDragMin;
		m_TitleBarDragRectMax = originalDragMax;
		m_TitleBarTransportRectMin = originalTransportMin;
		m_TitleBarTransportRectMax = originalTransportMax;
		m_AnimatedTitlebarColor = originalColor;
		m_TitlebarHeight = originalHeight;
		m_GizmoType = originalGizmo;
		ImGui::DestroyContext(context);
		ImGui::SetCurrentContext(previous);
		return failures.empty();
	}

	bool SceneRendererPanel::RunPanelSelfTests(Ref<SceneRenderer> renderer,
											   std::vector<std::string>& failures)
	{
		SettingsTestContext context;
		auto project = Project::GetActive();
		if (!renderer || !project)
		{
			failures.push_back("Scene Renderer tests need the runner's renderer and private project");
			return false;
		}
		const auto originalConfig = project->GetConfig().SceneRenderer;
		const auto originalOptions = renderer->GetOptions();
		try
		{
			SceneRendererPanel panel;
			panel.SetContext(renderer);
			panel.SetSceneContext(Ref<Scene>::Create());
			const std::string sceneBefore = SceneSerializer(panel.m_Scene).SerializeToString();
			ProjectSceneRendererSettings before;
			renderer->WriteProjectSettings(before);
			panel.LoadState(Yaml::Load("ShadowsOpen: true\nPostFXOpen: true\nColorGradingOpen: true\nQualityCategoriesOpen: true\n"));
			const auto state = StateOf(panel);
			Require(state["ShadowsOpen"].as<bool>() && state["PostFXOpen"].as<bool>(),
					"Scene Renderer lost existing card layout state");
			Require(state["QualityCategoriesOpen"].as<bool>(false),
					"Scene Renderer lost the Quality per category open state");
			panel.m_QualityCategoriesOpen = false;
			ImGui::NewFrame();
			bool open = true;
			panel.OnImGuiRender(open);
			ImGui::Render();
			Require(ImGui::FindWindowByName("Scene Renderer")->Size.x >= 300.0f,
					"Scene Renderer first open collapsed to wrapped-content auto-fit");
			const auto frame = [&](float width, float height = 720.0f)
			{
				ImGui::SetWindowSize("Scene Renderer", {width, height});
				Frame(panel, width, height);
			};
			const auto check = [&]()
			{
				CheckWidth("Scene Renderer");
				for (ImGuiWindow* window : GImGui->Windows)
				{
					if (!window->Active || window->SkipItems ||
						window->RootWindow != ImGui::FindWindowByName("Scene Renderer"))
						continue;
					if (window->DC.CursorMaxPos.x > window->InnerRect.Max.x + 1.0f)
						throw std::runtime_error(std::format(
							"Renderer child {} width {} content {} query {} scale {} mode {} submitted {} "
							"skip {}",
							window->Name, window->Size.x, window->ContentSize.x, panel.m_SearchBuffer,
							ImGui::GetStyle().FontScaleMain,
							static_cast<int>(renderer->GetOptions().ResolutionScaleMode),
							window->DC.CursorMaxPos.x - window->DC.CursorStartPos.x, window->SkipItems));
				}
			};
			ImGuiEx::DpiStyle dpi;
			dpi.Capture(ImGui::GetStyle());
			for (float scale : {1.0f, 1.5f})
			{
				dpi.Apply(ImGui::GetStyle(), scale);
				ImGui::GetStyle().FontScaleMain = scale;
				for (auto mode : {SceneRendererOptions::RenderResolutionScaleMode::Native,
								  SceneRendererOptions::RenderResolutionScaleMode::FixedResolution,
								  SceneRendererOptions::RenderResolutionScaleMode::Dynamic})
				{
					renderer->GetOptions().ResolutionScaleMode = mode;
					panel.m_CardDebugViews =
						mode != SceneRendererOptions::RenderResolutionScaleMode::FixedResolution;
					for (float width : {1400.0f, 650.0f, 280.0f, 190.0f, 1400.0f})
					{
						frame(width);
						frame(width);
						check();
					}
					for (const char* query :
						 {"Show", "Shadows", "Bloom", "GTAO", "Tonemap", "Saturation", "Render"})
					{
						std::snprintf(panel.m_SearchBuffer, sizeof(panel.m_SearchBuffer), "%s", query);
						frame(190.0f);
						frame(190.0f);
						check();
					}
					panel.m_SearchBuffer[0] = '\0';
				}
			}
			renderer->GetOptions().ResolutionScaleMode = originalOptions.ResolutionScaleMode;
			ProjectSceneRendererSettings after;
			renderer->WriteProjectSettings(after);
			Require(before == after && !panel.m_ProjectRendererSettingsDirty &&
						sceneBefore == SceneSerializer(panel.m_Scene).SerializeToString(),
					"Viewing renderer cards changed authored renderer or scene data");
			std::snprintf(panel.m_SearchBuffer, sizeof(panel.m_SearchBuffer), "%s", "shadows");
			panel.m_CardShadows = false;
			frame(190.0f);
			frame(190.0f);
			check();
			Require(!panel.m_CardShadows, "Searching changed the saved card collapse preference");
			panel.m_SearchBuffer[0] = '\0';
			panel.SetDebugViewsRuntimeSuspended(true);
			frame(190.0f);
			frame(190.0f);
			check();
			frame(190.0f, 40.0f);
			frame(1400.0f);
			panel.m_IsFocused = true;
			ImGui::GetIO().WantTextInput = true;
			KeyPressedEvent typing(Key::D, 0);
			panel.OnEvent(typing);
			Require(typing.Handled, "Renderer numeric/text editing leaked a scene shortcut");
			panel.OnClose();
			Require(!panel.m_IsFocused, "Hidden renderer controls retained keyboard focus");
			const bool authoredCulling = !renderer->GetOptions().EnableFrustumCulling;
			renderer->GetOptions().EnableFrustumCulling = authoredCulling;
			Require(panel.SaveProjectRendererSettings(), "Renderer settings save failed in private project");
			Require(!panel.m_ProjectRendererSettingsDirty && panel.m_SaveError.empty(),
					"Renderer save left stale dirty/error status");
			const auto path = project->GetProjectFilePath();
			Require(Yaml::LoadFile(path)["Project"]["SceneRenderer"]["Rendering"]["FrustumCulling"].as<bool>() ==
						authoredCulling,
					"Renderer settings save did not persist the authored value");
			auto backup = path;
			backup += ".renderer-save-test";
			Require(FileSystem::Rename(path, backup), "Cannot preserve private renderer save fixture");
			const bool madeDirectory = FileSystem::CreateDirectory(path);
			const bool failedSave = madeDirectory && !panel.SaveProjectRendererSettings();
			const bool retained = panel.m_ProjectRendererSettingsDirty && !panel.m_SaveError.empty();
			std::error_code removeError;
			if (madeDirectory)
				std::filesystem::remove(path, removeError);
			Require(FileSystem::Rename(backup, path), "Cannot restore private renderer save fixture");
			Require(failedSave && retained, "Failed renderer save lost dirty/error indication");
			frame(190.0f);
			frame(190.0f);
			check();
			Require(panel.SaveProjectRendererSettings() && panel.m_SaveError.empty(),
					"Renderer save did not recover after destination repair");
			panel.OnProjectChanged(nullptr);
			Require(!panel.m_IsFocused && !panel.m_ProjectRendererSettingsDirty && panel.m_SaveError.empty(),
					"Renderer project change retained old transient state");
			panel.SetContext(nullptr);
			frame(190.0f);
			frame(190.0f);
			check();
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		renderer->GetOptions() = originalOptions;
		project->GetConfig().SceneRenderer = originalConfig;
		return failures.empty();
	}

	bool GameViewPanel::RunPanelSelfTests(std::vector<std::string>& failures)
	{
		SettingsTestContext context;
		try
		{
			GameViewPanel panel;
			ImGuiEx::DpiStyle dpi;
			dpi.Capture(ImGui::GetStyle());
			for (float scale : {1.0f, 1.5f})
			{
				dpi.Apply(ImGui::GetStyle(), scale);
				ImGui::GetStyle().FontScaleMain = scale;
				for (float width : {1400.0f, 650.0f, 280.0f, 190.0f, 1400.0f})
				{
					ImGui::SetWindowSize("Game View", {width, 600.0f});
					Frame(panel, width, 600.0f);
					ImGui::SetWindowSize("Game View", {width, 600.0f});
					Frame(panel, width, 600.0f);
					const auto* window = ImGui::FindWindowByName("Game View");
					const auto* bounds = panel.m_Viewport->GetBounds();
					bool captionFound = false;
					for (const ImDrawVert& vertex : window->DrawList->VtxBuffer)
					{
						if (vertex.col != Colors::Theme::textCaption)
							continue;
						captionFound = true;
						Require(vertex.pos.x >= bounds[0].x && vertex.pos.x <= bounds[1].x,
								"No Camera guidance is clipped at narrow Game View widths");
					}
					Require(captionFound && !panel.m_RendererCreated,
							"Empty Game View lost its guidance or allocated a renderer while drawing");
				}
			}
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		return failures.empty();
	}

	bool ApplicationSettingsPanel::RunSelfTests(std::vector<std::string>& failures)
	{
		SettingsTestContext context;
		try
		{
			bool vsync = false;
			int rate = 120;
			SceneRenderer::GridSettings grid;
			EditorPreferencesBindings bindings;
			bindings.VSync = &vsync;
			bindings.TargetFrameRate = &rate;
			bindings.Grid = &grid;
			ApplicationSettingsPanel panel(nullptr, bindings, nullptr);
			panel.LoadState(Yaml::Load("Page: Viewport\nPageFraction: 0.37\n"));
			const auto state = StateOf(panel);
			Require(state["Page"].as<std::string>() == "Viewport", "Application page did not round-trip");
			const float fraction = panel.m_PageFraction;
			ImGuiEx::DpiStyle dpi;
			dpi.Capture(ImGui::GetStyle());
			for (float scale : {1.0f, 1.5f})
			{
				dpi.Apply(ImGui::GetStyle(), scale);
				ImGui::GetStyle().FontScaleMain = scale;
				for (uint32_t page = 0; page < panel.m_Pages.size(); ++page)
				{
					panel.m_CurrentPage = page;
					for (float width : {1200.0f, 500.0f, 280.0f, 190.0f, 1200.0f})
					{
						Frame(panel, width);
						Frame(panel, width);
						CheckWidth("Application Settings");
						Require(panel.m_PageFraction == fraction,
								"Reflow overwrote application sidebar preference");
					}
				}
			}
			panel.LoadState(Yaml::Load("Page: Editor\n"));
			Frame(panel, 190.0f);
			Frame(panel, 190.0f);
			auto* settings = ImGui::FindWindowByName("Application Settings");
			auto& io = ImGui::GetIO();
			io.AddMousePosEvent(settings->WorkRect.Min.x + 20.0f,
								settings->WorkRect.Min.y + settings->FontRefSize * 0.5f);
			Frame(panel, 190.0f);
			io.AddMouseButtonEvent(0, true);
			Frame(panel, 190.0f);
			io.AddMouseButtonEvent(0, false);
			Frame(panel, 190.0f);
			Require(!GImGui->OpenPopupStack.empty(), "Narrow settings page selector did not open");
			io.AddKeyEvent(ImGuiKey_DownArrow, true);
			Frame(panel, 190.0f);
			io.AddKeyEvent(ImGuiKey_DownArrow, false);
			Frame(panel, 190.0f);
			Frame(panel, 190.0f);
			io.AddKeyEvent(ImGuiKey_Enter, true);
			Frame(panel, 190.0f);
			io.AddKeyEvent(ImGuiKey_Enter, false);
			Frame(panel, 190.0f);
			Require(panel.m_CurrentPage == 1,
					"Narrow settings selector did not switch pages with the keyboard");
			// A completely clipped child must still close before the next panel/frame.
			Frame(panel, 190.0f, 40.0f);
			Frame(panel, 1200.0f);
			panel.m_IsFocused = true;
			ImGui::GetIO().WantTextInput = true;
			KeyPressedEvent typing(Key::D, 0);
			panel.OnEvent(typing);
			Require(typing.Handled, "Settings typing leaked a scene shortcut");
			panel.OnClose();
			Require(!panel.m_IsFocused, "Hidden settings retained keyboard focus");
			Require(!vsync && rate == 120 && grid.CellSize == SceneRenderer::GridSettings{}.CellSize,
					"Viewing settings changed preferences");
			panel.LoadState(Yaml::Load("Page: obsolete\nPageFraction: .nan\n"));
			Require(panel.m_PageFraction == fraction, "Invalid layout state poisoned sidebar sizing");
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		return failures.empty();
	}

	bool HelpPanel::RunSelfTests(std::vector<std::string>& failures)
	{
		SettingsTestContext context;
		try
		{
			HelpPanel panel;
			Require(!panel.m_RootMissing && panel.FindTopic("editor/interface"),
					"Bundled manual fixture missing");
			panel.LoadState(Yaml::Load("Page: editor/interface\nTopicFraction: 0.35\nShowTopics: false\n"));
			Require(panel.m_CurrentTopic == "editor/interface" && !panel.m_FocusRequested,
					"Help restore lost its page or stole focus");
			Require(panel.ResolveLink("../components/transform.md#position") == "components/transform",
					"Manual relative links lost their destination");
			const auto state = StateOf(panel);
			Require(state["Page"].as<std::string>() == "editor/interface" && !state["ShowTopics"].as<bool>(),
					"Help presentation state did not round-trip");
			const float fraction = panel.m_TopicFraction;
			ImGuiEx::DpiStyle dpi;
			dpi.Capture(ImGui::GetStyle());
			for (float scale : {1.0f, 1.5f})
			{
				dpi.Apply(ImGui::GetStyle(), scale);
				ImGui::GetStyle().FontScaleMain = scale;
				for (float width : {1200.0f, 500.0f, 280.0f, 190.0f, 1200.0f})
				{
					Frame(panel, width);
					Frame(panel, width);
					CheckWidth("Help");
					Require(panel.m_TopicFraction == fraction && panel.m_CurrentTopic == "editor/interface",
							"Help reflow lost page or sidebar proportion");
				}
			}
			panel.m_FocusSearch = true;
			Frame(panel, 280.0f);
			Frame(panel, 280.0f);
			ImGui::GetIO().AddInputCharactersUTF8("transform");
			Frame(panel, 280.0f);
			Frame(panel, 280.0f);
			if (panel.m_Search != "transform" || !panel.m_NarrowTopics)
				throw std::runtime_error(std::format(
					"Help search '{}' topics {} split {} focused {} active {}", panel.m_Search,
					panel.m_NarrowTopics, panel.m_SideBySide, panel.m_IsFocused, GImGui->ActiveId));
			ImGui::ClearActiveID();
			auto* root = ImGui::FindWindowByName("Help");
			ImGuiWindow* topics = nullptr;
			for (auto* window : GImGui->Windows)
				if (window->Active && window->ParentWindow == root && std::strstr(window->Name, "##topics"))
					topics = window;
			Require(topics, "Help search results child missing");
			auto& io = ImGui::GetIO();
			io.AddMousePosEvent(topics->WorkRect.Min.x + 20.0f,
								topics->WorkRect.Min.y + topics->FontRefSize * 0.5f);
			Frame(panel, 280.0f);
			io.AddMouseButtonEvent(0, true);
			Frame(panel, 280.0f);
			io.AddMouseButtonEvent(0, false);
			Frame(panel, 280.0f);
			Frame(panel, 280.0f);
			Require(!panel.m_NarrowTopics, "Selecting a narrow Help result did not return to its page");
			panel.m_IsFocused = true;
			io.WantTextInput = true;
			KeyPressedEvent typing(Key::F, 0);
			panel.OnEvent(typing);
			Require(typing.Handled, "Manual search leaked a scene shortcut");
			panel.OnClose();
			Require(!panel.m_IsFocused && !panel.m_FocusSearch, "Closing Help retained focus");
			panel.LoadState(Yaml::Load("TopicFraction: .nan\n"));
			Require(panel.m_TopicFraction == fraction, "Invalid Help ratio poisoned layout state");
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		return failures.empty();
	}

	bool ProjectSettingsWindow::RunPanelSelfTests(std::vector<std::string>& failures)
	{
		SettingsTestContext context;
		try
		{
			CheckExportPackaging();
			CheckObfuscation();
			RuntimeExport::RuntimeBuildJob job;
			ProjectSettingsWindow panel(job);
			auto project = Project::GetActive();
			Require(project != nullptr, "Project settings require the disposable runner project");
			panel.OnProjectChanged(project);
			panel.LoadState(Yaml::Load("Page: Physics\n"));
			Require(StateOf(panel)["Page"].as<std::string>() == "Physics",
					"Project section did not round-trip");
			ImGuiEx::DpiStyle dpi;
			dpi.Capture(ImGui::GetStyle());
			for (float scale : {1.0f, 1.5f})
			{
				dpi.Apply(ImGui::GetStyle(), scale);
				ImGui::GetStyle().FontScaleMain = scale;
				for (uint32_t page = 0; page < 7; ++page)
				{
					panel.m_CurrentPage = page;
					panel.m_OpenPage = true;
					for (float width : {1200.0f, 500.0f, 280.0f, 190.0f, 1200.0f})
					{
						Frame(panel, width);
						Frame(panel, width);
						CheckWidth("Project Settings");
					}
				}
			}
			Require(!panel.m_Dirty && !job.IsRunning(),
					"Viewing settings changed project data or launched a build");
			const std::string name = project->GetConfig().Name;
			project->GetConfig().Name = "SettingsSaved";
			panel.m_Dirty = true;
			panel.SaveProject();
			Require(!panel.m_Dirty &&
						Yaml::LoadFile(project->GetProjectFilePath())["Project"]["Name"].as<std::string>() ==
							"SettingsSaved",
					"Project settings save did not persist authored data");
			project->GetConfig().Name = name;
			panel.m_Dirty = true;
			panel.SaveProject();
			const auto path = project->GetProjectFilePath();
			auto backup = path;
			backup += ".before-save-test";
			Require(FileSystem::Rename(path, backup), "Could not preserve the private project save fixture");
			Require(FileSystem::CreateDirectory(path), "Could not construct the private failed-save fixture");
			panel.m_Dirty = true;
			panel.SaveProject();
			const bool retainedDirty = panel.m_Dirty;
			std::error_code removeError;
			Require(std::filesystem::remove(path, removeError) && FileSystem::Rename(backup, path),
					"Could not restore the private project save fixture");
			Require(retainedDirty, "A failed project save cleared the unsaved indication");
			panel.SaveProject();
			Require(!panel.m_Dirty, "Retrying a project save did not clear the dirty state");
			panel.m_IsFocused = true;
			ImGui::GetIO().WantTextInput = true;
			KeyPressedEvent typing(Key::D, 0);
			panel.OnEvent(typing);
			Require(typing.Handled, "Project text editing leaked a scene shortcut");
			panel.OnClose();
			Require(!panel.m_IsFocused, "Closing project settings retained focus");
			panel.m_LegacyErrors.push_back("old project cleanup report");
			panel.OnProjectChanged(nullptr);
			Require(!panel.m_Project && !panel.m_Dirty && !panel.m_DialoguePreview,
					"Project change retained settings context/audition");
			Require(panel.m_LegacyErrors.empty() && !panel.m_LegacyScan,
					"Project change retained old audio cleanup context");

			LightSettingsPanel lights;
			for (int frame = 0; frame < 3; ++frame)
			{
				ImGui::NewFrame();
				bool open = true;
				lights.OnImGuiRender(open);
				ImGui::Render();
			}
			Require(ImGui::FindWindowByName("Light Settings")->Size.x >= 200.0f,
				"Light Settings first floating open collapsed to unusable width");
			auto scene = Ref<Scene>::Create();
			Entity entity = scene->CreateEntity(std::string(200, 'L') + " ## literal 100%");
			entity.AddComponent<DirectionalLightComponent>();
			entity.AddComponent<PointLightComponent>();
			entity.AddComponent<SpotLightComponent>();
			entity.AddComponent<SkyLightComponent>();
			lights.SetSceneContext(scene);
			lights.OnProjectChanged(project);
			Require(lights.GetSceneContext() == scene,
					"Project notification discarded the newly installed light scene");
			for (float width : {1200.0f, 280.0f, 190.0f, 1200.0f})
			{
				Frame(lights, width, 720.0f, "Light Settings");
				Frame(lights, width, 720.0f, "Light Settings");
				CheckWidth("Light Settings");
			}
			lights.OnProjectChanged(nullptr);
			Require(!lights.GetSceneContext(), "Light settings retained the old project's scene");
			Frame(lights, 190.0f, 720.0f, "Light Settings");

			glm::vec3 values(1.0f, 2.0f, 3.0f);
			bool manuallyEdited = false;
			auto vectorFrame = [&]()
			{
				ImGui::NewFrame();
				ImGui::SetNextWindowPos({0.0f, 0.0f});
				ImGui::SetNextWindowSize({190.0f, 350.0f});
				ImGui::Begin("Vector controls");
				ImGuiEx::Widgets::EditVec3("##value", {ImGui::GetContentRegionAvail().x, 0.0f}, 0.0f,
										   manuallyEdited, values);
				ImGui::End();
				ImGui::Render();
			};
			vectorFrame();
			vectorFrame();
			CheckWidth("Vector controls");
			auto* vectorWindow = ImGui::FindWindowByName("Vector controls");
			auto& io = ImGui::GetIO();
			io.AddMousePosEvent(vectorWindow->WorkRect.Min.x + 10.0f, vectorWindow->WorkRect.Min.y + 12.0f);
			vectorFrame();
			io.AddMouseButtonEvent(0, true);
			vectorFrame();
			io.AddMouseButtonEvent(0, false);
			vectorFrame();
			Require(values == glm::vec3(0.0f, 2.0f, 3.0f),
					"Stacked vector axis reset changed the wrong values");
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		return failures.empty();
	}
} // namespace Lux
#endif
