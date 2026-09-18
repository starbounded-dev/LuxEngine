#include "lpch.h"
#include "MaterialEditorPanel.h"
#include "MaterialPreview.h"

#include "Lux/Asset/AssetImporter.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Core/Application.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/ImGuiUtilities.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Renderer/Texture.h"

#include <imgui/imgui.h>

#include <format>
#include <iterator>

namespace Lux
{
	namespace
	{
		constexpr float kPreviewMinWidth = 180.0f;
		constexpr float kPreviewMaxWidth = 420.0f;
		constexpr float kPreviewWidthFraction = 0.42f;
		constexpr float kOrbitDegreesPerPixel = 0.4f;
		constexpr float kZoomPerWheelStep = 0.1f;
		constexpr float kPreviewRounding = 6.0f;

		// Not constexpr: PropertyDropdown takes a mutable const char** list.
		const char* s_ChannelNames[] = { "R", "G", "B", "A" };

		std::string GetMaterialName(AssetHandle handle)
		{
			if (Ref<EditorAssetManager> editorAssetManager = Project::GetEditorAssetManager())
			{
				const AssetMetadata metadata = editorAssetManager->GetMetadata(handle);
				if (metadata.IsValid())
					return metadata.FilePath.stem().string();
			}
			return "Material";
		}

		ImTextureID GetImGuiTextureID(const Ref<Image2D>& image)
		{
			auto* imguiRenderer = Application::Get().GetImGuiLayer()->GetImGuiRenderer();
			return imguiRenderer->CreateFrameTexture(image->GetHandle().Get(), nvrhi::AllSubresources);
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
		m_Preview = nullptr;
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

		m_Documents.erase(m_Documents.begin() + (ptrdiff_t)index);
		if (m_ActiveDocument >= (int)m_Documents.size())
			m_ActiveDocument = (int)m_Documents.size() - 1;
		m_SelectActiveTab = true;
	}

	void MaterialEditorPanel::SaveDocument(Document& document)
	{
		Ref<MaterialAsset> material = AssetManager::GetAsset<MaterialAsset>(document.Handle);
		if (!material)
		{
			LUX_CORE_ERROR_TAG("Editor", "Material Editor: cannot save material {}: it is no longer loaded", (uint64_t)document.Handle);
			return;
		}

		AssetImporter::Serialize(material.As<Asset>());
		document.Saved = MaterialState::Capture(material);
	}

	bool MaterialEditorPanel::IsDirty(const Document& document) const
	{
		Ref<MaterialAsset> material = AssetManager::GetAsset<MaterialAsset>(document.Handle);
		return material && !(MaterialState::Capture(material) == document.Saved);
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

	void MaterialEditorPanel::OnImGuiRender(bool& isOpen)
	{
		// Without an imgui.ini entry a new window opens at its minimum size.
		ImGui::SetNextWindowSize(ImVec2(900.0f, 560.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Material Editor", &isOpen))
		{
			ImGui::End();
			return;
		}

		if (!Project::GetActive())
		{
			ImGui::TextDisabled("Open a project to edit materials.");
			ImGui::End();
			return;
		}

		if (m_Documents.empty())
		{
			ImGui::TextDisabled("No material open. Double-click a material in the Content Browser,");
			ImGui::TextDisabled("or use Edit next to a material slot in the Inspector.");
			ImGui::End();
			return;
		}

		UI_Tabs();
		UI_CloseConfirm();

		Document* document = GetActiveDocument();
		Ref<MaterialAsset> material = document ? AssetManager::GetAsset<MaterialAsset>(document->Handle) : nullptr;
		if (!document || !material || !material->GetMaterial())
		{
			ImGui::TextDisabled("This material could not be loaded.");
			ImGui::End();
			return;
		}

		if (!document->EditInProgress)
			document->EditBaseline = MaterialState::Capture(material);

		UI_Toolbar(*document, material);
		ImGui::Spacing();

		const float available = ImGui::GetContentRegionAvail().x;
		const float previewWidth = glm::clamp(available * kPreviewWidthFraction, kPreviewMinWidth, kPreviewMaxWidth);

		if (ImGui::BeginChild("##material_preview_column", ImVec2(previewWidth, 0.0f)))
		{
			if (!m_Preview)
				m_Preview = Ref<MaterialPreview>::Create();
			if (m_Preview->GetMaterial() != document->Handle)
				m_Preview->SetMaterial(document->Handle);
			UI_Preview(previewWidth);
		}
		ImGui::EndChild();

		ImGui::SameLine();

		if (ImGui::BeginChild("##material_properties_column", ImVec2(0.0f, 0.0f)))
			UI_Properties(*document, material);
		ImGui::EndChild();

		TrackEdit(*document, material);

		ImGui::End();
	}

	void MaterialEditorPanel::UI_Tabs()
	{
		int closeRequest = -1;
		if (ImGui::BeginTabBar("##material_editor_tabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll))
		{
			for (size_t i = 0; i < m_Documents.size(); ++i)
			{
				const Document& document = m_Documents[i];
				bool open = true;
				ImGuiTabItemFlags flags = IsDirty(document) ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None;
				if (m_SelectActiveTab && (int)i == m_ActiveDocument)
					flags |= ImGuiTabItemFlags_SetSelected;

				// ### keeps the tab ID stable while the displayed name changes.
				const std::string label = std::format("{}###material_tab_{}", GetMaterialName(document.Handle), (uint64_t)document.Handle);
				if (ImGui::BeginTabItem(label.c_str(), &open, flags))
				{
					if (!m_SelectActiveTab)
						m_ActiveDocument = (int)i;
					ImGui::EndTabItem();
				}

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
			ImGui::Text("'%s' has unsaved changes.", GetMaterialName(m_Documents[m_PendingCloseIndex].Handle).c_str());

		if (ImGui::Button("Save") && valid)
		{
			SaveDocument(m_Documents[m_PendingCloseIndex]);
			CloseDocument((size_t)m_PendingCloseIndex);
			m_PendingCloseIndex = -1;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Discard") && valid)
		{
			Document& document = m_Documents[m_PendingCloseIndex];
			if (Ref<MaterialAsset> material = AssetManager::GetAsset<MaterialAsset>(document.Handle))
				document.Saved.Apply(material);
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

	void MaterialEditorPanel::UI_Toolbar(Document& document, Ref<MaterialAsset> material)
	{
		const bool dirty = !(MaterialState::Capture(material) == document.Saved);

		ImGui::BeginDisabled(!dirty);
		if (ImGui::Button(LUX_ICON_FLOPPY_O "  Save"))
			SaveDocument(document);
		ImGui::SameLine();
		// Reverting goes through TrackEdit like any other change, so it can be undone.
		if (ImGui::Button(LUX_ICON_UNDO "  Revert"))
			document.Saved.Apply(material);
		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::TextDisabled("%s", dirty ? "Unsaved changes" : "Saved");
	}

	void MaterialEditorPanel::UI_Preview(float width)
	{
		const float size = glm::max(1.0f, width - ImGui::GetStyle().WindowPadding.x);
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
		if (Ref<Image2D> image = m_Preview->Render((uint32_t)size, (uint32_t)size))
			drawList->AddImageRounded(GetImGuiTextureID(image), imageMin, imageMax, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32_WHITE, kPreviewRounding);
		else
			drawList->AddText(ImVec2(imageMin.x + 8.0f, imageMin.y + 8.0f), Colors::Theme::textDarker, "Preparing preview...");
		drawList->AddRect(imageMin, imageMax, ImGui::IsItemHovered() ? Colors::Theme::accent : Colors::Theme::backgroundPopup, kPreviewRounding);

		ImGui::Spacing();

		MaterialPreview::Shape shape = m_Preview->GetShape();
		ImGui::SetNextItemWidth(size * 0.5f);
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

		ImGui::SameLine();
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

			surfaceChanged |= ImGuiEx::Property("Tiling", surface.UVTiling, 0.01f, 0.0f, 0.0f, "How many times the maps repeat across the surface.", false);
			surfaceChanged |= ImGuiEx::Property("Offset", surface.UVOffset, 0.01f, 0.0f, 0.0f, "", false);
			surfaceChanged |= ImGuiEx::Property("Rotation", surface.UVRotation, 0.5f, -360.0f, 360.0f, "Degrees.", false);

			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}

		if (ImGuiEx::PropertyGridHeader("Rendering"))
		{
			ImGuiEx::BeginPropertyGrid();

			bool castShadows = material->IsShadowCasting();
			if (ImGuiEx::Property("Cast Shadows", castShadows, "", false))
				material->SetShadowCasting(castShadows);

			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}

		if (surfaceChanged)
			material->SetSurfaceParameters(surface);
	}
}
