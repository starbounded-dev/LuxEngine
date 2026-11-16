// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "MaterialEditorPanel.h"

#ifndef LUX_DIST
#include "MaterialGraphCanvas.h"
#include "MaterialPreview.h"
#include "Lux/Asset/AssetImporter.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Asset/MaterialSerializer.h"
#include "Lux/ImGui/PanelControls.h"
#include "Lux/Core/Application.h"
#include "Lux/Renderer/MaterialGraph/MaterialGraphRegistry.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Serialization/Yaml.h"
#include "Lux/Utilities/FileSystem.h"

#include <imgui/imgui_internal.h>
#include <stdexcept>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

namespace Lux
{
	namespace
	{
		void Require(bool condition, const char* message)
		{
			if (!condition)
				throw std::runtime_error(message);
		}
	} // namespace

	bool MaterialEditorPanel::RunSelfTests(const std::filesystem::path& directory,
										   std::vector<std::string>& failures)
	{
#ifdef _MSC_VER
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
		_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
		ImGuiContext* previous = ImGui::GetCurrentContext();
		ImGuiContext* context = ImGui::CreateContext();
		ImGui::SetCurrentContext(context);
		try
		{
			Require(Project::GetActive() && Project::GetActive()->GetProjectDirectory() == directory,
					"Material tests require the runner's disposable project as the active project");
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			io.DisplaySize = {1920.0f, 1080.0f};
			io.Fonts->AddFontDefault();
			unsigned char* pixels = nullptr;
			int atlasWidth = 0, atlasHeight = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &atlasWidth, &atlasHeight);
			MaterialEditorPanel panel;
			const AssetHandle handle = AssetManager::ImportAsset("Surface.lmat");
			panel.OpenMaterial(handle);
			Require(panel.m_Documents.size() == 1, "Cannot open the test material");
			Ref<MaterialAsset> material = AssetManager::GetAsset<MaterialAsset>(handle);
			Document* document = panel.GetActiveDocument();
			material->SetRoughness(0.73f);
			Require(panel.IsDirty(*document) && panel.SaveDocument(*document) && !panel.IsDirty(*document),
					"A material edit did not save and clear its dirty state");
			const auto saved = document->Saved;
			auto manager = Project::GetEditorAssetManager();
			const AssetMetadata metadata = manager->GetMetadata(handle);
			AssetMetadata unavailable = metadata;
			unavailable.FilePath = "missing-folder/Surface.lmat";
			manager->SetMetadata(handle, unavailable);
			material->SetRoughness(0.19f);
			Require(!panel.SaveDocument(*document) && panel.IsDirty(*document) &&
						!document->SaveError.empty() && document->Saved == saved,
					"A failed save cleared the dirty state, saved snapshot or feedback");
			manager->SetMetadata(handle, metadata);
			Require(panel.SaveDocument(*document) && document->SaveError.empty(),
					"Retry did not save the retained material");
			Ref<Asset> loaded;
			Require(MaterialSerializer{}.TryLoadData(metadata, loaded) &&
						loaded.As<MaterialAsset>()->GetRoughness() == 0.19f,
					"Reload did not retain the authored value");

			std::vector<std::function<void()>> undo, redo;
			panel.SetUndoCallback(
				[&](const std::string&, std::function<void()> before, std::function<void()> after)
				{
					undo.push_back(std::move(before));
					redo.push_back(std::move(after));
				});
			ImGui::NewFrame();
			ImGui::Begin("Material undo test");
			document->EditBaseline = MaterialState::Capture(material);
			material->SetMetalness(0.66f);
			panel.TrackEdit(*document, material);
			ImGui::End();
			ImGui::Render();
			Require(undo.size() == 1, "Material edit did not make one undo command");
			undo.back()();
			Require(material->GetMetalness() == saved.Metalness,
					"Material undo did not restore the prior value");
			redo.back()();
			Require(material->GetMetalness() == 0.66f, "Material redo lost its authored value");

			material->SetGraph(MaterialGraph::CreateDefault());
			auto graph = material->GetGraph();
			const uint32_t output = graph->Nodes.front().ID;
			const uint32_t value = graph->AddNode("Float", {100.0f, 40.0f})->ID;
			Require(graph->Connect(value, 0, output, 1), "Cannot connect a graph value");
			Require(!graph->CanConnect(output, 0, value, 0), "Invalid graph link was accepted");
			Require(panel.SaveDocument(*document), "Graph material could not be saved");
			const std::string graphSaved = document->SavedGraph;
			material->SetGraph(nullptr);
			panel.RevertDocument(*document, material);
			Require(material->GetGraph() &&
						MaterialGraphCanvas::Snapshot(*material->GetGraph()) == graphSaved,
					"Revert did not restore a removed graph");
			undo.back()();
			Require(!material->GetGraph(), "Graph revert could not be undone");
			redo.back()();
			{
				AssetSerializationInfo info;
				{
					FileStreamWriter stream(directory / "material-pack.bin");
					Require(MaterialSerializer{}.SerializeToAssetPack(handle, stream, info) && stream.Flush(),
							"Cannot pack the authored graph material");
				}
				FileStreamReader stream(directory / "material-pack.bin");
				AssetPackFile::AssetInfo packed{};
				packed.PackedOffset = info.Offset;
				packed.PackedSize = info.Size;
				Ref<MaterialAsset> unpacked =
					MaterialSerializer{}.DeserializeFromAssetPack(stream, packed).As<MaterialAsset>();
				Require(unpacked && unpacked->GetGraph() && unpacked->GetGraph()->Links.size() == 1 &&
							unpacked->GetRoughness() == material->GetRoughness(),
						"Packed material lost its authored graph or surface values");
			}
			Require(FileSystem::WriteFileAtomically(directory / "Assets/Instance.lmat",
													"Material:\n  Roughness: 0.5\n"),
					"Cannot create instance fixture");
			const AssetHandle instanceHandle = AssetManager::ImportAsset("Instance.lmat");
			Ref<MaterialAsset> instance = AssetManager::GetAsset<MaterialAsset>(instanceHandle);
			Require(instance && instance->SetParent(handle), "Cannot parent the instance fixture");
			instance->InheritFrom(*material);
			MaterialEditorPanel instancePanel;
			instancePanel.OpenMaterial(instanceHandle);
			Document* instanceDocument = instancePanel.GetActiveDocument();
			Require(instanceDocument && instancePanel.SaveDocument(*instanceDocument),
					"Cannot save an instance");
			instance->SetRoughness(0.82f);
			instance->UpdateOverrides(*material);
			instancePanel.RevertDocument(*instanceDocument, instance);
			Require(instance->GetGraph() == material->GetGraph() &&
						instance->GetRoughness() == material->GetRoughness(),
					"Revert detached an instance from its inherited graph or values");

			panel.m_ShowPreview = false; // Exercise composition without scheduling a GPU preview.
			panel.m_ShowGraph = true;
			bool open = true;
			auto frame = [&](float width)
			{
				ImGui::NewFrame();
				ImGui::SetNextWindowPos({0.0f, 0.0f});
				// The panel supplies its own FirstUseEver default. Resize the existing window,
				// rather than overwriting next-window data that the panel then replaces.
				if (ImGui::FindWindowByName("Material Editor"))
					ImGui::SetWindowSize("Material Editor", {width, 700.0f}, ImGuiCond_Always);
				panel.OnImGuiRender(open);
				ImGui::Render();
			};
			for (float scale : {1.0f, 1.5f})
			{
				ImGui::GetStyle() = ImGuiStyle{};
				ImGui::GetStyle().ScaleAllSizes(scale);
				ImGui::GetStyle().FontScaleMain = scale;
				for (float width : {1400.0f, 700.0f, 280.0f, 1400.0f})
				{
					frame(width * scale);
					frame(width * scale);
					if (width == 280.0f)
						Require(panel.m_Compact, "The material did not reflow at a narrow width");
					if (width == 1400.0f)
						Require(!panel.m_Compact, "The material did not restore its wide composition");
				}
			}
			Require(!panel.IsDirty(*document), "View-only graph resizing changed the asset");
			float fraction = 0.3f;
			ImVec2 splitterPoint;
			auto splitterFrame = [&](bool focus = false)
			{
				ImGui::NewFrame();
				ImGui::SetNextWindowPos({0.0f, 0.0f});
				ImGui::SetNextWindowSize({900.0f, 300.0f});
				ImGui::Begin("Splitter navigation");
				const auto split =
					ImGuiEx::PanelSplitWidths(ImGui::GetContentRegionAvail().x, fraction, 100.0f, 100.0f);
				ImGuiEx::PanelSplitHandle("##splitter", split, fraction);
				const ImVec2 minimum = ImGui::GetItemRectMin();
				const ImVec2 maximum = ImGui::GetItemRectMax();
				splitterPoint = {(minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f};
				if (focus)
				{
					ImGui::FocusWindow(ImGui::GetCurrentWindow());
					ImGui::SetFocusID(ImGui::GetID("##splitter"), ImGui::GetCurrentWindow());
					ImGui::SetNavCursorVisible(true);
				}
				ImGui::SameLine();
				ImGui::Button("Another control");
				ImGui::End();
				ImGui::Render();
			};
			splitterFrame();
			splitterFrame(true);
			io.AddKeyEvent(ImGuiKey_Enter, true);
			splitterFrame();
			io.AddKeyEvent(ImGuiKey_Enter, false);
			splitterFrame();
			io.AddKeyEvent(ImGuiKey_RightArrow, true);
			splitterFrame();
			const float firstMove = fraction;
			io.AddKeyEvent(ImGuiKey_RightArrow, false);
			splitterFrame();
			io.AddKeyEvent(ImGuiKey_RightArrow, true);
			splitterFrame();
			if (!(firstMove > 0.3f && fraction > firstMove))
				throw std::runtime_error(
					std::format("Keyboard split did not move: first={}, second={}, active={}, nav={}",
								firstMove, fraction, GImGui->ActiveId, GImGui->NavId));
			io.AddKeyEvent(ImGuiKey_RightArrow, false);
			io.AddKeyEvent(ImGuiKey_Escape, true);
			splitterFrame();
			io.AddKeyEvent(ImGuiKey_Escape, false);
			splitterFrame();
			const float beforeDrag = fraction;
			const ImVec2 dragStart = splitterPoint;
			io.AddMousePosEvent(dragStart.x, dragStart.y);
			splitterFrame();
			io.AddMouseButtonEvent(0, true);
			splitterFrame();
			io.AddMousePosEvent(dragStart.x + 80.0f, dragStart.y);
			splitterFrame();
			io.AddMousePosEvent(dragStart.x + 160.0f, dragStart.y);
			splitterFrame();
			io.AddMouseButtonEvent(0, false);
			splitterFrame();
			Require(fraction > beforeDrag + 0.1f, "Pointer dragging did not resize the split");
			panel.LoadState(Yaml::Load(
				"ShowPreview: false\nGraphDetails: false\nGraphMinimap: false\nPreviewFraction: .nan\n"));
			Yaml::Writer state;
			state << Yaml::BeginMap;
			panel.SaveState(state);
			state << Yaml::EndMap;
			const auto restored = Yaml::Load(state.str());
			Require(!restored["ShowPreview"].as<bool>() && !restored["GraphDetails"].as<bool>() &&
						!restored["GraphMinimap"].as<bool>() && panel.m_PreviewFraction == 0.3f,
					"Presentation choices did not round-trip or nonfinite sizing was accepted");
			panel.OnClose();
			Require(panel.GetActiveDocument() == document, "Hiding materials destroyed an open document");
			panel.OnProjectChanged(nullptr);
			Require(panel.m_Documents.empty(),
					"Project change retained material documents from the old project");
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		ImGui::DestroyContext(context);
		ImGui::SetCurrentContext(previous);
		if (failures.empty())
		{
			try
			{
				// The normal frame handshake exercises GPU preview lifetime without desktop input.
				MaterialEditorPanel live;
				std::vector<int> submissionOrder;
				Renderer::Submit(
					[&submissionOrder]()
					{
						submissionOrder.push_back(1);
						Renderer::Submit([&submissionOrder]() { submissionOrder.push_back(2); });
						submissionOrder.push_back(3);
					});
				Renderer::Submit([&submissionOrder]() { submissionOrder.push_back(4); });
				const AssetHandle handle = AssetManager::ImportAsset("Surface.lmat");
				live.OpenMaterial(handle);
				live.m_ShowGraph = true;
				bool open = true;
				auto render = [&](float width)
				{
					for (int i = 0; i < 8; ++i)
					{
						MaterialGraphRegistry::Update();
						Application::Get().RenderStartupFrame(
							[&]()
							{
								ImGui::SetNextWindowPos(ImGui::GetMainViewport()->WorkPos);
								ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
								if (ImGui::FindWindowByName("Material Editor"))
									ImGui::SetWindowSize("Material Editor", {width, 700.0f},
														 ImGuiCond_Always);
								live.OnImGuiRender(open);
							});
					}
				};
				render(1250.0f);
				Require(submissionOrder == std::vector<int>({1, 2, 3, 4}),
						"Nested render work ran after a later command instead of inline");
				Require(live.m_Preview && live.m_Preview->GetMaterial() == handle && live.m_GraphCanvas,
						"The normal preview/canvas did not render in application frames");
				Ref<MaterialAsset> material = AssetManager::GetAsset<MaterialAsset>(handle);
				auto graph = material->GetGraph();
				const auto value =
					std::find_if(graph->Nodes.begin(), graph->Nodes.end(),
								 [](const MaterialGraphNode& node) { return node.Type == "Float"; });
				Require(value != graph->Nodes.end(), "The saved graph lost its value node");
				// Force reflection to drop graph-only uniforms, then introduce them again on the
				// same render passes. A warm shader cache cannot bypass this reload transition.
				material->SetGraph(nullptr);
				MaterialGraphRegistry::Unregister(handle);
				render(1250.0f);
				material->SetGraph(graph);
				MaterialGraphRegistry::Register(handle);
				render(1250.0f);
				value->Type = "UnknownNodeForValidation";
				MaterialGraphRegistry::MarkDirty();
				render(1250.0f);
				Require(!MaterialGraphRegistry::GetErrors(handle).empty(),
						"An invalid graph did not publish its compile errors");
				value->Type = "Float";
				MaterialGraphRegistry::MarkDirty();
				render(1250.0f);
				Require(MaterialGraphRegistry::GetErrors(handle).empty(),
						"Repairing a graph did not clear its compile errors");
				live.m_GraphCanvas->PreviewNode(value->ID);
				render(1250.0f);
				const AssetHandle preview = live.m_GraphCanvas->GetPreviewMaterial();
				Require(preview && live.m_Preview->GetMaterial() == preview,
						"Node preview did not reach the preview viewport");
				live.m_ShowPreview = false;
				render(1250.0f);
				Require(live.m_GraphCanvas->GetPreviewMaterial() == preview,
						"Hiding the preview replaced its node material");
				live.m_NarrowPreview = true;
				render(300.0f);
				Require(live.m_Compact && live.m_Preview->GetMaterial() == preview,
						"Narrow Preview did not retain the same node preview");
				live.CloseDocument(0);
				Require(!live.m_GraphCanvas->GetPreviewMaterial() &&
							!AssetManager::IsAssetHandleValid(preview),
						"Closing a material leaked its owned node preview");
				live.OnProjectChanged(nullptr);
				Require(!live.m_Preview, "Project change retained the previous preview scene");
			}
			catch (const std::exception& error)
			{
				failures.push_back(error.what());
			}
		}
		return failures.empty();
	}
} // namespace Lux
#endif
