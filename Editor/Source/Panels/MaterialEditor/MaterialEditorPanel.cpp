// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "MaterialEditorPanel.h"
#include "MaterialPreview.h"
#include "MaterialGraphCanvas.h"
#include "Lux/Renderer/MaterialGraph/MaterialGraphRegistry.h"
#include "Panels/ContentBrowserPanel.h"

#include "Lux/Asset/AssetImporter.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Audio/AcousticMaterial.h"
#include "Lux/Core/Application.h"
#include "Lux/Core/Events/KeyEvent.h"
#include "Lux/Core/Input.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/ImGuiUtilities.h"
#include "Lux/ImGui/PanelControls.h"
#include "Lux/Project/Project.h"
#include "Lux/Serialization/Yaml.h"
#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Renderer/Texture.h"

#include <imgui/imgui.h>

#include <format>
#include <iterator>
#include <cmath>

namespace Lux
{
	namespace
	{
		constexpr float kOrbitDegreesPerPixel = 0.4f;
		constexpr float kZoomPerWheelStep = 0.1f;
		constexpr float kPreviewRounding = 6.0f;
		enum class MaterialAction
		{
			Save,
			Revert,
			Properties,
			Graph,
			Preview,
			Instance,
			MakeGraph,
			RemoveGraph
		};

		// Not constexpr: PropertyDropdown takes a mutable const char** list.
		const char* s_ChannelNames[] = { "R", "G", "B", "A" };

		// Blend is not offered: it is the transparent material path, chosen when the material is
		// created, not a value an opaque material can switch to here.
		const char* s_AlphaModeNames[] = { "Opaque", "Cutout" };

		const char* s_MappingNames[] = { "UV", "Triplanar (World)", "Triplanar (Object)" };

		std::string GetMaterialName(AssetHandle handle)
		{
			if (Ref<EditorAssetManager> editorAssetManager = Project::GetEditorAssetManager())
			{
				const AssetMetadata metadata = editorAssetManager->GetMetadata(handle);
				if (metadata.IsValid())
				{
					const auto name = metadata.FilePath.stem().u8string();
					return {reinterpret_cast<const char*>(name.data()), name.size()};
				}
			}
			return "Material";
		}

		ImTextureID GetImGuiTextureID(const Ref<Image2D>& image)
		{
			auto* imguiRenderer = Application::Get().GetImGuiLayer()->GetImGuiRenderer();
			return imguiRenderer->CreateFrameTexture(image);
		}
	}

	namespace
	{
		std::string GraphSnapshot(const Ref<MaterialAsset>& material)
		{
			Ref<MaterialGraph> graph = material ? material->GetGraph() : nullptr;
			return graph && !material->GetParent() ? MaterialGraphCanvas::Snapshot(*graph) : std::string();
		}

		void ApplyGraphSnapshot(AssetHandle handle, const std::string& snapshot)
		{
			if (Ref<MaterialAsset> material = AssetManager::GetAsset<MaterialAsset>(handle))
			{
				if (material->GetParent())
				{
					if (Ref<MaterialAsset> parent =
							AssetManager::GetAsset<MaterialAsset>(material->GetParent()))
						material->InheritFrom(*parent);
					MaterialGraphRegistry::Unregister(handle);
					return;
				}
				if (snapshot.empty())
				{
					material->SetGraph(nullptr);
					MaterialGraphRegistry::Unregister(handle);
				}
				else
				{
					Ref<MaterialGraph> graph = MaterialGraph::CreateDefault();
					MaterialGraphCanvas::Restore(*graph, snapshot);
					material->SetGraph(graph);
					MaterialGraphRegistry::Register(handle);
				}
				material->MarkChanged();
				MaterialGraphRegistry::MarkDirty();
			}
		}
	}

	MaterialEditorPanel::MaterialEditorPanel() = default;
	MaterialEditorPanel::~MaterialEditorPanel() = default;

	// -- MaterialState ---------------------------------------------------------------------------

	MaterialEditorPanel::MaterialState MaterialEditorPanel::MaterialState::Capture(Ref<MaterialAsset> material)
	{
		MaterialState state;
		state.AlbedoColor = material->GetAlbedoColor();
		state.Metalness = material->GetMetalness();
		state.Roughness = material->GetRoughness();
		state.Emission = material->GetEmission();
		state.Transparency = material->GetTransparency();
		state.CastShadows = material->IsShadowCasting();
		state.UseNormalMap = material->IsUsingNormalMap();
		state.AlbedoMap = material->GetAlbedoMapHandle();
		state.NormalMap = material->GetNormalMapHandle();
		state.MetalnessMap = material->GetMetalnessMapHandle();
		state.RoughnessMap = material->GetRoughnessMapHandle();
		state.Surface = material->GetSurfaceParameters();
		state.AcousticTag = material->GetAcousticTag();
		state.Parent = material->GetParent();
		state.Overrides = material->GetOverrides();
		state.GraphParameters = material->GetGraphParameters();
		return state;
	}

	void MaterialEditorPanel::MaterialState::Apply(Ref<MaterialAsset> material) const
	{
		material->SetAlbedoColor(AlbedoColor);
		material->SetMetalness(Metalness);
		material->SetRoughness(Roughness);
		material->SetEmission(Emission);
		material->SetTransparency(Transparency);
		material->SetShadowCasting(CastShadows);

		if (AlbedoMap) material->SetAlbedoMap(AlbedoMap); else material->ClearAlbedoMap();
		if (NormalMap) material->SetNormalMap(NormalMap); else material->ClearNormalMap();
		if (MetalnessMap) material->SetMetalnessMap(MetalnessMap); else material->ClearMetalnessMap();
		if (RoughnessMap) material->SetRoughnessMap(RoughnessMap); else material->ClearRoughnessMap();

		// After the maps: assigning or clearing a normal map may change the flag.
		material->SetUseNormalMap(UseNormalMap);
		material->SetSurfaceParameters(Surface);
		material->SetAcousticTag(AcousticTag);
		material->SetParent(Parent);
		material->SetOverrides(Overrides);
		material->SetGraphParameters(GraphParameters);
	}

	// -- Documents -------------------------------------------------------------------------------

	void MaterialEditorPanel::OpenMaterial(AssetHandle handle)
	{
		if (!handle || AssetManager::GetAssetType(handle) != AssetType::Material)
		{
			LUX_CORE_WARN_TAG("Editor", "Material Editor: asset {} is not a material", (uint64_t)handle);
			return;
		}

		for (size_t i = 0; i < m_Documents.size(); ++i)
		{
			if (m_Documents[i].Handle == handle)
			{
				m_ActiveDocument = (int)i;
				m_SelectActiveTab = true;
				return;
			}
		}

		Ref<MaterialAsset> material = AssetManager::GetAsset<MaterialAsset>(handle);
		if (!material || !material->GetMaterial())
		{
			LUX_CORE_ERROR_TAG("Editor", "Material Editor: failed to load material {} (missing file or PBR shaders unavailable)", (uint64_t)handle);
			return;
		}

		Document document;
		document.Handle = handle;
		document.Saved = MaterialState::Capture(material);
		document.Propagated = document.Saved;
		document.SavedGraph = GraphSnapshot(material);
		document.TabLabel =
			std::format("{}###material_tab_{}", GetMaterialName(handle), static_cast<uint64_t>(handle));
		document.LabelPath = Project::GetEditorAssetManager()->GetMetadata(handle).FilePath;
		const auto path = document.LabelPath.generic_u8string();
		document.DisplayPath.assign(reinterpret_cast<const char*>(path.data()), path.size());
		m_Documents.push_back(document);
		m_ActiveDocument = (int)m_Documents.size() - 1;
		m_SelectActiveTab = true;
	}

	void MaterialEditorPanel::OnProjectChanged(const Ref<Project>&)
	{
		// Handles belong to the previous project, and the preview scene references its meshes.
		m_Documents.clear();
		m_ActiveDocument = -1;
		m_PendingCloseIndex = -1;
		m_OpenAfterFrame = 0;
		m_OpenCloseConfirm = false;
		m_KeyboardFocus = false;
		m_Preview = nullptr;
		if (m_GraphCanvas)
			m_GraphCanvas->ResetDocument();
	}

	MaterialEditorPanel::Document* MaterialEditorPanel::GetActiveDocument()
	{
		if (m_ActiveDocument < 0 || m_ActiveDocument >= (int)m_Documents.size())
			return nullptr;
		return &m_Documents[m_ActiveDocument];
	}

	void MaterialEditorPanel::CloseDocument(size_t index)
	{
		if (index >= m_Documents.size())
			return;
		if (m_GraphCanvas && m_GraphCanvas->GetSourceMaterial() == m_Documents[index].Handle)
			m_GraphCanvas->StopPreview();

		if (m_PendingCloseIndex == static_cast<int>(index))
			m_PendingCloseIndex = -1;
		else if (m_PendingCloseIndex > static_cast<int>(index))
			--m_PendingCloseIndex;
		m_Documents.erase(m_Documents.begin() + static_cast<ptrdiff_t>(index));
		if (m_ActiveDocument > static_cast<int>(index))
			--m_ActiveDocument;
		if (m_ActiveDocument >= (int)m_Documents.size())
			m_ActiveDocument = (int)m_Documents.size() - 1;
		m_SelectActiveTab = true;
	}

	bool MaterialEditorPanel::SaveDocument(Document& document)
	{
		Ref<MaterialAsset> material = AssetManager::GetAsset<MaterialAsset>(document.Handle);
		if (!material)
		{
			LUX_CORE_ERROR_TAG("Editor", "Material Editor: cannot save material {}: it is no longer loaded", (uint64_t)document.Handle);
			document.SaveError = "The material is no longer loaded. Your unsaved document remains open.";
			return false;
		}

		if (!AssetImporter::TrySerialize(material.As<Asset>()))
		{
			document.SaveError = "Could not write the material. Check that its folder is writable, then "
								 "retry Save. Your changes remain available.";
			return false;
		}
		document.Saved = MaterialState::Capture(material);
		document.SavedGraph = GraphSnapshot(material);
		document.SaveError.clear();
		return true;
	}

	void MaterialEditorPanel::RevertDocument(Document& document, Ref<MaterialAsset> material)
	{
		const MaterialState beforeState = MaterialState::Capture(material);
		const std::string before = GraphSnapshot(material);
		document.Saved.Apply(material);
		ApplyGraphSnapshot(document.Handle, document.SavedGraph);
		if (m_GraphCanvas)
			m_GraphCanvas->StopPreview();
		if (m_PushUndo && (!(beforeState == document.Saved) || before != document.SavedGraph))
		{
			const AssetHandle handle = document.Handle;
			const MaterialState afterState = MaterialState::Capture(material);
			const std::string after = document.SavedGraph;
			auto apply = [handle](const MaterialState& state, const std::string& graph)
			{
				if (Ref<MaterialAsset> target = AssetManager::GetAsset<MaterialAsset>(handle))
				{
					state.Apply(target);
					ApplyGraphSnapshot(handle, graph);
					MaterialAsset::PropagateToInstances(handle);
				}
			};
			m_PushUndo(
				"Revert Material", [apply, beforeState, before]() { apply(beforeState, before); },
				[apply, afterState, after]() { apply(afterState, after); });
		}
		document.EditBaseline = MaterialState::Capture(material);
		document.EditInProgress = false;
		document.SaveError.clear();
	}

	bool MaterialEditorPanel::IsDirty(const Document& document) const
	{
		Ref<MaterialAsset> material = AssetManager::GetAsset<MaterialAsset>(document.Handle);
		return material && (!(MaterialState::Capture(material) == document.Saved) || GraphSnapshot(material) != document.SavedGraph);
	}

	// Widgets change the material directly. The state before the first change is kept until no
	// widget is active any more, then the whole edit becomes one undo step — a slider drag is one
	// step, not one per frame.
	void MaterialEditorPanel::TrackEdit(Document& document, Ref<MaterialAsset> material)
	{
		const MaterialState current = MaterialState::Capture(material);

		if (!document.EditInProgress)
		{
			if (current == document.EditBaseline)
				return;
			document.EditInProgress = true;
		}

		if (ImGui::IsAnyItemActive())
			return;

		document.EditInProgress = false;
		const MaterialState before = document.EditBaseline;
		document.EditBaseline = current;
		if (before == current || !m_PushUndo)
			return;

		const AssetHandle handle = document.Handle;
		m_PushUndo(std::format("Edit Material '{}'", GetMaterialName(handle)),
			[handle, before]()
			{
				if (Ref<MaterialAsset> target = AssetManager::GetAsset<MaterialAsset>(handle))
					before.Apply(target);
			},
			[handle, current]()
			{
				if (Ref<MaterialAsset> target = AssetManager::GetAsset<MaterialAsset>(handle))
					current.Apply(target);
			});
	}

	// -- UI ----------------------------------------------------------------------------------------

	void MaterialEditorPanel::OnEvent(Event& event)
	{
		if (!m_KeyboardFocus || !event.IsInCategory(EventCategoryKeyboard))
			return;
		if (event.GetEventType() == EventType::KeyPressed)
		{
			const auto& key = static_cast<const KeyPressedEvent&>(event);
			const bool control =
				Input::IsKeyPressed(Key::LeftControl) || Input::IsKeyPressed(Key::RightControl);
			if (control && !ImGui::GetIO().WantTextInput &&
				(key.GetKeyCode() == Key::Z || key.GetKeyCode() == Key::Y))
				return; // Material edits use the editor's existing undo stack.
		}
		event.Handled = true;
	}

	void MaterialEditorPanel::SaveState(Yaml::Writer& out) const
	{
		out << Yaml::Key << "ShowGraph" << Yaml::Value << m_ShowGraph;
		out << Yaml::Key << "ShowPreview" << Yaml::Value << m_ShowPreview;
		out << Yaml::Key << "NarrowPreview" << Yaml::Value << m_NarrowPreview;
		out << Yaml::Key << "PreviewFraction" << Yaml::Value << m_PreviewFraction;
		if (m_GraphCanvas)
			m_GraphCanvas->SaveState(out);
	}

	void MaterialEditorPanel::LoadState(const Yaml::Node& state)
	{
		m_ShowGraph = state["ShowGraph"].as<bool>(false);
		m_ShowPreview = state["ShowPreview"].as<bool>(true);
		m_NarrowPreview = state["NarrowPreview"].as<bool>(false);
		const float fraction = state["PreviewFraction"].as<float>(0.3f);
		m_PreviewFraction = std::isfinite(fraction) ? std::clamp(fraction, 0.1f, 0.8f) : 0.3f;
		if (!m_GraphCanvas)
			m_GraphCanvas = CreateScope<MaterialGraphCanvas>();
		m_GraphCanvas->LoadState(state);
	}

	void MaterialEditorPanel::OnImGuiRender(bool& isOpen)
	{
		m_KeyboardFocus = false;
		// Without an imgui.ini entry a new window opens at its minimum size.
		ImGui::SetNextWindowSize(ImVec2(900.0f, 560.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Material Editor", &isOpen))
		{
			ImGui::End();
			return;
		}

		m_KeyboardFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		if (!Project::GetActive())
		{
			ImGuiEx::PanelEmptyState(
				"material_no_project", "Open a project",
				"Material documents belong to the active project. Open or create a project to begin.");
			ImGui::End();
			return;
		}

		if (m_Documents.empty())
		{
			ImGuiEx::PanelEmptyState(
				"material_empty", "No material open",
				"Double-click a material in the Content Browser, or choose Edit beside a material slot in "
				"Properties. Open materials stay with you across workspace changes.");
			ImGui::End();
			return;
		}

		UI_Tabs();
		UI_CloseConfirm();

		Document* document = GetActiveDocument();
		Ref<MaterialAsset> material = document ? AssetManager::GetAsset<MaterialAsset>(document->Handle) : nullptr;
		if (!document || !material || !material->GetMaterial())
		{
			ImGuiEx::PanelMessage("Material unavailable",
								  "The source asset could not be loaded. Check its path and the Log panel; "
								  "your open document has been retained.",
								  true);
			ImGui::End();
			return;
		}

		if (!document->EditInProgress)
			document->EditBaseline = MaterialState::Capture(material);
		if (m_GraphCanvas && m_GraphCanvas->GetSourceMaterial() != document->Handle)
			m_GraphCanvas->StopPreview();

		const float font = ImGui::GetFontSize();
		const ImGuiEx::PanelSplit split = ImGuiEx::PanelSplitWidths(
			ImGui::GetContentRegionAvail().x, m_PreviewFraction, font * 12.0f, font * 25.0f);
		m_Compact = !split.SideBySide;
		if (m_KeyboardFocus && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup) &&
			ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
			SaveDocument(*document);
		UI_Toolbar(*document, material);
		{
			ImGuiEx::ScopedColour color(ImGuiCol_Text, Colors::Theme::textCaption);
			ImGui::TextWrapped("%s%s", IsDirty(*document) ? "Unsaved changes" : "Saved",
							   material->GetParent() ? "  |  Instance: inherited values with local overrides"
													 : "");
		}
		if (!document->SaveError.empty())
			ImGuiEx::PanelMessage("Could not save material", document->SaveError.c_str(), true);
		ImGui::Spacing();

		const bool previewOnly = m_Compact && m_NarrowPreview;
		const bool previewBeside = !m_Compact && m_ShowPreview;
		if (previewOnly || previewBeside)
		{
			if (ImGui::BeginChild("##material_preview_column", {previewBeside ? split.First : 0.0f, 0.0f}))
			{
				if (!m_Preview)
					m_Preview = Ref<MaterialPreview>::Create();
				const AssetHandle shown = m_ShowGraph && m_GraphCanvas && m_GraphCanvas->GetPreviewMaterial()
											  ? m_GraphCanvas->GetPreviewMaterial()
											  : document->Handle;
				if (m_Preview->GetMaterial() != shown)
					m_Preview->SetMaterial(shown);
				UI_Preview(ImGui::GetContentRegionAvail().x);
			}
			ImGui::EndChild();
			if (previewBeside)
			{
				ImGui::SameLine();
				ImGuiEx::PanelSplitHandle("##material_preview_split", split, m_PreviewFraction);
				ImGui::SameLine();
			}
		}

		const bool ownsGraph = material->GetGraph() && !material->GetParent();
		if (!previewOnly && ownsGraph && m_ShowGraph)
		{
			if (!m_GraphCanvas)
				m_GraphCanvas = CreateScope<MaterialGraphCanvas>();
			if (ImGui::BeginChild("##material_graph_column", ImVec2(0.0f, 0.0f)))
				m_GraphCanvas->Draw(material, m_PushUndo);
			ImGui::EndChild();
		}
		else if (!previewOnly)
		{
			if (ImGui::BeginChild("##material_properties_column", ImVec2(0.0f, 0.0f)))
			{
				UI_Instance(material);
				UI_GraphParameters(material);
				UI_Properties(*document, material);
			}
			ImGui::EndChild();
		}

		// An instance's overrides are the values that differ from its parent; its inherited values
		// still match the parent here, since a parent change reaches it through propagation.
		if (material->GetParent() && !(MaterialState::Capture(material) == document->EditBaseline))
		{
			if (Ref<MaterialAsset> parent = AssetManager::GetAsset<MaterialAsset>(material->GetParent()))
				material->UpdateOverrides(*parent);
		}

		TrackEdit(*document, material);

		// Instances follow their parent live, including while a slider is dragged.
		if (MaterialState current = MaterialState::Capture(material); !(current == document->Propagated))
		{
			MaterialAsset::PropagateToInstances(document->Handle);
			document->Propagated = std::move(current);
		}

		// Opening an instance may grow the owning vector. Finish every use of document first.
		if (m_OpenAfterFrame)
		{
			const AssetHandle handle = m_OpenAfterFrame;
			m_OpenAfterFrame = 0;
			OpenMaterial(handle);
		}
		ImGui::End();
	}

	void MaterialEditorPanel::UI_Tabs()
	{
		int closeRequest = -1;
		if (ImGui::BeginTabBar("##material_editor_tabs", ImGuiTabBarFlags_Reorderable |
															 ImGuiTabBarFlags_FittingPolicyScroll |
															 ImGuiTabBarFlags_TabListPopupButton))
		{
			for (size_t i = 0; i < m_Documents.size(); ++i)
			{
				Document& document = m_Documents[i];
				const auto& metadata =
					Project::GetEditorAssetManager()->GetAssetRegistry().Get(document.Handle);
				if (document.LabelPath != metadata.FilePath)
				{
					document.LabelPath = metadata.FilePath;
					const auto path = document.LabelPath.generic_u8string();
					document.DisplayPath.assign(reinterpret_cast<const char*>(path.data()), path.size());
					document.TabLabel = std::format("{}###material_tab_{}", GetMaterialName(document.Handle),
													static_cast<uint64_t>(document.Handle));
				}
				bool open = true;
				ImGuiTabItemFlags flags = IsDirty(document) ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None;
				if (m_SelectActiveTab && (int)i == m_ActiveDocument)
					flags |= ImGuiTabItemFlags_SetSelected;

				// ### keeps the tab ID stable while the displayed name changes.
				ImGui::SetNextItemWidth(
					std::min(ImGui::CalcTextSize(document.TabLabel.c_str()).x + ImGui::GetFontSize() * 3.0f,
							 ImGui::GetFontSize() * 18.0f));
				if (ImGui::BeginTabItem(document.TabLabel.c_str(), &open, flags))
				{
					if (!m_SelectActiveTab)
						m_ActiveDocument = (int)i;
					ImGui::EndTabItem();
				}
				ImGui::SetItemTooltip("%s", document.DisplayPath.c_str());

				if (!open)
					closeRequest = (int)i;
			}
			ImGui::EndTabBar();
		}
		m_SelectActiveTab = false;

		if (closeRequest >= 0)
		{
			if (IsDirty(m_Documents[closeRequest]))
			{
				m_PendingCloseIndex = closeRequest;
				m_OpenCloseConfirm = true;
			}
			else
			{
				CloseDocument((size_t)closeRequest);
			}
		}
	}

	void MaterialEditorPanel::UI_CloseConfirm()
	{
		if (m_OpenCloseConfirm)
		{
			ImGui::OpenPopup("Unsaved material##material_close");
			m_OpenCloseConfirm = false;
		}

		if (!ImGui::BeginPopupModal("Unsaved material##material_close", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			return;

		const bool valid = m_PendingCloseIndex >= 0 && m_PendingCloseIndex < (int)m_Documents.size();
		if (valid)
		{
			const Document& document = m_Documents[m_PendingCloseIndex];
			ImGui::TextWrapped("Save changes to %s?", document.DisplayPath.c_str());
			if (!document.SaveError.empty())
				ImGuiEx::PanelMessage("Could not save material", document.SaveError.c_str(), true);
		}

		if (ImGui::Button("Save") && valid)
		{
			if (SaveDocument(m_Documents[m_PendingCloseIndex]))
			{
				CloseDocument(static_cast<size_t>(m_PendingCloseIndex));
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Discard") && valid)
		{
			Document& document = m_Documents[m_PendingCloseIndex];
			if (Ref<MaterialAsset> material = AssetManager::GetAsset<MaterialAsset>(document.Handle))
			{
				document.Saved.Apply(material);
				ApplyGraphSnapshot(document.Handle, document.SavedGraph);
				MaterialAsset::PropagateToInstances(document.Handle);
			}
			CloseDocument((size_t)m_PendingCloseIndex);
			m_PendingCloseIndex = -1;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || !valid)
		{
			m_PendingCloseIndex = -1;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	void MaterialEditorPanel::UI_Instance(Ref<MaterialAsset> material)
	{
		if (!ImGuiEx::PropertyGridHeader("Instance", material->GetParent() != 0))
			return;

		ImGuiEx::BeginPropertyGrid();
		AssetHandle parentHandle = material->GetParent();
		if (ImGuiEx::PropertyAssetReference<MaterialAsset>("Parent", parentHandle,
			"Values this material does not set itself come from the parent. Clear it to keep the current values.", nullptr, {}, false))
		{
			if (!material->SetParent(parentHandle))
				LUX_CONSOLE_LOG_ERROR("'{}' cannot be the parent: it is this material or one of its instances", GetMaterialName(parentHandle));
			else if (Ref<MaterialAsset> parent = parentHandle ? AssetManager::GetAsset<MaterialAsset>(parentHandle) : nullptr)
				material->InheritFrom(*parent);
		}
		ImGuiEx::EndPropertyGrid();

		Ref<MaterialAsset> parent = material->GetParent() ? AssetManager::GetAsset<MaterialAsset>(material->GetParent()) : nullptr;
		if (!parent)
		{
			if (material->GetParent())
				ImGuiEx::PanelMessage("Parent unavailable", "This material retains its own values. Restore "
															"the parent asset or choose another parent.");
			ImGui::TreePop();
			return;
		}

		if (material->GetOverrides().empty())
		{
			ImGuiEx::PanelMessage("Inherited values",
								  "Every value comes from the parent. Change one to override it.");
		}
		else
		{
			ImGui::TextDisabled("Set by this material:");
			// Copied: a reset changes the set.
			const std::set<std::string, std::less<>> overrides = material->GetOverrides();
			for (const std::string& key : overrides)
			{
				ImGui::PushID(key.c_str());
				if (ImGui::SmallButton("Reset"))
					material->ResetToParent(key, *parent);
				ImGui::SameLine();
				ImGui::TextUnformatted(key.c_str());
				ImGui::PopID();
			}
			if (ImGui::Button("Reset All to Parent"))
			{
				material->SetOverrides({});
				material->InheritFrom(*parent);
			}
		}
		ImGui::TreePop();
	}

	void MaterialEditorPanel::UI_GraphParameters(Ref<MaterialAsset> material)
	{
		Ref<MaterialGraph> graph = material->GetGraph();
		if (!graph)
			return;

		std::vector<MaterialGraphParameter> exposed;
		for (const MaterialGraphParameter& parameter : graph->GetParameters())
		{
			if (parameter.Exposed)
				exposed.push_back(parameter);
		}
		if (exposed.empty() || !ImGuiEx::PropertyGridHeader("Graph Parameters"))
			return;

		// No recompile: these go into the material's row. Instances override them like any value.
		ImGuiEx::BeginPropertyGrid();
		for (const MaterialGraphParameter& parameter : exposed)
		{
			MaterialGraphParameterValue value = material->GetGraphParameter(parameter);
			bool changed = false;
			switch (parameter.Kind)
			{
				case MaterialGraphParameterKind::Float:
					changed = ImGuiEx::Property(parameter.Name.c_str(), value.Value.x, 0.01f, 0.0f, 0.0f, "", false);
					break;
				case MaterialGraphParameterKind::Color:
					changed = ImGuiEx::PropertyColor(parameter.Name.c_str(), value.Value, "", false);
					break;
				case MaterialGraphParameterKind::Texture:
					changed = ImGuiEx::PropertyAssetReference<Texture2D>(parameter.Name.c_str(), value.Texture, "", nullptr, {}, false);
					break;
			}
			if (changed)
				material->SetGraphParameter(parameter.Name, value);
		}
		ImGuiEx::EndPropertyGrid();
		ImGui::TreePop();
	}

	void MaterialEditorPanel::UI_Toolbar(Document& document, Ref<MaterialAsset> material)
	{
		const bool dirty = IsDirty(document);
		const bool ownsGraph = material->GetGraph() && !material->GetParent();
		using ImGuiEx::PanelAction;
		const PanelAction actions[] = {
			{static_cast<int>(MaterialAction::Save), "Save", LUX_ICON_FLOPPY_O, "Ctrl+S",
			 dirty || !document.SaveError.empty(), false, true},
			{static_cast<int>(MaterialAction::Revert), "Revert", LUX_ICON_UNDO, nullptr, dirty},
			{static_cast<int>(MaterialAction::Properties), "Properties", LUX_ICON_SLIDERS, nullptr, true,
			 (!ownsGraph || !m_ShowGraph) && !(m_Compact && m_NarrowPreview)},
			{static_cast<int>(MaterialAction::Graph), "Graph", LUX_ICON_SITEMAP, nullptr, ownsGraph,
			 ownsGraph && m_ShowGraph && !(m_Compact && m_NarrowPreview)},
			{static_cast<int>(MaterialAction::Preview), "Preview", LUX_ICON_EYE, nullptr, true,
			 m_Compact ? m_NarrowPreview : m_ShowPreview},
			{static_cast<int>(MaterialAction::Instance), "Create Instance", LUX_ICON_CLONE, nullptr, true,
			 false, false, true},
			{static_cast<int>(MaterialAction::MakeGraph), "Make Graph Material", nullptr, nullptr,
			 !material->GetParent() && !material->GetGraph(), false, false, true},
			{static_cast<int>(MaterialAction::RemoveGraph), "Remove Graph", nullptr, nullptr, ownsGraph,
			 false, false, true}};
		const int action = ImGuiEx::PanelActionBar("material_actions", actions);
		if (action < 0)
			return;
		switch (static_cast<MaterialAction>(action))
		{
		case MaterialAction::Save:
			SaveDocument(document);
			break;
		case MaterialAction::Revert:
			RevertDocument(document, material);
			break;
		case MaterialAction::Properties:
			m_ShowGraph = false;
			m_NarrowPreview = false;
			break;
		case MaterialAction::Graph:
			m_ShowGraph = true;
			m_NarrowPreview = false;
			break;
		case MaterialAction::Preview:
			if (m_Compact)
				m_NarrowPreview = !m_NarrowPreview;
			else
				m_ShowPreview = !m_ShowPreview;
			break;
		case MaterialAction::Instance:
			m_OpenAfterFrame = ContentBrowserPanel::Get().CreateMaterialInstance(document.Handle);
			break;
		case MaterialAction::MakeGraph:
		case MaterialAction::RemoveGraph:
		{
			const AssetHandle handle = document.Handle;
			const std::string before = GraphSnapshot(material);
			const bool make = static_cast<MaterialAction>(action) == MaterialAction::MakeGraph;
			const std::string after =
				make ? MaterialGraphCanvas::Snapshot(*MaterialGraph::CreateDefault()) : std::string();
			ApplyGraphSnapshot(handle, after);
			if (m_GraphCanvas)
				m_GraphCanvas->StopPreview();
			m_ShowGraph = make;
			m_NarrowPreview = false;
			if (m_PushUndo)
				m_PushUndo(
					make ? "Make Graph Material" : "Remove Material Graph",
					[handle, before]() { ApplyGraphSnapshot(handle, before); },
					[handle, after]() { ApplyGraphSnapshot(handle, after); });
			break;
		}
		}
	}

	void MaterialEditorPanel::UI_Preview(float width)
	{
		const float controls = ImGui::GetFrameHeightWithSpacing() * 3.0f;
		const float size = std::max(1.0f, std::min(width, ImGui::GetContentRegionAvail().y - controls));
		const ImVec2 imageSize(size, size);

		// An invisible button owns the input (orbit drag, wheel zoom); the image is drawn behind it.
		ImGui::InvisibleButton("##material_preview_image", imageSize);
		const ImVec2 imageMin = ImGui::GetItemRectMin();
		const ImVec2 imageMax = ImGui::GetItemRectMax();

		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
		{
			const ImVec2 delta = ImGui::GetIO().MouseDelta;
			m_Preview->Orbit(-delta.x * kOrbitDegreesPerPixel, delta.y * kOrbitDegreesPerPixel);
		}
		if (ImGui::IsItemHovered())
		{
			const float wheel = ImGui::GetIO().MouseWheel;
			if (wheel != 0.0f)
				m_Preview->Zoom(1.0f - wheel * kZoomPerWheelStep);
			ImGui::SetTooltip("Drag to orbit, scroll to zoom");
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(imageMin, imageMax, Colors::Theme::backgroundDark, kPreviewRounding);
		const ImVec2 pixels = ImGui::GetWindowViewport()->FramebufferScale;
		if (Ref<Image2D> image = m_Preview->Render(static_cast<uint32_t>(size * pixels.x),
												   static_cast<uint32_t>(size * pixels.y)))
			drawList->AddImageRounded(GetImGuiTextureID(image), imageMin, imageMax, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32_WHITE, kPreviewRounding);
		else
			drawList->AddText(ImVec2(imageMin.x + 8.0f, imageMin.y + 8.0f), Colors::Theme::textDarker, "Preparing preview...");
		drawList->AddRect(imageMin, imageMax, ImGui::IsItemHovered() ? Colors::Theme::accent : Colors::Theme::backgroundPopup, kPreviewRounding);

		ImGui::Spacing();

		MaterialPreview::Shape shape = m_Preview->GetShape();
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("##material_preview_shape", MaterialPreview::GetShapeName(shape)))
		{
			for (uint8_t i = 0; i < (uint8_t)MaterialPreview::Shape::Count; ++i)
			{
				const auto candidate = (MaterialPreview::Shape)i;
				if (ImGui::Selectable(MaterialPreview::GetShapeName(candidate), candidate == shape))
					m_Preview->SetShape(candidate);
			}
			ImGui::EndCombo();
		}

		bool sky = m_Preview->IsSkyEnabled();
		if (ImGui::Checkbox("Sky light##material_preview_sky", &sky))
			m_Preview->SetSkyEnabled(sky);

		if (ImGui::SmallButton("Reset view##material_preview_reset"))
			m_Preview->ResetCamera();
	}

	void MaterialEditorPanel::UI_Properties(Document&, Ref<MaterialAsset> material)
	{
		// doPushUndo = false on every widget: these edit an asset, not the scene, so they must not
		// raise the scene-edited flag. TrackEdit records them on the editor undo stack instead.
		const bool transparent = material->IsTransparent();
		MaterialSurfaceParameters surface = material->GetSurfaceParameters();
		bool surfaceChanged = false;

		auto textureSlot = [](const char* label, AssetHandle handle, const char* helpText, AssetHandle& outHandle)
			{
				outHandle = handle;
				return ImGuiEx::PropertyAssetReference<Texture2D>(label, outHandle, helpText, nullptr, {}, false);
			};
		auto channelDropdown = [](const char* label, MaterialTextureChannel& channel, const char* helpText)
			{
				return ImGuiEx::PropertyDropdown(label, s_ChannelNames, (int32_t)std::size(s_ChannelNames), channel, helpText, false);
			};

		if (ImGuiEx::PropertyGridHeader("Surface"))
		{
			ImGuiEx::BeginPropertyGrid();

			glm::vec3 albedo = material->GetAlbedoColor();
			if (ImGuiEx::PropertyColor("Base Color", albedo, "", false))
				material->SetAlbedoColor(albedo);

			// The transparent path shades every surface as a dielectric, so metalness is not offered.
			if (!transparent)
			{
				float metalness = material->GetMetalness();
				if (ImGuiEx::Property("Metallic", metalness, 0.01f, 0.0f, 1.0f, "", false))
					material->SetMetalness(metalness);
			}

			float roughness = material->GetRoughness();
			if (ImGuiEx::Property("Roughness", roughness, 0.01f, 0.0f, 1.0f, "", false))
				material->SetRoughness(roughness);

			surfaceChanged |= ImGuiEx::Property("Specular", surface.Specular, 0.01f, 0.0f, 1.0f,
				"Reflectance of non-metals. 0.5 is the common 4% of plastics, paint and stone; lower for skin, higher for gems.", false);

			// A transparent material is always Blend; only the opaque path can be Opaque or Cutout.
			if (!transparent)
			{
				// PropertyDropdown indexes its name list by the enum value, so a file carrying
				// AlphaMode: Blend on a non-transparent material would read past s_AlphaModeNames.
				// Opaque is also what the renderer resolves that combination to.
				if (surface.AlphaMode == MaterialAlphaMode::Blend)
				{
					surface.AlphaMode = MaterialAlphaMode::Opaque;
					surfaceChanged = true;
				}

				surfaceChanged |= ImGuiEx::PropertyDropdown("Alpha Mode", s_AlphaModeNames, (int32_t)std::size(s_AlphaModeNames),
					surface.AlphaMode, "Cutout discards pixels below the cutoff, giving a hard edge that still lights and shadows as solid geometry.", false);

				if (surface.AlphaMode == MaterialAlphaMode::Cutout)
				{
					surfaceChanged |= ImGuiEx::Property("Alpha Cutoff", surface.AlphaThreshold, 0.01f, 0.0f, 1.0f,
						"Base colour alpha below this is discarded.", false);
				}
			}

			if (transparent)
			{
				float opacity = material->GetTransparency();
				if (ImGuiEx::Property("Opacity", opacity, 0.01f, 0.0f, 1.0f, "", false))
					material->SetTransparency(opacity);
			}

			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}

		if (ImGuiEx::PropertyGridHeader("Emission"))
		{
			ImGuiEx::BeginPropertyGrid();

			surfaceChanged |= ImGuiEx::PropertyColor("Emissive Color", surface.EmissiveColor, "", false);

			float emission = material->GetEmission();
			if (ImGuiEx::Property("Intensity", emission, 0.01f, 0.0f, 1000.0f, "Light the surface gives off, independent of lighting. 0 turns emission off.", false))
				material->SetEmission(emission);

			AssetHandle emissiveMap;
			if (textureSlot("Emissive Map", surface.EmissiveMap, "Multiplies the emissive color.", emissiveMap))
			{
				surface.EmissiveMap = emissiveMap;
				surfaceChanged = true;
			}

			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}

		if (transparent && ImGuiEx::PropertyGridHeader("Refraction"))
		{
			ImGuiEx::BeginPropertyGrid();

			surfaceChanged |= ImGuiEx::Property("Transmission", surface.Transmission, 0.01f, 0.0f, 1.0f,
				"Share of the light that passes through and refracts. 0 = plain alpha blending; refractive glass keeps Opacity at 1.", false);
			surfaceChanged |= ImGuiEx::Property("IOR", surface.IOR, 0.01f, 1.0f, 3.0f, "Index of refraction: 1.33 water, 1.5 glass, 2.4 diamond.", false);
			surfaceChanged |= ImGuiEx::Property("Thickness", surface.Thickness, 0.01f, 0.0f, 100.0f,
				"Metres the light travels inside. 0 = thin-walled: blurred by roughness, not displaced.", false);
			surfaceChanged |= ImGuiEx::PropertyColor("Attenuation Color", surface.AttenuationColor, "Colour light fades toward inside the volume.", false);
			surfaceChanged |= ImGuiEx::Property("Attenuation Distance", surface.AttenuationDistance, 0.01f, 0.0f, 1000.0f,
				"Metres for light to reach the attenuation colour. 0 = no absorption.", false);

			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}

		// Diffuse transmission is a deferred-lighting term: the transparent path has none.
		if (!transparent && ImGuiEx::PropertyGridHeader("Translucency"))
		{
			ImGuiEx::BeginPropertyGrid();

			surfaceChanged |= ImGuiEx::Property("Transmission", surface.DiffuseTransmission, 0.01f, 0.0f, 1.0f,
				"How much of the diffuse light passes through to the far side, as through a leaf lit from behind. 0 turns it off.", false);
			surfaceChanged |= ImGuiEx::PropertyColor("Transmission Color", surface.DiffuseTransmissionColor,
				"Tint of the light that passes through.", false);

			AssetHandle transmissionMap;
			if (textureSlot("Transmission Map", surface.DiffuseTransmissionMap, "Its alpha multiplies Transmission, to mask out veins and stems.", transmissionMap))
			{
				surface.DiffuseTransmissionMap = transmissionMap;
				surfaceChanged = true;
			}

			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}

		if (ImGuiEx::PropertyGridHeader("Advanced Shading", false))
		{
			ImGuiEx::BeginPropertyGrid();

			surfaceChanged |= ImGuiEx::Property("Clearcoat", surface.Clearcoat, 0.01f, 0.0f, 1.0f,
				"A clear varnish layer on top, like car paint or lacquered wood. 0 turns it off.", false);
			if (surface.Clearcoat > 0.0f)
				surfaceChanged |= ImGuiEx::Property("Clearcoat Roughness", surface.ClearcoatRoughness, 0.01f, 0.0f, 1.0f,
					"Roughness of the varnish layer alone.", false);

			surfaceChanged |= ImGuiEx::PropertyColor("Sheen Color", surface.SheenColor,
				"Soft light from fibres at grazing angles, like velvet or cloth. Black turns it off.", false);
			if (surface.SheenColor != glm::vec3(0.0f))
				surfaceChanged |= ImGuiEx::Property("Sheen Roughness", surface.SheenRoughness, 0.01f, 0.0f, 1.0f,
					"Low is a tight rim of sheen; high spreads it over the surface.", false);

			surfaceChanged |= ImGuiEx::PropertyColor("Subsurface Color", surface.SubsurfaceColor,
				"Light scattered inside the material, like wax, skin or jade. Black turns it off.", false);
			if (surface.SubsurfaceColor != glm::vec3(0.0f))
				surfaceChanged |= ImGuiEx::Property("Subsurface Thickness", surface.SubsurfaceThickness, 0.01f, 0.0f, 1.0f,
					"0 is thin and lets the most light through; 1 is thick and scatters none.", false);

			surfaceChanged |= ImGuiEx::Property("Anisotropy", surface.Anisotropy, 0.01f, -1.0f, 1.0f,
				"Stretches highlights along the surface, like brushed metal. Negative stretches them across. 0 turns it off. Needs a mesh with tangents.", false);
			if (surface.Anisotropy != 0.0f)
				surfaceChanged |= ImGuiEx::Property("Anisotropy Rotation", surface.AnisotropyRotation, 1.0f, -180.0f, 180.0f,
					"Turns the direction of the stretch, in degrees.", false);

			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}

		if (ImGuiEx::PropertyGridHeader("Texture Maps"))
		{
			ImGuiEx::BeginPropertyGrid();

			AssetHandle albedoMap;
			if (textureSlot("Base Color Map", material->GetAlbedoMapHandle(), "", albedoMap))
			{
				if (albedoMap)
					material->SetAlbedoMap(albedoMap);
				else
					material->ClearAlbedoMap();
			}

			AssetHandle normalMap;
			if (textureSlot("Normal Map", material->GetNormalMapHandle(), "", normalMap))
			{
				if (normalMap)
					material->SetNormalMap(normalMap);
				else
					material->ClearNormalMap();
			}

			bool useNormalMap = material->IsUsingNormalMap();
			if (ImGuiEx::Property("Use Normal Map", useNormalMap, "", false))
				material->SetUseNormalMap(useNormalMap);

			surfaceChanged |= ImGuiEx::Property("Normal Strength", surface.NormalStrength, 0.01f, 0.0f, 4.0f,
				"0 flattens the normal map, 1 is as authored.", false);

			if (!transparent)
			{
				AssetHandle metalnessMap;
				if (textureSlot("Metallic Map", material->GetMetalnessMapHandle(), "", metalnessMap))
				{
					if (metalnessMap)
						material->SetMetalnessMap(metalnessMap);
					else
						material->ClearMetalnessMap();
				}
				surfaceChanged |= channelDropdown("Metallic Channel", surface.MetalnessChannel, "Channel of the metallic map to read. glTF ORM maps keep metallic in B.");
			}

			AssetHandle roughnessMap;
			if (textureSlot("Roughness Map", material->GetRoughnessMapHandle(), "", roughnessMap))
			{
				if (roughnessMap)
					material->SetRoughnessMap(roughnessMap);
				else
					material->ClearRoughnessMap();
			}
			surfaceChanged |= channelDropdown("Roughness Channel", surface.RoughnessChannel, "Channel of the roughness map to read. glTF ORM maps keep roughness in G.");

			AssetHandle occlusionMap;
			if (textureSlot("Occlusion Map", surface.OcclusionMap, "Ambient occlusion: darkens ambient and reflected light in crevices.", occlusionMap))
			{
				surface.OcclusionMap = occlusionMap;
				surfaceChanged = true;
			}
			surfaceChanged |= channelDropdown("Occlusion Channel", surface.OcclusionChannel, "Channel of the occlusion map to read. glTF ORM maps keep occlusion in R.");
			surfaceChanged |= ImGuiEx::Property("Occlusion Strength", surface.OcclusionStrength, 0.01f, 0.0f, 1.0f, "", false);

			AssetHandle heightMap;
			if (textureSlot("Height Map", surface.HeightMap, "Grayscale height, used as a bump map (red channel).", heightMap))
			{
				surface.HeightMap = heightMap;
				surfaceChanged = true;
			}
			surfaceChanged |= ImGuiEx::Property("Bump Height", surface.BumpHeight, 0.001f, 0.0f, 1.0f,
				"The height map's full range in world units (metres).", false);

			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}

		if (ImGuiEx::PropertyGridHeader("UV"))
		{
			ImGuiEx::BeginPropertyGrid();

			surfaceChanged |= ImGuiEx::PropertyDropdown("Mapping", s_MappingNames, (int32_t)std::size(s_MappingNames), surface.Mapping,
				"UV uses the mesh's texture coordinates. Triplanar projects the maps from the three axes, in the world or on the object, so they never stretch and need no UVs.", false);
			const bool triplanar = surface.Mapping != MaterialMapping::UV;
			surfaceChanged |= ImGuiEx::Property("Tiling", surface.UVTiling, 0.01f, 0.0f, 0.0f,
				triplanar ? "Repeats per unit of distance." : "How many times the maps repeat across the surface.", false);
			surfaceChanged |= ImGuiEx::Property("Offset", surface.UVOffset, 0.01f, 0.0f, 0.0f, "", false);
			surfaceChanged |= ImGuiEx::Property("Rotation", surface.UVRotation, 0.5f, -360.0f, 360.0f, "Degrees.", false);
			if (triplanar)
				surfaceChanged |= ImGuiEx::Property("Blend Sharpness", surface.TriplanarSharpness, 0.1f, 1.0f, 64.0f,
					"How quickly one projection gives way to the next at corners. Higher is a harder seam.", false);

			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}

		if (ImGuiEx::PropertyGridHeader("Detail", false))
		{
			ImGuiEx::BeginPropertyGrid();

			AssetHandle detailMap;
			if (textureSlot("Detail Albedo", surface.DetailAlbedoMap, "A finer colour map laid over the base colour. Mid grey changes nothing; lighter brightens, darker darkens.", detailMap))
			{
				surface.DetailAlbedoMap = detailMap;
				surfaceChanged = true;
			}
			if (textureSlot("Detail Normal", surface.DetailNormalMap, "A finer normal map added on top of the main one.", detailMap))
			{
				surface.DetailNormalMap = detailMap;
				surfaceChanged = true;
			}
			if (textureSlot("Detail Mask", surface.DetailMaskMap, "Where the detail shows: its red channel, white for full detail. Without one, detail is everywhere.", detailMap))
			{
				surface.DetailMaskMap = detailMap;
				surfaceChanged = true;
			}
			surfaceChanged |= ImGuiEx::Property("Detail Tiling", surface.DetailTiling, 0.01f, 0.0f, 0.0f, "Repeats of the detail maps across the mesh's UVs.", false);
			surfaceChanged |= ImGuiEx::Property("Detail Normal Strength", surface.DetailNormalStrength, 0.01f, 0.0f, 4.0f, "", false);

			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}

		if (ImGuiEx::PropertyGridHeader("Rendering"))
		{
			ImGuiEx::BeginPropertyGrid();

			bool castShadows = material->IsShadowCasting();
			if (ImGuiEx::Property("Cast Shadows", castShadows, "", false))
				material->SetShadowCasting(castShadows);

			surfaceChanged |= ImGuiEx::Property("Two Sided", surface.TwoSided,
				"Draws back faces too, lit from the side the camera sees. For leaves, chains and other single-sheet cards; "
				"a transparent material blends its far side in first.", false);

			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}

		if (ImGuiEx::PropertyGridHeader("Acoustics"))
		{
			ImGuiEx::BeginPropertyGrid();

			// "None" first, then every tag in AcousticMaterial order, so index - 1 is the tag.
			static auto s_AcousticNames = []()
				{
					std::array<const char*, AcousticMaterialCount + 1> names{};
					names[0] = "None";
					for (size_t i = 0; i < AcousticMaterialCount; ++i)
						names[i + 1] = AcousticMaterialNames[i];
					return names;
				}();
			int32_t selected = material->GetAcousticTag() + 1;
			if (selected < 0 || selected > static_cast<int32_t>(AcousticMaterialCount))
				selected = 0;
			if (ImGuiEx::PropertyDropdown("Acoustic Material", s_AcousticNames.data(), static_cast<int32_t>(s_AcousticNames.size()), &selected,
				"How surfaces drawn with this material sound. Mesh colliders with Inherit From Material use it; an Audio Surface component still overrides it. None leaves the collider's own tag.", false))
				material->SetAcousticTag(selected - 1);

			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}

		if (surfaceChanged)
			material->SetSurfaceParameters(surface);
	}
}
