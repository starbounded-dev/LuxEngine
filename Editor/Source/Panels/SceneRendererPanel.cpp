// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "SceneRendererPanel.h"

#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/ImGuiWidgets.h"
#include "Lux/ImGui/PanelControls.h"
#include "Lux/Core/Events/KeyEvent.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Serialization/Yaml.h"
#include "Lux/Utilities/StringUtils.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <format>
#include <string>
#include <utility>

namespace Lux {

	namespace {

		bool DrawComboProperty(const char* label, int& value, const char* const* options, int optionCount)
		{
			bool modified = false;
			ImGuiEx::ShiftCursor(10.0f, 9.0f);
			ImGuiEx::PropertyLabel(label);
			ImGui::NextColumn();
			ImGuiEx::ShiftCursorY(4.0f);
			ImGui::PushItemWidth(ImGuiEx::GetPropertyItemWidth());

			const int clampedValue = std::clamp(value, 0, optionCount - 1);
			if (ImGui::BeginCombo(std::format("##{0}", label).c_str(), options[clampedValue]))
			{
				for (int i = 0; i < optionCount; i++)
				{
					const bool selected = value == i;
					if (ImGui::Selectable(options[i], selected))
					{
						value = i;
						modified = true;
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			ImGui::PopItemWidth();
			ImGui::NextColumn();
			return modified;
		}

		int EffectScaleToComboIndex(SceneRendererOptions::EffectResolutionScale scale)
		{
			switch (scale)
			{
				case SceneRendererOptions::EffectResolutionScale::Full: return 0;
				case SceneRendererOptions::EffectResolutionScale::Half: return 1;
				case SceneRendererOptions::EffectResolutionScale::Quarter: return 2;
				default: return 1;
			}
		}

		SceneRendererOptions::EffectResolutionScale ComboIndexToEffectScale(int index)
		{
			switch (index)
			{
				case 0: return SceneRendererOptions::EffectResolutionScale::Full;
				case 2: return SceneRendererOptions::EffectResolutionScale::Quarter;
				case 1:
				default: return SceneRendererOptions::EffectResolutionScale::Half;
			}
		}

		bool DrawEffectScaleProperty(const char* label, SceneRendererOptions::EffectResolutionScale& scale)
		{
			const char* effectScaleLabels[] = { "100%", "50%", "25%" };
			int scaleIndex = EffectScaleToComboIndex(scale);
			if (!DrawComboProperty(label, scaleIndex, effectScaleLabels, IM_ARRAYSIZE(effectScaleLabels)))
				return false;

			scale = ComboIndexToEffectScale(scaleIndex);
			return true;
		}

		SceneRendererOptions::EffectResolutionScale ResolutionScaleFromSSRQuality(SceneRendererOptions::SSRQualityPreset quality)
		{
			switch (quality)
			{
				case SceneRendererOptions::SSRQualityPreset::Full:
					return SceneRendererOptions::EffectResolutionScale::Full;
				case SceneRendererOptions::SSRQualityPreset::QuarterDebug:
					return SceneRendererOptions::EffectResolutionScale::Quarter;
				case SceneRendererOptions::SSRQualityPreset::HalfBilateral:
				default:
					return SceneRendererOptions::EffectResolutionScale::Half;
			}
		}

		void ResetDebugViews(SceneRendererOptions& options)
		{
			options.ShowGrid = false;
			options.ShowSelectedInWireframe = false;
			options.ShowPhysicsColliders = false;
			options.PhysicsColliderMode = SceneRendererOptions::PhysicsColliderView::SelectedEntity;
			options.ShowPhysicsCollidersOnTop = false;
			options.ShowShadowCascades = false;
			options.ShowCascadeFrustums = false;
			options.ShowLightComplexity = false;
			options.ShowMaterialComplexity = false;
		}

		// ---- Revamped-panel presentation helpers -------------------------------------------------

		// True when the search box is empty or the label contains the (already-lowercased) query.
		bool MatchesSearch(const std::string& queryLower, const char* label)
		{
			if (queryLower.empty())
				return true;
			return Utils::String::ToLowerCopy(label).find(queryLower) != std::string::npos;
		}

		// Any of a card's row labels (or its title) match — used to hide empty cards while searching.
		bool CardHasMatch(const std::string& queryLower, const char* title, std::initializer_list<const char*> labels)
		{
			if (queryLower.empty())
				return true;
			if (MatchesSearch(queryLower, title))
				return true;
			for (const char* label : labels)
			{
				if (MatchesSearch(queryLower, label))
					return true;
			}
			return false;
		}

		// Card header + auto-sizing bordered body. Returns whether the body is open (caller renders
		// rows only when true, and always calls EndSettingsCard).
		bool BeginSettingsCard(const char* id, const char* icon, const char* title, bool& open, bool forceOpen)
		{
			ImGui::PushID(id);
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(Colors::Theme::background));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
			const bool visible = ImGui::BeginChild("##card", ImVec2(0.0f, 0.0f),
												   ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
			if (!visible)
				return false;

			ImGui::SetNextItemOpen(forceOpen || open, ImGuiCond_Always);
			const std::string heading = std::format("{} {}", icon, title);
			const bool shown = ImGuiEx::BoundedCollapsingHeader(heading.c_str());
			if (!forceOpen)
				open = shown;

			return shown;
		}

		void EndSettingsCard()
		{
			ImGui::EndChild();
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor();
			ImGui::PopID();
			ImGui::Dummy(ImVec2(0.0f, 6.0f));
		}

		bool ToggleRow(const char* label, bool& value)
		{
			return ImGuiEx::Property(label, value, "", false);
		}

	}

	void SceneRendererPanel::SetContext(const Ref<SceneRenderer>& context)
	{
		m_Context = context;
		ApplyProjectSettingsToContext();
	}

	void SceneRendererPanel::OnEvent(Event& event)
	{
		if (m_IsFocused && ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput &&
			event.IsInCategory(EventCategoryKeyboard))
			event.Handled = true;
	}

	void SceneRendererPanel::OnProjectChanged(const Ref<Project>&)
	{
		m_IsFocused = false;
		m_SearchBuffer[0] = '\0';
		m_ProjectRendererSettingsDirty = false;
		m_SaveError.clear();
	}

	void SceneRendererPanel::SetDebugViewCallbacks(std::function<void()> onResetDebugViews, std::function<void()> onDebugViewsChanged)
	{
		m_OnResetDebugViews = std::move(onResetDebugViews);
		m_OnDebugViewsChanged = std::move(onDebugViewsChanged);
	}

	void SceneRendererPanel::SetDebugViewsRuntimeSuspended(bool suspended)
	{
		m_DebugViewsRuntimeSuspended = suspended;
	}

	void SceneRendererPanel::ApplyProjectSettingsToContext()
	{
		Ref<Project> project = Project::GetActive();
		if (!m_Context || !project)
			return;

		m_Context->ApplyProjectSettings(project->GetConfig().SceneRenderer);
	}

	void SceneRendererPanel::SyncProjectSettingsFromContext()
	{
		Ref<Project> project = Project::GetActive();
		if (!m_Context || !project)
			return;

		m_Context->WriteProjectSettings(project->GetConfig().SceneRenderer);
		m_ProjectRendererSettingsDirty = true;
	}

	bool SceneRendererPanel::SaveProjectRendererSettings()
	{
		Ref<Project> project = Project::GetActive();
		if (!m_Context || !project || project->GetProjectFilePath().empty())
			return false;

		SyncProjectSettingsFromContext();
		if (!Project::SaveActive(project->GetProjectFilePath()))
		{
			m_SaveError = "Could not save " + Utils::PathToUTF8(project->GetProjectFilePath()) +
						  ". Check the destination and try Save again. Your settings remain in memory.";
			return false;
		}

		m_ProjectRendererSettingsDirty = false;
		m_SaveError.clear();
		return true;
	}

	void SceneRendererPanel::OnImGuiRender(bool& isOpen)
	{
		ImGui::SetNextWindowSize({680.0f, 720.0f}, ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Scene Renderer", &isOpen))
		{
			m_IsFocused = false;
			ImGui::End();
			return;
		}
		m_IsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		if (!m_Context)
		{
			ImGuiEx::PanelEmptyState("renderer_empty", "No renderer",
									 "Open a project to inspect its scene renderer.");
			ImGui::End();
			return;
		}

		auto& options = m_Context->GetOptions();
		auto& bloom = m_Context->GetBloomSettings();
		auto& dof = m_Context->GetDOFSettings();
		auto& ssr = m_Context->GetSSROptions();
		bool projectSettingsChanged = false;
		bool screenSpaceResourcesChanged = false;

		const Ref<Project> project = Project::GetActive();
		const bool canSave = project && !project->GetProjectFilePath().empty();
		const ImGuiEx::PanelAction actions[] = {
			{0, "Save Renderer Settings", LUX_ICON_FLOPPY_O, nullptr, canSave, false, true},
			{1, "Reset Debug Views", nullptr, nullptr, !m_DebugViewsRuntimeSuspended, false, false, true}};
		const int action = ImGuiEx::PanelActionBar("renderer_actions", actions);
		if (action == 0)
			SaveProjectRendererSettings();
		else if (action == 1)
		{
			if (m_OnResetDebugViews)
				m_OnResetDebugViews();
			else
				ResetDebugViews(options);
		}
		if (m_ProjectRendererSettingsDirty)
			ImGuiEx::PanelMessage("Unsaved renderer settings", "Save to keep these settings in the project.");
		if (!m_SaveError.empty())
			ImGuiEx::PanelMessage("Renderer settings were not saved", m_SaveError.c_str(), true);

		const char* qualityLabels[] = {"Low", "Medium", "High", "Ultra", "Cinematic", "Custom"};
		const int currentQuality = std::clamp(static_cast<int>(options.Quality), 0, 5);
		ImGui::SetNextItemWidth(-1.0f);
		const std::string qualityPreview = std::format("Quality: {}", qualityLabels[currentQuality]);
		if (ImGui::BeginCombo("##renderer_quality", qualityPreview.c_str()))
		{
			for (int i = 0; i < 5; ++i)
			{
				if (ImGui::Selectable(qualityLabels[i], currentQuality == i) && currentQuality != i)
				{
					m_Context->SetQualityPreset(static_cast<QualityPreset>(i));
					projectSettingsChanged = true;
				}
			}
			ImGui::EndCombo();
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Overall renderer quality. Individual categories are below.");
		ImGui::SetNextItemWidth(-1.0f);
		ImGuiEx::Widgets::SearchWidget<sizeof(m_SearchBuffer)>(m_SearchBuffer, "Search settings...");
		const std::string queryLower = Utils::String::ToLowerCopy(m_SearchBuffer);
		const bool searching = !queryLower.empty();

		if (!ImGui::BeginChild("renderer_content", {0.0f, 0.0f}))
		{
			ImGui::EndChild();
			if (projectSettingsChanged)
				SyncProjectSettingsFromContext();
			ImGui::End();
			return;
		}
		ImGuiEx::PanelMessage("Renderer",
							  std::format("Ready: {} | View: {}x{} | Output: {}x{} | Scale: {:.0f}%",
										  m_Context->IsReady() ? "Yes" : "No", m_Context->GetViewportWidth(),
										  m_Context->GetViewportHeight(), m_Context->GetOutputViewportWidth(),
										  m_Context->GetOutputViewportHeight(),
										  m_Context->GetRenderResolutionScale() * 100.0f)
								  .c_str());
		if (!searching && ImGuiEx::BoundedCollapsingHeader("Quality per category"))
		{
			const char* tierLabels[] = {"Low", "Medium", "High", "Ultra", "Cinematic", "Custom"};
			ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
			for (uint32_t i = 0; i < static_cast<uint32_t>(QualityCategory::COUNT); ++i)
			{
				const QualityCategory category = static_cast<QualityCategory>(i);
				int tier = static_cast<int>(m_Context->GetCategoryQuality(category));
				if (!DrawComboProperty(QualityCategoryToDisplayString(category), tier, tierLabels,
									   IM_ARRAYSIZE(tierLabels)))
					continue;
				if (tier == static_cast<int>(QualityPreset::Custom))
					m_Context->MarkCategoryCustom(category);
				else
				{
					m_Context->SetCategoryQuality(category, static_cast<QualityPreset>(tier));
					m_Context->RefreshRenderResolutionScale();
					m_Context->RefreshScreenSpaceEffectResources();
				}
				projectSettingsChanged = true;
			}
			ImGuiEx::EndPropertyGrid();
		}

		auto match = [&](const char* label) { return MatchesSearch(queryLower, label); };
		// Categories whose tier-owned values were edited by hand this frame (see SceneRenderer::Apply*Quality).
		bool editedCategories[static_cast<uint32_t>(QualityCategory::COUNT)] = {};
		auto owned = [&](QualityCategory category, bool changed)
		{
			if (changed)
				editedCategories[static_cast<uint32_t>(category)] = true;
			return changed;
		};
		auto toggle = [&](const char* label, bool& value) -> bool
		{
			if (!match(label))
				return false;
			return ToggleRow(label, value);
		};

		// ---- Debug Views ---------------------------------------------------------------------
		if (CardHasMatch(queryLower, "Debug Views", { "Show Grid", "Show Selected Wireframe", "Show Physics Colliders", "Show Shadow Cascades", "Show Cascade Frustums", "Show Light Complexity", "Show Material Complexity" }))
		{
			if (BeginSettingsCard("card_debug", LUX_ICON_EYE, "Debug Views", m_CardDebugViews, searching))
			{
				if (!searching)
					ImGuiEx::PanelMessage("Editor-only debug views",
										  m_DebugViewsRuntimeSuspended
											  ? "Suspended while Play is running; restored on Stop."
											  : "Reset is available in More.");

				if (m_DebugViewsRuntimeSuspended)
					ImGui::BeginDisabled();
				bool debugViewsChanged = false;
				ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
				debugViewsChanged |= toggle("Show Grid", options.ShowGrid);
				debugViewsChanged |= toggle("Show Selected Wireframe", options.ShowSelectedInWireframe);
				debugViewsChanged |= toggle("Show Physics Colliders", options.ShowPhysicsColliders);
				debugViewsChanged |= toggle("Show Shadow Cascades", options.ShowShadowCascades);
				debugViewsChanged |= toggle("Show Cascade Frustums", options.ShowCascadeFrustums);
				debugViewsChanged |= toggle("Show Light Complexity", options.ShowLightComplexity);
				debugViewsChanged |= toggle("Show Material Complexity", options.ShowMaterialComplexity);
				ImGuiEx::EndPropertyGrid();
				if (m_DebugViewsRuntimeSuspended)
					ImGui::EndDisabled();

				if (debugViewsChanged && m_OnDebugViewsChanged)
					m_OnDebugViewsChanged();
			}
			EndSettingsCard();
		}

		// ---- Quality & Performance -----------------------------------------------------------
		if (CardHasMatch(queryLower, "Quality Performance", { "Frustum Culling", "Occlusion Culling", "Occlusion Depth Bias", "Occlusion Bounds Scale", "GPU Driven Indirect", "Mesh LODs", "LOD Distance Scale", "Mesh Shaders", "Variable Rate Shading", "Render Scale", "Fixed Render Width", "Fixed Render Height", "Dynamic Min Scale", "Dynamic Max Scale", "Target GPU ms", "Texture Mip Bias", "Distance Mip Bias", "Mip Bias Start", "Mip Bias End", "Mip Bias Max" }))
		{
			if (BeginSettingsCard("card_quality", LUX_ICON_TACHOMETER, "Quality & Performance", m_CardQuality, searching))
			{
				ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
				projectSettingsChanged |= toggle("Frustum Culling", options.EnableFrustumCulling);
				projectSettingsChanged |= toggle("Occlusion Culling", options.EnableOcclusionCulling);
				if (options.EnableOcclusionCulling)
				{
					if (match("Occlusion Depth Bias"))
						projectSettingsChanged |= ImGuiEx::Property("Occlusion Depth Bias", options.OcclusionDepthBias, 0.0005f, 0.0f, 0.1f);
					if (match("Occlusion Bounds Scale"))
						projectSettingsChanged |= ImGuiEx::Property("Occlusion Bounds Scale", options.OcclusionBoundsScale, 0.01f, 1.0f, 2.0f);
				}
				projectSettingsChanged |= toggle("GPU Driven Indirect", options.EnableGPUDrivenRendering);
				projectSettingsChanged |= toggle("Mesh LODs", options.EnableMeshLODs);
				if (options.EnableMeshLODs && match("LOD Distance Scale"))
				{
					if (ImGuiEx::Property("LOD Distance Scale", options.MeshLODDistanceScale, 0.05f, 0.25f, 4.0f))
					{
						options.MeshLODDistanceScale = std::clamp(options.MeshLODDistanceScale, 0.25f, 4.0f);
						projectSettingsChanged = true;
					}
				}
				if (Renderer::SupportsMeshShaders())
				{
					projectSettingsChanged |= toggle("Mesh Shaders (Experimental)", options.EnableMeshShaders);
				}
				else if (match("Mesh Shaders (Unsupported)"))
				{
					ImGui::BeginDisabled();
					bool unsupported = false;
					ToggleRow("Mesh Shaders (Unsupported)", unsupported);
					ImGui::EndDisabled();
				}
				if (Renderer::SupportsVariableRateShading())
				{
					projectSettingsChanged |= toggle("Variable Rate Shading", options.EnableVariableRateShading);
				}
				else if (match("Variable Rate Shading (Unsupported)"))
				{
					ImGui::BeginDisabled();
					bool unsupported = false;
					ToggleRow("Variable Rate Shading (Unsupported)", unsupported);
					ImGui::EndDisabled();
				}
				const char* renderScaleLabels[] = { "100%", "75%", "50%", "Dynamic", "Fixed Resolution" };
				int renderScaleMode = static_cast<int>(options.ResolutionScaleMode);
				if (match("Render Scale") && DrawComboProperty("Render Scale", renderScaleMode, renderScaleLabels, IM_ARRAYSIZE(renderScaleLabels)))
				{
					options.ResolutionScaleMode = static_cast<SceneRendererOptions::RenderResolutionScaleMode>(renderScaleMode);
					owned(QualityCategory::ResolutionScale, true);
					m_Context->RefreshRenderResolutionScale();
					projectSettingsChanged = true;
				}
				if (options.ResolutionScaleMode == SceneRendererOptions::RenderResolutionScaleMode::FixedResolution)
				{
					int32_t fixedWidth = (int32_t)options.FixedRenderWidth;
					int32_t fixedHeight = (int32_t)options.FixedRenderHeight;
					bool fixedResolutionChanged = false;
					if (match("Fixed Render Width"))
						fixedResolutionChanged |= ImGuiEx::Property("Fixed Render Width", fixedWidth, 64, 16384);
					if (match("Fixed Render Height"))
						fixedResolutionChanged |= ImGuiEx::Property("Fixed Render Height", fixedHeight, 64, 16384);
					if (fixedResolutionChanged)
					{
						options.FixedRenderWidth = (uint32_t)std::clamp(fixedWidth, 64, 16384);
						options.FixedRenderHeight = (uint32_t)std::clamp(fixedHeight, 64, 16384);
						m_Context->RefreshRenderResolutionScale();
						projectSettingsChanged = true;
					}
					if (!searching)
					{
						// Break out of the property grid to draw the full-width preset buttons, then
						// reopen it for the remaining rows. End/Begin must stay balanced: a bare
						// Columns(1) + second BeginPropertyGrid would leak a pushed ID and two style
						// vars every frame this mode is active.
						ImGuiEx::EndPropertyGrid();
						const ImGuiEx::PanelAction presets[] = {
							{0, "1920x1080"}, {1, "2560x1440"}, {2, "1280x720"}};
						const int preset = ImGuiEx::PanelActionBar("fixed_resolution", presets);
						if (preset >= 0 && preset < 3)
						{
							const uint32_t widths[] = {1920, 2560, 1280};
							const uint32_t heights[] = {1080, 1440, 720};
							options.FixedRenderWidth = widths[preset];
							options.FixedRenderHeight = heights[preset];
							m_Context->RefreshRenderResolutionScale();
							projectSettingsChanged = true;
						}
						ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f,
												   ImGui::GetFontSize() * 28.0f);
					}
				}
				if (options.ResolutionScaleMode == SceneRendererOptions::RenderResolutionScaleMode::Dynamic)
				{
					bool dynamicScaleChanged = false;
					if (match("Dynamic Min Scale"))
						dynamicScaleChanged |= ImGuiEx::Property("Dynamic Min Scale", options.DynamicResolutionMinScale, 0.01f, 0.25f, 1.0f);
					if (match("Dynamic Max Scale"))
						dynamicScaleChanged |= ImGuiEx::Property("Dynamic Max Scale", options.DynamicResolutionMaxScale, 0.01f, 0.25f, 1.0f);
					if (match("Target GPU ms"))
						dynamicScaleChanged |= ImGuiEx::Property("Target GPU ms", options.DynamicResolutionTargetGPUTime, 0.1f, 1.0f, 100.0f);
					if (owned(QualityCategory::ResolutionScale, dynamicScaleChanged))
					{
						options.DynamicResolutionMinScale = std::clamp(options.DynamicResolutionMinScale, 0.25f, 1.0f);
						options.DynamicResolutionMaxScale = std::clamp(options.DynamicResolutionMaxScale, options.DynamicResolutionMinScale, 1.0f);
						options.DynamicResolutionScale = std::clamp(options.DynamicResolutionScale, options.DynamicResolutionMinScale, options.DynamicResolutionMaxScale);
						m_Context->RefreshRenderResolutionScale();
						projectSettingsChanged = true;
					}
				}
				if (match("Texture Mip Bias"))
					projectSettingsChanged |= owned(QualityCategory::Textures, ImGuiEx::Property("Texture Mip Bias", options.TextureMipBias, 0.05f, -4.0f, 8.0f));
				projectSettingsChanged |= owned(QualityCategory::Textures, toggle("Distance Mip Bias", options.EnableDistanceMipBias));
				if (options.EnableDistanceMipBias)
				{
					bool mipBiasChanged = false;
					if (match("Mip Bias Start"))
						mipBiasChanged |= ImGuiEx::Property("Mip Bias Start", options.DistanceMipBiasStart, 1.0f, 0.0f, 10000.0f);
					if (match("Mip Bias End"))
						mipBiasChanged |= ImGuiEx::Property("Mip Bias End", options.DistanceMipBiasEnd, 1.0f, 1.0f, 10000.0f);
					if (match("Mip Bias Max"))
						mipBiasChanged |= ImGuiEx::Property("Mip Bias Max", options.DistanceMipBiasMax, 0.05f, 0.0f, 8.0f);
					if (owned(QualityCategory::Textures, mipBiasChanged))
					{
						options.DistanceMipBiasStart = std::max(0.0f, options.DistanceMipBiasStart);
						options.DistanceMipBiasEnd = std::max(options.DistanceMipBiasStart + 1.0f, options.DistanceMipBiasEnd);
						options.DistanceMipBiasMax = std::clamp(options.DistanceMipBiasMax, 0.0f, 8.0f);
						projectSettingsChanged = true;
					}
				}
				ImGuiEx::EndPropertyGrid();
			}
			EndSettingsCard();
		}

		// ---- Screen-Space Effects ------------------------------------------------------------
		if (CardHasMatch(queryLower, "Screen Space Effects", { "GTAO", "GTAO Bent Normals", "GTAO Visibility Bitmask", "GTAO Thickness", "GTAO Noise Passes", "GTAO Resolution", "GTAO Denoise Passes", "SSR", "SSR Quality", "SMAA", "SMAA Threshold", "SMAA Local Contrast", "Jump Flood Outline", "Async Compute" }))
		{
			if (BeginSettingsCard("card_ss", LUX_ICON_MAGIC, "Screen-Space Effects", m_CardScreenSpace, searching))
			{
				ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
				bool gtaoSettingsChanged = false;
				gtaoSettingsChanged |= owned(QualityCategory::AmbientOcclusion, toggle("GTAO", options.EnableGTAO));
				gtaoSettingsChanged |= owned(QualityCategory::AmbientOcclusion, toggle("GTAO Bent Normals", options.GTAOBentNormals));
				projectSettingsChanged |= toggle("GTAO Visibility Bitmask", options.GTAOVisibilityBitmask);
				if (options.GTAOVisibilityBitmask && match("GTAO Thickness"))
					projectSettingsChanged |= ImGuiEx::Property("GTAO Thickness", options.GTAOThickness, 0.01f, 0.01f, 10.0f,
						"Assumed depth of every occluder, in metres. Thin values let light past thin objects.");
				if (match("GTAO Noise Passes"))
					projectSettingsChanged |= owned(QualityCategory::AmbientOcclusion, ImGuiEx::Property("GTAO Noise Passes", options.GTAONoisePasses, 1u, 3u));
				if (match("GTAO Resolution") && owned(QualityCategory::AmbientOcclusion, DrawEffectScaleProperty("GTAO Resolution", options.GTAOResolutionScale)))
				{
					screenSpaceResourcesChanged = true;
					projectSettingsChanged = true;
				}
				projectSettingsChanged |= gtaoSettingsChanged;
				if (match("GTAO Denoise Passes"))
					projectSettingsChanged |= owned(QualityCategory::AmbientOcclusion, ImGuiEx::Property("GTAO Denoise Passes", options.GTAODenoisePasses, 0, 8));
				gtaoSettingsChanged |= owned(QualityCategory::Reflections, toggle("SSR", options.EnableSSR));
				const char* ssrQualityLabels[] = { "Full", "Half + Bilateral Upscale", "Quarter Debug Only" };
				int ssrQuality = static_cast<int>(options.SSRQuality);
				if (match("SSR Quality") && DrawComboProperty("SSR Quality", ssrQuality, ssrQualityLabels, IM_ARRAYSIZE(ssrQualityLabels)))
				{
					options.SSRQuality = static_cast<SceneRendererOptions::SSRQualityPreset>(std::clamp(ssrQuality, 0, 2));
					owned(QualityCategory::Reflections, true);
					options.SSRResolutionScale = ResolutionScaleFromSSRQuality(options.SSRQuality);
					ssr.HalfRes = options.SSRResolutionScale != SceneRendererOptions::EffectResolutionScale::Full;
					ssr.ResolutionScale = static_cast<uint32_t>(options.SSRResolutionScale);
					screenSpaceResourcesChanged = true;
					projectSettingsChanged = true;
				}
				if (options.SSRQuality == SceneRendererOptions::SSRQualityPreset::QuarterDebug && !searching)
					ImGuiEx::PanelMessage("Quarter SSR",
										  "Debug-only; use Full or Half + Bilateral for normal rendering.");
				projectSettingsChanged |= owned(QualityCategory::AntiAliasing, toggle("SMAA", options.EnableSMAA));
				if (options.EnableSMAA)
				{
					if (match("SMAA Threshold"))
						projectSettingsChanged |= owned(QualityCategory::AntiAliasing, ImGuiEx::Property("SMAA Threshold", options.SMAAThreshold, 0.005f, 0.01f, 0.5f));
					if (match("SMAA Local Contrast"))
						projectSettingsChanged |= owned(QualityCategory::AntiAliasing, ImGuiEx::Property("SMAA Local Contrast", options.SMAALocalContrastAdaptationFactor, 0.05f, 1.0f, 8.0f));
					if (!m_Context->IsSMAAReady() && !searching)
						ImGuiEx::PanelMessage("SMAA unavailable",
											  "Lookup textures missing - see Core/vendor/smaa.", true);
				}
				projectSettingsChanged |= gtaoSettingsChanged;
				if (gtaoSettingsChanged)
					m_Context->UpdateGTAOData();
				projectSettingsChanged |= toggle("Jump Flood Outline", options.EnableJumpFlood);
				projectSettingsChanged |= toggle("Async Compute (experimental)", options.EnableAsyncCompute);
				ImGuiEx::EndPropertyGrid();
			}
			EndSettingsCard();
		}

		// ---- Shadows -------------------------------------------------------------------------
		if (CardHasMatch(queryLower, "Shadows", { "Soft Shadows", "Shadow Culling", "Max Distance", "Distance Fade", "Active Cascades", "Split Lambda", "Near Offset", "Far Offset", "Cascade Fade", "Filter", "PCSS Cascades", "PCSS Blocker Samples", "PCSS Filter Samples", "PCF Radius", "Spot PCF Radius", "Resolution Limit" }))
		{
			if (BeginSettingsCard("card_shadows", LUX_ICON_SUN_O, "Shadows", m_CardShadows, searching))
			{
				ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
				projectSettingsChanged |= owned(QualityCategory::Shadows, toggle("Soft Shadows", options.SoftShadows));
				projectSettingsChanged |= toggle("Shadow Culling", options.EnableShadowCulling);
				if (match("Max Distance"))
					projectSettingsChanged |= owned(QualityCategory::Shadows, ImGuiEx::Property("Max Distance", options.MaxShadowDistance, 1.0f, 1.0f, 1000.0f));
				if (match("Distance Fade"))
					projectSettingsChanged |= owned(QualityCategory::Shadows, ImGuiEx::Property("Distance Fade", options.ShadowFade, 0.25f, 0.01f, 250.0f));
				if (match("Active Cascades"))
					projectSettingsChanged |= owned(QualityCategory::Shadows, ImGuiEx::Property("Active Cascades", options.ActiveShadowCascadeCount, 1u, SceneRenderer::ShadowCascadeCount));
				if (match("Split Lambda"))
					projectSettingsChanged |= owned(QualityCategory::Shadows, ImGuiEx::Property("Split Lambda", options.ShadowCascadeSplitLambda, 0.01f, 0.0f, 1.0f));
				if (match("Near Offset"))
					projectSettingsChanged |= ImGuiEx::Property("Near Offset", options.ShadowCascadeNearPlaneOffset, 0.1f, 0.0f, 200.0f);
				if (match("Far Offset"))
					projectSettingsChanged |= ImGuiEx::Property("Far Offset", options.ShadowCascadeFarPlaneOffset, 0.5f, 0.0f, 500.0f);
				if (match("Cascade Fade"))
					projectSettingsChanged |= ImGuiEx::Property("Cascade Fade", options.ShadowCascadeTransitionFade, 0.05f, 0.0f, 25.0f);
				const char* shadowFilterLabels[] = { "Tuned PCF", "PCSS", "Hybrid" };
				int shadowFilter = static_cast<int>(options.ShadowFilter);
				if (match("Filter") && DrawComboProperty("Filter", shadowFilter, shadowFilterLabels, IM_ARRAYSIZE(shadowFilterLabels)))
				{
					options.ShadowFilter = static_cast<SceneRendererOptions::ShadowFilterMode>(std::clamp(shadowFilter, 0, 2));
					owned(QualityCategory::Shadows, true);
					projectSettingsChanged = true;
				}
				if (match("PCSS Cascades"))
					projectSettingsChanged |= owned(QualityCategory::Shadows, ImGuiEx::Property("PCSS Cascades", options.DirectionalPCSSCascadeCount, 0u, options.ActiveShadowCascadeCount));
				if (match("PCSS Blocker Samples"))
					projectSettingsChanged |= owned(QualityCategory::Shadows, ImGuiEx::Property("PCSS Blocker Samples", options.ShadowBlockerSamples, 1u, SceneRenderer::ShadowKernelSize));
				if (match("PCSS Filter Samples"))
					projectSettingsChanged |= owned(QualityCategory::Shadows, ImGuiEx::Property("PCSS Filter Samples", options.ShadowFilterSamples, 1u, SceneRenderer::ShadowKernelSize));
				if (match("PCF Radius"))
					projectSettingsChanged |= owned(QualityCategory::Shadows, ImGuiEx::Property("PCF Radius", options.ShadowPCFRadiusTexels, 0.05f, 0.25f, 8.0f));
				if (match("Spot PCF Radius"))
					projectSettingsChanged |= owned(QualityCategory::Shadows, ImGuiEx::Property("Spot PCF Radius", options.SpotShadowPCFRadiusTexels, 0.05f, 0.25f, 8.0f));
				const char* shadowResolutionLabels[] = { "1K", "2K", "4K", "8K" };
				int shadowResolution = static_cast<int>(options.ShadowResolution);
				if (match("Resolution Limit") && DrawComboProperty("Resolution Limit", shadowResolution, shadowResolutionLabels, IM_ARRAYSIZE(shadowResolutionLabels)))
				{
					options.ShadowResolution = static_cast<SceneRendererOptions::ShadowResolutionTier>(std::clamp(shadowResolution, 0, 3));
					owned(QualityCategory::Shadows, true);
					projectSettingsChanged = true;
				}
				ImGuiEx::EndPropertyGrid();
			}
			EndSettingsCard();
		}

		// ---- Post FX -------------------------------------------------------------------------
		if (CardHasMatch(queryLower, "Post FX", { "Bloom", "Bloom Resolution", "Bloom Threshold", "Bloom Knee", "Bloom Intensity", "Bloom Dirt Intensity", "Bloom Streaks", "Bloom Streak Length", "Bloom Streak Intensity", "Bloom Streak Rotation", "Bloom Halo Intensity", "Bloom Chromatic Spread", "DOF", "DOF Resolution", "DOF Focus Distance", "DOF Blur Size", "SSR Max Steps", "SSR Brightness", "SSR Depth Tolerance" }))
		{
			if (BeginSettingsCard("card_postfx", LUX_ICON_FILM, "Post FX", m_CardPostFX, searching))
			{
				ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
				projectSettingsChanged |= owned(QualityCategory::PostProcessing, toggle("Bloom", bloom.Enabled));
				if (match("Bloom Resolution") && owned(QualityCategory::PostProcessing, DrawEffectScaleProperty("Bloom Resolution", bloom.ResolutionScale)))
				{
					screenSpaceResourcesChanged = true;
					projectSettingsChanged = true;
				}
				if (match("Bloom Threshold"))
					projectSettingsChanged |= owned(QualityCategory::PostProcessing, ImGuiEx::Property("Bloom Threshold", bloom.Threshold, 0.01f, 0.0f, 25.0f));
				if (match("Bloom Knee"))
					projectSettingsChanged |= owned(QualityCategory::PostProcessing, ImGuiEx::Property("Bloom Knee", bloom.Knee, 0.01f, 0.0f, 1.0f));
				if (match("Bloom Intensity"))
					projectSettingsChanged |= owned(QualityCategory::PostProcessing, ImGuiEx::Property("Bloom Intensity", bloom.Intensity, 0.01f, 0.0f, 10.0f));
				if (match("Bloom Dirt Intensity"))
					projectSettingsChanged |= owned(QualityCategory::PostProcessing, ImGuiEx::Property("Bloom Dirt Intensity", bloom.DirtIntensity, 0.01f, 0.0f, 10.0f));
				if (match("Bloom Streaks"))
					projectSettingsChanged |= owned(QualityCategory::PostProcessing, ImGuiEx::Property("Bloom Streaks", bloom.StreakCount, 0u, BloomSettings::MaxStreakCount,
						"Glare streak lines through each bright point; each line makes two rays. 0 = none."));
				if (match("Bloom Streak Length"))
					projectSettingsChanged |= owned(QualityCategory::PostProcessing, ImGuiEx::Property("Bloom Streak Length", bloom.StreakLength, 0.01f, 0.0f, 4.0f));
				if (match("Bloom Streak Intensity"))
					projectSettingsChanged |= owned(QualityCategory::PostProcessing, ImGuiEx::Property("Bloom Streak Intensity", bloom.StreakIntensity, 0.01f, 0.0f, 10.0f));
				if (match("Bloom Streak Rotation"))
					projectSettingsChanged |= owned(QualityCategory::PostProcessing, ImGuiEx::Property("Bloom Streak Rotation", bloom.StreakRotation, 0.5f, -180.0f, 180.0f, "Degrees."));
				if (match("Bloom Halo Intensity"))
					projectSettingsChanged |= owned(QualityCategory::PostProcessing, ImGuiEx::Property("Bloom Halo Intensity", bloom.HaloIntensity, 0.01f, 0.0f, 10.0f));
				if (match("Bloom Chromatic Spread"))
					projectSettingsChanged |= owned(QualityCategory::PostProcessing, ImGuiEx::Property("Bloom Chromatic Spread", bloom.ChromaticSpread, 0.005f, 0.0f, 0.5f,
						"Red glare spreads this much wider than green, blue this much narrower."));
				projectSettingsChanged |= toggle("DOF", dof.Enabled);
				if (match("DOF Resolution") && owned(QualityCategory::PostProcessing, DrawEffectScaleProperty("DOF Resolution", dof.ResolutionScale)))
				{
					screenSpaceResourcesChanged = true;
					projectSettingsChanged = true;
				}
				if (match("DOF Focus Distance"))
					projectSettingsChanged |= ImGuiEx::Property("DOF Focus Distance", dof.FocusDistance, 0.1f, 0.0f, 1000.0f);
				if (match("DOF Blur Size"))
					projectSettingsChanged |= ImGuiEx::Property("DOF Blur Size", dof.BlurSize, 0.05f, 0.0f, 20.0f);
				if (match("SSR Max Steps"))
				{
					int32_t ssrMaxSteps = ssr.MaxSteps;
					if (ImGuiEx::Property("SSR Max Steps", ssrMaxSteps, 1, 256))
					{
						ssr.MaxSteps = ssrMaxSteps;
						owned(QualityCategory::Reflections, true);
						projectSettingsChanged = true;
					}
				}
				if (match("SSR Brightness"))
					projectSettingsChanged |= owned(QualityCategory::Reflections, ImGuiEx::Property("SSR Brightness", ssr.Brightness, 0.01f, 0.0f, 5.0f));
				if (match("SSR Depth Tolerance"))
					projectSettingsChanged |= owned(QualityCategory::Reflections, ImGuiEx::Property("SSR Depth Tolerance", ssr.DepthTolerance, 0.01f, 0.0f, 5.0f));
				ImGuiEx::EndPropertyGrid();
			}
			EndSettingsCard();
		}

		// ---- Color Grading (scene post-processing) -------------------------------------------
		if (m_Scene && CardHasMatch(queryLower, "Color Grading Exposure Tonemap", { "Mode", "Aperture (f-stop)", "Shutter Speed (s)", "ISO", "EV100", "Exposure Compensation", "Min EV100", "Max EV100", "Adapt Up (EV/s)", "Adapt Down (EV/s)", "Tonemap", "Gamma", "Color Filter", "Saturation", "Contrast", "White Temperature", "White Tint", "Lift", "Gamma (grade)", "Gain" }))
		{
			if (BeginSettingsCard("card_grade", LUX_ICON_PAINT_BRUSH, "Color Grading", m_CardColorGrading, searching))
			{
				PostProcessSettings& post = m_Scene->GetPostProcessSettings();

				if (!searching)
					ImGui::TextDisabled("Exposure");
				ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
				static const char* s_ExposureModes[] = { "Manual", "Manual EV100", "Camera (physical)", "Automatic" };
				if (match("Mode"))
					ImGuiEx::PropertyDropdown("Mode", s_ExposureModes, 4, post.ExposureControl);
				if (match("Aperture (f-stop)"))
					ImGuiEx::Property("Aperture (f-stop)", post.Aperture, 0.1f, 0.5f, 64.0f);
				if (match("Shutter Speed (s)"))
					ImGuiEx::Property("Shutter Speed (s)", post.ShutterSpeed, 0.0001f, 0.0001f, 10.0f);
				if (match("ISO"))
					ImGuiEx::Property("ISO", post.ISO, 1.0f, 1.0f, 25600.0f);
				if (match("EV100"))
					ImGuiEx::Property("EV100", post.ExposureEV100, 0.05f, -10.0f, 20.0f);
				if (match("Exposure Compensation"))
					ImGuiEx::Property("Exposure Compensation", post.ExposureCompensation, 0.05f, -10.0f, 10.0f);
				ImGuiEx::EndPropertyGrid();

				if (!searching)
				{
					ImGui::Spacing();
					ImGui::TextDisabled("Auto Exposure");
				}
				ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
				if (match("Min EV100"))
					ImGuiEx::Property("Min EV100", post.AutoMinEV100, 0.05f, -16.0f, 20.0f);
				if (match("Max EV100"))
					ImGuiEx::Property("Max EV100", post.AutoMaxEV100, 0.05f, -16.0f, 20.0f);
				if (match("Adapt Up (EV/s)"))
					ImGuiEx::Property("Adapt Up (EV/s)", post.AutoAdaptationSpeedUp, 0.05f, 0.0f, 20.0f);
				if (match("Adapt Down (EV/s)"))
					ImGuiEx::Property("Adapt Down (EV/s)", post.AutoAdaptationSpeedDown, 0.05f, 0.0f, 20.0f);
				ImGuiEx::EndPropertyGrid();

				if (!searching)
				{
					ImGui::Spacing();
					ImGui::TextDisabled("Tonemapping");
				}
				ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
				static const char* s_TonemapOperators[] = { "ACES", "AgX", "None" };
				if (match("Tonemap"))
					ImGuiEx::PropertyDropdown("Tonemap", s_TonemapOperators, 3, post.Tonemap);
				if (match("Gamma"))
					ImGuiEx::Property("Gamma", post.Gamma, 0.01f, 1.0f, 4.0f);
				ImGuiEx::EndPropertyGrid();

				if (!searching)
				{
					ImGui::Spacing();
					ImGui::TextDisabled("Color Grading");
				}
				ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
				if (match("Color Filter"))
					ImGuiEx::PropertyColor("Color Filter", post.ColorFilter);
				if (match("Saturation"))
					ImGuiEx::Property("Saturation", post.Saturation, 0.01f, 0.0f, 2.0f);
				if (match("Contrast"))
					ImGuiEx::Property("Contrast", post.Contrast, 0.01f, 0.0f, 2.0f);
				if (match("White Temperature"))
					ImGuiEx::Property("White Temperature", post.WhiteTemperature, 0.01f, -1.0f, 1.0f);
				if (match("White Tint"))
					ImGuiEx::Property("White Tint", post.WhiteTint, 0.01f, -1.0f, 1.0f);
				if (match("Lift"))
					ImGuiEx::Property("Lift", post.Lift, 0.01f, -1.0f, 1.0f);
				if (match("Gamma (grade)"))
					ImGuiEx::Property("Gamma (grade)", post.GradeGamma, 0.01f, 0.0f, 4.0f);
				if (match("Gain"))
					ImGuiEx::Property("Gain", post.Gain, 0.01f, 0.0f, 4.0f);
				ImGuiEx::EndPropertyGrid();
			}
			EndSettingsCard();
		}

		for (uint32_t i = 0; i < static_cast<uint32_t>(QualityCategory::COUNT); i++)
		{
			if (editedCategories[i])
				m_Context->MarkCategoryCustom(static_cast<QualityCategory>(i));
		}

		if (screenSpaceResourcesChanged)
			m_Context->RefreshScreenSpaceEffectResources();

		if (projectSettingsChanged)
			SyncProjectSettingsFromContext();

		ImGui::EndChild();
		ImGui::End();
	}

	void SceneRendererPanel::SaveState(Yaml::Writer& out) const
	{
		out << Yaml::Key << "DebugViewsOpen" << Yaml::Value << m_CardDebugViews;
		out << Yaml::Key << "QualityOpen" << Yaml::Value << m_CardQuality;
		out << Yaml::Key << "ScreenSpaceOpen" << Yaml::Value << m_CardScreenSpace;
		out << Yaml::Key << "ShadowsOpen" << Yaml::Value << m_CardShadows;
		out << Yaml::Key << "PostFXOpen" << Yaml::Value << m_CardPostFX;
		out << Yaml::Key << "ColorGradingOpen" << Yaml::Value << m_CardColorGrading;
	}

	void SceneRendererPanel::LoadState(const Yaml::Node& state)
	{
		m_CardDebugViews = state["DebugViewsOpen"].as<bool>(m_CardDebugViews);
		m_CardQuality = state["QualityOpen"].as<bool>(m_CardQuality);
		m_CardScreenSpace = state["ScreenSpaceOpen"].as<bool>(m_CardScreenSpace);
		m_CardShadows = state["ShadowsOpen"].as<bool>(m_CardShadows);
		m_CardPostFX = state["PostFXOpen"].as<bool>(m_CardPostFX);
		m_CardColorGrading = state["ColorGradingOpen"].as<bool>(m_CardColorGrading);
	}


}
