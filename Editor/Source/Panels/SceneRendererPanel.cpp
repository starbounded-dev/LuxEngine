#include "lpch.h"
#include "SceneRendererPanel.h"

#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/ImGuiWidgets.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Utilities/StringUtils.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <format>
#include <string>
#include <utility>

namespace Lux {

	namespace {

		void DrawStat(const char* label, const char* value)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextDisabled("%s", label);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(value);
		}

		void DrawStat(const char* label, uint32_t value)
		{
			DrawStat(label, std::to_string(value).c_str());
		}

		void DrawStat(const char* label, uint64_t value)
		{
			DrawStat(label, std::to_string(value).c_str());
		}

		void DrawStat(const char* label, float value, const char* suffix = "")
		{
			char buffer[64];
			std::snprintf(buffer, sizeof(buffer), "%.3f%s", value, suffix);
			DrawStat(label, buffer);
		}

		bool DrawComboProperty(const char* label, int& value, const char* const* options, int optionCount)
		{
			bool modified = false;
			ImGuiEx::ShiftCursor(10.0f, 9.0f);
			ImGui::TextUnformatted(label);
			ImGui::NextColumn();
			ImGuiEx::ShiftCursorY(4.0f);
			ImGui::PushItemWidth(-1);

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

		// A compact readonly "chip": rounded pill with a dim label and a bright value.
		void StatChip(const char* label, const char* value)
		{
			const ImVec2 pad(9.0f, 4.0f);
			const std::string text = std::format("{} {}", label, value);
			const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
			const ImVec2 size(textSize.x + pad.x * 2.0f, textSize.y + pad.y * 2.0f);

			const ImVec2 p0 = ImGui::GetCursorScreenPos();
			const ImVec2 p1(p0.x + size.x, p0.y + size.y);
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(p0, p1, Colors::Theme::backgroundDark, 4.0f);
			dl->AddText(ImVec2(p0.x + pad.x, p0.y + pad.y), Colors::Theme::textDarker, label);
			const float labelW = ImGui::CalcTextSize(label).x;
			dl->AddText(ImVec2(p0.x + pad.x + labelW + 5.0f, p0.y + pad.y), Colors::Theme::text, value);
			ImGui::Dummy(size);
		}

		// Card header + auto-sizing bordered body. Returns whether the body is open (caller renders
		// rows only when true, and always calls EndSettingsCard).
		bool BeginSettingsCard(const char* id, const char* icon, const char* title, bool& open, bool forceOpen)
		{
			if (forceOpen)
				open = true;

			ImGui::PushID(id);
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(Colors::Theme::background));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
			ImGui::BeginChild("##card", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

			// Header row: chevron + accent icon + title, the whole strip toggling the card.
			const float headerHeight = ImGui::GetFrameHeight();
			const ImVec2 headerMin = ImGui::GetCursorScreenPos();
			ImGui::InvisibleButton("##card_header", ImVec2(ImGui::GetContentRegionAvail().x, headerHeight));
			if (ImGui::IsItemClicked() && !forceOpen)
				open = !open;
			const bool hovered = ImGui::IsItemHovered();

			ImDrawList* dl = ImGui::GetWindowDrawList();
			const float cy = headerMin.y + headerHeight * 0.5f;
			ImU32 iconColor = Colors::Theme::accent;
			ImU32 titleColor = hovered ? Colors::Theme::textBrighter : Colors::Theme::text;

			// Chevron.
			const float chevX = headerMin.x + 2.0f;
			if (open)
			{
				dl->AddTriangleFilled(ImVec2(chevX, cy - 3.0f), ImVec2(chevX + 8.0f, cy - 3.0f), ImVec2(chevX + 4.0f, cy + 3.0f), Colors::Theme::textDarker);
			}
			else
			{
				dl->AddTriangleFilled(ImVec2(chevX, cy - 4.0f), ImVec2(chevX, cy + 4.0f), ImVec2(chevX + 6.0f, cy), Colors::Theme::textDarker);
			}

			const float iconX = chevX + 16.0f;
			const ImVec2 iconSize = ImGui::CalcTextSize(icon);
			dl->AddText(ImVec2(iconX, cy - iconSize.y * 0.5f), iconColor, icon);
			dl->AddText(ImVec2(iconX + iconSize.x + 8.0f, cy - ImGui::GetTextLineHeight() * 0.5f), titleColor, title);

			if (open)
			{
				dl->AddLine(ImVec2(headerMin.x, headerMin.y + headerHeight + 2.0f),
					ImVec2(headerMin.x + ImGui::GetContentRegionAvail().x, headerMin.y + headerHeight + 2.0f),
					Colors::Theme::backgroundDark);
				ImGui::Dummy(ImVec2(0.0f, 6.0f));
			}

			return open;
		}

		void EndSettingsCard()
		{
			ImGui::EndChild();
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor();
			ImGui::PopID();
			ImGui::Dummy(ImVec2(0.0f, 6.0f));
		}

		// Modern toggle switch, aligned to the property grid's two columns (label | control), matching
		// DrawComboProperty. Returns true when toggled. Note: unlike ImGuiEx::Property(bool) this does
		// not push an undo entry (fine for renderer settings).
		bool ToggleRow(const char* label, bool& value)
		{
			ImGuiEx::ShiftCursor(10.0f, 6.0f);
			ImGui::TextUnformatted(label);
			ImGui::NextColumn();
			ImGuiEx::ShiftCursorY(3.0f);

			const float height = ImGui::GetFrameHeight() * 0.72f;
			const float width = height * 1.9f;
			const float radius = height * 0.5f;
			const ImVec2 p = ImGui::GetCursorScreenPos();

			ImGui::PushID(label);
			ImGui::InvisibleButton("##toggle", ImVec2(width, height));
			bool changed = false;
			if (ImGui::IsItemClicked())
			{
				value = !value;
				changed = true;
			}
			ImGui::PopID();

			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImU32 trackOn = Colors::Theme::accent;
			const ImU32 trackOff = Colors::Theme::backgroundDark;
			const ImU32 knobOn = Colors::Theme::titlebar;   // dark knob reads on the lime track
			const ImU32 knobOff = Colors::Theme::textDarker;
			dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height), value ? trackOn : trackOff, radius);
			const float knobX = value ? (p.x + width - radius) : (p.x + radius);
			dl->AddCircleFilled(ImVec2(knobX, p.y + radius), radius - 2.0f, value ? knobOn : knobOff);

			ImGui::NextColumn();
			return changed;
		}

	}

	void SceneRendererPanel::SetContext(const Ref<SceneRenderer>& context)
	{
		m_Context = context;
		ApplyProjectSettingsToContext();
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
			return false;

		m_ProjectRendererSettingsDirty = false;
		return true;
	}

	void SceneRendererPanel::OnImGuiRender(bool& isOpen)
	{
		if (!ImGui::Begin("Scene Renderer", &isOpen))
		{
			ImGui::End();
			return;
		}

		if (!m_Context)
		{
			ImGui::TextDisabled("No SceneRenderer context.");
			ImGui::End();
			return;
		}

		auto& options = m_Context->GetOptions();
		auto& bloom = m_Context->GetBloomSettings();
		auto& dof = m_Context->GetDOFSettings();
		auto& ssr = m_Context->GetSSROptions();
		bool projectSettingsChanged = false;
		bool screenSpaceResourcesChanged = false;

		// ---- Header: status chips ------------------------------------------------------------
		StatChip("Ready", m_Context->IsReady() ? "Yes" : "No");
		ImGui::SameLine();
		StatChip("View", std::format("{}x{}", m_Context->GetViewportWidth(), m_Context->GetViewportHeight()).c_str());
		ImGui::SameLine();
		StatChip("Output", std::format("{}x{}", m_Context->GetOutputViewportWidth(), m_Context->GetOutputViewportHeight()).c_str());
		ImGui::SameLine();
		StatChip("Scale", std::format("{:.0f}%", m_Context->GetRenderResolutionScale() * 100.0f).c_str());

		ImGui::Spacing();

		// ---- Header: quality preset segmented selector ---------------------------------------
		{
			const char* qualityLabels[] = { "Low", "Medium", "High", "Ultra", "Cinematic" };
			const int currentQuality = static_cast<int>(options.Quality);
			for (int i = 0; i < IM_ARRAYSIZE(qualityLabels); i++)
			{
				if (i > 0)
					ImGui::SameLine();
				const bool active = currentQuality == i;
				if (active)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, Colors::Theme::accent);
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Colors::Theme::accent);
					ImGui::PushStyleColor(ImGuiCol_Text, Colors::Theme::titlebar);
				}
				if (ImGui::Button(qualityLabels[i]) && !active)
				{
					m_Context->SetQualityPreset(static_cast<QualityPreset>(std::clamp(i, 0, 4)));
					projectSettingsChanged = true;
				}
				if (active)
					ImGui::PopStyleColor(3);
			}
		}

		ImGui::Spacing();

		// ---- Header: save + search -----------------------------------------------------------
		if (Ref<Project> project = Project::GetActive())
		{
			const bool canSaveProject = !project->GetProjectFilePath().empty();
			if (!canSaveProject)
				ImGui::BeginDisabled();
			if (ImGui::Button("Save Renderer Settings"))
				SaveProjectRendererSettings();
			if (!canSaveProject)
				ImGui::EndDisabled();
			if (m_ProjectRendererSettingsDirty)
			{
				ImGui::SameLine();
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Colors::Theme::accent), "Unsaved");
			}
		}

		ImGui::Spacing();
		ImGuiEx::Widgets::SearchWidget<sizeof(m_SearchBuffer)>(m_SearchBuffer, "Search settings...");
		const std::string queryLower = Utils::String::ToLowerCopy(m_SearchBuffer);
		const bool searching = !queryLower.empty();
		ImGui::Spacing();

		auto match = [&](const char* label) { return MatchesSearch(queryLower, label); };
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
				{
					ImGui::TextDisabled("Editor-only debug views");
					ImGui::SameLine();
					if (m_DebugViewsRuntimeSuspended)
						ImGui::BeginDisabled();
					if (ImGui::Button("Reset"))
					{
						if (m_OnResetDebugViews)
							m_OnResetDebugViews();
						else
							ResetDebugViews(options);
					}
					if (m_DebugViewsRuntimeSuspended)
						ImGui::EndDisabled();
					if (m_DebugViewsRuntimeSuspended)
						ImGui::TextDisabled("Suspended while Play is running; restored on Stop.");
				}

				if (m_DebugViewsRuntimeSuspended)
					ImGui::BeginDisabled();
				bool debugViewsChanged = false;
				ImGuiEx::BeginPropertyGrid();
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
				ImGuiEx::BeginPropertyGrid();
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
						auto resPreset = [&](const char* label, uint32_t w, uint32_t h)
						{
							if (ImGui::Button(label))
							{
								options.FixedRenderWidth = w;
								options.FixedRenderHeight = h;
								m_Context->RefreshRenderResolutionScale();
								projectSettingsChanged = true;
							}
						};
						resPreset("1920x1080", 1920, 1080);
						ImGui::SameLine();
						resPreset("2560x1440", 2560, 1440);
						ImGui::SameLine();
						resPreset("1280x720", 1280, 720);
						ImGuiEx::BeginPropertyGrid();
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
					if (dynamicScaleChanged)
					{
						options.DynamicResolutionMinScale = std::clamp(options.DynamicResolutionMinScale, 0.25f, 1.0f);
						options.DynamicResolutionMaxScale = std::clamp(options.DynamicResolutionMaxScale, options.DynamicResolutionMinScale, 1.0f);
						options.DynamicResolutionScale = std::clamp(options.DynamicResolutionScale, options.DynamicResolutionMinScale, options.DynamicResolutionMaxScale);
						m_Context->RefreshRenderResolutionScale();
						projectSettingsChanged = true;
					}
				}
				if (match("Texture Mip Bias"))
					projectSettingsChanged |= ImGuiEx::Property("Texture Mip Bias", options.TextureMipBias, 0.05f, -4.0f, 8.0f);
				projectSettingsChanged |= toggle("Distance Mip Bias", options.EnableDistanceMipBias);
				if (options.EnableDistanceMipBias)
				{
					bool mipBiasChanged = false;
					if (match("Mip Bias Start"))
						mipBiasChanged |= ImGuiEx::Property("Mip Bias Start", options.DistanceMipBiasStart, 1.0f, 0.0f, 10000.0f);
					if (match("Mip Bias End"))
						mipBiasChanged |= ImGuiEx::Property("Mip Bias End", options.DistanceMipBiasEnd, 1.0f, 1.0f, 10000.0f);
					if (match("Mip Bias Max"))
						mipBiasChanged |= ImGuiEx::Property("Mip Bias Max", options.DistanceMipBiasMax, 0.05f, 0.0f, 8.0f);
					if (mipBiasChanged)
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
		if (CardHasMatch(queryLower, "Screen Space Effects", { "GTAO", "GTAO Bent Normals", "GTAO Resolution", "GTAO Denoise Passes", "AO Shadow Tolerance", "SSR", "SSR Quality", "SMAA", "SMAA Threshold", "SMAA Local Contrast", "Jump Flood Outline", "Async Compute" }))
		{
			if (BeginSettingsCard("card_ss", LUX_ICON_MAGIC, "Screen-Space Effects", m_CardScreenSpace, searching))
			{
				ImGuiEx::BeginPropertyGrid();
				bool gtaoSettingsChanged = false;
				gtaoSettingsChanged |= toggle("GTAO", options.EnableGTAO);
				gtaoSettingsChanged |= toggle("GTAO Bent Normals", options.GTAOBentNormals);
				if (match("GTAO Resolution") && DrawEffectScaleProperty("GTAO Resolution", options.GTAOResolutionScale))
				{
					screenSpaceResourcesChanged = true;
					projectSettingsChanged = true;
				}
				projectSettingsChanged |= gtaoSettingsChanged;
				if (match("GTAO Denoise Passes"))
					projectSettingsChanged |= ImGuiEx::Property("GTAO Denoise Passes", options.GTAODenoisePasses, 0, 8);
				if (match("AO Shadow Tolerance"))
					projectSettingsChanged |= ImGuiEx::Property("AO Shadow Tolerance", options.AOShadowTolerance, 0.01f, 0.0f, 4.0f);
				gtaoSettingsChanged |= toggle("SSR", options.EnableSSR);
				const char* ssrQualityLabels[] = { "Full", "Half + Bilateral Upscale", "Quarter Debug Only" };
				int ssrQuality = static_cast<int>(options.SSRQuality);
				if (match("SSR Quality") && DrawComboProperty("SSR Quality", ssrQuality, ssrQualityLabels, IM_ARRAYSIZE(ssrQualityLabels)))
				{
					options.SSRQuality = static_cast<SceneRendererOptions::SSRQualityPreset>(std::clamp(ssrQuality, 0, 2));
					options.SSRResolutionScale = ResolutionScaleFromSSRQuality(options.SSRQuality);
					ssr.HalfRes = options.SSRResolutionScale != SceneRendererOptions::EffectResolutionScale::Full;
					ssr.ResolutionScale = static_cast<uint32_t>(options.SSRResolutionScale);
					screenSpaceResourcesChanged = true;
					projectSettingsChanged = true;
				}
				if (options.SSRQuality == SceneRendererOptions::SSRQualityPreset::QuarterDebug && !searching)
					ImGui::TextDisabled("Quarter SSR is debug-only; use Full or Half + Bilateral for normal rendering.");
				projectSettingsChanged |= toggle("SMAA", options.EnableSMAA);
				if (options.EnableSMAA)
				{
					if (match("SMAA Threshold"))
						projectSettingsChanged |= ImGuiEx::Property("SMAA Threshold", options.SMAAThreshold, 0.005f, 0.01f, 0.5f);
					if (match("SMAA Local Contrast"))
						projectSettingsChanged |= ImGuiEx::Property("SMAA Local Contrast", options.SMAALocalContrastAdaptationFactor, 0.05f, 1.0f, 8.0f);
					if (!m_Context->IsSMAAReady() && !searching)
						ImGui::TextDisabled("SMAA lookup textures missing - see Core/vendor/smaa");
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
		if (CardHasMatch(queryLower, "Shadows", { "Soft Shadows", "Shadow Culling", "Max Distance", "Distance Fade", "Active Cascades", "Split Lambda", "Near Offset", "Far Offset", "Cascade Fade", "Filter", "PCSS Cascades", "PCF Radius", "Spot PCF Radius", "Resolution Limit" }))
		{
			if (BeginSettingsCard("card_shadows", LUX_ICON_SUN_O, "Shadows", m_CardShadows, searching))
			{
				ImGuiEx::BeginPropertyGrid();
				projectSettingsChanged |= toggle("Soft Shadows", options.SoftShadows);
				projectSettingsChanged |= toggle("Shadow Culling", options.EnableShadowCulling);
				if (match("Max Distance"))
					projectSettingsChanged |= ImGuiEx::Property("Max Distance", options.MaxShadowDistance, 1.0f, 1.0f, 1000.0f);
				if (match("Distance Fade"))
					projectSettingsChanged |= ImGuiEx::Property("Distance Fade", options.ShadowFade, 0.25f, 0.01f, 250.0f);
				if (match("Active Cascades"))
					projectSettingsChanged |= ImGuiEx::Property("Active Cascades", options.ActiveShadowCascadeCount, 1u, SceneRenderer::ShadowCascadeCount);
				if (match("Split Lambda"))
					projectSettingsChanged |= ImGuiEx::Property("Split Lambda", options.ShadowCascadeSplitLambda, 0.01f, 0.0f, 1.0f);
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
					projectSettingsChanged = true;
				}
				if (match("PCSS Cascades"))
					projectSettingsChanged |= ImGuiEx::Property("PCSS Cascades", options.DirectionalPCSSCascadeCount, 0u, options.ActiveShadowCascadeCount);
				if (match("PCF Radius"))
					projectSettingsChanged |= ImGuiEx::Property("PCF Radius", options.ShadowPCFRadiusTexels, 0.05f, 0.25f, 8.0f);
				if (match("Spot PCF Radius"))
					projectSettingsChanged |= ImGuiEx::Property("Spot PCF Radius", options.SpotShadowPCFRadiusTexels, 0.05f, 0.25f, 8.0f);
				const char* shadowResolutionLabels[] = { "1K", "2K", "4K", "8K" };
				int shadowResolution = static_cast<int>(options.ShadowResolution);
				if (match("Resolution Limit") && DrawComboProperty("Resolution Limit", shadowResolution, shadowResolutionLabels, IM_ARRAYSIZE(shadowResolutionLabels)))
				{
					options.ShadowResolution = static_cast<SceneRendererOptions::ShadowResolutionTier>(std::clamp(shadowResolution, 0, 3));
					projectSettingsChanged = true;
				}
				ImGuiEx::EndPropertyGrid();
			}
			EndSettingsCard();
		}

		// ---- Post FX -------------------------------------------------------------------------
		if (CardHasMatch(queryLower, "Post FX", { "Bloom", "Bloom Resolution", "Bloom Threshold", "Bloom Knee", "Bloom Upsample Scale", "Bloom Intensity", "Bloom Dirt Intensity", "DOF", "DOF Resolution", "DOF Focus Distance", "DOF Blur Size", "SSR Max Steps", "SSR Brightness", "SSR Depth Tolerance" }))
		{
			if (BeginSettingsCard("card_postfx", LUX_ICON_FILM, "Post FX", m_CardPostFX, searching))
			{
				ImGuiEx::BeginPropertyGrid();
				projectSettingsChanged |= toggle("Bloom", bloom.Enabled);
				if (match("Bloom Resolution") && DrawEffectScaleProperty("Bloom Resolution", bloom.ResolutionScale))
				{
					screenSpaceResourcesChanged = true;
					projectSettingsChanged = true;
				}
				if (match("Bloom Threshold"))
					projectSettingsChanged |= ImGuiEx::Property("Bloom Threshold", bloom.Threshold, 0.01f, 0.0f, 25.0f);
				if (match("Bloom Knee"))
					projectSettingsChanged |= ImGuiEx::Property("Bloom Knee", bloom.Knee, 0.01f, 0.0f, 1.0f);
				if (match("Bloom Upsample Scale"))
					projectSettingsChanged |= ImGuiEx::Property("Bloom Upsample Scale", bloom.UpsampleScale, 0.01f, 0.0f, 10.0f);
				if (match("Bloom Intensity"))
					projectSettingsChanged |= ImGuiEx::Property("Bloom Intensity", bloom.Intensity, 0.01f, 0.0f, 10.0f);
				if (match("Bloom Dirt Intensity"))
					projectSettingsChanged |= ImGuiEx::Property("Bloom Dirt Intensity", bloom.DirtIntensity, 0.01f, 0.0f, 10.0f);
				projectSettingsChanged |= toggle("DOF", dof.Enabled);
				if (match("DOF Resolution") && DrawEffectScaleProperty("DOF Resolution", dof.ResolutionScale))
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
						projectSettingsChanged = true;
					}
				}
				if (match("SSR Brightness"))
					projectSettingsChanged |= ImGuiEx::Property("SSR Brightness", ssr.Brightness, 0.01f, 0.0f, 5.0f);
				if (match("SSR Depth Tolerance"))
					projectSettingsChanged |= ImGuiEx::Property("SSR Depth Tolerance", ssr.DepthTolerance, 0.01f, 0.0f, 5.0f);
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
				ImGuiEx::BeginPropertyGrid();
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
				ImGuiEx::BeginPropertyGrid();
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
				ImGuiEx::BeginPropertyGrid();
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
				ImGuiEx::BeginPropertyGrid();
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

		if (screenSpaceResourcesChanged)
			m_Context->RefreshScreenSpaceEffectResources();

		if (projectSettingsChanged)
			SyncProjectSettingsFromContext();

		ImGui::End();
	}


}
