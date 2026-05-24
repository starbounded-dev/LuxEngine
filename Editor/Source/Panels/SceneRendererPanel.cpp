#include "lpch.h"
#include "SceneRendererPanel.h"

#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/Project/Project.h"

#include <imgui/imgui.h>

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

		if (ImGui::BeginTable("##scene_renderer_summary", 2, ImGuiTableFlags_SizingStretchProp))
		{
			DrawStat("Ready", m_Context->IsReady() ? "Yes" : "No");
			DrawStat("Technique", RenderingTechniqueToString(m_Context->GetRenderingTechnique()));
			DrawStat("Viewport", (std::to_string(m_Context->GetViewportWidth()) + " x " + std::to_string(m_Context->GetViewportHeight())).c_str());
			DrawStat("Output Viewport", (std::to_string(m_Context->GetOutputViewportWidth()) + " x " + std::to_string(m_Context->GetOutputViewportHeight())).c_str());
			DrawStat("Render Scale", m_Context->GetRenderResolutionScale() * 100.0f, "%");
			ImGui::EndTable();
		}

		ImGui::Spacing();
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
				ImGui::TextDisabled("Unsaved renderer settings");
			}
		}

		ImGui::Spacing();

		if (ImGuiEx::PropertyGridHeader("Visualization", true))
		{
			ImGui::TextDisabled("Editor-only debug views");
			ImGui::SameLine();
			if (m_DebugViewsRuntimeSuspended)
				ImGui::BeginDisabled();
			if (ImGui::Button("Reset Debug Views"))
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

			if (m_DebugViewsRuntimeSuspended)
				ImGui::BeginDisabled();

			bool debugViewsChanged = false;
			ImGuiEx::BeginPropertyGrid();
			debugViewsChanged |= ImGuiEx::Property("Show Grid", options.ShowGrid);
			debugViewsChanged |= ImGuiEx::Property("Show Selected Wireframe", options.ShowSelectedInWireframe);
			debugViewsChanged |= ImGuiEx::Property("Show Physics Colliders", options.ShowPhysicsColliders);
			debugViewsChanged |= ImGuiEx::Property("Show Shadow Cascades", options.ShowShadowCascades);
			debugViewsChanged |= ImGuiEx::Property("Show Cascade Frustums", options.ShowCascadeFrustums);
			debugViewsChanged |= ImGuiEx::Property("Show Light Complexity", options.ShowLightComplexity);
			ImGuiEx::EndPropertyGrid();

			if (m_DebugViewsRuntimeSuspended)
				ImGui::EndDisabled();

			if (debugViewsChanged && m_OnDebugViewsChanged)
				m_OnDebugViewsChanged();

			ImGui::TreePop();
		}

		if (ImGuiEx::PropertyGridHeader("Rendering", true))
		{
			ImGuiEx::BeginPropertyGrid();
			const char* qualityPresetLabels[] = { "Low", "Medium", "High", "Ultra", "Cinematic" };
			int qualityPreset = static_cast<int>(options.Quality);
			if (DrawComboProperty("Quality Preset", qualityPreset, qualityPresetLabels, IM_ARRAYSIZE(qualityPresetLabels)))
			{
				m_Context->SetQualityPreset(static_cast<QualityPreset>(std::clamp(qualityPreset, 0, 4)));
				projectSettingsChanged = true;
			}
			projectSettingsChanged |= ImGuiEx::Property("Frustum Culling", options.EnableFrustumCulling);
			projectSettingsChanged |= ImGuiEx::Property("Occlusion Culling", options.EnableOcclusionCulling);
			if (options.EnableOcclusionCulling)
			{
				projectSettingsChanged |= ImGuiEx::Property("Occlusion Depth Bias", options.OcclusionDepthBias, 0.0005f, 0.0f, 0.1f);
				projectSettingsChanged |= ImGuiEx::Property("Occlusion Bounds Scale", options.OcclusionBoundsScale, 0.01f, 1.0f, 2.0f);
			}
			projectSettingsChanged |= ImGuiEx::Property("GPU Driven Indirect", options.EnableGPUDrivenRendering);
			const char* renderScaleLabels[] = { "100%", "75%", "50%", "Dynamic" };
			int renderScaleMode = static_cast<int>(options.ResolutionScaleMode);
			if (DrawComboProperty("Render Scale", renderScaleMode, renderScaleLabels, IM_ARRAYSIZE(renderScaleLabels)))
			{
				options.ResolutionScaleMode = static_cast<SceneRendererOptions::RenderResolutionScaleMode>(renderScaleMode);
				m_Context->RefreshRenderResolutionScale();
				projectSettingsChanged = true;
			}
			if (options.ResolutionScaleMode == SceneRendererOptions::RenderResolutionScaleMode::Dynamic)
			{
				bool dynamicScaleChanged = false;
				dynamicScaleChanged |= ImGuiEx::Property("Dynamic Min Scale", options.DynamicResolutionMinScale, 0.01f, 0.25f, 1.0f);
				dynamicScaleChanged |= ImGuiEx::Property("Dynamic Max Scale", options.DynamicResolutionMaxScale, 0.01f, 0.25f, 1.0f);
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
			projectSettingsChanged |= ImGuiEx::Property("Texture Mip Bias", options.TextureMipBias, 0.05f, -4.0f, 8.0f);
			projectSettingsChanged |= ImGuiEx::Property("Distance Mip Bias", options.EnableDistanceMipBias);
			if (options.EnableDistanceMipBias)
			{
				bool mipBiasChanged = false;
				mipBiasChanged |= ImGuiEx::Property("Mip Bias Start", options.DistanceMipBiasStart, 1.0f, 0.0f, 10000.0f);
				mipBiasChanged |= ImGuiEx::Property("Mip Bias End", options.DistanceMipBiasEnd, 1.0f, 1.0f, 10000.0f);
				mipBiasChanged |= ImGuiEx::Property("Mip Bias Max", options.DistanceMipBiasMax, 0.05f, 0.0f, 8.0f);
				if (mipBiasChanged)
				{
					options.DistanceMipBiasStart = std::max(0.0f, options.DistanceMipBiasStart);
					options.DistanceMipBiasEnd = std::max(options.DistanceMipBiasStart + 1.0f, options.DistanceMipBiasEnd);
					options.DistanceMipBiasMax = std::clamp(options.DistanceMipBiasMax, 0.0f, 8.0f);
					projectSettingsChanged = true;
				}
			}
			bool gtaoSettingsChanged = false;
			gtaoSettingsChanged |= ImGuiEx::Property("GTAO", options.EnableGTAO);
			gtaoSettingsChanged |= ImGuiEx::Property("GTAO Bent Normals", options.GTAOBentNormals);
			if (DrawEffectScaleProperty("GTAO Resolution", options.GTAOResolutionScale))
			{
				screenSpaceResourcesChanged = true;
				projectSettingsChanged = true;
			}
			projectSettingsChanged |= ImGuiEx::Property("GTAO Temporal", options.EnableGTAOTemporalAccumulation);
			if (options.EnableGTAOTemporalAccumulation)
				projectSettingsChanged |= ImGuiEx::Property("GTAO Temporal Blend", options.GTAOTemporalBlend, 0.01f, 0.0f, 0.98f);
			projectSettingsChanged |= gtaoSettingsChanged;
			projectSettingsChanged |= ImGuiEx::Property("GTAO Denoise Passes", options.GTAODenoisePasses, 0, 8);
			projectSettingsChanged |= ImGuiEx::Property("AO Shadow Tolerance", options.AOShadowTolerance, 0.01f, 0.0f, 4.0f);
			gtaoSettingsChanged |= ImGuiEx::Property("SSR", options.EnableSSR);
			const char* ssrQualityLabels[] = { "Full", "Half + Bilateral Upscale", "Quarter Debug Only" };
			int ssrQuality = static_cast<int>(options.SSRQuality);
			if (DrawComboProperty("SSR Quality", ssrQuality, ssrQualityLabels, IM_ARRAYSIZE(ssrQualityLabels)))
			{
				options.SSRQuality = static_cast<SceneRendererOptions::SSRQualityPreset>(std::clamp(ssrQuality, 0, 2));
				options.SSRResolutionScale = ResolutionScaleFromSSRQuality(options.SSRQuality);
				ssr.HalfRes = options.SSRResolutionScale != SceneRendererOptions::EffectResolutionScale::Full;
				ssr.ResolutionScale = static_cast<uint32_t>(options.SSRResolutionScale);
				screenSpaceResourcesChanged = true;
				projectSettingsChanged = true;
			}
			if (options.SSRQuality == SceneRendererOptions::SSRQualityPreset::QuarterDebug)
				ImGui::TextDisabled("Quarter SSR is debug-only; use Full or Half + Bilateral for normal rendering.");
			projectSettingsChanged |= ImGuiEx::Property("SSR Temporal", options.EnableSSRTemporalAccumulation);
			if (options.EnableSSRTemporalAccumulation)
				projectSettingsChanged |= ImGuiEx::Property("SSR Temporal Blend", options.SSRTemporalBlend, 0.01f, 0.0f, 0.98f);
			projectSettingsChanged |= gtaoSettingsChanged;
			if (gtaoSettingsChanged)
				m_Context->UpdateGTAOData();
			projectSettingsChanged |= ImGuiEx::Property("Jump Flood Outline", options.EnableJumpFlood);
			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}

		if (ImGuiEx::PropertyGridHeader("Shadows", true))
		{
			ImGuiEx::BeginPropertyGrid();
			projectSettingsChanged |= ImGuiEx::Property("Soft Shadows", options.SoftShadows);
			projectSettingsChanged |= ImGuiEx::Property("Shadow Culling", options.EnableShadowCulling);
			projectSettingsChanged |= ImGuiEx::Property("Max Distance", options.MaxShadowDistance, 1.0f, 1.0f, 1000.0f);
			projectSettingsChanged |= ImGuiEx::Property("Distance Fade", options.ShadowFade, 0.25f, 0.01f, 250.0f);
			projectSettingsChanged |= ImGuiEx::Property("Split Lambda", options.ShadowCascadeSplitLambda, 0.01f, 0.0f, 1.0f);
			projectSettingsChanged |= ImGuiEx::Property("Near Offset", options.ShadowCascadeNearPlaneOffset, 0.1f, 0.0f, 200.0f);
			projectSettingsChanged |= ImGuiEx::Property("Far Offset", options.ShadowCascadeFarPlaneOffset, 0.5f, 0.0f, 500.0f);
			projectSettingsChanged |= ImGuiEx::Property("Cascade Fade", options.ShadowCascadeTransitionFade, 0.05f, 0.0f, 25.0f);
			const char* shadowResolutionLabels[] = { "1K", "2K", "4K", "8K" };
			int shadowResolution = static_cast<int>(options.ShadowResolution);
			if (DrawComboProperty("Resolution Limit", shadowResolution, shadowResolutionLabels, IM_ARRAYSIZE(shadowResolutionLabels)))
			{
				options.ShadowResolution = static_cast<SceneRendererOptions::ShadowResolutionTier>(std::clamp(shadowResolution, 0, 3));
				projectSettingsChanged = true;
			}
			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}

		if (ImGuiEx::PropertyGridHeader("Post FX", false))
		{
			ImGuiEx::BeginPropertyGrid();
			projectSettingsChanged |= ImGuiEx::Property("Bloom", bloom.Enabled);
			if (DrawEffectScaleProperty("Bloom Resolution", bloom.ResolutionScale))
			{
				screenSpaceResourcesChanged = true;
				projectSettingsChanged = true;
			}
			projectSettingsChanged |= ImGuiEx::Property("Bloom Threshold", bloom.Threshold, 0.01f, 0.0f, 25.0f);
			projectSettingsChanged |= ImGuiEx::Property("Bloom Knee", bloom.Knee, 0.01f, 0.0f, 1.0f);
			projectSettingsChanged |= ImGuiEx::Property("Bloom Upsample Scale", bloom.UpsampleScale, 0.01f, 0.0f, 10.0f);
			projectSettingsChanged |= ImGuiEx::Property("Bloom Intensity", bloom.Intensity, 0.01f, 0.0f, 10.0f);
			projectSettingsChanged |= ImGuiEx::Property("Bloom Dirt Intensity", bloom.DirtIntensity, 0.01f, 0.0f, 10.0f);
			projectSettingsChanged |= ImGuiEx::Property("DOF", dof.Enabled);
			if (DrawEffectScaleProperty("DOF Resolution", dof.ResolutionScale))
			{
				screenSpaceResourcesChanged = true;
				projectSettingsChanged = true;
			}
			projectSettingsChanged |= ImGuiEx::Property("DOF Focus Distance", dof.FocusDistance, 0.1f, 0.0f, 1000.0f);
			projectSettingsChanged |= ImGuiEx::Property("DOF Blur Size", dof.BlurSize, 0.05f, 0.0f, 20.0f);
			int32_t ssrMaxSteps = ssr.MaxSteps;
			if (ImGuiEx::Property("SSR Max Steps", ssrMaxSteps, 1, 256))
			{
				ssr.MaxSteps = ssrMaxSteps;
				projectSettingsChanged = true;
			}
			projectSettingsChanged |= ImGuiEx::Property("SSR Brightness", ssr.Brightness, 0.01f, 0.0f, 5.0f);
			projectSettingsChanged |= ImGuiEx::Property("SSR Depth Tolerance", ssr.DepthTolerance, 0.01f, 0.0f, 5.0f);
			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}

		if (screenSpaceResourcesChanged)
			m_Context->RefreshScreenSpaceEffectResources();

		if (projectSettingsChanged)
			SyncProjectSettingsFromContext();

		ImGui::End();
	}

}
