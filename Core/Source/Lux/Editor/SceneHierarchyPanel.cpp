// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/ImGui/AudioWidgets.h"

#include "SceneHierarchyPanel.h"
#include "ParticleWidgets.h"

#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/Sound.h"

#include "Lux/Animation/Animation.h"
#include "Lux/Animation/AnimationGraph.h"
#include "Lux/Asset/Asset.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Asset/AssetImporter.h"
#include "Lux/Core/Events/KeyEvent.h"
#include "Lux/Core/Events/MouseEvent.h"
#include "Lux/Core/Input.h"
#include "Lux/Editor/ComponentDocs.h"
#include "Lux/Editor/EditorDocs.h"
#include "Lux/Editor/EditorResources.h"
#include "Lux/Editor/EditorStack.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/PanelControls.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/SpriteSheet.h"
#include "Lux/Physics2D/PhysicsScene2D.h"
#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Renderer/Mesh.h"
#include "Lux/Animation/Skeleton.h"
#include "Lux/Renderer/SceneEnvironment.h"
#include "Lux/Renderer/UI/Font.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Scene/Prefab.h"
#include "Lux/Scene/SceneSerializer.h"
#include "Lux/Asset/PrefabSerializer.h"
#include "Lux/Scripting/ScriptEngine.h"
#include "Lux/Core/Hash.h"

#include <concepts>
#include <entt/entt.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <format>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace Lux {

	SelectionContext SceneHierarchyPanel::s_ActiveSelectionContext = SelectionContext::Scene;

	namespace {

		// Clip preview and animation graph view under the Skeleton section. Editor tools only: they change the
		// component's runtime State (never its saved settings), so using them does not dirty the scene.
		void DrawSkeletonAnimationTools(SkeletonComponent& component)
		{
			Ref<Animation> clip = component.Clip ? AssetManager::GetAsset<Animation>(component.Clip) : nullptr;
			Ref<AnimationGraph> graph = component.Graph ? AssetManager::GetAsset<AnimationGraph>(component.Graph) : nullptr;
			if (!clip && !graph)
				return;

			ImGui::PushID("SkeletonAnimationTools");
			ImGui::Separator();
			AnimatorState& state = component.State;
			const ImGuiEx::ScopedDisable previewDisabled(!component.Enabled || !state.Initialized || state.PlayMode);

			// The clip being played: the graph's current state's, else the component's.
			Ref<Animation> current = clip;
			if (graph && state.Graph.Initialized && state.Graph.CurrentState < graph->States.size())
				current = AssetManager::GetAsset<Animation>(graph->States[state.Graph.CurrentState].Clip);
			if (current)
			{
				ImGui::Text("%s: %.2f s, %zu animated bones, %zu events", current->GetName().c_str(), current->GetDuration(), current->GetTracks().size(), current->GetEvents().size());

				if (ImGui::Button(state.Playing ? "Pause###Preview" : "Play###Preview"))
				{
					state.Playing = !state.Playing;
					state.PreviewOverride = true;
				}
				ImGui::SameLine();
				float time = state.Time;
				ImGui::SetNextItemWidth(-1.0f);
				const ImGuiEx::ScopedDisable noDuration(current->GetDuration() <= 0.0f);
				if (ImGui::SliderFloat("##PreviewTime", &time, 0.0f, current->GetDuration(), "%.2f s", ImGuiSliderFlags_AlwaysClamp))
				{
					state.Time = time;
					state.FadeFromClip = 0;
					state.Playing = false;
					state.PreviewOverride = true;
				}
			}

			if (graph && ImGui::TreeNode("Animation graph"))
			{
				ImGui::TextDisabled("States");
				for (uint32_t i = 0; i < graph->States.size(); i++)
				{
					ImGui::PushID(static_cast<int>(i));
					const AnimationGraphState& graphState = graph->States[i];
					const bool isCurrent = state.Graph.Initialized && state.Graph.CurrentState == i;
					Ref<Animation> stateClip = AssetManager::GetAsset<Animation>(graphState.Clip);
					ImGui::TextColored(isCurrent ? ImVec4(0.3f, 1.0f, 0.5f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s %s  (%s, x%.2f%s)", isCurrent ? ">" : " ", graphState.Name.c_str(),
						stateClip ? stateClip->GetName().c_str() : "no clip", graphState.Speed, graphState.Loop ? "" : ", once");
					ImGui::PopID();
				}

				ImGui::TextDisabled("Transitions");
				static constexpr const char* k_Ops[] = { ">", "<", "==", "!=", "is true", "is false", "triggered" };
				for (uint32_t i = 0; i < graph->Transitions.size(); i++)
				{
					ImGui::PushID(1000 + static_cast<int>(i));
					const AnimationGraphTransition& transition = graph->Transitions[i];
					std::string conditions;
					for (const AnimationGraphCondition& condition : transition.Conditions)
					{
						if (!conditions.empty())
							conditions += ", ";
						conditions += condition.Parameter + " " + k_Ops[static_cast<size_t>(condition.Op)];
						if (condition.Op <= AnimationConditionOp::NotEqual)
							conditions += " " + std::format("{:.2f}", condition.Value);
					}
					const std::string from = transition.From < graph->States.size() ? graph->States[transition.From].Name : std::string("Any");
					ImGui::BulletText("%s -> %s  %.2f s%s%s", from.c_str(), transition.To < graph->States.size() ? graph->States[transition.To].Name.c_str() : "?", transition.Duration,
						conditions.empty() ? "" : "  if ", conditions.c_str());
					ImGui::PopID();
				}

				if (!graph->Parameters.empty())
				{
					ImGui::TextDisabled(state.Graph.Initialized ? "Parameters" : "Parameters (they start when the animation does)");
					for (uint32_t i = 0; i < graph->Parameters.size() && state.Graph.Initialized && i < state.Graph.Values.size(); i++)
					{
						ImGui::PushID(2000 + static_cast<int>(i));
						const AnimationGraphParameter& parameter = graph->Parameters[i];
						float& value = state.Graph.Values[i];
						if (parameter.Type == AnimationParameterType::Float)
						{
							ImGui::DragFloat(parameter.Name.c_str(), &value, 0.01f);
						}
						else if (parameter.Type == AnimationParameterType::Bool)
						{
							bool flag = value != 0.0f;
							if (ImGui::Checkbox(parameter.Name.c_str(), &flag))
								value = flag ? 1.0f : 0.0f;
						}
						else
						{
							if (ImGui::Button("Set"))
								value = 1.0f;
							ImGui::SameLine();
							ImGui::Text("%s%s", parameter.Name.c_str(), value != 0.0f ? "  (pending)" : "");
						}
						ImGui::PopID();
					}
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}

		// A per-entity type icon (FontAwesome glyph) plus a category tint, picked from the entity's
		// most representative component. Tints are tasteful and theme-adjacent: warm for lights,
		// cool for cameras, purple for audio, green for scripts, neutral for renderables.
		struct EntityIconInfo { const char* Glyph; ImU32 Color; };

		EntityIconInfo GetEntityIcon(Entity entity)
		{
			constexpr ImU32 warm   = IM_COL32(224, 176, 92, 255);
			constexpr ImU32 cool   = IM_COL32(120, 170, 255, 255);
			constexpr ImU32 purple = IM_COL32(184, 148, 232, 255);
			constexpr ImU32 green  = IM_COL32(150, 200, 130, 255);
			const ImU32 neutral = Colors::Theme::text;
			const ImU32 muted   = Colors::Theme::textDarker;

			if (entity.HasComponent<FolderComponent>())           return { LUX_ICON_FOLDER, warm };
			if (entity.HasComponent<CameraComponent>())           return { LUX_ICON_VIDEO_CAMERA, cool };
			if (entity.HasComponent<DirectionalLightComponent>()) return { LUX_ICON_SUN_O, warm };
			if (entity.HasComponent<PointLightComponent>() ||
			    entity.HasComponent<SpotLightComponent>() ||
			    entity.HasComponent<AreaLightComponent>())        return { LUX_ICON_LIGHTBULB_O, warm };
			if (entity.HasComponent<SkyLightComponent>())         return { LUX_ICON_SUN_O, warm };
			if (entity.HasComponent<StaticMeshComponent>() ||
			    entity.HasComponent<MeshComponent>())             return { LUX_ICON_CUBE, neutral };
			if (entity.HasComponent<TextComponent>())             return { LUX_ICON_FONT, neutral };
			if (entity.HasComponent<SpriteRendererComponent>())   return { LUX_ICON_PICTURE_O, neutral };
			if (entity.HasComponent<AudioSourceComponent>())      return { LUX_ICON_MUSIC, purple };
			if (entity.HasComponent<AudioPortalComponent>())      return { LUX_ICON_VOLUME_UP, purple };
			if (entity.HasComponent<AudioZoneComponent>())        return { LUX_ICON_VOLUME_UP, purple };
			if (entity.HasComponent<AudioListenerComponent>())    return { LUX_ICON_VOLUME_UP, purple };
			if (entity.HasComponent<ScriptComponent>())           return { LUX_ICON_CODE, green };
			return { LUX_ICON_DOT_CIRCLE_O, muted };
		}

		// Draws the ImGui control for one script field via its dual-mode FieldStorage (edits the
		// serializable buffer when idle, the live managed field when playing). Returns true if changed.
		bool DrawScriptFieldControl(const std::string& name, DataType type, FieldStorage& storage, bool hasRange = false, float rangeMin = 0.0f, float rangeMax = 1.0f)
		{
			switch (type)
			{
				case DataType::Float:   { float v = storage.GetValue<float>();     const bool edited = hasRange ? ImGui::SliderFloat(name.c_str(), &v, rangeMin, rangeMax) : ImGui::DragFloat(name.c_str(), &v, 0.1f); if (edited) { storage.SetValue(v); return true; } return false; }
				case DataType::Double:  { double v = storage.GetValue<double>();    if (ImGui::DragScalar(name.c_str(), ImGuiDataType_Double, &v, 0.1f)) { storage.SetValue(v); return true; } return false; }
				case DataType::Bool:    { bool v = storage.GetValue<uint32_t>() != 0; if (ImGui::Checkbox(name.c_str(), &v)) { storage.SetValue<uint32_t>(v ? 1u : 0u); return true; } return false; }
				case DataType::SByte:   { int8_t v = storage.GetValue<int8_t>();    if (ImGui::DragScalar(name.c_str(), ImGuiDataType_S8, &v)) { storage.SetValue(v); return true; } return false; }
				case DataType::Byte:    { uint8_t v = storage.GetValue<uint8_t>();  if (ImGui::DragScalar(name.c_str(), ImGuiDataType_U8, &v)) { storage.SetValue(v); return true; } return false; }
				case DataType::Short:   { int16_t v = storage.GetValue<int16_t>();  if (ImGui::DragScalar(name.c_str(), ImGuiDataType_S16, &v)) { storage.SetValue(v); return true; } return false; }
				case DataType::UShort:  { uint16_t v = storage.GetValue<uint16_t>(); if (ImGui::DragScalar(name.c_str(), ImGuiDataType_U16, &v)) { storage.SetValue(v); return true; } return false; }
				case DataType::Int:     { int32_t v = storage.GetValue<int32_t>();  const bool edited = hasRange ? ImGui::SliderInt(name.c_str(), &v, static_cast<int>(rangeMin), static_cast<int>(rangeMax)) : ImGui::DragScalar(name.c_str(), ImGuiDataType_S32, &v); if (edited) { storage.SetValue(v); return true; } return false; }
				case DataType::UInt:    { uint32_t v = storage.GetValue<uint32_t>(); if (ImGui::DragScalar(name.c_str(), ImGuiDataType_U32, &v)) { storage.SetValue(v); return true; } return false; }
				case DataType::Long:    { int64_t v = storage.GetValue<int64_t>();  if (ImGui::DragScalar(name.c_str(), ImGuiDataType_S64, &v)) { storage.SetValue(v); return true; } return false; }
				case DataType::ULong:   { uint64_t v = storage.GetValue<uint64_t>(); if (ImGui::DragScalar(name.c_str(), ImGuiDataType_U64, &v)) { storage.SetValue(v); return true; } return false; }
				case DataType::Vector2: { glm::vec2 v = storage.GetValue<glm::vec2>(); if (ImGui::DragFloat2(name.c_str(), &v.x, 0.1f)) { storage.SetValue(v); return true; } return false; }
				case DataType::Vector3: { glm::vec3 v = storage.GetValue<glm::vec3>(); if (ImGui::DragFloat3(name.c_str(), &v.x, 0.1f)) { storage.SetValue(v); return true; } return false; }
				case DataType::Vector4: { glm::vec4 v = storage.GetValue<glm::vec4>(); if (ImGui::DragFloat4(name.c_str(), &v.x, 0.1f)) { storage.SetValue(v); return true; } return false; }
				default:
				{
					// Entity + asset-refs are UUID-backed; editable as a raw handle (drag-drop is TODO).
					uint64_t v = storage.GetValue<uint64_t>();
					if (ImGui::DragScalar(name.c_str(), ImGuiDataType_U64, &v)) { storage.SetValue(v); return true; }
					return false;
				}
			}
		}

		template<typename TComponent, typename Fn>
		void ApplyToSelection(const Ref<Scene>& scene, const std::vector<UUID>& entityIDs, Fn&& fn)
		{
			if (!scene)
				return;

			for (UUID entityID : entityIDs)
			{
				Entity entity = scene->GetEntityByUUID(entityID);
				if (!entity || !entity.HasComponent<TComponent>())
					continue;

				fn(entity.GetComponent<TComponent>(), entity);
			}
		}

		// Sorting layer and order in layer for a 2D renderer (sprite, circle, text). The layer list is the
		// project's, so a layer is stored by ID and shown by name.
		template<typename TComponent>
		void DrawSortingProperties(const Ref<Scene>& scene, TComponent& first, const std::vector<UUID>& selectedEntities)
		{
			const Ref<Project> project = Project::GetActive();
			if (!project)
				return;

			const auto& layers = project->GetConfig().SortingLayers;
			std::vector<std::string> names;
			names.reserve(layers.size());
			int32_t current = static_cast<int32_t>(project->GetSortingLayerRank(first.SortingLayer));
			for (const ProjectSortingLayer& layer : layers)
				names.push_back(layer.Name.empty() ? std::format("Layer {}", layer.ID) : layer.Name);

			if (ImGuiEx::PropertyDropdown("Sorting Layer", names, static_cast<int32_t>(names.size()), &current,
				"Layers draw back to front in the order of Project Settings > 2D."))
			{
				const uint32_t layerID = layers[static_cast<size_t>(current)].ID;
				first.SortingLayer = layerID;
				ApplyToSelection<TComponent>(scene, selectedEntities, [layerID](TComponent& component, Entity) { component.SortingLayer = layerID; });
			}

			if (ImGuiEx::Property("Order In Layer", first.OrderInLayer, -32768, 32767, "Higher draws later, on top of lower orders in the same layer."))
			{
				const int32_t order = first.OrderInLayer;
				ApplyToSelection<TComponent>(scene, selectedEntities, [order](TComponent& component, Entity) { component.OrderInLayer = order; });
			}
		}

		// The rows every 2D collider shares: how it feels and whether it blocks. Density only where the component has one
		// (an edge collider has no mass).
		template<typename TComponent>
		void DrawCollider2DMaterial(const Ref<Scene>& scene, TComponent& first, const std::vector<UUID>& selectedEntities)
		{
			if constexpr (requires { first.Density; })
			{
				if (ImGuiEx::Property("Density", first.Density, 0.01f, 0.0f, 100.0f))
				{
					const float value = first.Density;
					ApplyToSelection<TComponent>(scene, selectedEntities, [value](TComponent& component, Entity) { component.Density = value; });
				}
			}
			if (ImGuiEx::Property("Friction", first.Friction, 0.01f, 0.0f, 1.0f))
			{
				const float value = first.Friction;
				ApplyToSelection<TComponent>(scene, selectedEntities, [value](TComponent& component, Entity) { component.Friction = value; });
			}
			if (ImGuiEx::Property("Restitution", first.Restitution, 0.01f, 0.0f, 1.0f))
			{
				const float value = first.Restitution;
				ApplyToSelection<TComponent>(scene, selectedEntities, [value](TComponent& component, Entity) { component.Restitution = value; });
			}
			if (ImGuiEx::Property("Restitution Threshold", first.RestitutionThreshold, 0.01f, 0.0f))
			{
				const float value = first.RestitutionThreshold;
				ApplyToSelection<TComponent>(scene, selectedEntities, [value](TComponent& component, Entity) { component.RestitutionThreshold = value; });
			}
			if (ImGuiEx::Property("Is Trigger", first.IsTrigger, "A trigger detects overlaps (OnTriggerBegin/End) and blocks nothing."))
			{
				const bool value = first.IsTrigger;
				ApplyToSelection<TComponent>(scene, selectedEntities, [value](TComponent& component, Entity) { component.IsTrigger = value; });
			}
		}

		// A list of 2D points (a polygon's or an edge collider's), edited as one row each with Add and Remove.
		template<typename TComponent>
		void DrawPoints2D(const Ref<Scene>& scene, TComponent& first, const std::vector<UUID>& selectedEntities, size_t minimum, size_t maximum)
		{
			bool changed = false;
			for (size_t i = 0; i < first.Points.size(); i++)
				changed |= ImGuiEx::Property(std::format("Point {}", i).c_str(), first.Points[i], 0.01f);
			ImGuiEx::ScopedID buttons(0);
			if (first.Points.size() < maximum && ImGui::Button("Add Point"))
			{
				const glm::vec2 last = first.Points.empty() ? glm::vec2(0.0f) : first.Points.back();
				first.Points.push_back(last + glm::vec2(0.5f, 0.0f));
				changed = true;
			}
			if (first.Points.size() > minimum)
			{
				ImGui::SameLine();
				if (ImGui::Button("Remove Last Point"))
				{
					first.Points.pop_back();
					changed = true;
				}
			}
			if (changed)
			{
				const std::vector<glm::vec2> points = first.Points;
				ApplyToSelection<TComponent>(scene, selectedEntities, [&points](TComponent& component, Entity) { component.Points = points; });
			}
		}

		template<typename TValue, typename Getter>
		bool IsSelectionInconsistent(const Ref<Scene>& scene, const std::vector<UUID>& entityIDs, Getter&& getter)
		{
			if (!scene || entityIDs.size() < 2)
				return false;

			Entity firstEntity = scene->GetEntityByUUID(entityIDs.front());
			if (!firstEntity)
				return false;

			const TValue firstValue = getter(firstEntity);
			for (size_t i = 1; i < entityIDs.size(); i++)
			{
				Entity entity = scene->GetEntityByUUID(entityIDs[i]);
				if (!entity)
					continue;

				if (getter(entity) != firstValue)
					return true;
			}

			return false;
		}

		AssetHandle GetDefaultMeshSourceHandle(const char* filename)
		{
			Ref<EditorAssetManager> editorAssetManager = Project::GetEditorAssetManager();
			if (!editorAssetManager)
				return 0;

			const AssetHandle handle = editorAssetManager->GetOrImportAsset(std::filesystem::path("Meshes") / "Source" / "Default" / filename);
			if (!handle || AssetManager::GetAssetType(handle) != AssetType::MeshSource)
				return 0;

			return handle;
		}

		// The mesh's own material for a slot: the StaticMesh asset's table first, then the source's
		// imported materials. Mirrors the fallback order of ResolveStaticMeshMaterialHandle.
		AssetHandle GetMeshDefaultMaterial(AssetHandle meshHandle, uint32_t slot)
		{
			if (!meshHandle || !AssetManager::IsAssetHandleValid(meshHandle))
				return 0;

			Ref<MeshSource> meshSource;
			if (AssetManager::GetAssetType(meshHandle) == AssetType::StaticMesh)
			{
				Ref<StaticMesh> staticMesh = AssetManager::GetAsset<StaticMesh>(meshHandle);
				if (!staticMesh)
					return 0;
				if (Ref<MaterialTable> table = staticMesh->GetMaterials(); table && table->HasMaterial(slot))
					return table->GetMaterial(slot);
				meshSource = AssetManager::GetAsset<MeshSource>(staticMesh->GetMeshSource());
			}
			else if (AssetManager::GetAssetType(meshHandle) == AssetType::MeshSource)
			{
				meshSource = AssetManager::GetAsset<MeshSource>(meshHandle);
			}

			if (meshSource && slot < meshSource->GetMaterials().size())
				return meshSource->GetMaterials()[slot];
			return 0;
		}

		uint32_t GetMeshMaterialSlotCount(AssetHandle meshHandle)
		{
			if (!meshHandle || !AssetManager::IsAssetHandleValid(meshHandle))
				return 0;

			AssetHandle sourceHandle = meshHandle;
			if (AssetManager::GetAssetType(meshHandle) == AssetType::StaticMesh)
			{
				Ref<StaticMesh> staticMesh = AssetManager::GetAsset<StaticMesh>(meshHandle);
				sourceHandle = staticMesh ? staticMesh->GetMeshSource() : AssetHandle{};
			}

			if (!sourceHandle || AssetManager::GetAssetType(sourceHandle) != AssetType::MeshSource)
				return 0;

			Ref<MeshSource> meshSource = AssetManager::GetAsset<MeshSource>(sourceHandle);
			return meshSource ? (uint32_t)meshSource->GetMaterials().size() : 0;
		}

		// What the renderer draws in a slot: a per-slot override, else a lone slot-0 entry (which
		// applies to every submesh), else the mesh's own material.
		AssetHandle GetEffectiveSlotMaterial(const StaticMeshComponent& component, uint32_t slot)
		{
			const Ref<MaterialTable>& table = component.MaterialTable;
			if (table && table->HasMaterial(slot))
				return table->GetMaterial(slot);
			if (table && table->GetMaterials().size() == 1 && table->HasMaterial(0))
				return table->GetMaterial(0);
			return GetMeshDefaultMaterial(component.StaticMesh, slot);
		}

		std::string GetAssetDisplayName(AssetHandle handle)
		{
			if (Ref<EditorAssetManager> editorAssetManager = Project::GetEditorAssetManager())
			{
				const AssetMetadata metadata = editorAssetManager->GetMetadata(handle);
				if (metadata.IsValid())
					return metadata.FilePath.stem().string();
			}
			return "Material";
		}

		void DeselectEntityEverywhere(UUID entityID)
		{
			constexpr std::array<SelectionContext, 4> contexts = {
				SelectionContext::Global,
				SelectionContext::Scene,
				SelectionContext::ContentBrowser,
				SelectionContext::PrefabEditor
			};

			for (SelectionContext context : contexts)
				SelectionManager::Deselect(context, entityID);
		}

		template<typename TComponent>
		void ResetComponentToDefault(TComponent& component)
		{
			component = TComponent{};
		}

		template<>
		void ResetComponentToDefault<TransformComponent>(TransformComponent& component)
		{
			component.Translation = glm::vec3(0.0f);
			component.SetRotationEuler(glm::vec3(0.0f));
			component.Scale = glm::vec3(1.0f);
		}

		template<>
		void ResetComponentToDefault<TextComponent>(TextComponent& component)
		{
			const AssetHandle defaultFont = Font::GetDefaultFont() ? Font::GetDefaultFont()->Handle : AssetHandle{};
			component = TextComponent{};
			component.FontHandle = defaultFont;
		}

		static uint32_t DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f)
		{
			ImGuiEx::ScopedID id(label.c_str());
			const float available = ImGui::GetContentRegionAvail().x;
			const float spacing = ImGui::GetStyle().ItemSpacing.x;
			const float buttonWidth = ImGui::GetFrameHeight();
			const float minimumAxis = buttonWidth + ImGui::GetFontSize() * 4.5f;
			const bool inlineLabel = available >= ImGui::GetFontSize() * 34.0f;
			const float labelWidth = inlineLabel ? ImGui::GetFontSize() * 8.0f : 0.0f;
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label.c_str());
			if (inlineLabel)
				ImGui::SameLine(labelWidth);
			const float axesWidth = inlineLabel ? available - labelWidth : available;
			const bool inlineAxes = axesWidth >= minimumAxis * 3.0f + spacing * 2.0f;
			const float axisWidth = inlineAxes ? (axesWidth - spacing * 2.0f) / 3.0f : axesWidth;
			constexpr const char* axes[] = {"X", "Y", "Z"};
			constexpr ImVec4 colors[] = {
				{0.8f, 0.1f, 0.15f, 1.0f}, {0.2f, 0.7f, 0.2f, 1.0f}, {0.1f, 0.25f, 0.8f, 1.0f}};
			uint32_t changedAxes = 0;
			for (int axis = 0; axis < 3; ++axis)
			{
				ImGuiEx::ScopedID axisID(axis);
				bool changed = false;
				if (axis && inlineAxes)
					ImGui::SameLine(0.0f, spacing);
				ImGui::BeginGroup();
				{
					ImGuiEx::ScopedColour button(ImGuiCol_Button, colors[axis]);
					if (ImGui::Button(axes[axis], {buttonWidth, 0.0f}))
					{
						values[axis] = resetValue;
						changed = true;
					}
					ImGui::SetItemTooltip("Reset %s to %.2f", axes[axis], resetValue);
				}
				ImGui::SameLine(0.0f, 0.0f);
				ImGui::SetNextItemWidth(std::max(1.0f, axisWidth - buttonWidth));
				changed |= ImGuiEx::DragFloat("##value", &values[axis], 0.1f, 0.0f, 0.0f, "%.2f");
				changed |=
					ImGuiEx::NumericControlMenu(values[axis], 0.0f, 0.0f, std::optional<float>(resetValue));
				ImGui::EndGroup();
				if (changed)
					changedAxes |= BIT(axis);
			}
			if (changedAxes)
				EditorStack::Get().MarkEditTargetEdited("Edit Transform");
			return changedAxes;
		}

		template<typename TComponent, typename Fn>
		void DrawComponentSection(const Ref<Scene>& scene, const std::vector<UUID>& entityIDs, const char* name, const Ref<Texture2D>& icon, Fn&& drawUI)
		{
			if (!scene || entityIDs.empty())
				return;

			for (UUID entityID : entityIDs)
			{
				Entity entity = scene->GetEntityByUUID(entityID);
				if (!entity || !entity.HasComponent<TComponent>())
					return;
			}

			Entity firstEntity = scene->GetEntityByUUID(entityIDs.front());
			TComponent& firstComponent = firstEntity.GetComponent<TComponent>();

			ImGui::PushID((void*)typeid(TComponent).hash_code());

			(void)icon; // texture section icons dropped for the flat concept look

			// Flat collapsible section: a clipped label + chevron drawn over an empty-label node,
			// with a faint hover/active wash — no framed header or Hazel texture icon.
			ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(255, 255, 255, 10));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(255, 255, 255, 16));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(255, 255, 255, 22));
			const ImGuiTreeNodeFlags sectionFlags = ImGuiTreeNodeFlags_SpanAvailWidth
				| ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_FramePadding;
			const bool open = ImGui::TreeNodeEx("##section", sectionFlags, "");
			ImGui::PopStyleColor(3);

			const ImRect sectionRect = ImGuiEx::GetItemRect();
			const ImVec2 afterHeader = ImGui::GetCursorScreenPos();
			const float lineHeight = ImGui::GetFrameHeight();
			const float settingsX = sectionRect.Max.x - lineHeight;
			const float labelRight = settingsX -
									 (ComponentDocTopic<TComponent>::Topic.empty() ? 0.0f : lineHeight) -
									 ImGui::GetStyle().ItemInnerSpacing.x;

			{
				const ImRect secRect = ImGuiEx::GetItemRect();
				ImGuiContext& g = *GImGui;
				float labelX = secRect.Min.x + g.Style.FramePadding.x + g.FontSize + g.Style.ItemInnerSpacing.x;
				bool enabled = true;

				// Enable checkbox between the chevron and the label; it overlaps the node (AllowOverlap),
				// so clicking it doesn't fold the section.
				if constexpr (HasEnabledFlag<TComponent>)
				{
					enabled = GetEnabledFlag(firstComponent);
					const bool mixed = IsSelectionInconsistent<bool>(scene, entityIDs, [](Entity entity)
					{
						return GetEnabledFlag(entity.GetComponent<TComponent>());
					});

					ImGui::SameLine();
					ImGui::SetCursorScreenPos(ImVec2(labelX, secRect.Min.y));
					{
						ImGuiEx::ScopedItemFlags mixedFlag(ImGuiItemFlags_MixedValue, mixed);
						if (ImGui::Checkbox("##ComponentEnabled", &enabled))
						{
							ApplyToSelection<TComponent>(scene, entityIDs, [enabled](TComponent& component, Entity)
							{
								GetEnabledFlag(component) = enabled;
							});
							EditorStack::Get().MarkEntitiesEdited(entityIDs, enabled ? "Enable Component" : "Disable Component");
						}
					}
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("%s", enabled ? "Enabled - untick to switch this component off" : "Disabled - tick to switch this component on");

					labelX = ImGui::GetItemRectMax().x + g.Style.ItemInnerSpacing.x;
				}

				const float labelY = secRect.Min.y + (secRect.GetHeight() - ImGui::GetTextLineHeight()) * 0.5f;
				ImGuiEx::ScopedColour title(ImGuiCol_Text, enabled ? Colors::Theme::textBrighter
																   : Colors::Theme::textDarker);
				ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), {labelX, labelY},
										  {std::max(labelX, labelRight), secRect.Max.y},
										  std::max(labelX, labelRight), name, nullptr, nullptr);
			}

			bool resetComponent = false;
			bool removeComponent = false;

			// The component's manual page, left of the settings gear.
			if constexpr (!ComponentDocTopic<TComponent>::Topic.empty())
			{
				ImGui::SetCursorScreenPos({settingsX - lineHeight, sectionRect.Min.y});
				const ImVec2 docsMin = ImGui::GetCursorScreenPos();
				ImGuiEx::DocsButton("##ComponentDocs", ComponentDocTopic<TComponent>::Topic, ComponentDocTopic<TComponent>::Title,
					ImRect(docsMin, ImVec2(docsMin.x + lineHeight, docsMin.y + lineHeight)));
			}

			ImGui::SetCursorScreenPos({settingsX, sectionRect.Min.y});
			if (ImGui::InvisibleButton("##ComponentSettings", ImVec2{lineHeight, lineHeight},
									   ImGuiButtonFlags_EnableNav))
				ImGui::OpenPopup("ComponentSettings");

			ImGuiEx::DrawButtonImage(EditorResources::GearIcon,
				IM_COL32(160, 160, 160, 200),
				IM_COL32(160, 160, 160, 255),
				IM_COL32(160, 160, 160, 150),
				ImGuiEx::GetItemRect());

			ImGui::SetItemTooltip("%s settings", name);
			ImGui::SetCursorScreenPos(afterHeader);
			ImGui::Dummy({0.0f, 0.0f});

			if (ImGui::BeginPopup("ComponentSettings"))
			{
				if (ImGui::MenuItem("Reset"))
					resetComponent = true;

				if constexpr (!std::is_same_v<TComponent, TransformComponent>)
				{
					if (ImGui::MenuItem("Remove component"))
						removeComponent = true;
				}
				else
				{
					ImGui::MenuItem("Remove component", nullptr, false, false);
				}

				ImGui::EndPopup();
			}

			if (open)
			{
				drawUI(firstComponent, entityIDs, entityIDs.size() > 1);
				ImGui::TreePop();
			}

			if (resetComponent)
			{
				ApplyToSelection<TComponent>(scene, entityIDs, [](TComponent& component, Entity)
				{
					ResetComponentToDefault(component);
				});
			}

			if (removeComponent)
			{
				for (UUID entityID : entityIDs)
				{
					Entity entity = scene->GetEntityByUUID(entityID);
					if (entity && entity.HasComponent<TComponent>())
						entity.RemoveComponent<TComponent>();
				}

				EditorStack::Get().MarkSceneEdited("Remove Component");
			}

			ImGui::PopID();
		}

	} // namespace

	SceneHierarchyPanel::SceneHierarchyPanel(const Ref<Scene>& context, SelectionContext selectionContext, bool isWindow)
		: m_SelectionContext(selectionContext), m_IsWindow(isWindow)
	{
		SetContext(context);
	}

	void SceneHierarchyPanel::SetContext(const Ref<Scene>& context)
	{
		SelectionManager::DeselectAll(m_SelectionContext);
		m_Context = context;
		m_SearchString.clear();
		m_ComponentSearch.clear();
		m_ActivateSearchWidget = false;
		m_IsHierarchyFocused = false;
		m_IsPropertiesFocused = false;
		m_QueuedEntityDeletions.clear();
	}

	Entity SceneHierarchyPanel::GetSelectedEntity() const
	{
		if (!m_Context || SelectionManager::GetSelectionCount(m_SelectionContext) == 0)
			return {};

		return m_Context->GetEntityByUUID(SelectionManager::GetSelection(m_SelectionContext, 0));
	}

	std::vector<Entity> SceneHierarchyPanel::GetSelectedEntities() const
	{
		std::vector<Entity> entities;
		if (!m_Context)
			return entities;

		const auto& selections = SelectionManager::GetSelections(m_SelectionContext);
		entities.reserve(selections.size());

		for (UUID entityID : selections)
		{
			Entity entity = m_Context->GetEntityByUUID(entityID);
			if (entity)
				entities.push_back(entity);
		}

		return entities;
	}

	void SceneHierarchyPanel::SetSelectedEntity(Entity entity)
	{
		SelectionManager::DeselectAll(m_SelectionContext);
		if (entity)
			SelectionManager::Select(m_SelectionContext, entity.GetUUID());
	}

	void SceneHierarchyPanel::PruneInvalidSelection()
	{
		if (!m_Context)
		{
			SelectionManager::DeselectAll(m_SelectionContext);
			return;
		}

		std::vector<UUID> invalidSelections;
		for (UUID entityID : SelectionManager::GetSelections(m_SelectionContext))
		{
			if (!m_Context->GetEntityByUUID(entityID))
				invalidSelections.push_back(entityID);
		}

		for (UUID entityID : invalidSelections)
			SelectionManager::Deselect(m_SelectionContext, entityID);
	}

	void SceneHierarchyPanel::QueueEntityDeletion(const std::vector<UUID>& entityIDs)
	{
		if (!m_Context)
			return;

		for (UUID entityID : entityIDs)
		{
			if (std::find(m_QueuedEntityDeletions.begin(), m_QueuedEntityDeletions.end(), entityID) != m_QueuedEntityDeletions.end())
				continue;

			m_QueuedEntityDeletions.emplace_back(entityID);
		}
	}

	void SceneHierarchyPanel::FlushQueuedEntityDeletions()
	{
		if (!m_Context || m_QueuedEntityDeletions.empty())
			return;

		std::vector<UUID> queuedDeletions = std::move(m_QueuedEntityDeletions);
		m_QueuedEntityDeletions.clear();
		auto sortUUIDs = [](std::vector<UUID>& ids)
		{
			std::sort(ids.begin(), ids.end(), [](UUID lhs, UUID rhs)
			{
				return (uint64_t)lhs < (uint64_t)rhs;
			});
		};

		sortUUIDs(queuedDeletions);
		queuedDeletions.erase(std::unique(queuedDeletions.begin(), queuedDeletions.end()), queuedDeletions.end());

		std::vector<UUID> entitiesToDeselect;
		for (UUID entityID : queuedDeletions)
		{
			Entity entity = m_Context->GetEntityByUUID(entityID);
			if (!entity)
				continue;

			entitiesToDeselect.emplace_back(entityID);

			std::vector<UUID> childIDs = m_Context->GetAllChildren(entity);
			entitiesToDeselect.insert(entitiesToDeselect.end(), childIDs.begin(), childIDs.end());
		}

		sortUUIDs(entitiesToDeselect);
		entitiesToDeselect.erase(std::unique(entitiesToDeselect.begin(), entitiesToDeselect.end()), entitiesToDeselect.end());

		for (UUID entityID : entitiesToDeselect)
			DeselectEntityEverywhere(entityID);

		for (UUID entityID : queuedDeletions)
		{
			Entity entity = m_Context->GetEntityByUUID(entityID);
			if (entity)
			{
				Scene::RecordPrefabRemoval(entity); // a prefab child deleted from an instance stays deleted
				m_Context->DestroyEntity(entity);
			}
		}

		if (!queuedDeletions.empty())
			EditorStack::Get().MarkSceneEdited("Delete Entity");

		PruneInvalidSelection();
	}

	void SceneHierarchyPanel::OnImGuiRender(bool& isOpen)
	{
		m_IsHierarchyFocused = false;
		if (!isOpen)
			return;

		if (m_IsWindow)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
			if (!ImGui::Begin("Scene Hierarchy", &isOpen))
			{
				ImGui::End();
				ImGui::PopStyleVar();
				return;
			}
			ImGui::PopStyleVar();
		}

		s_ActiveSelectionContext = m_SelectionContext;
		PruneInvalidSelection();

		m_IsHierarchyFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		const ImGuiEx::PanelAction actions[] = {
			{0, "Create", LUX_ICON_PLUS, nullptr, m_Context != nullptr, false, true}};
		if (ImGuiEx::PanelActionBar("##hierarchy_actions", actions) == 0)
			ImGui::OpenPopup("CreateEntity");
		if (ImGui::BeginPopup("CreateEntity"))
		{
			DrawEntityCreateMenu({});
			ImGui::EndPopup();
		}
		if (m_ActivateSearchWidget)
		{
			ImGui::SetKeyboardFocusHere();
			m_ActivateSearchWidget = false;
		}
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##hierarchy_search", "Search entities... (Ctrl+F)", &m_SearchString);
		ImGui::Separator();
		ImGui::BeginChild("##hierarchy_entities");
		m_VisibleEntityCount = 0;

		if (m_Context)
		{
			m_Context->m_Registry.each([&](auto entityID)
			{
				Entity entity{ entityID, m_Context.get() };
				if (!entity.HasComponent<RelationshipComponent>())
				{
					DrawEntityNode(entity, m_SearchString);
					return;
				}

				const auto& relationship = entity.GetComponent<RelationshipComponent>();
				const bool hasValidParent = relationship.ParentHandle != 0 && (bool)m_Context->GetEntityByUUID(relationship.ParentHandle);
				if (!hasValidParent)
					DrawEntityNode(entity, m_SearchString);
			});

			const ImRect hierarchyRect(ImGui::GetWindowPos(), ImGui::GetWindowPos() + ImGui::GetWindowSize());
			if (ImGui::BeginDragDropTargetCustom(hierarchyRect, ImGui::GetID("SceneHierarchyRootTarget")))
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_HIERARCHY_ENTITY"))
				{
					const size_t entityCount = payload->DataSize / sizeof(UUID);
					const UUID* entityIDs = (const UUID*)payload->Data;
					for (size_t i = 0; i < entityCount; i++)
					{
						Entity draggedEntity = m_Context->GetEntityByUUID(entityIDs[i]);
						if (draggedEntity)
						{
							Scene::RecordPrefabReparent(draggedEntity, {});
							draggedEntity.SetParent({});
						}
					}
					EditorStack::Get().MarkSceneEdited("Reparent");
				}

				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
				{
					const AssetHandle handle = *(const AssetHandle*)payload->Data;
					if (AssetManager::GetAssetType(handle) == AssetType::Prefab)
					{
						Ref<Prefab> prefab = AssetManager::GetAsset<Prefab>(handle);
						if (prefab)
						{
							SetSelectedEntity(m_Context->InstantiatePrefab(prefab));
							EditorStack::Get().MarkSceneEdited("Add Prefab");
						}
					}
				}

				ImGui::EndDragDropTarget();
			}
		}

		if (!m_VisibleEntityCount)
			ImGuiEx::PanelEmptyState(
				"##hierarchy_empty", m_SearchString.empty() ? "No entities" : "No matching entities",
				m_Context ? (m_SearchString.empty() ? "Use Create to add an entity."
													: "Change the search to see other entities.")
						  : "Open a scene to browse its entities.");

		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)
			&& ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)
			&& !ImGui::IsAnyItemHovered())
		{
			SelectionManager::DeselectAll(m_SelectionContext);
		}

		if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_NoOpenOverItems))
		{
			DrawEntityCreateMenu({});
			ImGui::EndPopup();
		}

		ImGui::EndChild();

		if (m_IsWindow)
			ImGui::End();

		FlushQueuedEntityDeletions();
	}

	void SceneHierarchyPanel::OnPropertiesImGuiRender(bool& isOpen)
	{
		m_IsPropertiesFocused = false;
		PruneInvalidSelection();
		FlushQueuedEntityDeletions();
		s_ActiveSelectionContext = m_SelectionContext;
		if (ImGui::Begin("Properties", &isOpen))
		{
			m_IsPropertiesFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
			DrawComponents(SelectionManager::GetSelections(m_SelectionContext));
		}
		ImGui::End();
	}

	void SceneHierarchyPanel::OnEvent(Event& e)
	{
		if (!m_IsHierarchyFocused)
			return;
		OnPropertiesEvent(e);
	}

	void SceneHierarchyPanel::OnPropertiesEvent(Event& e)
	{
		if (!m_IsHierarchyFocused && !m_IsPropertiesFocused)
			return;

		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<KeyPressedEvent>(
			[this](KeyPressedEvent& event)
			{
				if (ImGui::GetIO().WantTextInput || GImGui->OpenPopupStack.Size > 0)
					return true;
				if (event.GetRepeatCount() != 0)
					return false;
				switch (event.GetKeyCode())
				{
				case Key::F:
					if (!m_IsHierarchyFocused || !ImGui::GetIO().KeyCtrl)
						return false;
					m_ActivateSearchWidget = true;
					return true;
				case Key::Escape:
					SelectionManager::DeselectAll(m_SelectionContext);
					return true;
				default:
					return false;
				}
			});
	}

	void SceneHierarchyPanel::DrawEntityCreateMenu(Entity parent)
	{
		if (m_Context == nullptr)
			return;

		auto createEntity = [this, parent](const char* name)
		{
			Entity entity = m_Context->CreateEntity(name);
			if (parent)
				entity.SetParent(parent);

			SetSelectedEntity(entity);
			EditorStack::Get().MarkSceneEdited("Create Entity");   // covers every create-menu item
			return entity;
		};

		auto createStaticMeshEntity = [&createEntity](const char* name, AssetHandle meshHandle)
		{
			Entity entity = createEntity(name);
			auto& staticMesh = entity.AddComponent<StaticMeshComponent>();
			staticMesh.StaticMesh = meshHandle;
			return entity;
		};

		if (ImGui::MenuItem("Create Empty Entity"))
			createEntity("Empty Entity");

		if (ImGui::MenuItem("Create Folder"))
		{
			Entity folder = createEntity("Folder");
			folder.AddComponent<FolderComponent>();
		}

		if (ImGui::BeginMenu("Create Effects"))
		{
			if (ImGui::MenuItem("Particle System"))
			{
				Entity entity = createEntity("Particle System");
				entity.AddComponent<ParticleSystemComponent>();
			}

			if (ImGui::MenuItem("Particle System (2D)"))
			{
				Entity entity = createEntity("Particle System");
				auto& particles = entity.AddComponent<ParticleSystemComponent>();
				particles.Settings.Mode = Particles::RenderMode::Sprite2D;
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Create 2D"))
		{
			if (ImGui::MenuItem("Sprite"))
			{
				Entity entity = createEntity("Sprite");
				entity.AddComponent<SpriteRendererComponent>();
			}

			if (ImGui::MenuItem("Animated Sprite"))
			{
				Entity entity = createEntity("Animated Sprite");
				entity.AddComponent<SpriteRendererComponent>();
				entity.AddComponent<SpriteAnimatorComponent>();
			}

			if (ImGui::MenuItem("Tilemap"))
			{
				// Static body: the solid tiles become its collision shapes.
				Entity entity = createEntity("Tilemap");
				entity.AddComponent<TilemapComponent>();
				entity.AddComponent<RigidBody2DComponent>();
			}

			if (ImGui::MenuItem("Circle"))
			{
				Entity entity = createEntity("Circle");
				entity.AddComponent<CircleRendererComponent>();
			}

			if (ImGui::MenuItem("Text"))
			{
				Entity entity = createEntity("Text");
				auto& text = entity.AddComponent<TextComponent>();
				if (Ref<Font> defaultFont = Font::GetDefaultFont())
					text.FontHandle = defaultFont->Handle;
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Create UI"))
		{
			// A UI element goes under the selected canvas or element; with none selected it gets a new canvas.
			auto uiParent = [&]() -> Entity
			{
				if (parent && (parent.HasComponent<UICanvasComponent>() || parent.HasComponent<UIElementComponent>()))
					return parent;
				Entity canvas = m_Context->CreateEntity("Canvas");
				canvas.AddComponent<UICanvasComponent>();
				if (parent)
					canvas.SetParent(parent);
				return canvas;
			};
			auto createElement = [&](const char* name)
			{
				Entity owner = uiParent();
				Entity entity = m_Context->CreateEntity(name);
				entity.SetParent(owner);
				entity.AddComponent<UIElementComponent>();
				SetSelectedEntity(entity);
				EditorStack::Get().MarkSceneEdited("Create Entity");
				return entity;
			};

			if (ImGui::MenuItem("Canvas"))
			{
				Entity entity = createEntity("Canvas");
				entity.AddComponent<UICanvasComponent>();
			}

			if (ImGui::MenuItem("Panel"))
			{
				Entity entity = createElement("Panel");
				entity.AddComponent<UIImageComponent>().Color = { 0.1f, 0.1f, 0.1f, 0.8f };
				entity.GetComponent<UIElementComponent>().Rect = { { 0.5f, 0.5f }, { 0.5f, 0.5f }, { -200.0f, -150.0f }, { 200.0f, 150.0f } };
			}

			// A widget: a rectangle with its look (image, text) and behaviour. Child text is added where the widget has a label.
			const auto createWidget = [&](const char* name, UIWidgetKind kind, const glm::vec2& halfSize, const glm::vec4& color, const char* label)
			{
				Entity entity = createElement(name);
				entity.GetComponent<UIElementComponent>().Rect = { { 0.5f, 0.5f }, { 0.5f, 0.5f }, -halfSize, halfSize };
				entity.AddComponent<UIImageComponent>().Color = color;
				entity.AddComponent<UIWidgetComponent>().Kind = kind;
				if (label)
				{
					auto& text = entity.AddComponent<UITextComponent>();
					text.Text = label;
					text.AlignH = UITextAlignH::Center;
					text.AlignV = UITextAlignV::Middle;
					text.Wrap = false;
				}
				return entity;
			};

			if (ImGui::MenuItem("Button"))
				createWidget("Button", UIWidgetKind::Button, { 150.0f, 30.0f }, { 0.2f, 0.3f, 0.5f, 1.0f }, "Button");

			if (ImGui::MenuItem("Toggle"))
				createWidget("Toggle", UIWidgetKind::Toggle, { 20.0f, 20.0f }, { 0.15f, 0.15f, 0.2f, 1.0f }, nullptr);

			if (ImGui::MenuItem("Slider"))
			{
				Entity entity = createWidget("Slider", UIWidgetKind::Slider, { 150.0f, 16.0f }, { 0.15f, 0.15f, 0.2f, 1.0f }, nullptr);
				entity.GetComponent<UIWidgetComponent>().Value = 0.5f;
			}

			if (ImGui::MenuItem("Progress Bar"))
			{
				Entity entity = createWidget("Progress Bar", UIWidgetKind::ProgressBar, { 150.0f, 12.0f }, { 0.15f, 0.15f, 0.2f, 1.0f }, nullptr);
				entity.GetComponent<UIWidgetComponent>().Value = 0.5f;
			}

			if (ImGui::MenuItem("Text Input"))
			{
				Entity entity = createWidget("Text Input", UIWidgetKind::TextInput, { 150.0f, 20.0f }, { 0.12f, 0.12f, 0.16f, 1.0f }, "");
				entity.GetComponent<UIWidgetComponent>().Placeholder = "Type here";
				auto& text = entity.GetComponent<UITextComponent>();
				text.AlignH = UITextAlignH::Left;
				text.Size = 24.0f;
				entity.GetComponent<UIElementComponent>().Rect.OffsetMin = { -150.0f, -20.0f };
			}

			if (ImGui::MenuItem("Scroll List"))
			{
				Entity entity = createWidget("Scroll List", UIWidgetKind::ScrollList, { 150.0f, 100.0f }, { 0.08f, 0.08f, 0.1f, 0.9f }, nullptr);
				auto& layout = entity.GetComponent<UIElementComponent>().Layout;
				layout.Kind = UI::LayoutKind::Vertical;
				layout.Spacing = 6.0f;
				layout.Padding = { 6.0f, 6.0f, 6.0f, 6.0f };
			}

			if (ImGui::MenuItem("Text"))
			{
				Entity entity = createElement("Text");
				entity.AddComponent<UITextComponent>().Text = "Text";
				entity.GetComponent<UIElementComponent>().Rect = { { 0.5f, 0.5f }, { 0.5f, 0.5f }, { -150.0f, -25.0f }, { 150.0f, 25.0f } };
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Create 3D"))
		{
			if (ImGui::BeginMenu("Meshes"))
			{
				auto defaultMeshMenuItem = [&createStaticMeshEntity](const char* name, const char* filename)
				{
					AssetHandle meshHandle = GetDefaultMeshSourceHandle(filename);
					if (ImGui::MenuItem(name, nullptr, false, meshHandle != 0))
						createStaticMeshEntity(name, meshHandle);
				};

				defaultMeshMenuItem("Capsule", "Capsule.gltf");
				defaultMeshMenuItem("Cone", "Cone.gltf");
				defaultMeshMenuItem("Cube", "Cube.gltf");
				defaultMeshMenuItem("Cylinder", "Cylinder.gltf");
				defaultMeshMenuItem("Plane", "Plane.gltf");
				defaultMeshMenuItem("Sphere", "Sphere.gltf");
				defaultMeshMenuItem("Torus", "Torus.gltf");

				ImGui::Separator();

				if (ImGui::MenuItem("Empty Static Mesh"))
				{
					Entity entity = createEntity("Static Mesh");
					entity.AddComponent<StaticMeshComponent>();
				}

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Lights"))
			{
				if (ImGui::MenuItem("Directional Light"))
				{
					Entity entity = createEntity("Directional Light");
					entity.AddComponent<DirectionalLightComponent>();
				}

				if (ImGui::MenuItem("Point Light"))
				{
					Entity entity = createEntity("Point Light");
					entity.AddComponent<PointLightComponent>();
				}

				if (ImGui::MenuItem("Spot Light"))
				{
					Entity entity = createEntity("Spot Light");
					entity.AddComponent<SpotLightComponent>();
				}

				if (ImGui::MenuItem("Area Light"))
				{
					Entity entity = createEntity("Area Light");
					entity.AddComponent<AreaLightComponent>();
				}

				if (ImGui::MenuItem("Sky Light"))
				{
					Entity entity = createEntity("Sky Light");
					entity.AddComponent<SkyLightComponent>();
				}

				ImGui::EndMenu();
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Create Utility"))
		{
			if (ImGui::MenuItem("Camera"))
			{
				Entity entity = createEntity("Camera");
				entity.AddComponent<CameraComponent>();
			}

			if (ImGui::MenuItem("Audio Source"))
			{
				Entity entity = createEntity("Audio Source");
				entity.AddComponent<AudioSourceComponent>();
			}

			if (ImGui::MenuItem("Audio Listener"))
			{
				Entity entity = createEntity("Audio Listener");
				entity.AddComponent<AudioListenerComponent>();
			}

			ImGui::EndMenu();
		}
	}

	bool SceneHierarchyPanel::TagSearchRecursive(Entity entity, std::string_view searchFilter, uint32_t maxSearchDepth, uint32_t currentDepth)
	{
		if (!entity || searchFilter.empty() || currentDepth > maxSearchDepth)
			return false;

		const auto& relationship = entity.GetComponent<RelationshipComponent>();
		for (UUID childID : relationship.Children)
		{
			Entity childEntity = m_Context->GetEntityByUUID(childID);
			if (!childEntity || !childEntity.HasComponent<TagComponent>())
				continue;

			if (ImGuiEx::IsMatchingSearch(childEntity.GetComponent<TagComponent>().Tag, searchFilter))
				return true;

			if (TagSearchRecursive(childEntity, searchFilter, maxSearchDepth, currentDepth + 1))
				return true;
		}

		return false;
	}

	void SceneHierarchyPanel::DrawEntityNode(Entity entity, const std::string& searchFilter)
	{
		auto& tagComponent = entity.GetComponent<TagComponent>();
		auto& tag = tagComponent.Tag;
		const bool locked = tagComponent.Locked;
		const auto& relationship = entity.GetComponent<RelationshipComponent>();
		const bool hasChildren = !relationship.Children.empty();
		const bool hasChildMatchingSearch =
			TagSearchRecursive(entity, searchFilter,
							   static_cast<uint32_t>(m_Context->GetAllEntitiesWith<TagComponent>().size()));
		const bool isSelected = SelectionManager::IsSelected(s_ActiveSelectionContext, entity.GetUUID());

		if (!ImGuiEx::IsMatchingSearch(tag, searchFilter) && !hasChildMatchingSearch)
			return;

		++m_VisibleEntityCount;
		ImGuiTreeNodeFlags flags = (isSelected ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
		flags |= ImGuiTreeNodeFlags_SpanAvailWidth;
		if (hasChildMatchingSearch)
			ImGui::SetNextItemOpen(true, ImGuiCond_Always);
		if (!hasChildren)
			flags |= ImGuiTreeNodeFlags_Leaf;

		// Lime selection fill + a faint hover highlight, via the tree-node header colours.
		ImGui::PushStyleColor(ImGuiCol_Header, Colors::Theme::selectionMuted);
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(255, 255, 255, 12));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, Colors::Theme::selectionMuted);

		// Empty label: the type icon and name are drawn manually below so the icon keeps its own
		// category tint and the name turns accent-lime when selected.
		const bool opened = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, flags, "");
		ImGui::PopStyleColor(3);

		const bool rowHovered = ImGui::IsItemHovered();
		const bool showTooltip = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
		const ImRect rowRect = ImGuiEx::GetItemRect();
		ImGuiContext& g = *GImGui;

		// Visibility toggle (mesh entities only): an eye at the right edge, revealed on hover or
		// when hidden. Handled through the node's own click, guarded by the eye rect, so it needs
		// no overlapping item (which would steal the node's drag-drop binding).
		const bool hasVisToggle = entity.HasComponent<StaticMeshComponent>();
		bool meshVisible = true;
		ImRect eyeRect;
		bool overEye = false;
		if (hasVisToggle)
		{
			meshVisible = entity.GetComponent<StaticMeshComponent>().Visible;
			const float eyeW = g.FontSize + 6.0f;
			eyeRect = ImRect(ImVec2(rowRect.Max.x - eyeW, rowRect.Min.y), ImVec2(rowRect.Max.x, rowRect.Max.y));
			overEye = rowHovered && eyeRect.Contains(ImGui::GetMousePos());
		}

		// Type icon + name, vertically centred over the node row.
		{
			EntityIconInfo iconInfo = GetEntityIcon(entity);
			// A folder shows an open glyph while expanded.
			if (entity.HasComponent<FolderComponent>() && opened && hasChildren)
				iconInfo.Glyph = LUX_ICON_FOLDER_OPEN;
			const float arrowWidth = g.FontSize;
			const float iconX = rowRect.Min.x + g.Style.FramePadding.x + arrowWidth + g.Style.ItemInnerSpacing.x;
			const float centerY = rowRect.Min.y + rowRect.GetHeight() * 0.5f;
			ImDrawList* dl = ImGui::GetWindowDrawList();

			// Colour-label stripe at the very left edge of the row.
			if (tagComponent.LabelColor != 0)
				dl->AddRectFilled(ImVec2(rowRect.Min.x, rowRect.Min.y + 1.0f),
					ImVec2(rowRect.Min.x + 3.0f, rowRect.Max.y - 1.0f), tagComponent.LabelColor, 1.0f);

			const ImU32 iconColor = isSelected ? Colors::Theme::accent : iconInfo.Color;
			const ImVec2 iconSize = ImGui::CalcTextSize(iconInfo.Glyph);
			dl->AddText(ImVec2(iconX, centerY - iconSize.y * 0.5f), iconColor, iconInfo.Glyph);

			// Right-side reservations: the visibility eye (if any), then a lock badge (if locked).
			float contentRight = hasVisToggle ? eyeRect.Min.x - 4.0f : rowRect.Max.x - 4.0f;
			if (locked)
			{
				const ImVec2 lockSize = ImGui::CalcTextSize(LUX_ICON_LOCK);
				dl->AddText(ImVec2(contentRight - lockSize.x, centerY - lockSize.y * 0.5f), Colors::Theme::textDarker, LUX_ICON_LOCK);
				contentRight -= lockSize.x + 4.0f;
			}

			const float nameX = iconX + iconSize.x + g.Style.ItemInnerSpacing.x + 2.0f;
			const float nameRight = contentRight;
			const ImU32 baseName = isSelected ? Colors::Theme::accent : Colors::Theme::text;
			const bool inactive = !m_Context->IsEntityActiveInHierarchy(entity);
			const ImU32 nameColor = (locked || inactive || (hasVisToggle && !meshVisible)) ? Colors::Theme::textDarker : baseName;
			const ImVec2 nameSize = ImGui::CalcTextSize(tag.c_str());
			dl->PushClipRect(ImVec2(nameX, rowRect.Min.y), ImVec2(std::max(nameX, nameRight), rowRect.Max.y), true);
			dl->AddText(ImVec2(nameX, centerY - nameSize.y * 0.5f), nameColor, tag.c_str());
			dl->PopClipRect();

			if (hasVisToggle && (rowHovered || !meshVisible))
			{
				const char* eyeGlyph = meshVisible ? LUX_ICON_EYE : LUX_ICON_EYE_SLASH;
				const ImVec2 eyeSize = ImGui::CalcTextSize(eyeGlyph);
				const ImU32 eyeColor = overEye ? Colors::Theme::textBrighter : Colors::Theme::textDarker;
				dl->AddText(ImVec2(eyeRect.GetCenter().x - eyeSize.x * 0.5f, centerY - eyeSize.y * 0.5f), eyeColor, eyeGlyph);
			}
		}

		if (ImGui::IsItemClicked())
		{
			if (overEye)
			{
				entity.GetComponent<StaticMeshComponent>().Visible = !meshVisible;
				EditorStack::Get().MarkEntitiesEdited({ entity.GetUUID() }, meshVisible ? "Hide Mesh" : "Show Mesh");
			}
			else
			{
				const bool ctrlDown = Input::IsKeyDown(Key::LeftControl) || Input::IsKeyDown(Key::RightControl);
				if (ctrlDown)
				{
					if (isSelected)
						SelectionManager::Deselect(s_ActiveSelectionContext, entity.GetUUID());
					else
						SelectionManager::Select(s_ActiveSelectionContext, entity.GetUUID());
				}
				else
				{
					if (!isSelected || SelectionManager::GetSelectionCount(s_ActiveSelectionContext) > 1)
					{
						SelectionManager::DeselectAll(s_ActiveSelectionContext);
						SelectionManager::Select(s_ActiveSelectionContext, entity.GetUUID());
					}
				}
			}
		}

		if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !isSelected)
		{
			SelectionManager::DeselectAll(s_ActiveSelectionContext);
			SelectionManager::Select(s_ActiveSelectionContext, entity.GetUUID());
		}

		if (!locked && ImGui::BeginDragDropSource())
		{
			static std::vector<UUID> draggedEntityIDs;
			const auto& selectedEntities = SelectionManager::GetSelections(s_ActiveSelectionContext);

			if (isSelected && selectedEntities.size() > 1)
				draggedEntityIDs = selectedEntities;
			else
				draggedEntityIDs = { entity.GetUUID() };

			ImGui::SetDragDropPayload("SCENE_HIERARCHY_ENTITY", draggedEntityIDs.data(), draggedEntityIDs.size() * sizeof(UUID));
			ImGui::TextUnformatted(tag.c_str());
			ImGui::EndDragDropSource();
		}

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_HIERARCHY_ENTITY"))
			{
				const size_t entityCount = payload->DataSize / sizeof(UUID);
				const UUID* entityIDs = (const UUID*)payload->Data;
				for (size_t i = 0; i < entityCount; i++)
				{
					Entity draggedEntity = m_Context->GetEntityByUUID(entityIDs[i]);
					if (draggedEntity && draggedEntity != entity)
					{
						Scene::RecordPrefabReparent(draggedEntity, entity);
						draggedEntity.SetParent(entity);
					}
				}
				EditorStack::Get().MarkSceneEdited("Reparent");
			}

			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
			{
				const AssetHandle handle = *(const AssetHandle*)payload->Data;
				if (AssetManager::GetAssetType(handle) == AssetType::Prefab)
				{
					Ref<Prefab> prefab = AssetManager::GetAsset<Prefab>(handle);
					if (prefab)
					{
						Entity instantiated = m_Context->InstantiatePrefab(prefab);
						if (instantiated)
						{
							instantiated.SetParent(entity);
							SetSelectedEntity(instantiated);
							EditorStack::Get().MarkSceneEdited("Add Prefab");
						}
					}
				}
			}

			ImGui::EndDragDropTarget();
		}

		bool deleteRequested = false;
		if (ImGui::BeginPopupContextItem())
		{
			DrawEntityCreateMenu(entity);
			ImGui::Separator();

			if (ImGui::MenuItem(locked ? LUX_ICON_UNLOCK "  Unlock" : LUX_ICON_LOCK "  Lock"))
			{
				tagComponent.Locked = !locked;
				EditorStack::Get().MarkSceneEdited(tagComponent.Locked ? "Lock Entity" : "Unlock Entity");
			}

			if (ImGui::BeginMenu(LUX_ICON_TAG "  Label Color"))
			{
				// A small fixed palette (packed RGBA) plus a clear option.
				static const uint32_t kLabelColors[] = {
					IM_COL32(232, 84, 84, 255),   // red
					IM_COL32(232, 148, 64, 255),  // orange
					IM_COL32(230, 202, 72, 255),  // yellow
					IM_COL32(120, 200, 96, 255),  // green
					IM_COL32(84, 158, 232, 255),  // blue
					IM_COL32(168, 120, 224, 255), // purple
				};
				for (uint32_t color : kLabelColors)
				{
					ImGui::PushID(reinterpret_cast<void*>(static_cast<uintptr_t>(color)));
					if (ImGui::ColorButton("##label", ImGui::ColorConvertU32ToFloat4(color), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(18.0f, 18.0f)))
					{
						tagComponent.LabelColor = color;
						EditorStack::Get().MarkSceneEdited("Set Label Color");
						ImGui::CloseCurrentPopup();
					}
					ImGui::SameLine();
					ImGui::PopID();
				}
				ImGui::NewLine();
				if (ImGui::MenuItem("None", nullptr, false, tagComponent.LabelColor != 0))
				{
					tagComponent.LabelColor = 0;
					EditorStack::Get().MarkSceneEdited("Clear Label Color");
				}
				ImGui::EndMenu();
			}

			ImGui::Separator();

			const size_t selectionCount = SelectionManager::GetSelectionCount(s_ActiveSelectionContext);
			const char* deleteLabel = (isSelected && selectionCount > 1) ? "Delete Selected Entities" : "Delete Entity";
			// Locked entities are undeletable from here (a multi-selection still deletes the unlocked ones).
			if (ImGui::MenuItem(deleteLabel, nullptr, false, !locked))
				deleteRequested = true;

			ImGui::EndPopup();
		}

		// Tooltip submission replaces LastItemData. Finish row selection, drag/drop and context actions
		// first.
		if (showTooltip && ImGui::BeginTooltip())
		{
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
			ImGui::TextUnformatted(tag.c_str());
			ImGui::PopTextWrapPos();
			ImGui::Text("Entity %llu", static_cast<unsigned long long>(entity.GetUUID()));
			ImGui::EndTooltip();
		}

		if (opened)
		{
			const std::vector<UUID> childIDs = relationship.Children;
			for (UUID childID : childIDs)
			{
				Entity childEntity = m_Context->GetEntityByUUID(childID);
				if (childEntity)
					DrawEntityNode(childEntity, searchFilter);
			}

			ImGui::TreePop();
		}

		if (deleteRequested)
		{
			std::vector<UUID> entitiesToDelete;
			if (isSelected && SelectionManager::GetSelectionCount(s_ActiveSelectionContext) > 1)
				entitiesToDelete = SelectionManager::GetSelections(s_ActiveSelectionContext);
			else
				entitiesToDelete = { entity.GetUUID() };

			// Never delete a locked entity, even as part of a multi-selection.
			std::erase_if(entitiesToDelete, [this](UUID id)
			{
				Entity e = m_Context->GetEntityByUUID(id);
				return e && e.GetComponent<TagComponent>().Locked;
			});

			if (!entitiesToDelete.empty())
				QueueEntityDeletion(entitiesToDelete);
		}
	}

	void SceneHierarchyPanel::DrawAudioParameterOverrides(AudioSourceComponent& component)
	{
		if (!ImGuiEx::PropertyGridHeader("Parameter Overrides", false))
			return;

		ImGui::TextDisabled("Applied once when the event instance is created.\nNames must match parameters authored on the event.");

		int removeAt = -1;
		for (int i = 0; i < (int)component.ParameterOverrides.size(); i++)
		{
			auto& [name, value] = component.ParameterOverrides[(size_t)i];

			ImGui::PushID(i);

			ImGui::SetNextItemWidth(150.0f);
			ImGui::InputTextWithHint("##name", "Parameter", &name);

			ImGui::SameLine();
			ImGui::SetNextItemWidth(110.0f);
			ImGui::DragFloat("##value", &value, 0.01f);

			ImGui::SameLine();
			if (ImGui::SmallButton("Remove"))
				removeAt = i;

			ImGui::PopID();
		}

		if (removeAt >= 0)
			component.ParameterOverrides.erase(component.ParameterOverrides.begin() + removeAt);

		if (ImGui::Button("Add Parameter"))
			component.ParameterOverrides.emplace_back(std::string{}, 0.0f);

		ImGui::TreePop();
	}

	void SceneHierarchyPanel::DrawStaticMeshMaterialSlots(StaticMeshComponent& component, const std::vector<UUID>& selectedEntities)
	{
		// Per-slot overrides are edited for one entity at a time: selected entities may use
		// different meshes with different slot counts.
		const uint32_t slotCount = GetMeshMaterialSlotCount(component.StaticMesh);
		if (selectedEntities.size() == 1 && slotCount > 1 && ImGuiEx::PropertyGridHeader(std::format("Material Slots ({})", slotCount), false))
		{
			ImGuiEx::BeginPropertyGrid();
			for (uint32_t slot = 0; slot < slotCount; ++slot)
			{
				ImGuiEx::ScopedID slotID((int)slot);
				AssetHandle materialHandle = GetEffectiveSlotMaterial(component, slot);
				if (!ImGuiEx::PropertyAssetReference<MaterialAsset>(std::format("Slot {}", slot).c_str(), materialHandle, "Material for this submesh slot. Clearing restores the mesh's own material."))
					continue;

				// Write every slot explicitly: a table holding only a slot-0 entry means "this material
				// on every submesh", so a partial table would repaint the whole mesh. All slots are
				// resolved before any write, since the resolution depends on the table's shape.
				std::vector<AssetHandle> effective(slotCount);
				for (uint32_t fill = 0; fill < slotCount; ++fill)
					effective[fill] = GetEffectiveSlotMaterial(component, fill);
				effective[slot] = materialHandle ? materialHandle : GetMeshDefaultMaterial(component.StaticMesh, slot);

				if (!component.MaterialTable)
					component.MaterialTable = Ref<MaterialTable>::Create();
				for (uint32_t fill = 0; fill < slotCount; ++fill)
					component.MaterialTable->SetMaterial(fill, effective[fill]);
			}
			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}

		if (!m_OpenMaterialCallback)
			return;

		// One Edit button per distinct material shown, keyed by slot so IDs stay stable.
		std::vector<AssetHandle> listed;
		const uint32_t buttonSlots = glm::max(slotCount, 1u);
		for (uint32_t slot = 0; slot < buttonSlots; ++slot)
		{
			const AssetHandle materialHandle = GetEffectiveSlotMaterial(component, slot);
			if (!materialHandle || AssetManager::GetAssetType(materialHandle) != AssetType::Material
				|| std::find(listed.begin(), listed.end(), materialHandle) != listed.end())
				continue;

			listed.push_back(materialHandle);
			if (listed.size() > 1)
				ImGui::SameLine();
			if (ImGui::SmallButton(std::format("{}  Edit {}##edit_material_slot_{}", LUX_ICON_PENCIL, GetAssetDisplayName(materialHandle), slot).c_str()))
				m_OpenMaterialCallback(materialHandle);
		}

		// A graph material's exposed parameters, editable here without opening the material. They
		// belong to the material asset (every entity using it changes), and are saved to it.
		for (AssetHandle materialHandle : listed)
		{
			Ref<MaterialAsset> material = AssetManager::GetAsset<MaterialAsset>(materialHandle);
			Ref<MaterialGraph> graph = material ? material->GetGraph() : nullptr;
			if (!graph)
				continue;

			std::vector<MaterialGraphParameter> exposed;
			for (const MaterialGraphParameter& parameter : graph->GetParameters())
			{
				if (parameter.Exposed)
					exposed.push_back(parameter);
			}
			ImGuiEx::ScopedID materialID(static_cast<int>(static_cast<uint64_t>(materialHandle) & 0x7fffffff));
			if (exposed.empty() || !ImGuiEx::PropertyGridHeader(std::format("{} Parameters", GetAssetDisplayName(materialHandle)), false))
				continue;

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
				{
					material->SetGraphParameter(parameter.Name, value);
					m_PendingMaterialSave = materialHandle;
				}
			}
			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}

		if (m_PendingMaterialSave && !ImGui::IsAnyItemActive())
		{
			if (Ref<MaterialAsset> material = AssetManager::GetAsset<MaterialAsset>(m_PendingMaterialSave))
				AssetImporter::Serialize(material.As<Asset>());
			m_PendingMaterialSave = 0;
		}
	}

	void SceneHierarchyPanel::DrawAudioEventPicker(AudioSourceComponent& component, const std::vector<UUID>& selectedEntities)
	{
		AssetHandle sound = component.Event.Sound;
		const bool mixed = selectedEntities.size() > 1 && IsSelectionInconsistent<AssetHandle>(m_Context, selectedEntities, [](Entity entity)
		{
			return AssetHandle(entity.GetComponent<AudioSourceComponent>().Event.Sound);
		});
		ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixed);
		if (ImGuiEx::PropertyAssetReference<Sound>("Sound", sound, "The Sound asset this source plays. Create one from an audio file in the Content Browser."))
		{
			component.Event.Sound = sound;
			ApplyToSelection<AudioSourceComponent>(m_Context, selectedEntities, [sound](AudioSourceComponent& audioComponent, Entity)
			{
				audioComponent.Event.Sound = sound;
			});
		}
		ImGui::PopItemFlag();
		if (m_OpenSoundCallback && !mixed && sound && AssetManager::GetAssetType(sound) == AssetType::Sound)
		{
			ImGui::NextColumn();
			ImGuiEx::ShiftCursorY(4.0f);
			if (ImGui::SmallButton(std::format("{}  Edit Sound##edit_audio_source_sound", LUX_ICON_PENCIL).c_str()))
				m_OpenSoundCallback(sound);
			ImGui::NextColumn();
		}
	}

	void SceneHierarchyPanel::DrawComponents(const std::vector<UUID>& entityIDs)
	{
		// Property widgets here edit only the selected entities (ApplyToSelection), so their undo
		// snapshots can skip the rest of the scene. Actions that reach other entities (add/remove
		// component, prefab revert, reparent) call MarkSceneEdited() and stay scene-wide.
		EditorStack::ScopedEditTarget editTarget(entityIDs);
		if (!m_Context || entityIDs.empty())
		{
			ImGuiEx::PanelEmptyState("##properties_empty", "No entity selected",
									 "Select an entity in Scene Hierarchy to inspect its properties.");
			return;
		}

		Entity firstEntity = m_Context->GetEntityByUUID(entityIDs.front());
		if (!firstEntity)
			return;

		const bool isMultiSelect = entityIDs.size() > 1;
		// A folder is purely organizational: no editable transform, no components to add.
		const bool isFolder = !isMultiSelect && firstEntity.HasComponent<FolderComponent>();
		// Inspector-wide: every input renders as a bordered, slightly-inset "field box" (the
		// concept's .field look). Scoped RAII so it unwinds cleanly across this function's returns.
		ImGuiEx::ScopedStyle frameBorderSize(ImGuiStyleVar_FrameBorderSize, 1.0f);
		ImGuiEx::ScopedStyle frameRounding(ImGuiStyleVar_FrameRounding, 3.0f);
		ImGuiEx::ScopedColour frameBg(ImGuiCol_FrameBg, Colors::Theme::backgroundDark);
		ImGuiEx::ScopedColour frameBorder(ImGuiCol_Border, Colors::Theme::muted);

		// A locked, single-selected entity is read-only in the inspector. The unlock control is drawn
		// before the disable scope opens, so it stays clickable.
		const bool lockedInspect = !isMultiSelect && firstEntity.GetComponent<TagComponent>().Locked;
		if (lockedInspect)
		{
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
			{
				ImGuiEx::ScopedColour lockText(ImGuiCol_Text, Colors::Theme::textDarker);
				ImGui::TextWrapped(LUX_ICON_LOCK "  This entity is locked.");
			}
			if (ImGui::SmallButton("Unlock"))
			{
				firstEntity.GetComponent<TagComponent>().Locked = false;
				EditorStack::Get().MarkSceneEdited("Unlock Entity");
			}
		}
		ImGuiEx::ScopedDisable inspectorDisabled(lockedInspect);

		{
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);

			// Active checkbox before the name (Unity's activeSelf): unticked switches the entity and
			// all its children off, in the editor and at runtime.
			{
				bool active = firstEntity.GetComponent<TagComponent>().Active;
				const bool mixed = IsSelectionInconsistent<bool>(m_Context, entityIDs, [](Entity entity)
				{
					return entity.GetComponent<TagComponent>().Active;
				});
				{
					ImGuiEx::ScopedItemFlags mixedFlag(ImGuiItemFlags_MixedValue, mixed);
					if (ImGui::Checkbox("##EntityActive", &active))
					{
						ApplyToSelection<TagComponent>(m_Context, entityIDs, [active](TagComponent& component, Entity)
						{
							component.Active = active;
						});
						EditorStack::Get().MarkEntitiesEdited(entityIDs, active ? "Activate Entity" : "Deactivate Entity");
					}
				}
				if (ImGui::IsItemHovered())
				{
					if (active && !isMultiSelect && !m_Context->IsEntityActiveInHierarchy(firstEntity))
						ImGui::SetTooltip("Active, but a parent is inactive, so this entity is off too");
					else
						ImGui::SetTooltip("%s", active ? "Active - untick to switch this entity and its children off" : "Inactive - tick to switch it back on");
				}
				ImGui::SameLine(0.0f, 8.0f);
			}

			std::string tagValue = firstEntity.GetName();
			if (isMultiSelect && IsSelectionInconsistent<std::string>(m_Context, entityIDs, [](Entity entity)
			{
				return entity.GetComponent<TagComponent>().Tag;
			}))
			{
				tagValue = "---";
			}

			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::InputText("##Tag", &tagValue))
			{
				const std::string newName = tagValue.empty() ? "Unnamed Entity" : tagValue;
				ApplyToSelection<TagComponent>(m_Context, entityIDs, [&newName](TagComponent& component, Entity)
				{
					component.Tag = newName;
				});
				EditorStack::Get().MarkEntitiesEdited(entityIDs, "Rename");
			}
			if (ImGui::BeginItemTooltip())
			{
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
				ImGui::TextUnformatted(firstEntity.GetName().c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}

			// The tag scripts find entities by (C# Entity.FindWithTag); free text, empty for none.
			{
				std::string userTag = firstEntity.GetComponent<TagComponent>().UserTag;
				if (isMultiSelect && IsSelectionInconsistent<std::string>(m_Context, entityIDs, [](Entity entity)
				{
					return entity.GetComponent<TagComponent>().UserTag;
				}))
				{
					userTag = "---";
				}

				ImGui::SetNextItemWidth(-FLT_MIN);
				if (ImGui::InputTextWithHint("##UserTag", "Tag (scripts find entities by it)", &userTag))
				{
					ApplyToSelection<TagComponent>(m_Context, entityIDs, [&userTag](TagComponent& component, Entity)
					{
						component.UserTag = userTag;
					});
					EditorStack::Get().MarkEntitiesEdited(entityIDs, "Set Tag");
				}
			}
			if (isMultiSelect)
			{
				ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
				ImGui::TextWrapped("%zu entities selected", entityIDs.size());
			}
			if (!isFolder)
			{
				const ImGuiEx::PanelAction actions[] = {
					{0, "Add component", LUX_ICON_PLUS, nullptr, true, false, true}};
				if (ImGuiEx::PanelActionBar("##inspector_actions", actions) == 0)
					ImGui::OpenPopup("AddComponentPanel");
			}
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		ImGui::SetNextWindowSize(
			{std::min(ImGui::GetFontSize() * 24.0f, ImGui::GetMainViewport()->WorkSize.x * 0.9f), 0.0f});
		if (ImGui::BeginPopup("AddComponentPanel"))
		{
			if (ImGui::IsWindowAppearing())
			{
				m_ComponentSearch.clear();
				ImGui::SetKeyboardFocusHere();
			}
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputTextWithHint("##component_search", "Search components...", &m_ComponentSearch);
			ImGui::BeginChild(
				"##component_choices",
				{0.0f, std::min(ImGui::GetFontSize() * 18.0f, ImGui::GetMainViewport()->WorkSize.y * 0.6f)});
			size_t matchingComponents = 0;
			const char* category = "";
			auto canAddComponent = [this, &entityIDs]<typename TComponent>() -> bool
			{
				for (UUID entityID : entityIDs)
				{
					Entity entity = m_Context->GetEntityByUUID(entityID);
					if (entity && !entity.HasComponent<TComponent>())
						return true;
				}

				return false;
			};

			auto addComponentRow = [this, &entityIDs, &matchingComponents,
									&category](const char* label, const Ref<Texture2D>& icon,
											   const std::function<void()>& addCallback)
			{
				if (!ImGuiEx::IsMatchingSearch(label, m_ComponentSearch) &&
					!ImGuiEx::IsMatchingSearch(category, m_ComponentSearch))
					return;
				++matchingComponents;
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				if (icon)
					ImGui::Image(ImGuiEx::GetTextureID(icon), ImVec2(16.0f, 16.0f));
				else
					ImGui::Dummy(ImVec2(16.0f, 16.0f));

				ImGui::TableSetColumnIndex(1);
				if (ImGui::Selectable(label, false, ImGuiSelectableFlags_SpanAllColumns))
				{
					addCallback();
					EditorStack::Get().MarkSceneEdited("Add Component");
					ImGui::CloseCurrentPopup();
				}
			};

			auto addCategoryHeader = [this, &category](const char* label)
			{
				category = label;
				if (!m_ComponentSearch.empty())
					return;
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Dummy(ImVec2(16.0f, 4.0f));

				ImGui::TableSetColumnIndex(1);
				ImGui::Spacing();
				ImGui::TextDisabled("%s", label);
			};

			if (ImGui::BeginTable("##AddComponentTable", 2, ImGuiTableFlags_SizingFixedFit))
			{
				ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed, 24.0f);
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);

				const bool canAddCamera = canAddComponent.template operator()<CameraComponent>();
				const bool canAddScript = canAddComponent.template operator()<ScriptComponent>();
				const bool canAddParticles = canAddComponent.template operator()<ParticleSystemComponent>();
				const bool canAddUICanvas = canAddComponent.template operator()<UICanvasComponent>();
				const bool canAddUIElement = canAddComponent.template operator()<UIElementComponent>();
				const bool canAddUIImage = canAddComponent.template operator()<UIImageComponent>();
				const bool canAddUIWidget = canAddComponent.template operator()<UIWidgetComponent>();
				const bool canAddUIText = canAddComponent.template operator()<UITextComponent>();
				const bool canAddText = canAddComponent.template operator()<TextComponent>();
				const bool canAddSpriteRenderer = canAddComponent.template operator()<SpriteRendererComponent>();
				const bool canAddCircleRenderer = canAddComponent.template operator()<CircleRendererComponent>();
				const bool canAddSpriteAnimator = canAddComponent.template operator()<SpriteAnimatorComponent>();
				const bool canAddTilemap = canAddComponent.template operator()<TilemapComponent>();
				const bool canAddStaticMesh = canAddComponent.template operator()<StaticMeshComponent>();
				const bool canAddSkeleton = canAddComponent.template operator()<SkeletonComponent>();
				const bool canAddRigidBody2D = canAddComponent.template operator()<RigidBody2DComponent>();
				const bool canAddBoxCollider2D = canAddComponent.template operator()<BoxCollider2DComponent>();
				const bool canAddCircleCollider2D = canAddComponent.template operator()<CircleCollider2DComponent>();
				const bool canAddPolygonCollider2D = canAddComponent.template operator()<PolygonCollider2DComponent>();
				const bool canAddCapsuleCollider2D = canAddComponent.template operator()<CapsuleCollider2DComponent>();
				const bool canAddEdgeCollider2D = canAddComponent.template operator()<EdgeCollider2DComponent>();
				const bool canAddJoint2D = canAddComponent.template operator()<Joint2DComponent>();
				const bool canAddRigidBody = canAddComponent.template operator()<RigidBodyComponent>();
				const bool canAddCharacterController = canAddComponent.template operator()<CharacterControllerComponent>();
				const bool canAddCompoundCollider = canAddComponent.template operator()<CompoundColliderComponent>();
				const bool canAddBoxCollider = canAddComponent.template operator()<BoxColliderComponent>();
				const bool canAddSphereCollider = canAddComponent.template operator()<SphereColliderComponent>();
				const bool canAddCapsuleCollider = canAddComponent.template operator()<CapsuleColliderComponent>();
				const bool canAddMeshCollider = canAddComponent.template operator()<MeshColliderComponent>();
				const bool canAddAudioSource = canAddComponent.template operator()<AudioSourceComponent>();
				const bool canAddMusic = canAddComponent.template operator()<MusicDirectorComponent>();
				const bool canAddAudioPortal = canAddComponent.template operator()<AudioPortalComponent>();
				const bool canAddAudioZone = canAddComponent.template operator()<AudioZoneComponent>();
				const bool canAddAudioSurface = canAddComponent.template operator()<AudioSurfaceComponent>();
				const bool canAddAudioListener = canAddComponent.template operator()<AudioListenerComponent>();
				const bool canAddDirectionalLight = canAddComponent.template operator()<DirectionalLightComponent>();
				const bool canAddPointLight = canAddComponent.template operator()<PointLightComponent>();
				const bool canAddSpotLight = canAddComponent.template operator()<SpotLightComponent>();
				const bool canAddAreaLight = canAddComponent.template operator()<AreaLightComponent>();
				const bool canAddSkyLight = canAddComponent.template operator()<SkyLightComponent>();

				if (canAddCamera || canAddScript)
					addCategoryHeader("General");

				if (canAddCamera)
				{
					addComponentRow("Camera", EditorResources::CameraIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (!entity.HasComponent<CameraComponent>())
								entity.AddComponent<CameraComponent>();
						}
					});
				}

				if (canAddScript)
				{
					addComponentRow("Script", EditorResources::ScriptIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<ScriptComponent>())
								entity.AddComponent<ScriptComponent>();
						}
					});
				}

				if (canAddText || canAddSpriteRenderer || canAddSpriteAnimator || canAddCircleRenderer || canAddStaticMesh || canAddParticles)
					addCategoryHeader("Rendering");

				if (canAddParticles)
				{
					addComponentRow("Particle System", EditorResources::SpriteIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<ParticleSystemComponent>())
								entity.AddComponent<ParticleSystemComponent>();
						}
					});
				}

				if (canAddUICanvas)
				{
					addComponentRow("UI Canvas", EditorResources::SpriteIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<UICanvasComponent>())
								entity.AddComponent<UICanvasComponent>();
						}
					});
				}

				if (canAddUIElement)
				{
					addComponentRow("UI Element", EditorResources::SpriteIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<UIElementComponent>())
								entity.AddComponent<UIElementComponent>();
						}
					});
				}

				if (canAddUIImage)
				{
					addComponentRow("UI Image", EditorResources::SpriteIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<UIImageComponent>())
								entity.AddComponent<UIImageComponent>();
						}
					});
				}

				if (canAddUIWidget)
				{
					addComponentRow("UI Widget", EditorResources::SpriteIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<UIWidgetComponent>())
								entity.AddComponent<UIWidgetComponent>();
						}
					});
				}

				if (canAddUIText)
				{
					addComponentRow("UI Text", EditorResources::SpriteIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<UITextComponent>())
								entity.AddComponent<UITextComponent>();
						}
					});
				}

				if (canAddText)
				{
					addComponentRow("Text", EditorResources::TextIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (!entity || entity.HasComponent<TextComponent>())
								continue;

							auto& text = entity.AddComponent<TextComponent>();
							if (Font::GetDefaultFont())
								text.FontHandle = Font::GetDefaultFont()->Handle;
						}
					});
				}

				if (canAddSpriteRenderer)
				{
					addComponentRow("Sprite Renderer", EditorResources::SpriteIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<SpriteRendererComponent>())
								entity.AddComponent<SpriteRendererComponent>();
						}
					});
				}

				if (canAddSpriteAnimator)
				{
					addComponentRow("Sprite Animator", EditorResources::SpriteIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<SpriteAnimatorComponent>())
								entity.AddComponent<SpriteAnimatorComponent>();
						}
					});
				}

				if (canAddTilemap)
				{
					addComponentRow("Tilemap", EditorResources::SpriteIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<TilemapComponent>())
								entity.AddComponent<TilemapComponent>();
						}
					});
				}

				if (canAddCircleRenderer)
				{
					addComponentRow("Circle Renderer", EditorResources::SpriteIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<CircleRendererComponent>())
								entity.AddComponent<CircleRendererComponent>();
						}
					});
				}

				if (canAddStaticMesh)
				{
					addComponentRow("Static Mesh", EditorResources::StaticMeshIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<StaticMeshComponent>())
								entity.AddComponent<StaticMeshComponent>();
						}
					});
				}

				if (canAddSkeleton)
				{
					addComponentRow("Skeleton", EditorResources::SkeletonIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<SkeletonComponent>())
								entity.AddComponent<SkeletonComponent>();
						}
					});
				}

				if (canAddRigidBody2D || canAddBoxCollider2D || canAddCircleCollider2D || canAddPolygonCollider2D || canAddCapsuleCollider2D || canAddEdgeCollider2D || canAddJoint2D)
					addCategoryHeader("Physics 2D");

				if (canAddRigidBody2D)
				{
					addComponentRow("Rigidbody 2D", EditorResources::RigidBody2DIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<RigidBody2DComponent>())
								entity.AddComponent<RigidBody2DComponent>();
						}
					});
				}

				if (canAddBoxCollider2D)
				{
					addComponentRow("Box Collider 2D", EditorResources::BoxCollider2DIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<BoxCollider2DComponent>())
								entity.AddComponent<BoxCollider2DComponent>();
						}
					});
				}

				if (canAddCircleCollider2D)
				{
					addComponentRow("Circle Collider 2D", EditorResources::CircleCollider2DIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<CircleCollider2DComponent>())
								entity.AddComponent<CircleCollider2DComponent>();
						}
					});
				}

				if (canAddPolygonCollider2D)
				{
					addComponentRow("Polygon Collider 2D", EditorResources::BoxCollider2DIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<PolygonCollider2DComponent>())
								entity.AddComponent<PolygonCollider2DComponent>();
						}
					});
				}

				if (canAddCapsuleCollider2D)
				{
					addComponentRow("Capsule Collider 2D", EditorResources::CircleCollider2DIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<CapsuleCollider2DComponent>())
								entity.AddComponent<CapsuleCollider2DComponent>();
						}
					});
				}

				if (canAddEdgeCollider2D)
				{
					addComponentRow("Edge Collider 2D", EditorResources::BoxCollider2DIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<EdgeCollider2DComponent>())
								entity.AddComponent<EdgeCollider2DComponent>();
						}
					});
				}

				if (canAddJoint2D)
				{
					addComponentRow("Joint 2D", EditorResources::RigidBody2DIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<Joint2DComponent>())
								entity.AddComponent<Joint2DComponent>();
						}
					});
				}

				if (canAddRigidBody || canAddCharacterController || canAddCompoundCollider || canAddBoxCollider || canAddSphereCollider || canAddCapsuleCollider || canAddMeshCollider)
					addCategoryHeader("Physics");

				if (canAddRigidBody)
				{
					addComponentRow("Rigidbody", EditorResources::RigidBodyIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<RigidBodyComponent>())
								entity.AddComponent<RigidBodyComponent>();
						}
					});
				}

				if (canAddCharacterController)
				{
					addComponentRow("Character Controller", EditorResources::CharacterControllerIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<CharacterControllerComponent>())
								entity.AddComponent<CharacterControllerComponent>();
						}
					});
				}

				if (canAddCompoundCollider)
				{
					addComponentRow("Compound Collider", EditorResources::CompoundColliderIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<CompoundColliderComponent>())
								entity.AddComponent<CompoundColliderComponent>();
						}
					});
				}

				if (canAddBoxCollider)
				{
					addComponentRow("Box Collider", EditorResources::BoxColliderIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<BoxColliderComponent>())
								entity.AddComponent<BoxColliderComponent>();
						}
					});
				}

				if (canAddSphereCollider)
				{
					addComponentRow("Sphere Collider", EditorResources::SphereColliderIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<SphereColliderComponent>())
								entity.AddComponent<SphereColliderComponent>();
						}
					});
				}

				if (canAddCapsuleCollider)
				{
					addComponentRow("Capsule Collider", EditorResources::CapsuleColliderIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<CapsuleColliderComponent>())
								entity.AddComponent<CapsuleColliderComponent>();
						}
					});
				}

				if (canAddMeshCollider)
				{
					addComponentRow("Mesh Collider", EditorResources::MeshColliderIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<MeshColliderComponent>())
								entity.AddComponent<MeshColliderComponent>();
						}
					});
				}

				if (canAddMusic || canAddAudioSource || canAddAudioListener || canAddAudioSurface || canAddAudioZone)
					addCategoryHeader("Audio");

				if (canAddAudioSource)
				{
					addComponentRow("Audio Source", EditorResources::AudioIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<AudioSourceComponent>())
								entity.AddComponent<AudioSourceComponent>();
						}
					});
				}

				if (canAddMusic)
				{
					addComponentRow("Music Director", EditorResources::AudioIcon, [this, &entityIDs]()
					{
						for (UUID id : entityIDs)
						{
							Entity entity = m_Context->TryGetEntityWithUUID(id);
							if (entity && !entity.HasComponent<MusicDirectorComponent>())
								entity.AddComponent<MusicDirectorComponent>();
						}
					});
				}
				if (canAddAudioPortal)
				{
					addComponentRow("Audio Portal", EditorResources::AudioIcon, [this, &entityIDs]()
					{
						for (UUID id : entityIDs)
						{
							Entity entity = m_Context->TryGetEntityWithUUID(id);
							if (entity && !entity.HasComponent<AudioPortalComponent>())
								entity.AddComponent<AudioPortalComponent>();
						}
					});
				}
				if (canAddAudioZone)
				{
					addComponentRow("Audio Zone", EditorResources::AudioIcon, [this, &entityIDs]()
					{
						for (UUID id : entityIDs)
						{
							Entity entity = m_Context->TryGetEntityWithUUID(id);
							if (entity && !entity.HasComponent<AudioZoneComponent>())
								entity.AddComponent<AudioZoneComponent>();
						}
					});
				}
				if (canAddAudioSurface)
				{
					addComponentRow("Audio Surface", EditorResources::AudioIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<AudioSurfaceComponent>())
								entity.AddComponent<AudioSurfaceComponent>();
						}
					});
				}

				if (canAddAudioListener)
				{
					addComponentRow("Audio Listener", EditorResources::AudioListenerIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<AudioListenerComponent>())
								entity.AddComponent<AudioListenerComponent>();
						}
					});
				}

				if (canAddDirectionalLight || canAddPointLight || canAddSpotLight || canAddAreaLight || canAddSkyLight)
					addCategoryHeader("Lighting");

				if (canAddDirectionalLight)
				{
					addComponentRow("Directional Light", EditorResources::DirectionalLightIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<DirectionalLightComponent>())
								entity.AddComponent<DirectionalLightComponent>();
						}
					});
				}

				if (canAddPointLight)
				{
					addComponentRow("Point Light", EditorResources::PointLightIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<PointLightComponent>())
								entity.AddComponent<PointLightComponent>();
						}
					});
				}

				if (canAddSpotLight)
				{
					addComponentRow("Spot Light", EditorResources::SpotLightIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<SpotLightComponent>())
								entity.AddComponent<SpotLightComponent>();
						}
					});
				}

				if (canAddAreaLight)
				{
					addComponentRow("Area Light", EditorResources::PointLightIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<AreaLightComponent>())
								entity.AddComponent<AreaLightComponent>();
						}
					});
				}

				if (canAddSkyLight)
				{
					addComponentRow("Sky Light", EditorResources::SkyLightIcon, [this, &entityIDs]()
					{
						for (UUID entityID : entityIDs)
						{
							Entity entity = m_Context->GetEntityByUUID(entityID);
							if (entity && !entity.HasComponent<SkyLightComponent>())
								entity.AddComponent<SkyLightComponent>();
						}
					});
				}

				ImGui::EndTable();
			}

			if (!matchingComponents)
				ImGuiEx::PanelEmptyState(
					"##no_components", "No components available",
					"Change the search. Components already on every selected entity are omitted.");
			ImGui::EndChild();
			ImGui::EndPopup();
		}

		DrawComponentSection<PrefabComponent>(m_Context, entityIDs, "Prefab", EditorResources::AssetIcon,
			[this](PrefabComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				const bool validPrefab = AssetManager::IsAssetHandleValid(firstComponent.PrefabID)
					&& AssetManager::GetAssetType(firstComponent.PrefabID) == AssetType::Prefab;

				std::string prefabName = "(missing prefab)";
				if (validPrefab)
					prefabName = Project::GetEditorAssetManager()->GetMetadata(firstComponent.PrefabID).FilePath.stem().string();

				{
					ImGuiEx::ScopedColour label(ImGuiCol_Text, Colors::Theme::textDarker);
					ImGui::TextUnformatted("Source");
				}
				ImGui::SameLine();
				ImGui::TextUnformatted(prefabName.c_str());

				Ref<Prefab> prefab = validPrefab ? AssetManager::GetAsset<Prefab>(firstComponent.PrefabID) : nullptr;
				Entity sourceEntity = prefab ? prefab->GetScene()->TryGetEntityWithUUID(firstComponent.EntityID) : Entity{};
				Entity instanceEntity = m_Context->TryGetEntityWithUUID(selectedEntities.front());

				const bool singleSelect = selectedEntities.size() == 1;
				const bool canSync = singleSelect && prefab && sourceEntity && instanceEntity;

				if (!singleSelect)
				{
					ImGuiEx::ScopedColour note(ImGuiCol_Text, Colors::Theme::textDarker);
					ImGui::TextUnformatted("Select a single instance to revert or apply.");
				}
				else if (!canSync)
				{
					ImGuiEx::ScopedColour note(ImGuiCol_Text, Colors::Theme::textDarker);
					ImGui::TextUnformatted("Prefab source unavailable.");
				}
				else
				{
					const std::unordered_set<std::string> overrides =
						SceneSerializer::GetOverriddenComponentKeys(instanceEntity, sourceEntity);

					auto serializePrefab = [&]()
					{
						const std::filesystem::path path = Project::GetEditorAssetManager()->GetFileSystemPath(firstComponent.PrefabID);
						// Route through the prefab writer so a variant keeps its base link.
						PrefabSerializer::WritePrefabFile(path, prefab->GetScene(), prefab->GetBasePrefab());
					};
					// Other un-overridden instances in this scene adopt the applied values (as saving from prefab edit mode does).
					auto propagateApply = [&](const Ref<Scene>& oldPrefab)
					{
						serializePrefab();
						m_Context->PropagatePrefabEdits(firstComponent.PrefabID, oldPrefab, prefab->GetScene());
						EditorStack::Get().MarkSceneEdited("Apply Prefab Override");
					};

					// One override row: label + per-component Revert (prefab -> instance) and Apply
					// (instance -> prefab, then re-serialize). T reconciles add/replace/remove.
					auto row = [&]<typename T>(const char* key, const char* label)
					{
						if (!overrides.contains(key))
							return;

						ImGuiEx::ScopedID id(key);
						ImGui::Bullet();
						ImGui::SameLine();
						ImGui::TextUnformatted(label);

						ImGui::SameLine();
						if (ImGui::SmallButton("Revert"))
						{
							if (sourceEntity.HasComponent<T>())
								instanceEntity.AddOrReplaceComponent<T>(sourceEntity.GetComponent<T>());
							else
								instanceEntity.RemoveComponentIfExists<T>();
							if constexpr (std::is_same_v<T, AudioListenerComponent>)
							{
								if (auto* listener = instanceEntity.TryGetComponent<AudioListenerComponent>())
									listener->AttenuationTarget = Scene::MapPrefabEntityReference(listener->AttenuationTarget, sourceEntity, instanceEntity);
							}
							EditorStack::Get().MarkSceneEdited("Revert Prefab Override");
						}
						ImGui::SameLine();
						if (ImGui::SmallButton("Apply"))
						{
							const Ref<Scene> oldPrefab = Scene::Copy(prefab->GetScene());
							if (instanceEntity.HasComponent<T>())
								sourceEntity.AddOrReplaceComponent<T>(instanceEntity.GetComponent<T>());
							else
								sourceEntity.RemoveComponentIfExists<T>();
							if constexpr (std::is_same_v<T, AudioListenerComponent>)
							{
								if (auto* listener = sourceEntity.TryGetComponent<AudioListenerComponent>())
									listener->AttenuationTarget = Scene::MapPrefabEntityReference(listener->AttenuationTarget, instanceEntity, sourceEntity);
							}
							propagateApply(oldPrefab);
							LUX_CORE_INFO_TAG("Prefab", "Applied {} to prefab '{}'", label, prefabName);
						}
					};

					if (overrides.empty())
					{
						ImGuiEx::ScopedColour note(ImGuiCol_Text, Colors::Theme::textDarker);
						ImGui::TextUnformatted("No overrides.");
					}
					else
					{
						{
							ImGuiEx::ScopedColour hdr(ImGuiCol_Text, Colors::Theme::textDarker);
							ImGui::TextUnformatted("Overrides");
						}

						// This list must cover every component SerializeEntity emits, or a detected
						// override could have no row here (and Revert All wouldn't reconcile it).
						row.operator()<TransformComponent>("TransformComponent", "Transform");
						row.operator()<StaticMeshComponent>("StaticMeshComponent", "Static Mesh");
						row.operator()<SkeletonComponent>("SkeletonComponent", "Skeleton");
						row.operator()<MeshComponent>("MeshComponent", "Mesh");
						row.operator()<SubmeshComponent>("SubmeshComponent", "Submesh");
						row.operator()<MeshTagComponent>("MeshTagComponent", "Mesh Tag");
						row.operator()<CameraComponent>("CameraComponent", "Camera");
						row.operator()<ScriptComponent>("ScriptComponent", "Script");
						row.operator()<SpriteRendererComponent>("SpriteRendererComponent", "Sprite Renderer");
						row.operator()<SpriteAnimatorComponent>("SpriteAnimatorComponent", "Sprite Animator");
						row.operator()<TilemapComponent>("TilemapComponent", "Tilemap");
						row.operator()<CircleRendererComponent>("CircleRendererComponent", "Circle Renderer");
						row.operator()<ParticleSystemComponent>("ParticleSystemComponent", "Particle System");
						row.operator()<UICanvasComponent>("UICanvasComponent", "UI Canvas");
						row.operator()<UIElementComponent>("UIElementComponent", "UI Element");
						row.operator()<UIImageComponent>("UIImageComponent", "UI Image");
						row.operator()<UIWidgetComponent>("UIWidgetComponent", "UI Widget");
						row.operator()<UITextComponent>("UITextComponent", "UI Text");
						row.operator()<TextComponent>("TextComponent", "Text");
						row.operator()<DirectionalLightComponent>("DirectionalLightComponent", "Directional Light");
						row.operator()<PointLightComponent>("PointLightComponent", "Point Light");
						row.operator()<SpotLightComponent>("SpotLightComponent", "Spot Light");
						row.operator()<AreaLightComponent>("AreaLightComponent", "Area Light");
						row.operator()<SkyLightComponent>("SkyLightComponent", "Sky Light");
						row.operator()<RigidBodyComponent>("RigidBodyComponent", "Rigid Body");
						row.operator()<CharacterControllerComponent>("CharacterControllerComponent", "Character Controller");
						row.operator()<CompoundColliderComponent>("CompoundColliderComponent", "Compound Collider");
						row.operator()<BoxColliderComponent>("BoxColliderComponent", "Box Collider");
						row.operator()<SphereColliderComponent>("SphereColliderComponent", "Sphere Collider");
						row.operator()<CapsuleColliderComponent>("CapsuleColliderComponent", "Capsule Collider");
						row.operator()<MeshColliderComponent>("MeshColliderComponent", "Mesh Collider");
						row.operator()<RigidBody2DComponent>("RigidBody2DComponent", "Rigid Body 2D");
						row.operator()<BoxCollider2DComponent>("BoxCollider2DComponent", "Box Collider 2D");
						row.operator()<CircleCollider2DComponent>("CircleCollider2DComponent", "Circle Collider 2D");
						row.operator()<PolygonCollider2DComponent>("PolygonCollider2DComponent", "Polygon Collider 2D");
						row.operator()<CapsuleCollider2DComponent>("CapsuleCollider2DComponent", "Capsule Collider 2D");
						row.operator()<EdgeCollider2DComponent>("EdgeCollider2DComponent", "Edge Collider 2D");
						row.operator()<Joint2DComponent>("Joint2DComponent", "Joint 2D");
						row.operator()<AudioSourceComponent>("AudioSourceComponent", "Audio Source");
						row.operator()<AudioListenerComponent>("AudioListenerComponent", "Audio Listener");
						row.operator()<AudioSurfaceComponent>("AudioSurfaceComponent", "Audio Surface");
						row.operator()<AudioZoneComponent>("AudioZoneComponent", "Audio Zone");
						row.operator()<AudioPortalComponent>("AudioPortalComponent", "Audio Portal");
						row.operator()<MusicDirectorComponent>("MusicDirectorComponent", "Music Director");
						row.operator()<FolderComponent>("Folder", "Folder");

						ImGui::Spacing();
						if (ImGui::Button("Revert All"))
						{
							Scene::ReconcilePrefabComponents(instanceEntity, sourceEntity);
							EditorStack::Get().MarkSceneEdited("Revert Prefab Instance");
						}
						ImGui::SameLine();
						if (ImGui::Button("Apply All"))
							ImGui::OpenPopup("Apply to Prefab?");

						if (ImGui::BeginPopupModal("Apply to Prefab?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
						{
							ImGui::TextUnformatted("Overwrite the prefab asset with this instance's values?\n"
								"Other un-overridden instances follow.");
							ImGui::Spacing();
							if (ImGui::Button("Apply"))
							{
								const Ref<Scene> oldPrefab = Scene::Copy(prefab->GetScene());
								Scene::ReconcilePrefabComponents(sourceEntity, instanceEntity);
								propagateApply(oldPrefab);
								LUX_CORE_INFO_TAG("Prefab", "Applied instance to prefab '{}'", prefabName);
								ImGui::CloseCurrentPopup();
							}
							ImGui::SameLine();
							if (ImGui::Button("Cancel"))
								ImGui::CloseCurrentPopup();
							ImGui::EndPopup();
						}
					}

					// Prefab children deleted from this instance stay deleted when the prefab changes. An
					// instance saved before deletions were recorded had its missing children taken as deleted
					// (Scene::SyncPrefabInstance); Restore brings them back from the prefab.
					if (Entity instanceRoot = Scene::FindPrefabInstanceRoot(instanceEntity))
					{
						const size_t removed = instanceRoot.GetComponent<PrefabComponent>().RemovedEntities.size();
						if (removed > 0)
						{
							ImGui::Spacing();
							{
								ImGuiEx::ScopedColour note(ImGuiCol_Text, Colors::Theme::textDarker);
								ImGui::Text("%zu prefab %s deleted from this instance", removed, removed == 1 ? "child" : "children");
							}
							ImGui::SameLine();
							if (ImGui::SmallButton("Restore##PrefabRemovals"))
							{
								const AssetHandle rootPrefabID = instanceRoot.GetComponent<PrefabComponent>().PrefabID;
								instanceRoot.GetComponent<PrefabComponent>().RemovedEntities.clear();
								if (Ref<Prefab> rootPrefab = AssetManager::GetAsset<Prefab>(rootPrefabID))
									m_Context->SyncPrefabInstance(instanceRoot, rootPrefab->GetScene());
								EditorStack::Get().MarkSceneEdited("Restore Prefab Children");
							}
						}
					}
				}
			});

		// Folders keep an identity transform for the scene graph, but it isn't user-editable.
		if (!isFolder)
		DrawComponentSection<TransformComponent>(m_Context, entityIDs, "Transform", EditorResources::TransformIcon,
			[this](TransformComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				glm::vec3 translation = firstComponent.Translation;
				glm::vec3 rotation = glm::degrees(firstComponent.GetRotationEuler());
				glm::vec3 scale = firstComponent.Scale;

				if (const uint32_t axes = DrawVec3Control("Translation", translation))
				{
					ApplyToSelection<TransformComponent>(
						m_Context, selectedEntities,
						[&translation, axes](TransformComponent& component, Entity)
						{
							for (int axis = 0; axis < 3; ++axis)
								if (axes & BIT(axis))
									component.Translation[axis] = translation[axis];
						});
				}

				if (const uint32_t axes = DrawVec3Control("Rotation", rotation))
				{
					const glm::vec3 rotationRadians = glm::radians(rotation);
					ApplyToSelection<TransformComponent>(
						m_Context, selectedEntities,
						[&rotationRadians, axes](TransformComponent& component, Entity)
						{
							glm::vec3 current = component.GetRotationEuler();
							for (int axis = 0; axis < 3; ++axis)
								if (axes & BIT(axis))
									current[axis] = rotationRadians[axis];
							component.SetRotationEuler(current);
						});
				}

				if (const uint32_t axes = DrawVec3Control("Scale", scale, 1.0f))
				{
					ApplyToSelection<TransformComponent>(m_Context, selectedEntities,
														 [&scale, axes](TransformComponent& component, Entity)
														 {
															 for (int axis = 0; axis < 3; ++axis)
																 if (axes & BIT(axis))
																	 component.Scale[axis] = scale[axis];
														 });
				}
			});

		DrawComponentSection<CameraComponent>(m_Context, entityIDs, "Camera", EditorResources::CameraIcon,
			[this](CameraComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();

				if (ImGuiEx::Property("Primary", firstComponent.Primary))
				{
					ApplyToSelection<CameraComponent>(m_Context, selectedEntities, [&firstComponent](CameraComponent& component, Entity)
					{
						component.Primary = firstComponent.Primary;
					});
				}

				auto& camera = firstComponent.Camera;
				const char* projectionTypeStrings[] = { "Perspective", "Orthographic" };
				int currentProjection = (int)camera.GetProjectionType();
				if (ImGuiEx::PropertyDropdown("Projection", projectionTypeStrings, 2, &currentProjection))
				{
					camera.SetProjectionType((SceneCamera::ProjectionType)currentProjection);
					ApplyToSelection<CameraComponent>(m_Context, selectedEntities, [currentProjection](CameraComponent& component, Entity)
					{
						component.Camera.SetProjectionType((SceneCamera::ProjectionType)currentProjection);
					});
				}

				if (camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective)
				{
					float perspectiveNear = camera.GetPerspectiveNearClip();
					if (ImGuiEx::Property("Near", perspectiveNear))
					{
						camera.SetPerspectiveNearClip(perspectiveNear);
						ApplyToSelection<CameraComponent>(m_Context, selectedEntities, [perspectiveNear](CameraComponent& component, Entity)
						{
							component.Camera.SetPerspectiveNearClip(perspectiveNear);
						});
					}

					float perspectiveFar = camera.GetPerspectiveFarClip();
					if (ImGuiEx::Property("Far", perspectiveFar))
					{
						camera.SetPerspectiveFarClip(perspectiveFar);
						ApplyToSelection<CameraComponent>(m_Context, selectedEntities, [perspectiveFar](CameraComponent& component, Entity)
						{
							component.Camera.SetPerspectiveFarClip(perspectiveFar);
						});
					}
				}
				else
				{
					bool pixelPerfect = camera.IsPixelPerfect();
					if (ImGuiEx::Property("Pixel Perfect", pixelPerfect, "Whole-number pixel scale and a camera snapped to the pixel grid. Needs the game rendered at native resolution."))
					{
						camera.SetPixelPerfect(pixelPerfect);
						ApplyToSelection<CameraComponent>(m_Context, selectedEntities, [pixelPerfect](CameraComponent& component, Entity)
						{
							component.Camera.SetPixelPerfect(pixelPerfect);
						});
					}
					if (pixelPerfect)
					{
						float pixelsPerUnit = camera.GetPixelsPerUnit();
						if (ImGuiEx::Property("Pixels Per Unit", pixelsPerUnit, 1.0f, 1.0f, 4096.0f))
						{
							camera.SetPixelsPerUnit(pixelsPerUnit);
							ApplyToSelection<CameraComponent>(m_Context, selectedEntities, [pixelsPerUnit](CameraComponent& component, Entity)
							{
								component.Camera.SetPixelsPerUnit(pixelsPerUnit);
							});
						}

						float referenceHeight = camera.GetReferenceHeight();
						if (ImGuiEx::Property("Reference Height", referenceHeight, 1.0f, 1.0f, 8192.0f))
						{
							camera.SetReferenceHeight(referenceHeight);
							ApplyToSelection<CameraComponent>(m_Context, selectedEntities, [referenceHeight](CameraComponent& component, Entity)
							{
								component.Camera.SetReferenceHeight(referenceHeight);
							});
						}
					}

					float orthoSize = camera.GetOrthographicSize();
					if (!pixelPerfect && ImGuiEx::Property("Size", orthoSize))
					{
						camera.SetOrthographicSize(orthoSize);
						ApplyToSelection<CameraComponent>(m_Context, selectedEntities, [orthoSize](CameraComponent& component, Entity)
						{
							component.Camera.SetOrthographicSize(orthoSize);
						});
					}

					float orthoNear = camera.GetOrthographicNearClip();
					if (ImGuiEx::Property("Near", orthoNear))
					{
						camera.SetOrthographicNearClip(orthoNear);
						ApplyToSelection<CameraComponent>(m_Context, selectedEntities, [orthoNear](CameraComponent& component, Entity)
						{
							component.Camera.SetOrthographicNearClip(orthoNear);
						});
					}

					float orthoFar = camera.GetOrthographicFarClip();
					if (ImGuiEx::Property("Far", orthoFar))
					{
						camera.SetOrthographicFarClip(orthoFar);
						ApplyToSelection<CameraComponent>(m_Context, selectedEntities, [orthoFar](CameraComponent& component, Entity)
						{
							component.Camera.SetOrthographicFarClip(orthoFar);
						});
					}

					if (ImGuiEx::Property("Fixed Aspect Ratio", firstComponent.FixedAspectRatio))
					{
						ApplyToSelection<CameraComponent>(m_Context, selectedEntities, [&firstComponent](CameraComponent& component, Entity)
						{
							component.FixedAspectRatio = firstComponent.FixedAspectRatio;
						});
					}
				}

				if (ImGuiEx::Property("Solid Background", firstComponent.SolidBackground))
				{
					ApplyToSelection<CameraComponent>(m_Context, selectedEntities, [&firstComponent](CameraComponent& component, Entity)
					{
						component.SolidBackground = firstComponent.SolidBackground;
					});
				}
				if (firstComponent.SolidBackground && ImGuiEx::PropertyColor("Background", firstComponent.BackgroundColor))
				{
					ApplyToSelection<CameraComponent>(m_Context, selectedEntities, [&firstComponent](CameraComponent& component, Entity)
					{
						component.BackgroundColor = firstComponent.BackgroundColor;
					});
				}

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponentSection<ScriptComponent>(m_Context, entityIDs, "Script", EditorResources::ScriptIcon,
			[this, firstEntity](ScriptComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool isMultiEdit) mutable
			{
				ImGuiEx::BeginPropertyGrid();
				UUID scriptID = firstComponent.ScriptID;
				std::string className = firstComponent.ClassName;
				if (ImGuiEx::PropertyScriptReference("Class", scriptID, className))
				{
					firstComponent.ScriptID = scriptID;
					firstComponent.ClassName = className;
					ApplyToSelection<ScriptComponent>(m_Context, selectedEntities, [&](ScriptComponent& component, Entity)
					{
						component.ScriptID = scriptID;
						component.ClassName = className;
					});
				}
				ImGuiEx::EndPropertyGrid();

				const auto& scriptEngine = ScriptEngine::GetInstance();
				const bool valid = scriptEngine.IsValidScript(firstComponent.ScriptID);
				if (!valid && !firstComponent.ClassName.empty())
					ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.3f, 1.0f), "Script class not found.");

				if (isMultiEdit)
					ImGui::TextDisabled("Script field editing is shown for the first selected entity.");

				if (!valid)
					return;

				// While the scene runs, FieldStorage reads/writes the live Coral instance, so the
				// controls below reflect and edit the running object rather than the stored snapshot.
				if (m_Context && m_Context->IsRunning())
				{
					ImGuiEx::ScopedColour live(ImGuiCol_Text, Colors::Theme::titlebarGreen);
					ImGui::TextUnformatted("\xE2\x97\x8F  Live \xE2\x80\x94 editing the running instance");
				}

				// Ensure per-entity storage exists (and matches the current script).
				UUID entityID = firstEntity.GetUUID();
				ScriptStorage& storage = m_Context->GetScriptStorage();
				auto storageIt = storage.EntityStorage.find(entityID);
				if (storageIt != storage.EntityStorage.end() && storageIt->second.ScriptID != firstComponent.ScriptID)
				{
					storage.ShutdownEntityStorage(storageIt->second.ScriptID, entityID);
					storageIt = storage.EntityStorage.end();
				}
				if (storageIt == storage.EntityStorage.end())
					storage.InitializeEntityStorage(firstComponent.ScriptID, entityID);

				auto& entityStorage = storage.EntityStorage.at(entityID);
				const auto& metadata = scriptEngine.GetScriptMetadata(firstComponent.ScriptID);
				for (const auto& [fieldID, fieldMetadata] : metadata.Fields)
				{
					auto it = entityStorage.Fields.find(fieldID);
					if (it == entityStorage.Fields.end())
						continue;

					FieldStorage& fs = it->second;

					// [Header] groups fields with a bold label above them.
					if (!fieldMetadata.Header.empty())
					{
						ImGuiEx::ScopedColour headerColour(ImGuiCol_Text, Colors::Theme::textBrighter);
						ImGui::TextUnformatted(fieldMetadata.Header.c_str());
					}

					if (fs.IsArray())
					{
						ImGui::TextDisabled("%s (array)", fieldMetadata.Name.c_str());
						continue;
					}
					DrawScriptFieldControl(fieldMetadata.Name, fieldMetadata.Type, fs,
						fieldMetadata.HasRange, fieldMetadata.RangeMin, fieldMetadata.RangeMax);

					// [Tooltip] shows help when the control is hovered.
					if (!fieldMetadata.Tooltip.empty() && ImGui::IsItemHovered())
						ImGui::SetTooltip("%s", fieldMetadata.Tooltip.c_str());
				}
			});

		DrawComponentSection<SpriteRendererComponent>(m_Context, entityIDs, "Sprite Renderer", EditorResources::SpriteIcon,
			[this](SpriteRendererComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();

				if (ImGuiEx::PropertyColor("Color", firstComponent.Color))
				{
					ApplyToSelection<SpriteRendererComponent>(m_Context, selectedEntities, [&firstComponent](SpriteRendererComponent& component, Entity)
					{
						component.Color = firstComponent.Color;
					});
				}

				AssetHandle textureHandle = firstComponent.Texture;
				const bool mixedTexture = selectedEntities.size() > 1 && IsSelectionInconsistent<AssetHandle>(m_Context, selectedEntities, [](Entity entity)
				{
					return entity.GetComponent<SpriteRendererComponent>().Texture;
				});
				ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixedTexture);
				if (ImGuiEx::PropertyAssetReference<Texture2D>("Texture", textureHandle, "Sprite Renderer only accepts texture assets"))
				{
					firstComponent.Texture = textureHandle;
					ApplyToSelection<SpriteRendererComponent>(m_Context, selectedEntities, [textureHandle](SpriteRendererComponent& component, Entity)
					{
						component.Texture = textureHandle;
					});
				}
				ImGui::PopItemFlag();

				AssetHandle sheetHandle = firstComponent.Sheet;
				if (ImGuiEx::PropertyAssetReference<SpriteSheet>("Sheet", sheetHandle, "A sprite sheet replaces Texture: the sprite shows one of its frames, sized and anchored by the frame."))
				{
					firstComponent.Sheet = sheetHandle;
					ApplyToSelection<SpriteRendererComponent>(m_Context, selectedEntities, [sheetHandle](SpriteRendererComponent& component, Entity)
					{
						component.Sheet = sheetHandle;
						component.SpriteIndex = 0;
					});
				}
				if (const Ref<SpriteSheet> sheet = firstComponent.Sheet ? AssetManager::GetAsset<SpriteSheet>(firstComponent.Sheet) : nullptr; sheet && !sheet->Frames.empty())
				{
					int32_t frame = std::clamp<int32_t>(firstComponent.SpriteIndex, 0, static_cast<int32_t>(sheet->Frames.size()) - 1);
					if (ImGuiEx::PropertyDropdown("Frame", static_cast<int32_t>(sheet->Frames.size()), &frame,
						[&sheet](uint32_t index) { return sheet->Frames[index].Name.c_str(); }))
					{
						firstComponent.SpriteIndex = frame;
						ApplyToSelection<SpriteRendererComponent>(m_Context, selectedEntities, [frame](SpriteRendererComponent& component, Entity)
						{
							component.SpriteIndex = frame;
						});
					}
				}

				if (ImGuiEx::Property("Tiling Factor", firstComponent.TilingFactor, 0.1f, 0.0f, 100.0f))
				{
					ApplyToSelection<SpriteRendererComponent>(m_Context, selectedEntities, [&firstComponent](SpriteRendererComponent& component, Entity)
					{
						component.TilingFactor = firstComponent.TilingFactor;
					});
				}

				if (ImGuiEx::Property("Point Filter", firstComponent.PointFilter, "Nearest-neighbour sampling: pixel art stays sharp when magnified."))
				{
					ApplyToSelection<SpriteRendererComponent>(m_Context, selectedEntities, [&firstComponent](SpriteRendererComponent& component, Entity)
					{
						component.PointFilter = firstComponent.PointFilter;
					});
				}

				DrawSortingProperties(m_Context, firstComponent, selectedEntities);

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponentSection<SpriteAnimatorComponent>(m_Context, entityIDs, "Sprite Animator", EditorResources::SpriteIcon,
			[this, firstEntity](SpriteAnimatorComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();

				// The clips come from the sheet on the sprite renderer beside this component.
				const SpriteRendererComponent* sprite = firstEntity.TryGetComponent<SpriteRendererComponent>();
				const Ref<SpriteSheet> sheet = sprite && sprite->Sheet ? AssetManager::GetAsset<SpriteSheet>(sprite->Sheet) : nullptr;
				if (!sheet)
					ImGui::TextDisabled("Give the Sprite Renderer a sprite sheet to choose a clip.");
				else
				{
					int32_t clip = sheet->FindClip(firstComponent.Clip) + 1; // 0 is "None"
					if (ImGuiEx::PropertyDropdown("Clip", static_cast<int32_t>(sheet->Clips.size()) + 1, &clip,
						[&sheet](uint32_t index) { return index == 0 ? "None" : sheet->Clips[index - 1].Name.c_str(); }))
					{
						const std::string name = clip == 0 ? std::string() : sheet->Clips[static_cast<size_t>(clip - 1)].Name;
						ApplyToSelection<SpriteAnimatorComponent>(m_Context, selectedEntities, [name](SpriteAnimatorComponent& component, Entity)
						{
							component.Clip = name;
							component.Time = 0.0f;
							component.Finished = false;
						});
						firstComponent.Clip = name;
					}
				}

				if (ImGuiEx::Property("Playing", firstComponent.Playing, "Whether the clip plays when the scene starts."))
				{
					ApplyToSelection<SpriteAnimatorComponent>(m_Context, selectedEntities, [&firstComponent](SpriteAnimatorComponent& component, Entity)
					{
						component.Playing = firstComponent.Playing;
					});
				}

				if (ImGuiEx::Property("Speed", firstComponent.Speed, 0.05f, 0.0f, 20.0f))
				{
					ApplyToSelection<SpriteAnimatorComponent>(m_Context, selectedEntities, [&firstComponent](SpriteAnimatorComponent& component, Entity)
					{
						component.Speed = firstComponent.Speed;
					});
				}

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponentSection<TilemapComponent>(m_Context, entityIDs, "Tilemap", EditorResources::SpriteIcon,
			[this, firstEntity](TilemapComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();

				AssetHandle sheetHandle = firstComponent.TileSet;
				if (ImGuiEx::PropertyAssetReference<SpriteSheet>("Tile Set", sheetHandle, "The sprite sheet whose frames are the tiles. A frame flagged Solid blocks movement."))
				{
					firstComponent.TileSet = sheetHandle;
					ApplyToSelection<TilemapComponent>(m_Context, selectedEntities, [sheetHandle](TilemapComponent& component, Entity) { component.TileSet = sheetHandle; });
				}

				if (ImGuiEx::Property("Cell Size", firstComponent.CellSize, 0.01f, 0.01f, 1000.0f, "The size of one cell in world units."))
				{
					ApplyToSelection<TilemapComponent>(m_Context, selectedEntities, [&firstComponent](TilemapComponent& component, Entity) { component.CellSize = firstComponent.CellSize; });
				}

				if (ImGuiEx::Property("Point Filter", firstComponent.PointFilter, "Nearest-neighbour sampling: pixel art stays sharp when magnified."))
				{
					ApplyToSelection<TilemapComponent>(m_Context, selectedEntities, [&firstComponent](TilemapComponent& component, Entity) { component.PointFilter = firstComponent.PointFilter; });
				}

				DrawSortingProperties(m_Context, firstComponent, selectedEntities);
				DrawCollider2DMaterial(m_Context, firstComponent, selectedEntities);
				ImGuiEx::EndPropertyGrid();

				// Match Tile Size: one frame's size in world units, from the tile set.
				if (const Ref<SpriteSheet> sheet = firstComponent.TileSet ? AssetManager::GetAsset<SpriteSheet>(firstComponent.TileSet) : nullptr; sheet && !sheet->Frames.empty())
				{
					ImGuiEx::ScopedID matchID(1);
					if (ImGui::Button("Match Tile Size"))
					{
						const glm::ivec4& rect = sheet->Frames[0].Rect;
						const glm::vec2 size = glm::vec2(rect.z, rect.w) / std::max(sheet->PixelsPerUnit, 1.0f);
						firstComponent.CellSize = size;
						ApplyToSelection<TilemapComponent>(m_Context, selectedEntities, [size](TilemapComponent& component, Entity) { component.CellSize = size; });
						EditorStack::Get().MarkEntitiesEdited(selectedEntities, "Match Tile Size");
					}
				}

				// Layers: first selected entity only (the list has no common shape across a selection).
				ImGui::SeparatorText("Layers");
				ImGuiEx::ScopedID layersID(2);
				bool layersChanged = false;
				int32_t remove = -1, moveUp = -1;
				for (int32_t i = 0; i < static_cast<int32_t>(firstComponent.Layers.size()); i++)
				{
					ImGuiEx::ScopedID layerID(i);
					TilemapLayer& layer = firstComponent.Layers[static_cast<size_t>(i)];
					ImGui::SetNextItemWidth(120.0f);
					ImGui::InputText("##name", &layer.Name);
					layersChanged |= ImGui::IsItemDeactivatedAfterEdit();
					ImGui::SameLine();
					layersChanged |= ImGui::Checkbox("Visible", &layer.Visible);
					ImGui::SameLine();
					layersChanged |= ImGui::Checkbox("Collides", &layer.Collides);
					ImGui::SameLine();
					ImGui::TextDisabled("%zu tiles", layer.Cells.size());
					ImGui::SameLine();
					if (i > 0 && ImGui::SmallButton("Up"))
						moveUp = i;
					ImGui::SameLine();
					if (firstComponent.Layers.size() > 1 && ImGui::SmallButton("Remove"))
						remove = i;
				}
				if (moveUp > 0)
				{
					std::swap(firstComponent.Layers[static_cast<size_t>(moveUp)], firstComponent.Layers[static_cast<size_t>(moveUp) - 1]);
					layersChanged = true;
				}
				if (remove >= 0)
				{
					firstComponent.Layers.erase(firstComponent.Layers.begin() + remove);
					layersChanged = true;
				}
				if (ImGui::Button("Add Layer"))
				{
					TilemapLayer layer;
					layer.Name = std::format("Layer {}", firstComponent.Layers.size() + 1);
					firstComponent.Layers.push_back(std::move(layer));
					layersChanged = true;
				}
				if (layersChanged)
				{
					EditorStack::Get().MarkEntitiesEdited(selectedEntities, "Edit Tilemap Layers");
					if (PhysicsScene2D* physics = m_Context->GetPhysicsScene2D())
						physics->RebuildTilemap(firstEntity);
				}

				if (m_OpenTilePaletteCallback)
				{
					ImGui::SameLine();
					if (ImGui::Button("Open Tile Palette"))
						m_OpenTilePaletteCallback();
				}
			});

		DrawComponentSection<CircleRendererComponent>(m_Context, entityIDs, "Circle Renderer", EditorResources::SpriteIcon,
			[this](CircleRendererComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();

				if (ImGuiEx::PropertyColor("Color", firstComponent.Color))
				{
					ApplyToSelection<CircleRendererComponent>(m_Context, selectedEntities, [&firstComponent](CircleRendererComponent& component, Entity)
					{
						component.Color = firstComponent.Color;
					});
				}

				if (ImGuiEx::Property("Thickness", firstComponent.Thickness, 0.025f, 0.0f, 1.0f))
				{
					ApplyToSelection<CircleRendererComponent>(m_Context, selectedEntities, [&firstComponent](CircleRendererComponent& component, Entity)
					{
						component.Thickness = firstComponent.Thickness;
					});
				}

				if (ImGuiEx::Property("Fade", firstComponent.Fade, 0.00025f, 0.0f, 1.0f))
				{
					ApplyToSelection<CircleRendererComponent>(m_Context, selectedEntities, [&firstComponent](CircleRendererComponent& component, Entity)
					{
						component.Fade = firstComponent.Fade;
					});
				}

				DrawSortingProperties(m_Context, firstComponent, selectedEntities);

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponentSection<RigidBody2DComponent>(m_Context, entityIDs, "Rigidbody 2D", EditorResources::RigidBody2DIcon,
			[this](RigidBody2DComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				const char* bodyTypeStrings[] = { "Static", "Dynamic", "Kinematic" };
				int currentBodyType = (int)firstComponent.BodyType;
				ImGuiEx::BeginPropertyGrid();

				if (ImGuiEx::PropertyDropdown("Body Type", bodyTypeStrings, 3, &currentBodyType))
				{
					firstComponent.BodyType = (RigidBody2DComponent::Type)currentBodyType;
					ApplyToSelection<RigidBody2DComponent>(m_Context, selectedEntities, [currentBodyType](RigidBody2DComponent& component, Entity)
					{
						component.BodyType = (RigidBody2DComponent::Type)currentBodyType;
					});
				}

				if (ImGuiEx::Property("Fixed Rotation", firstComponent.FixedRotation))
				{
					ApplyToSelection<RigidBody2DComponent>(m_Context, selectedEntities, [&firstComponent](RigidBody2DComponent& component, Entity)
					{
						component.FixedRotation = firstComponent.FixedRotation;
					});
				}

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponentSection<BoxCollider2DComponent>(m_Context, entityIDs, "Box Collider 2D", EditorResources::BoxCollider2DIcon,
			[this](BoxCollider2DComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();

				if (ImGuiEx::Property("Offset", firstComponent.Offset))
				{
					ApplyToSelection<BoxCollider2DComponent>(m_Context, selectedEntities, [&firstComponent](BoxCollider2DComponent& component, Entity)
					{
						component.Offset = firstComponent.Offset;
					});
				}

				if (ImGuiEx::Property("Size", firstComponent.Size))
				{
					ApplyToSelection<BoxCollider2DComponent>(m_Context, selectedEntities, [&firstComponent](BoxCollider2DComponent& component, Entity)
					{
						component.Size = firstComponent.Size;
					});
				}

				if (ImGuiEx::Property("Density", firstComponent.Density, 0.01f, 0.0f, 1.0f))
				{
					ApplyToSelection<BoxCollider2DComponent>(m_Context, selectedEntities, [&firstComponent](BoxCollider2DComponent& component, Entity)
					{
						component.Density = firstComponent.Density;
					});
				}

				if (ImGuiEx::Property("Friction", firstComponent.Friction, 0.01f, 0.0f, 1.0f))
				{
					ApplyToSelection<BoxCollider2DComponent>(m_Context, selectedEntities, [&firstComponent](BoxCollider2DComponent& component, Entity)
					{
						component.Friction = firstComponent.Friction;
					});
				}

				if (ImGuiEx::Property("Restitution", firstComponent.Restitution, 0.01f, 0.0f, 1.0f))
				{
					ApplyToSelection<BoxCollider2DComponent>(m_Context, selectedEntities, [&firstComponent](BoxCollider2DComponent& component, Entity)
					{
						component.Restitution = firstComponent.Restitution;
					});
				}

				if (ImGuiEx::Property("Restitution Threshold", firstComponent.RestitutionThreshold, 0.01f, 0.0f))
				{
					ApplyToSelection<BoxCollider2DComponent>(m_Context, selectedEntities, [&firstComponent](BoxCollider2DComponent& component, Entity)
					{
						component.RestitutionThreshold = firstComponent.RestitutionThreshold;
					});
				}

				if (ImGuiEx::Property("Is Trigger", firstComponent.IsTrigger, "A trigger detects overlaps (OnTriggerBegin/End) and blocks nothing."))
				{
					ApplyToSelection<BoxCollider2DComponent>(m_Context, selectedEntities, [&firstComponent](BoxCollider2DComponent& component, Entity)
					{
						component.IsTrigger = firstComponent.IsTrigger;
					});
				}

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponentSection<PolygonCollider2DComponent>(m_Context, entityIDs, "Polygon Collider 2D", EditorResources::BoxCollider2DIcon,
			[this](PolygonCollider2DComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();
				if (ImGuiEx::Property("Offset", firstComponent.Offset))
				{
					ApplyToSelection<PolygonCollider2DComponent>(m_Context, selectedEntities, [&firstComponent](PolygonCollider2DComponent& component, Entity)
					{
						component.Offset = firstComponent.Offset;
					});
				}
				DrawPoints2D(m_Context, firstComponent, selectedEntities, 3, PolygonCollider2DComponent::MaxPoints);
				DrawCollider2DMaterial(m_Context, firstComponent, selectedEntities);
				ImGuiEx::EndPropertyGrid();
			});

		DrawComponentSection<CapsuleCollider2DComponent>(m_Context, entityIDs, "Capsule Collider 2D", EditorResources::CircleCollider2DIcon,
			[this](CapsuleCollider2DComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();
				if (ImGuiEx::Property("Offset", firstComponent.Offset))
				{
					ApplyToSelection<CapsuleCollider2DComponent>(m_Context, selectedEntities, [&firstComponent](CapsuleCollider2DComponent& component, Entity)
					{
						component.Offset = firstComponent.Offset;
					});
				}
				if (ImGuiEx::Property("Radius", firstComponent.Radius, 0.01f, 0.0f))
				{
					ApplyToSelection<CapsuleCollider2DComponent>(m_Context, selectedEntities, [&firstComponent](CapsuleCollider2DComponent& component, Entity)
					{
						component.Radius = firstComponent.Radius;
					});
				}
				if (ImGuiEx::Property("Height", firstComponent.Height, 0.01f, 0.0f, 0.0f, "The whole length, rounded ends included."))
				{
					ApplyToSelection<CapsuleCollider2DComponent>(m_Context, selectedEntities, [&firstComponent](CapsuleCollider2DComponent& component, Entity)
					{
						component.Height = firstComponent.Height;
					});
				}
				const char* directions[] = { "Vertical", "Horizontal" };
				int32_t direction = static_cast<int32_t>(firstComponent.CapsuleDirection);
				if (ImGuiEx::PropertyDropdown("Direction", directions, 2, &direction))
				{
					const auto value = static_cast<CapsuleCollider2DComponent::Direction>(direction);
					firstComponent.CapsuleDirection = value;
					ApplyToSelection<CapsuleCollider2DComponent>(m_Context, selectedEntities, [value](CapsuleCollider2DComponent& component, Entity)
					{
						component.CapsuleDirection = value;
					});
				}
				DrawCollider2DMaterial(m_Context, firstComponent, selectedEntities);
				ImGuiEx::EndPropertyGrid();
			});

		DrawComponentSection<EdgeCollider2DComponent>(m_Context, entityIDs, "Edge Collider 2D", EditorResources::BoxCollider2DIcon,
			[this](EdgeCollider2DComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();
				if (ImGuiEx::Property("Offset", firstComponent.Offset))
				{
					ApplyToSelection<EdgeCollider2DComponent>(m_Context, selectedEntities, [&firstComponent](EdgeCollider2DComponent& component, Entity)
					{
						component.Offset = firstComponent.Offset;
					});
				}
				if (ImGuiEx::Property("Loop", firstComponent.Loop, "Joins the last point to the first. Needs three or more points."))
				{
					ApplyToSelection<EdgeCollider2DComponent>(m_Context, selectedEntities, [&firstComponent](EdgeCollider2DComponent& component, Entity)
					{
						component.Loop = firstComponent.Loop;
					});
				}
				DrawPoints2D(m_Context, firstComponent, selectedEntities, 2, 1024);
				DrawCollider2DMaterial(m_Context, firstComponent, selectedEntities);
				ImGuiEx::EndPropertyGrid();
			});

		DrawComponentSection<Joint2DComponent>(m_Context, entityIDs, "Joint 2D", EditorResources::RigidBody2DIcon,
			[this](Joint2DComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();

				const char* types[] = { "Distance", "Revolute (Hinge)", "Prismatic (Slider)", "Weld" };
				int32_t type = static_cast<int32_t>(firstComponent.JointType);
				if (ImGuiEx::PropertyDropdown("Type", types, 4, &type))
				{
					const auto value = static_cast<Joint2DComponent::Type>(type);
					firstComponent.JointType = value;
					ApplyToSelection<Joint2DComponent>(m_Context, selectedEntities, [value](Joint2DComponent& component, Entity)
					{
						component.JointType = value;
					});
				}

				if (ImGuiEx::PropertyEntityReference("Connected Body", firstComponent.ConnectedEntity, m_Context, "The entity with a Rigidbody 2D this one is joined to. Empty anchors this body to the world."))
				{
					const UUID connected = firstComponent.ConnectedEntity;
					ApplyToSelection<Joint2DComponent>(m_Context, selectedEntities, [connected](Joint2DComponent& component, Entity)
					{
						component.ConnectedEntity = connected;
					});
				}

				const bool isDistance = firstComponent.JointType == Joint2DComponent::Type::Distance;
				const bool isHinge = firstComponent.JointType == Joint2DComponent::Type::Revolute;
				const bool isSlider = firstComponent.JointType == Joint2DComponent::Type::Prismatic;
				const bool isWeld = firstComponent.JointType == Joint2DComponent::Type::Weld;

				if (ImGuiEx::Property("Anchor", firstComponent.Anchor, 0.01f, 0.0f, 0.0f, "On this body, in its local space."))
				{
					ApplyToSelection<Joint2DComponent>(m_Context, selectedEntities, [&firstComponent](Joint2DComponent& component, Entity)
					{
						component.Anchor = firstComponent.Anchor;
					});
				}
				if (isDistance && ImGuiEx::Property("Connected Anchor", firstComponent.ConnectedAnchor, 0.01f, 0.0f, 0.0f, "On the connected body (local), or a world point when there is none."))
				{
					ApplyToSelection<Joint2DComponent>(m_Context, selectedEntities, [&firstComponent](Joint2DComponent& component, Entity)
					{
						component.ConnectedAnchor = firstComponent.ConnectedAnchor;
					});
				}
				if (ImGuiEx::Property("Collide Connected", firstComponent.CollideConnected))
				{
					ApplyToSelection<Joint2DComponent>(m_Context, selectedEntities, [&firstComponent](Joint2DComponent& component, Entity)
					{
						component.CollideConnected = firstComponent.CollideConnected;
					});
				}

				if (isDistance)
				{
					if (ImGuiEx::Property("Auto Length", firstComponent.AutoLength, "Use the distance between the anchors at the start."))
					{
						ApplyToSelection<Joint2DComponent>(m_Context, selectedEntities, [&firstComponent](Joint2DComponent& component, Entity)
						{
							component.AutoLength = firstComponent.AutoLength;
						});
					}
					if (!firstComponent.AutoLength && ImGuiEx::Property("Length", firstComponent.Length, 0.01f, 0.0f))
					{
						ApplyToSelection<Joint2DComponent>(m_Context, selectedEntities, [&firstComponent](Joint2DComponent& component, Entity)
						{
							component.Length = firstComponent.Length;
						});
					}
				}
				if ((isDistance || isWeld) && ImGuiEx::Property("Stiffness", firstComponent.Stiffness, 0.5f, 0.0f, 0.0f, "N/m. 0 is rigid; more makes a spring."))
				{
					ApplyToSelection<Joint2DComponent>(m_Context, selectedEntities, [&firstComponent](Joint2DComponent& component, Entity)
					{
						component.Stiffness = firstComponent.Stiffness;
					});
				}
				if ((isDistance || isWeld) && ImGuiEx::Property("Damping", firstComponent.Damping, 0.05f, 0.0f))
				{
					ApplyToSelection<Joint2DComponent>(m_Context, selectedEntities, [&firstComponent](Joint2DComponent& component, Entity)
					{
						component.Damping = firstComponent.Damping;
					});
				}

				if (isSlider && ImGuiEx::Property("Axis", firstComponent.Axis, 0.01f, 0.0f, 0.0f, "The slide direction, in this body's local space."))
				{
					ApplyToSelection<Joint2DComponent>(m_Context, selectedEntities, [&firstComponent](Joint2DComponent& component, Entity)
					{
						component.Axis = firstComponent.Axis;
					});
				}
				if (isHinge || isSlider)
				{
					const char* unit = isHinge ? " (degrees)" : " (units)";
					if (ImGuiEx::Property("Enable Limit", firstComponent.EnableLimit))
					{
						ApplyToSelection<Joint2DComponent>(m_Context, selectedEntities, [&firstComponent](Joint2DComponent& component, Entity)
						{
							component.EnableLimit = firstComponent.EnableLimit;
						});
					}
					if (firstComponent.EnableLimit)
					{
						if (ImGuiEx::Property(std::format("Lower Limit{}", unit).c_str(), firstComponent.LowerLimit, 0.1f))
						{
							ApplyToSelection<Joint2DComponent>(m_Context, selectedEntities, [&firstComponent](Joint2DComponent& component, Entity)
							{
								component.LowerLimit = firstComponent.LowerLimit;
							});
						}
						if (ImGuiEx::Property(std::format("Upper Limit{}", unit).c_str(), firstComponent.UpperLimit, 0.1f))
						{
							ApplyToSelection<Joint2DComponent>(m_Context, selectedEntities, [&firstComponent](Joint2DComponent& component, Entity)
							{
								component.UpperLimit = firstComponent.UpperLimit;
							});
						}
					}
					if (ImGuiEx::Property("Enable Motor", firstComponent.EnableMotor))
					{
						ApplyToSelection<Joint2DComponent>(m_Context, selectedEntities, [&firstComponent](Joint2DComponent& component, Entity)
						{
							component.EnableMotor = firstComponent.EnableMotor;
						});
					}
					if (firstComponent.EnableMotor)
					{
						if (ImGuiEx::Property(std::format("Motor Speed{}", isHinge ? " (degrees/s)" : " (units/s)").c_str(), firstComponent.MotorSpeed, 0.1f))
						{
							ApplyToSelection<Joint2DComponent>(m_Context, selectedEntities, [&firstComponent](Joint2DComponent& component, Entity)
							{
								component.MotorSpeed = firstComponent.MotorSpeed;
							});
						}
						if (ImGuiEx::Property("Max Motor Force", firstComponent.MaxMotorForce, 0.5f, 0.0f, 0.0f, isHinge ? "Maximum torque, N*m." : "Maximum force, N."))
						{
							ApplyToSelection<Joint2DComponent>(m_Context, selectedEntities, [&firstComponent](Joint2DComponent& component, Entity)
							{
								component.MaxMotorForce = firstComponent.MaxMotorForce;
							});
						}
					}
				}

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponentSection<CircleCollider2DComponent>(m_Context, entityIDs, "Circle Collider 2D", EditorResources::CircleCollider2DIcon,
			[this](CircleCollider2DComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();

				if (ImGuiEx::Property("Offset", firstComponent.Offset))
				{
					ApplyToSelection<CircleCollider2DComponent>(m_Context, selectedEntities, [&firstComponent](CircleCollider2DComponent& component, Entity)
					{
						component.Offset = firstComponent.Offset;
					});
				}

				if (ImGuiEx::Property("Radius", firstComponent.Radius))
				{
					ApplyToSelection<CircleCollider2DComponent>(m_Context, selectedEntities, [&firstComponent](CircleCollider2DComponent& component, Entity)
					{
						component.Radius = firstComponent.Radius;
					});
				}

				if (ImGuiEx::Property("Density", firstComponent.Density, 0.01f, 0.0f, 1.0f))
				{
					ApplyToSelection<CircleCollider2DComponent>(m_Context, selectedEntities, [&firstComponent](CircleCollider2DComponent& component, Entity)
					{
						component.Density = firstComponent.Density;
					});
				}

				if (ImGuiEx::Property("Friction", firstComponent.Friction, 0.01f, 0.0f, 1.0f))
				{
					ApplyToSelection<CircleCollider2DComponent>(m_Context, selectedEntities, [&firstComponent](CircleCollider2DComponent& component, Entity)
					{
						component.Friction = firstComponent.Friction;
					});
				}

				if (ImGuiEx::Property("Restitution", firstComponent.Restitution, 0.01f, 0.0f, 1.0f))
				{
					ApplyToSelection<CircleCollider2DComponent>(m_Context, selectedEntities, [&firstComponent](CircleCollider2DComponent& component, Entity)
					{
						component.Restitution = firstComponent.Restitution;
					});
				}

				if (ImGuiEx::Property("Restitution Threshold", firstComponent.RestitutionThreshold, 0.01f, 0.0f))
				{
					ApplyToSelection<CircleCollider2DComponent>(m_Context, selectedEntities, [&firstComponent](CircleCollider2DComponent& component, Entity)
					{
						component.RestitutionThreshold = firstComponent.RestitutionThreshold;
					});
				}

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponentSection<RigidBodyComponent>(m_Context, entityIDs, "Rigidbody", EditorResources::RigidBodyIcon,
			[this](RigidBodyComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				const char* bodyTypeStrings[] = { "Static", "Dynamic", "Kinematic" };
				int currentBodyType = (int)firstComponent.BodyType;
				const char* collisionDetectionStrings[] = { "Discrete", "Continuous" };
				int currentCollisionDetection = (int)firstComponent.CollisionDetection;
				ImGuiEx::BeginPropertyGrid();

				if (ImGuiEx::PropertyDropdown("Body Type", bodyTypeStrings, 3, &currentBodyType))
				{
					firstComponent.BodyType = (EBodyType)currentBodyType;
					ApplyToSelection<RigidBodyComponent>(m_Context, selectedEntities, [currentBodyType](RigidBodyComponent& component, Entity)
					{
						component.BodyType = (EBodyType)currentBodyType;
					});
				}

				if (ImGuiEx::Property("Layer ID", firstComponent.LayerID, 0u, 255u))
				{
					ApplyToSelection<RigidBodyComponent>(m_Context, selectedEntities, [&firstComponent](RigidBodyComponent& component, Entity)
					{
						component.LayerID = firstComponent.LayerID;
					});
				}

				if (ImGuiEx::Property("Dynamic Type Change", firstComponent.EnableDynamicTypeChange))
				{
					ApplyToSelection<RigidBodyComponent>(m_Context, selectedEntities, [&firstComponent](RigidBodyComponent& component, Entity)
					{
						component.EnableDynamicTypeChange = firstComponent.EnableDynamicTypeChange;
					});
				}

				if (ImGuiEx::Property("Mass", firstComponent.Mass, 0.05f, 0.001f))
				{
					ApplyToSelection<RigidBodyComponent>(m_Context, selectedEntities, [&firstComponent](RigidBodyComponent& component, Entity)
					{
						component.Mass = firstComponent.Mass;
					});
				}

				if (ImGuiEx::Property("Linear Drag", firstComponent.LinearDrag, 0.01f, 0.0f))
				{
					ApplyToSelection<RigidBodyComponent>(m_Context, selectedEntities, [&firstComponent](RigidBodyComponent& component, Entity)
					{
						component.LinearDrag = firstComponent.LinearDrag;
					});
				}

				if (ImGuiEx::Property("Angular Drag", firstComponent.AngularDrag, 0.01f, 0.0f))
				{
					ApplyToSelection<RigidBodyComponent>(m_Context, selectedEntities, [&firstComponent](RigidBodyComponent& component, Entity)
					{
						component.AngularDrag = firstComponent.AngularDrag;
					});
				}

				if (ImGuiEx::Property("Disable Gravity", firstComponent.DisableGravity))
				{
					ApplyToSelection<RigidBodyComponent>(m_Context, selectedEntities, [&firstComponent](RigidBodyComponent& component, Entity)
					{
						component.DisableGravity = firstComponent.DisableGravity;
					});
				}

				if (ImGuiEx::Property("Is Trigger", firstComponent.IsTrigger))
				{
					ApplyToSelection<RigidBodyComponent>(m_Context, selectedEntities, [&firstComponent](RigidBodyComponent& component, Entity)
					{
						component.IsTrigger = firstComponent.IsTrigger;
					});
				}

				if (ImGuiEx::PropertyDropdown("Collision Detection", collisionDetectionStrings, 2, &currentCollisionDetection))
				{
					firstComponent.CollisionDetection = (ECollisionDetectionType)currentCollisionDetection;
					ApplyToSelection<RigidBodyComponent>(m_Context, selectedEntities, [currentCollisionDetection](RigidBodyComponent& component, Entity)
					{
						component.CollisionDetection = (ECollisionDetectionType)currentCollisionDetection;
					});
				}

				if (ImGuiEx::Property("Initial Linear Velocity", firstComponent.InitialLinearVelocity))
				{
					ApplyToSelection<RigidBodyComponent>(m_Context, selectedEntities, [&firstComponent](RigidBodyComponent& component, Entity)
					{
						component.InitialLinearVelocity = firstComponent.InitialLinearVelocity;
					});
				}

				if (ImGuiEx::Property("Initial Angular Velocity", firstComponent.InitialAngularVelocity))
				{
					ApplyToSelection<RigidBodyComponent>(m_Context, selectedEntities, [&firstComponent](RigidBodyComponent& component, Entity)
					{
						component.InitialAngularVelocity = firstComponent.InitialAngularVelocity;
					});
				}

				if (ImGuiEx::Property("Max Linear Velocity", firstComponent.MaxLinearVelocity, 1.0f, 0.0f))
				{
					ApplyToSelection<RigidBodyComponent>(m_Context, selectedEntities, [&firstComponent](RigidBodyComponent& component, Entity)
					{
						component.MaxLinearVelocity = firstComponent.MaxLinearVelocity;
					});
				}

				if (ImGuiEx::Property("Max Angular Velocity", firstComponent.MaxAngularVelocity, 1.0f, 0.0f))
				{
					ApplyToSelection<RigidBodyComponent>(m_Context, selectedEntities, [&firstComponent](RigidBodyComponent& component, Entity)
					{
						component.MaxAngularVelocity = firstComponent.MaxAngularVelocity;
					});
				}

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponentSection<CharacterControllerComponent>(m_Context, entityIDs, "Character Controller", EditorResources::CharacterControllerIcon,
			[this](CharacterControllerComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();

				if (ImGuiEx::Property("Layer ID", firstComponent.LayerID, 0u, 255u))
				{
					ApplyToSelection<CharacterControllerComponent>(m_Context, selectedEntities, [&firstComponent](CharacterControllerComponent& component, Entity)
					{
						component.LayerID = firstComponent.LayerID;
					});
				}

				if (ImGuiEx::Property("Disable Gravity", firstComponent.DisableGravity))
				{
					ApplyToSelection<CharacterControllerComponent>(m_Context, selectedEntities, [&firstComponent](CharacterControllerComponent& component, Entity)
					{
						component.DisableGravity = firstComponent.DisableGravity;
					});
				}

				if (ImGuiEx::Property("Control Movement In Air", firstComponent.ControlMovementInAir))
				{
					ApplyToSelection<CharacterControllerComponent>(m_Context, selectedEntities, [&firstComponent](CharacterControllerComponent& component, Entity)
					{
						component.ControlMovementInAir = firstComponent.ControlMovementInAir;
					});
				}

				if (ImGuiEx::Property("Control Rotation In Air", firstComponent.ControlRotationInAir))
				{
					ApplyToSelection<CharacterControllerComponent>(m_Context, selectedEntities, [&firstComponent](CharacterControllerComponent& component, Entity)
					{
						component.ControlRotationInAir = firstComponent.ControlRotationInAir;
					});
				}

				if (ImGuiEx::Property("Slope Limit", firstComponent.SlopeLimitDeg, 0.5f, 0.0f, 89.0f))
				{
					ApplyToSelection<CharacterControllerComponent>(m_Context, selectedEntities, [&firstComponent](CharacterControllerComponent& component, Entity)
					{
						component.SlopeLimitDeg = firstComponent.SlopeLimitDeg;
					});
				}

				if (ImGuiEx::Property("Step Offset", firstComponent.StepOffset, 0.01f, 0.0f))
				{
					ApplyToSelection<CharacterControllerComponent>(m_Context, selectedEntities, [&firstComponent](CharacterControllerComponent& component, Entity)
					{
						component.StepOffset = firstComponent.StepOffset;
					});
				}

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponentSection<CompoundColliderComponent>(m_Context, entityIDs, "Compound Collider", EditorResources::CompoundColliderIcon,
			[this](CompoundColliderComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();

				if (ImGuiEx::Property("Include Static Children", firstComponent.IncludeStaticChildColliders))
				{
					ApplyToSelection<CompoundColliderComponent>(m_Context, selectedEntities, [&firstComponent](CompoundColliderComponent& component, Entity)
					{
						component.IncludeStaticChildColliders = firstComponent.IncludeStaticChildColliders;
					});
				}

				if (ImGuiEx::Property("Immutable", firstComponent.IsImmutable))
				{
					ApplyToSelection<CompoundColliderComponent>(m_Context, selectedEntities, [&firstComponent](CompoundColliderComponent& component, Entity)
					{
						component.IsImmutable = firstComponent.IsImmutable;
					});
				}

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponentSection<BoxColliderComponent>(m_Context, entityIDs, "Box Collider", EditorResources::BoxColliderIcon,
			[this](BoxColliderComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();

				if (ImGuiEx::Property("Half Size", firstComponent.HalfSize, 0.05f, 0.001f))
				{
					ApplyToSelection<BoxColliderComponent>(m_Context, selectedEntities, [&firstComponent](BoxColliderComponent& component, Entity)
					{
						component.HalfSize = firstComponent.HalfSize;
					});
				}

				if (ImGuiEx::Property("Offset", firstComponent.Offset))
				{
					ApplyToSelection<BoxColliderComponent>(m_Context, selectedEntities, [&firstComponent](BoxColliderComponent& component, Entity)
					{
						component.Offset = firstComponent.Offset;
					});
				}

				if (ImGuiEx::Property("Density", firstComponent.Material.Density, 0.01f, 0.0f))
				{
					ApplyToSelection<BoxColliderComponent>(m_Context, selectedEntities, [&firstComponent](BoxColliderComponent& component, Entity)
					{
						component.Material.Density = firstComponent.Material.Density;
					});
				}

				if (ImGuiEx::Property("Friction", firstComponent.Material.Friction, 0.01f, 0.0f, 1.0f))
				{
					ApplyToSelection<BoxColliderComponent>(m_Context, selectedEntities, [&firstComponent](BoxColliderComponent& component, Entity)
					{
						component.Material.Friction = firstComponent.Material.Friction;
					});
				}

				if (ImGuiEx::Property("Restitution", firstComponent.Material.Restitution, 0.01f, 0.0f, 1.0f))
				{
					ApplyToSelection<BoxColliderComponent>(m_Context, selectedEntities, [&firstComponent](BoxColliderComponent& component, Entity)
					{
						component.Material.Restitution = firstComponent.Material.Restitution;
					});
				}

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponentSection<SphereColliderComponent>(m_Context, entityIDs, "Sphere Collider", EditorResources::SphereColliderIcon,
			[this](SphereColliderComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();

				if (ImGuiEx::Property("Radius", firstComponent.Radius, 0.05f, 0.001f))
				{
					ApplyToSelection<SphereColliderComponent>(m_Context, selectedEntities, [&firstComponent](SphereColliderComponent& component, Entity)
					{
						component.Radius = firstComponent.Radius;
					});
				}

				if (ImGuiEx::Property("Offset", firstComponent.Offset))
				{
					ApplyToSelection<SphereColliderComponent>(m_Context, selectedEntities, [&firstComponent](SphereColliderComponent& component, Entity)
					{
						component.Offset = firstComponent.Offset;
					});
				}

				if (ImGuiEx::Property("Density", firstComponent.Material.Density, 0.01f, 0.0f))
				{
					ApplyToSelection<SphereColliderComponent>(m_Context, selectedEntities, [&firstComponent](SphereColliderComponent& component, Entity)
					{
						component.Material.Density = firstComponent.Material.Density;
					});
				}

				if (ImGuiEx::Property("Friction", firstComponent.Material.Friction, 0.01f, 0.0f, 1.0f))
				{
					ApplyToSelection<SphereColliderComponent>(m_Context, selectedEntities, [&firstComponent](SphereColliderComponent& component, Entity)
					{
						component.Material.Friction = firstComponent.Material.Friction;
					});
				}

				if (ImGuiEx::Property("Restitution", firstComponent.Material.Restitution, 0.01f, 0.0f, 1.0f))
				{
					ApplyToSelection<SphereColliderComponent>(m_Context, selectedEntities, [&firstComponent](SphereColliderComponent& component, Entity)
					{
						component.Material.Restitution = firstComponent.Material.Restitution;
					});
				}

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponentSection<CapsuleColliderComponent>(m_Context, entityIDs, "Capsule Collider", EditorResources::CapsuleColliderIcon,
			[this](CapsuleColliderComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();

				if (ImGuiEx::Property("Radius", firstComponent.Radius, 0.05f, 0.001f))
				{
					ApplyToSelection<CapsuleColliderComponent>(m_Context, selectedEntities, [&firstComponent](CapsuleColliderComponent& component, Entity)
					{
						component.Radius = firstComponent.Radius;
					});
				}

				if (ImGuiEx::Property("Half Height", firstComponent.HalfHeight, 0.05f, 0.001f))
				{
					ApplyToSelection<CapsuleColliderComponent>(m_Context, selectedEntities, [&firstComponent](CapsuleColliderComponent& component, Entity)
					{
						component.HalfHeight = firstComponent.HalfHeight;
					});
				}

				if (ImGuiEx::Property("Offset", firstComponent.Offset))
				{
					ApplyToSelection<CapsuleColliderComponent>(m_Context, selectedEntities, [&firstComponent](CapsuleColliderComponent& component, Entity)
					{
						component.Offset = firstComponent.Offset;
					});
				}

				if (ImGuiEx::Property("Density", firstComponent.Material.Density, 0.01f, 0.0f))
				{
					ApplyToSelection<CapsuleColliderComponent>(m_Context, selectedEntities, [&firstComponent](CapsuleColliderComponent& component, Entity)
					{
						component.Material.Density = firstComponent.Material.Density;
					});
				}

				if (ImGuiEx::Property("Friction", firstComponent.Material.Friction, 0.01f, 0.0f, 1.0f))
				{
					ApplyToSelection<CapsuleColliderComponent>(m_Context, selectedEntities, [&firstComponent](CapsuleColliderComponent& component, Entity)
					{
						component.Material.Friction = firstComponent.Material.Friction;
					});
				}

				if (ImGuiEx::Property("Restitution", firstComponent.Material.Restitution, 0.01f, 0.0f, 1.0f))
				{
					ApplyToSelection<CapsuleColliderComponent>(m_Context, selectedEntities, [&firstComponent](CapsuleColliderComponent& component, Entity)
					{
						component.Material.Restitution = firstComponent.Material.Restitution;
					});
				}

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponentSection<MeshColliderComponent>(m_Context, entityIDs, "Mesh Collider", EditorResources::MeshColliderIcon,
			[this](MeshColliderComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();

				{
					static auto s_MaterialNames = AcousticMaterialNames;
					int acousticMaterial = static_cast<int>(firstComponent.Acoustic);
					const bool mixedMaterial = IsSelectionInconsistent<AcousticMaterial>(m_Context, selectedEntities, [](Entity entity)
					{
						return entity.GetComponent<MeshColliderComponent>().Acoustic;
					});
					ImGuiEx::ScopedItemFlags mixedFlag(ImGuiItemFlags_MixedValue, mixedMaterial);
					if (ImGuiEx::PropertyDropdown("Acoustic Material", s_MaterialNames.data(), static_cast<int>(AcousticMaterialCount), &acousticMaterial, "An Audio Surface component on this entity takes precedence. With Inherit From Material, this is used only when the render material has no acoustic tag. Runtime edits enter the geometry update queue.", false))
					{
						ApplyToSelection<MeshColliderComponent>(m_Context, selectedEntities, [acousticMaterial](MeshColliderComponent& component, Entity)
						{
							component.Acoustic = static_cast<AcousticMaterial>(acousticMaterial);
						});
					}
				}

				{
					bool inherit = firstComponent.AcousticFromMaterial;
					const bool mixed = IsSelectionInconsistent<bool>(m_Context, selectedEntities, [](Entity entity)
					{
						return entity.GetComponent<MeshColliderComponent>().AcousticFromMaterial;
					});
					ImGuiEx::ScopedItemFlags flags(ImGuiItemFlags_MixedValue, mixed);
					if (ImGuiEx::Property("Inherit From Material", inherit, "Use the acoustic material set on this entity's render material (Material Editor > Acoustics), falling back to Acoustic Material above.", false))
						ApplyToSelection<MeshColliderComponent>(m_Context, selectedEntities, [inherit](MeshColliderComponent& component, Entity)
						{
							component.AcousticFromMaterial = inherit;
						});
					if (m_Context && !selectedEntities.empty() && !mixed)
					{
						if (Entity entity = m_Context->TryGetEntityWithUUID(selectedEntities.front()))
						{
							const std::string effective = AcousticMaterialName(m_Context->ResolveAcousticMaterial(entity));
							ImGuiEx::Property("Effective Acoustics", effective,
								"The tag the acoustics actually use for this entity, after the Audio Surface component and inheritance.");
						}
					}
				}

				{
					static const char* modes[] = { "Static", "Dynamic", "Disabled" };
					int mode = static_cast<int>(firstComponent.AcousticMotion);
					const bool mixed = IsSelectionInconsistent<AcousticGeometryMode>(m_Context, selectedEntities, [](Entity entity)
					{
						return entity.GetComponent<MeshColliderComponent>().AcousticMotion;
					});
					ImGuiEx::ScopedItemFlags flags(ImGuiItemFlags_MixedValue, mixed);
					if (ImGuiEx::PropertyDropdown("Acoustic Motion", modes, 3, &mode, "Dynamic follows world transforms; Static captures the initial transform. Disabled excludes this mesh from acoustics.", false))
						ApplyToSelection<MeshColliderComponent>(m_Context, selectedEntities, [mode](MeshColliderComponent& component, Entity)
						{
							component.AcousticMotion = static_cast<AcousticGeometryMode>(mode);
						});
				}

				AssetHandle colliderAsset = firstComponent.ColliderAsset;
				if (ImGuiEx::PropertyAssetReference<MeshSource>("Collider Mesh", colliderAsset, "Leave empty to use the entity Static Mesh. Mesh colliders are static in Jolt."))
				{
					firstComponent.ColliderAsset = colliderAsset;
					ApplyToSelection<MeshColliderComponent>(m_Context, selectedEntities, [colliderAsset](MeshColliderComponent& component, Entity)
					{
						component.ColliderAsset = colliderAsset;
					});
				}

				bool allSubmeshes = firstComponent.SubmeshIndex == MeshColliderComponent::AllSubmeshes;
				if (ImGuiEx::Property("All Submeshes", allSubmeshes, "Collide with every part of the mesh; off collides with one part, picked by index."))
				{
					const uint32_t index = allSubmeshes ? MeshColliderComponent::AllSubmeshes : 0u;
					ApplyToSelection<MeshColliderComponent>(m_Context, selectedEntities, [index](MeshColliderComponent& component, Entity)
					{
						component.SubmeshIndex = index;
					});
				}

				if (!allSubmeshes && ImGuiEx::Property("Submesh Index", firstComponent.SubmeshIndex, 0u))
				{
					ApplyToSelection<MeshColliderComponent>(m_Context, selectedEntities, [&firstComponent](MeshColliderComponent& component, Entity)
					{
						component.SubmeshIndex = firstComponent.SubmeshIndex;
					});
				}

				if (ImGuiEx::Property("Use Shared Shape", firstComponent.UseSharedShape))
				{
					ApplyToSelection<MeshColliderComponent>(m_Context, selectedEntities, [&firstComponent](MeshColliderComponent& component, Entity)
					{
						component.UseSharedShape = firstComponent.UseSharedShape;
					});
				}

				const char* collisionComplexityStrings[] = { "Default", "Simple As Complex", "Complex As Simple" };
				int currentCollisionComplexity = (int)firstComponent.CollisionComplexity;
				if (ImGuiEx::PropertyDropdown("Collision Complexity", collisionComplexityStrings, 3, &currentCollisionComplexity))
				{
					firstComponent.CollisionComplexity = (ECollisionComplexity)currentCollisionComplexity;
					ApplyToSelection<MeshColliderComponent>(m_Context, selectedEntities, [currentCollisionComplexity](MeshColliderComponent& component, Entity)
					{
						component.CollisionComplexity = (ECollisionComplexity)currentCollisionComplexity;
					});
				}

				if (ImGuiEx::Property("Density", firstComponent.Material.Density, 0.01f, 0.0f))
				{
					ApplyToSelection<MeshColliderComponent>(m_Context, selectedEntities, [&firstComponent](MeshColliderComponent& component, Entity)
					{
						component.Material.Density = firstComponent.Material.Density;
					});
				}

				if (ImGuiEx::Property("Friction", firstComponent.Material.Friction, 0.01f, 0.0f, 1.0f))
				{
					ApplyToSelection<MeshColliderComponent>(m_Context, selectedEntities, [&firstComponent](MeshColliderComponent& component, Entity)
					{
						component.Material.Friction = firstComponent.Material.Friction;
					});
				}

				if (ImGuiEx::Property("Restitution", firstComponent.Material.Restitution, 0.01f, 0.0f, 1.0f))
				{
					ApplyToSelection<MeshColliderComponent>(m_Context, selectedEntities, [&firstComponent](MeshColliderComponent& component, Entity)
					{
						component.Material.Restitution = firstComponent.Material.Restitution;
					});
				}

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponentSection<ParticleSystemComponent>(m_Context, entityIDs, "Particle System", EditorResources::SpriteIcon,
			[this](ParticleSystemComponent& first, const std::vector<UUID>& selectedEntities, bool)
			{
				using namespace Particles;
				Particles::Settings& s = first.Settings;
				bool changed = false;
				const auto range = [&changed](const char* label, Range& r, float speed, float min, float max, const char* help)
				{
					glm::vec2 v{ r.Min, r.Max };
					if (ImGuiEx::Property(label, v, speed, min, max, help))
					{
						r.Min = std::min(v.x, v.y);
						r.Max = std::max(v.x, v.y);
						changed = true;
					}
				};
				if (m_OpenParticleEditorCallback && ImGui::Button("Open Particle Editor"))
					m_OpenParticleEditorCallback();

				ImGui::SeparatorText("Emission");
				ImGuiEx::BeginPropertyGrid();
				changed |= ImGuiEx::Property("Looping", s.Looping, "A looping system starts over when its duration ends.");
				changed |= ImGuiEx::Property("Duration", s.Duration, 0.1f, 0.05f, 600.0f, "Seconds the system releases particles for (a looping one repeats)." );
				if (s.Looping)
					changed |= ImGuiEx::Property("Prewarm", s.Prewarm, "Start as if one whole duration had already played.");
				changed |= ImGuiEx::Property("Rate", s.Rate, 0.5f, 0.0f, 10000.0f, "Particles released per second.");
				changed |= ImGuiEx::Property("Max Particles", s.MaxParticles, 1u, Particles::Settings::HardMaxParticles, "A release past this many live particles is dropped.");
				int32_t bursts = static_cast<int32_t>(s.BurstCount);
				if (ImGuiEx::Property("Bursts", bursts, 0, static_cast<int32_t>(Particles::Settings::MaxBursts), "Particles released all at once."))
				{
					s.BurstCount = static_cast<uint32_t>(std::clamp(bursts, 0, static_cast<int32_t>(Particles::Settings::MaxBursts)));
					changed = true;
				}
				for (uint32_t i = 0; i < s.BurstCount; i++)
				{
					Burst& burst = s.Bursts[i];
					int32_t count = burst.Count, cycles = burst.Cycles;
					const std::string name = std::format("Burst {}", i + 1);
					changed |= ImGuiEx::Property((name + " Time").c_str(), burst.Time, 0.05f, 0.0f, 600.0f);
					if (ImGuiEx::Property((name + " Count").c_str(), count, 1, 5000))
					{
						burst.Count = static_cast<uint16_t>(std::clamp(count, 1, 5000));
						changed = true;
					}
					if (ImGuiEx::Property((name + " Cycles").c_str(), cycles, 1, 1000))
					{
						burst.Cycles = static_cast<uint16_t>(std::clamp(cycles, 1, 1000));
						changed = true;
					}
					changed |= ImGuiEx::Property((name + " Interval").c_str(), burst.Interval, 0.05f, 0.0f, 600.0f);
				}
				ImGuiEx::EndPropertyGrid();

				ImGui::SeparatorText("Shape");
				ImGuiEx::BeginPropertyGrid();
				static const char* shapes[] = { "Cone", "Sphere", "Box" };
				changed |= ImGuiEx::PropertyDropdown("Shape", shapes, 3, s.EmitShape);
				if (s.EmitShape == Shape::Cone)
					changed |= ImGuiEx::Property("Cone Angle", s.ConeAngle, 0.5f, 0.0f, 180.0f, "Degrees from straight up (+Y). 180 sends particles every way.");
				if (s.EmitShape != Shape::Box)
					changed |= ImGuiEx::Property("Radius", s.Radius, 0.05f, 0.0f, 1000.0f);
				else
					changed |= ImGuiEx::Property("Box Size", s.BoxSize, 0.05f, 0.0f, 1000.0f);
				ImGuiEx::EndPropertyGrid();

				ImGui::SeparatorText("Start");
				ImGuiEx::BeginPropertyGrid();
				range("Lifetime", first.Settings.Lifetime, 0.05f, 0.01f, 600.0f, "Seconds a particle lives (min, max).");
				range("Speed", first.Settings.Speed, 0.05f, 0.0f, 1000.0f, "Units per second at birth (min, max).");
				range("Size", first.Settings.Size, 0.01f, 0.0f, 1000.0f, "Width and height at birth (min, max).");
				range("Rotation", first.Settings.Rotation, 1.0f, -360.0f, 360.0f, "Degrees at birth (min, max).");
				range("Angular Velocity", first.Settings.AngularVelocity, 1.0f, -3600.0f, 3600.0f, "Degrees per second (min, max).");
				changed |= ImGuiEx::PropertyColor("Start Color", s.StartColor, "Multiplied by the color over life. Values above 1 glow with Bloom.");
				ImGuiEx::EndPropertyGrid();

				ImGui::SeparatorText("Motion");
				ImGuiEx::BeginPropertyGrid();
				changed |= ImGuiEx::Property("Gravity", s.Gravity, 0.05f, -1000.0f, 1000.0f, "Units per second squared. A negative Y falls; a positive one rises, like smoke.");
				changed |= ImGuiEx::Property("Drag", s.Drag, 0.01f, 0.0f, 60.0f);
				changed |= ImGuiEx::Property("World Space", s.WorldSpace, "On: particles stay where they were released when the entity moves. Off: they follow it.");
				ImGuiEx::EndPropertyGrid();

				ImGui::SeparatorText("Over Life");
				ImGuiEx::BeginPropertyGrid();
				changed |= ParticleWidgets::PropertyCurve("Size Over Life", s.SizeOverLife, 4.0f, "A multiplier on the start size from birth (left) to death (right). Double-click to add a key, right-click a key to remove it.");
				changed |= ParticleWidgets::PropertyCurve("Speed Over Life", s.SpeedOverLife, 4.0f, "A multiplier on the particle's speed over its life.");
				changed |= ParticleWidgets::PropertyGradient("Color Over Life", s.ColorOverLife, "Multiplied by the start color from birth (left) to death (right).");
				ImGuiEx::EndPropertyGrid();

				ImGui::SeparatorText("Texture Sheet");
				ImGuiEx::BeginPropertyGrid();
				changed |= ImGuiEx::Property("Tiles X", s.TilesX, 1u, 64u, "Columns of frames in the texture.");
				changed |= ImGuiEx::Property("Tiles Y", s.TilesY, 1u, 64u, "Rows of frames. Frame 0 is the top left.");
				if (s.TilesX * s.TilesY > 1)
				{
					static const char* frameModes[] = { "Over Life", "Random" };
					changed |= ImGuiEx::PropertyDropdown("Frame", frameModes, 2, s.Frames);
					if (s.Frames == FrameMode::OverLife)
						changed |= ImGuiEx::Property("Cycles", s.FrameCycles, 0.05f, 0.0f, 100.0f, "Times the whole sheet plays during a particle's life.");
				}
				ImGuiEx::EndPropertyGrid();

				ImGui::SeparatorText("Renderer");
				ImGuiEx::BeginPropertyGrid();
				static const char* renderModes[] = { "Billboard (3D)", "Sprite (2D)" };
				changed |= ImGuiEx::PropertyDropdown("Render Mode", renderModes, 2, s.Mode);
				static const char* blends[] = { "Alpha", "Additive", "Premultiplied" };
				changed |= ImGuiEx::PropertyDropdown("Blend", blends, 3, s.BlendMode, "2D sprites always blend with alpha.");
				if (s.Mode == RenderMode::Billboard3D)
					changed |= ImGuiEx::Property("Soft Distance", s.SoftDistance, 0.01f, 0.0f, 100.0f, "Units over which a particle fades out where it meets geometry. 0 is a hard edge.");
				AssetHandle texture = first.Texture;
				if (ImGuiEx::PropertyAssetReference<Texture2D>("Texture", texture, "None draws a plain colored shape."))
				{
					first.Texture = texture;
					changed = true;
				}
				changed |= ImGuiEx::Property("Seed", first.Seed, 0u, 0xFFFFFFFFu, "The same seed gives the same particles every time.");
				changed |= ImGuiEx::Property("Play On Awake", first.PlayOnAwake, "Off: nothing is released until a script plays it.");
				changed |= ImGuiEx::Property("Preview In Editor", first.PreviewInEditor, "Plays the system in the editor, outside Play.");
				if (s.Mode == RenderMode::Sprite2D)
					DrawSortingProperties(m_Context, first, selectedEntities);
				ImGuiEx::EndPropertyGrid();

				// Whole-component copy: a multi-selection edit sets every field on all of them.
				if (changed)
					ApplyToSelection<ParticleSystemComponent>(m_Context, selectedEntities, [&first](ParticleSystemComponent& component, Entity) { component = first; });
			});

		DrawComponentSection<UICanvasComponent>(m_Context, entityIDs, "UI Canvas", EditorResources::SpriteIcon,
			[this](UICanvasComponent& first, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();
				bool changed = false;
				static const char* scaleModes[] = { "Constant Pixel Size", "Scale With Screen", "Fit Inside", "Fill Screen" };
				changed |= ImGuiEx::PropertyDropdown("Scale Mode", scaleModes, 4, first.ScaleMode);
				if (first.ScaleMode == UI::ScaleMode::ConstantPixelSize)
					changed |= ImGuiEx::Property("Scale Factor", first.ScaleFactor, 0.05f, 0.1f, 8.0f);
				else
					changed |= ImGuiEx::Property("Reference Resolution", first.ReferenceResolution, 1.0f, 16.0f, 16384.0f);
				if (first.ScaleMode == UI::ScaleMode::ScaleWithScreen)
					changed |= ImGuiEx::PropertySlider("Match Width / Height", first.Match, 0.0f, 1.0f);
				changed |= ImGuiEx::Property("Sort Order", first.SortOrder, -1000, 1000);
				ImGuiEx::EndPropertyGrid();
				// Whole-component copy: a multi-selection edit sets every field on all of them.
				if (changed)
					ApplyToSelection<UICanvasComponent>(m_Context, selectedEntities, [&first](UICanvasComponent& component, Entity) { component = first; });
			});

		DrawComponentSection<UIElementComponent>(m_Context, entityIDs, "UI Element", EditorResources::SpriteIcon,
			[this](UIElementComponent& first, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();
				bool changed = false;
				changed |= ImGuiEx::Property("Visible", first.Visible);
				changed |= ImGuiEx::Property("Anchor Min", first.Rect.AnchorMin, 0.01f, 0.0f, 1.0f);
				changed |= ImGuiEx::Property("Anchor Max", first.Rect.AnchorMax, 0.01f, 0.0f, 1.0f);
				changed |= ImGuiEx::Property("Offset Min", first.Rect.OffsetMin, 1.0f);
				changed |= ImGuiEx::Property("Offset Max", first.Rect.OffsetMax, 1.0f);
				static const char* layoutKinds[] = { "None", "Horizontal", "Vertical", "Grid" };
				changed |= ImGuiEx::PropertyDropdown("Layout", layoutKinds, 4, first.Layout.Kind);
				if (first.Layout.Kind != UI::LayoutKind::None)
				{
					changed |= ImGuiEx::Property("Padding (L B R T)", first.Layout.Padding, 1.0f, 0.0f, 1000.0f);
					changed |= ImGuiEx::Property("Spacing", first.Layout.Spacing, 0.5f, 0.0f, 1000.0f);
					static const char* aligns[] = { "Start", "Center", "End" };
					changed |= ImGuiEx::PropertyDropdown("Align", aligns, 3, first.Layout.Align);
					if (first.Layout.Kind == UI::LayoutKind::Grid)
					{
						changed |= ImGuiEx::Property("Cell Size", first.Layout.CellSize, 1.0f, 1.0f, 4096.0f);
						changed |= ImGuiEx::Property("Columns", first.Layout.Columns, 1, 64);
					}
					else
					{
						changed |= ImGuiEx::Property("Expand Children", first.Layout.ExpandChildren);
					}
				}
				ImGuiEx::EndPropertyGrid();
				if (changed)
					ApplyToSelection<UIElementComponent>(m_Context, selectedEntities, [&first](UIElementComponent& component, Entity) { component = first; });
			});

		DrawComponentSection<UIImageComponent>(m_Context, entityIDs, "UI Image", EditorResources::SpriteIcon,
			[this](UIImageComponent& first, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();
				bool changed = false;
				changed |= ImGuiEx::PropertyColor("Color", first.Color);
				AssetHandle texture = first.Texture;
				if (ImGuiEx::PropertyAssetReference<Texture2D>("Texture", texture, "UI image only accepts texture assets"))
				{
					first.Texture = texture;
					changed = true;
				}
				changed |= ImGuiEx::Property("Point Filter", first.PointFilter);
				changed |= ImGuiEx::Property("Slice Border (L B R T)", first.SliceBorder, 0.5f, 0.0f, 4096.0f);
				if (first.SliceBorder != glm::vec4(0.0f))
					changed |= ImGuiEx::Property("Slice Scale", first.SliceScale, 0.05f, 0.05f, 16.0f);
				ImGuiEx::EndPropertyGrid();
				if (changed)
					ApplyToSelection<UIImageComponent>(m_Context, selectedEntities, [&first](UIImageComponent& component, Entity) { component = first; });
			});

		DrawComponentSection<UIWidgetComponent>(m_Context, entityIDs, "UI Widget", EditorResources::SpriteIcon,
			[this](UIWidgetComponent& first, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();
				bool changed = false;
				static const char* kinds[] = { "Button", "Toggle", "Slider", "Progress Bar", "Text Input", "Scroll List" };
				changed |= ImGuiEx::PropertyDropdown("Kind", kinds, 6, first.Kind);
				changed |= ImGuiEx::Property("Interactable", first.Interactable);
				changed |= ImGuiEx::Property("Start Focused", first.StartFocused);
				if (first.Kind != UIWidgetKind::ScrollList)
				{
					changed |= ImGuiEx::PropertyColor("Hover Tint", first.HoverTint);
					changed |= ImGuiEx::PropertyColor("Pressed Tint", first.PressedTint);
					changed |= ImGuiEx::PropertyColor("Disabled Tint", first.DisabledTint);
					changed |= ImGuiEx::PropertyColor("Accent Color", first.AccentColor);
				}
				if (first.Kind == UIWidgetKind::Toggle)
					changed |= ImGuiEx::Property("Checked", first.Checked);
				if (first.Kind == UIWidgetKind::Slider || first.Kind == UIWidgetKind::ProgressBar)
				{
					changed |= ImGuiEx::Property("Min", first.MinValue, 0.05f);
					changed |= ImGuiEx::Property("Max", first.MaxValue, 0.05f);
					changed |= ImGuiEx::Property("Value", first.Value, 0.01f);
				}
				if (first.Kind == UIWidgetKind::Slider)
					changed |= ImGuiEx::Property("Whole Numbers", first.WholeNumbers);
				if (first.Kind == UIWidgetKind::TextInput)
				{
					changed |= ImGuiEx::Property("Placeholder", first.Placeholder);
					changed |= ImGuiEx::Property("Max Length", first.MaxLength, 1, 4096);
				}
				if (first.Kind == UIWidgetKind::ScrollList)
					changed |= ImGuiEx::Property("Scroll Offset", first.ScrollOffset, 1.0f, 0.0f, 100000.0f);
				ImGuiEx::EndPropertyGrid();
				if (changed)
					ApplyToSelection<UIWidgetComponent>(m_Context, selectedEntities, [&first](UIWidgetComponent& component, Entity) { component = first; });
			});

		DrawComponentSection<UITextComponent>(m_Context, entityIDs, "UI Text", EditorResources::TextIcon,
			[this](UITextComponent& first, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();
				bool changed = false;
				changed |= ImGuiEx::PropertyMultiline("Text", first.Text);
				AssetHandle font = first.FontHandle;
				if (ImGuiEx::PropertyAssetReference<Font>("Font", font, "UI text only accepts font assets"))
				{
					first.FontHandle = font;
					changed = true;
				}
				changed |= ImGuiEx::PropertyColor("Color", first.Color);
				changed |= ImGuiEx::Property("Size", first.Size, 0.5f, 1.0f, 1000.0f);
				changed |= ImGuiEx::Property("Line Spacing", first.LineSpacing, 0.025f);
				static const char* alignH[] = { "Left", "Center", "Right" };
				static const char* alignV[] = { "Top", "Middle", "Bottom" };
				changed |= ImGuiEx::PropertyDropdown("Horizontal", alignH, 3, first.AlignH);
				changed |= ImGuiEx::PropertyDropdown("Vertical", alignV, 3, first.AlignV);
				changed |= ImGuiEx::Property("Wrap", first.Wrap);
				ImGuiEx::EndPropertyGrid();
				if (changed)
					ApplyToSelection<UITextComponent>(m_Context, selectedEntities, [&first](UITextComponent& component, Entity) { component = first; });
			});

		DrawComponentSection<TextComponent>(m_Context, entityIDs, "Text", EditorResources::TextIcon,
			[this](TextComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();

				if (ImGuiEx::PropertyMultiline("Text", firstComponent.TextString))
				{
					ApplyToSelection<TextComponent>(m_Context, selectedEntities, [&firstComponent](TextComponent& component, Entity)
					{
						component.TextString = firstComponent.TextString;
					});
				}

				AssetHandle fontHandle = firstComponent.FontHandle;
				const bool mixedFont = selectedEntities.size() > 1 && IsSelectionInconsistent<AssetHandle>(m_Context, selectedEntities, [](Entity entity)
				{
					return entity.GetComponent<TextComponent>().FontHandle;
				});
				ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixedFont);
				if (ImGuiEx::PropertyAssetReference<Font>("Font", fontHandle, "Text component only accepts font assets"))
				{
					firstComponent.FontHandle = fontHandle;
					ApplyToSelection<TextComponent>(m_Context, selectedEntities, [fontHandle](TextComponent& component, Entity)
					{
						component.FontHandle = fontHandle;
					});
				}
				ImGui::PopItemFlag();

				if (ImGuiEx::PropertyColor("Color", firstComponent.Color))
				{
					ApplyToSelection<TextComponent>(m_Context, selectedEntities, [&firstComponent](TextComponent& component, Entity)
					{
						component.Color = firstComponent.Color;
					});
				}

				if (ImGuiEx::Property("Kerning", firstComponent.Kerning, 0.025f))
				{
					ApplyToSelection<TextComponent>(m_Context, selectedEntities, [&firstComponent](TextComponent& component, Entity)
					{
						component.Kerning = firstComponent.Kerning;
					});
				}

				if (ImGuiEx::Property("Line Spacing", firstComponent.LineSpacing, 0.025f))
				{
					ApplyToSelection<TextComponent>(m_Context, selectedEntities, [&firstComponent](TextComponent& component, Entity)
					{
						component.LineSpacing = firstComponent.LineSpacing;
					});
				}

				if (ImGuiEx::Property("Max Width", firstComponent.MaxWidth, 0.1f, 0.0f, 1000.0f))
				{
					ApplyToSelection<TextComponent>(m_Context, selectedEntities, [&firstComponent](TextComponent& component, Entity)
					{
						component.MaxWidth = firstComponent.MaxWidth;
					});
				}

				if (ImGuiEx::Property("Screen Space", firstComponent.ScreenSpace))
				{
					ApplyToSelection<TextComponent>(m_Context, selectedEntities, [&firstComponent](TextComponent& component, Entity)
					{
						component.ScreenSpace = firstComponent.ScreenSpace;
					});
				}

				DrawSortingProperties(m_Context, firstComponent, selectedEntities);

				if (ImGuiEx::Property("Drop Shadow", firstComponent.DropShadow))
				{
					ApplyToSelection<TextComponent>(m_Context, selectedEntities, [&firstComponent](TextComponent& component, Entity)
					{
						component.DropShadow = firstComponent.DropShadow;
					});
				}

				if (firstComponent.DropShadow)
				{
					if (ImGuiEx::Property("Shadow Distance", firstComponent.ShadowDistance, 0.01f, 0.0f, 100.0f))
					{
						ApplyToSelection<TextComponent>(m_Context, selectedEntities, [&firstComponent](TextComponent& component, Entity)
						{
							component.ShadowDistance = firstComponent.ShadowDistance;
						});
					}

					if (ImGuiEx::PropertyColor("Shadow Color", firstComponent.ShadowColor))
					{
						ApplyToSelection<TextComponent>(m_Context, selectedEntities, [&firstComponent](TextComponent& component, Entity)
						{
							component.ShadowColor = firstComponent.ShadowColor;
						});
					}
				}

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponentSection<AudioSourceComponent>(m_Context, entityIDs, "Audio Source", EditorResources::AudioIcon,
			[this](AudioSourceComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool isMultiEdit) mutable
			{
				auto& component = firstComponent;
				auto& config = component.Config;

				// The Sound authors the audio; gameplay controls placement, volume and pitch.
				ImGuiEx::BeginPropertyGrid();
				DrawAudioEventPicker(component, selectedEntities);

				auto property = [&]<typename T>(const char* label, T AudioSourceComponent::* member, auto draw)
				{
					T value = component.*member;
					const bool mixed = IsSelectionInconsistent<T>(m_Context, selectedEntities, [member](Entity entity)
					{
						return entity.GetComponent<AudioSourceComponent>().*member;
					});
					ImGuiEx::ScopedItemFlags flags(ImGuiItemFlags_MixedValue, mixed);
					if (draw(label, value))
						ApplyToSelection<AudioSourceComponent>(m_Context, selectedEntities, [member, value](AudioSourceComponent& source, Entity)
						{
							source.*member = value;
						});
				};
				property("Priority", &AudioSourceComponent::Priority, [](const char* label, int& value)
				{
					return ImGuiEx::Property(label, value, 0, 256, "0 is highest priority; the voice budget keeps the highest priorities mixing.", false);
				});
				property("Distance Culling", &AudioSourceComponent::DistanceCulling, [](const char* label, bool& value)
				{
					return ImGuiEx::Property(label, value, "Release beyond every listener's event max distance. Loops suspend; inaudible one-shots are discarded.", false);
				});

				if (ImGuiEx::Property("Volume", config.VolumeMultiplier, 0.01f, 0.0f, 2.0f))
				{
					ApplyToSelection<AudioSourceComponent>(m_Context, selectedEntities, [&config](AudioSourceComponent& audioComponent, Entity)
					{
						audioComponent.Config.VolumeMultiplier = config.VolumeMultiplier;
					});
				}

				if (ImGuiEx::Property("Pitch", config.PitchMultiplier, 0.01f, 0.0f, 3.0f))
				{
					ApplyToSelection<AudioSourceComponent>(m_Context, selectedEntities, [&config](AudioSourceComponent& audioComponent, Entity)
					{
						audioComponent.Config.PitchMultiplier = config.PitchMultiplier;
					});
				}

				if (ImGuiEx::Property("Play On Awake", config.PlayOnAwake))
				{
					ApplyToSelection<AudioSourceComponent>(m_Context, selectedEntities, [&config](AudioSourceComponent& audioComponent, Entity)
					{
						audioComponent.Config.PlayOnAwake = config.PlayOnAwake;
					});
				}
				ImGuiEx::EndPropertyGrid();

				DrawAudioParameterOverrides(component);

				if (component.LegacyAudio && !component.Event.IsValid())
					ImGui::TextWrapped("Raw-file playback has been removed. Legacy asset %llu is preserved for migration. Create a Sound from it and assign it above.",
						static_cast<unsigned long long>(component.LegacyAudio));
				else if (!component.Event.IsValid() && component.Event.HasReference())
					ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "This source names an FMOD event (%s) but no Sound, so it is silent. Create a Sound and assign it above.",
						component.Event.Path.empty() ? component.Event.Guid.c_str() : component.Event.Path.c_str());
				ImGui::TextDisabled("Looping and spatialization are set on the Sound.");

				if (isMultiEdit)
					ImGui::TextDisabled("Parameter overrides apply to the first selected entity.");
			});


		DrawComponentSection<MusicDirectorComponent>(m_Context, entityIDs, "Music Director", EditorResources::AudioIcon,
			[this](MusicDirectorComponent& first, const std::vector<UUID>& selected, bool)
			{
				auto reference = first.Event;
				const bool mixedEvent = IsSelectionInconsistent<std::string>(m_Context, selected, [](Entity entity)
				{
					return entity.GetComponent<MusicDirectorComponent>().Event.GetReference();
				});
				if (ImGuiEx::SurfaceEventPicker("Music Event", reference, false, mixedEvent, true))
					ApplyToSelection<MusicDirectorComponent>(m_Context, selected, [&reference](MusicDirectorComponent& component, Entity)
					{
						component.Event = reference;
					});
				ImGuiEx::BeginPropertyGrid();
				auto property = [&]<typename T>(const char* label, T MusicDirectorComponent::* member, auto draw)
				{
					T value = first.*member;
					const bool mixed = IsSelectionInconsistent<T>(m_Context, selected, [member](Entity entity)
					{
						return entity.GetComponent<MusicDirectorComponent>().*member;
					});
					ImGuiEx::ScopedItemFlags flags(ImGuiItemFlags_MixedValue, mixed);
					if (draw(label, value))
						ApplyToSelection<MusicDirectorComponent>(m_Context, selected, [member, value](MusicDirectorComponent& component, Entity)
						{
							component.*member = value;
						});
				};
				property("Play On Awake", &MusicDirectorComponent::PlayOnAwake, [](const char* label, bool& value)
				{
					return ImGuiEx::Property(label, value, "", false);
				});
				property("Initial State", &MusicDirectorComponent::InitialState, [](const char* label, std::string& value)
				{
					return ImGuiEx::Property(label, value, "Optional authored State parameter label.", false);
				});
				property("Intensity", &MusicDirectorComponent::Intensity, [](const char* label, float& value)
				{
					return ImGuiEx::Property(label, value, 0.01f, 0.0f, 1.0f, "Authored Intensity parameter, 0–1.", false);
				});
				ImGuiEx::EndPropertyGrid();
				ImGui::TextWrapped("Startup settings apply on Play. Use one Music Director per scene. Scripts control state, intensity, layers and stingers through Lux.Music.");
			});

		DrawComponentSection<AudioPortalComponent>(m_Context, entityIDs, "Audio Portal", EditorResources::AudioIcon,
			[this](AudioPortalComponent& first, const std::vector<UUID>& selected, bool)
			{
				ImGuiEx::BeginPropertyGrid();
				auto property = [&]<typename T>(const char* label, T AudioPortalComponent::* member, auto draw)
				{
					T value = first.*member;
					const bool mixed = IsSelectionInconsistent<T>(m_Context, selected, [member](Entity entity)
					{
						return entity.GetComponent<AudioPortalComponent>().*member;
					});
					ImGuiEx::ScopedItemFlags flags(ImGuiItemFlags_MixedValue, mixed);
					if (draw(label, value))
						ApplyToSelection<AudioPortalComponent>(m_Context, selected, [member, value](AudioPortalComponent& component, Entity)
						{
							component.*member = value;
						});
				};
				property("Open", &AudioPortalComponent::Open, [](const char* label, float& value) { return ImGuiEx::Property(label, value, 0.01f, 0.0f, 1.0f, "0 closes the shutter; 1 removes it.", false); });
				property("Half Extents", &AudioPortalComponent::HalfExtents, [](const char* label, glm::vec3& value) { return ImGuiEx::Property(label, value, 0.01f, 0.001f, 10000.0f, "Local X width, Y height, Z thickness.", false); });
				property("Blend Distance", &AudioPortalComponent::BlendDistance, [](const char* label, float& value) { return ImGuiEx::Property(label, value, 0.1f, 0.001f, 10000.0f, "Room leakage fades away with distance from the opening.", false); });
				property("Material", &AudioPortalComponent::Material, [](const char* label, AcousticMaterial& value)
				{
					int material = static_cast<int>(value);
					static auto s_MaterialNames = AcousticMaterialNames;
					const bool changed = ImGuiEx::PropertyDropdown(label, s_MaterialNames.data(), static_cast<int>(AcousticMaterialCount), &material, "Shutter acoustic material.", false);
					value = static_cast<AcousticMaterial>(material);
					return changed;
				});
				auto room = [&](const char* label, UUID& value)
				{
					if (!ImGuiEx::PropertyEntityReference(label, value, m_Context, "Select an entity with Audio Zone. Both rooms are needed for zone blending."))
						return false;
					const Entity target = m_Context->TryGetEntityWithUUID(value);
					if (value != 0 && (!target || !target.HasComponent<AudioZoneComponent>()))
					{
						LUX_CORE_ERROR_TAG("Audio", "Portal room must have AudioZoneComponent");
						return false;
					}
					return true;
				};
				property("Room A", &AudioPortalComponent::ZoneA, room);
				property("Room B", &AudioPortalComponent::ZoneB, room);
				ImGuiEx::EndPropertyGrid();
				ImGui::TextWrapped("The shutter retracts toward local -X. Place it in a real opening: it cannot cut a hole in another collider. Disable overlapping door mesh acoustics when this portal supplies the barrier.");
			});

		DrawComponentSection<AudioZoneComponent>(m_Context, entityIDs, "Audio Zone", EditorResources::AudioIcon,
			[this](AudioZoneComponent& first, const std::vector<UUID>& selected, bool)
			{
				ImGuiEx::BeginPropertyGrid();
				auto property = [&]<typename T>(const char* label, T AudioZoneComponent::* member, auto draw)
				{
					T value = first.*member;
					const bool mixed = IsSelectionInconsistent<T>(m_Context, selected, [member](Entity entity)
					{
						return entity.GetComponent<AudioZoneComponent>().*member;
					});
					ImGuiEx::ScopedItemFlags flags(ImGuiItemFlags_MixedValue, mixed);
					if (draw(label, value))
						ApplyToSelection<AudioZoneComponent>(m_Context, selected, [member, value](AudioZoneComponent& component, Entity)
						{
							component.*member = value;
						});
				};
				property("Shape", &AudioZoneComponent::Shape, [](const char* label, AudioZoneShape& value)
				{
					static const char* names[] = { "Box", "Sphere", "Collider" };
					return ImGuiEx::PropertyDropdown(label, names, 3, value, "Collider uses exactly one box, sphere or capsule on this entity.", false);
				});
				property("Priority", &AudioZoneComponent::Priority, [](const char* label, float& value)
				{
					return ImGuiEx::Property(label, value, 0.1f, -10000.0f, 10000.0f, "", false);
				});
				property("Blend Distance", &AudioZoneComponent::BlendDistance, [](const char* label, float& value)
				{
					return ImGuiEx::Property(label, value, 0.1f, 0.0f, 10000.0f, "", false);
				});
				property("Fade Time (s)", &AudioZoneComponent::FadeTime, [](const char* label, float& value)
				{
					return ImGuiEx::Property(label, value, 0.1f, 0.0f, 60.0f, "", false);
				});
				property("Ambience Volume", &AudioZoneComponent::Volume, [](const char* label, float& value)
				{
					return ImGuiEx::Property(label, value, 0.1f, 0.0f, 10.0f, "", false);
				});
				if (first.Shape != AudioZoneShape::Collider)
				{
					property("Offset", &AudioZoneComponent::Offset, [](const char* label, glm::vec3& value)
					{
						return ImGuiEx::Property(label, value, 0.1f, 0.0f, 0.0f, "", false);
					});
					if (first.Shape == AudioZoneShape::Box)
						property("Half Extents", &AudioZoneComponent::HalfExtents, [](const char* label, glm::vec3& value)
						{
							return ImGuiEx::Property(label, value, 0.1f, 0.001f, 10000.0f, "", false);
						});
					else
						property("Radius", &AudioZoneComponent::Radius, [](const char* label, float& value)
						{
							return ImGuiEx::Property(label, value, 0.1f, 0.001f, 10000.0f, "", false);
						});
				}
				ImGuiEx::EndPropertyGrid();
				auto picker = [&](const char* label, AudioEventRef AudioZoneComponent::* member, bool snapshot)
				{
					ImGuiEx::ScopedID id(label);
					const auto& reference = first.*member;
					{
						// A looping Sound for ambience and a mixer snapshot by ID, both in the reference's Sound slot.
						const bool mixedNative = IsSelectionInconsistent<uint64_t>(m_Context, selected, [member](Entity entity)
						{
							return static_cast<uint64_t>((entity.GetComponent<AudioZoneComponent>().*member).Sound);
						});
						AudioEventRef value = reference;
						bool changed = false;
						if (!snapshot)
							changed = ImGuiEx::SurfaceEventPicker(label, value, false, mixedNative);
						else
						{
							const auto project = Project::GetActive();
							static const std::vector<AudioMixerSnapshot> s_None;
							const auto& snapshots = project ? project->GetConfig().Audio.Mixer.Snapshots : s_None;
							const auto assigned = std::find_if(snapshots.begin(), snapshots.end(), [&](const AudioMixerSnapshot& entry) { return entry.ID == value.Sound; });
							const std::string preview = mixedNative ? "Multiple values" : value.Sound == 0 ? "None"
								: assigned != snapshots.end() ? assigned->Name : "Missing snapshot";
							if (ImGui::BeginCombo(label, preview.c_str()))
							{
								if (ImGui::Selectable("None", value.Sound == 0))
								{
									value.Sound = 0;
									changed = true;
								}
								for (const AudioMixerSnapshot& entry : snapshots)
								{
									ImGuiEx::ScopedID snapshotID(&entry);
									if (ImGui::Selectable(entry.Name.c_str(), entry.ID == value.Sound))
									{
										value.Sound = entry.ID;
										changed = true;
									}
								}
								if (snapshots.empty())
									ImGui::TextDisabled("No snapshots. Add one in View > Audio Mixer.");
								ImGui::EndCombo();
							}
						}
						if (changed)
						{
							ApplyToSelection<AudioZoneComponent>(m_Context, selected, [member, &value](AudioZoneComponent& component, Entity)
							{
								(component.*member).Sound = value.Sound;
							});
						}
					}
				};
				picker("Ambience", &AudioZoneComponent::AmbienceEvent, false);
				picker("Snapshot", &AudioZoneComponent::Snapshot, true);
				ImGui::TextWrapped("Blend Distance fades inward from the boundary in metres. Higher priorities consume the mix first; equal priorities share it. The zone weight is the snapshot's intensity (View > Audio Mixer).");
				if (first.Shape == AudioZoneShape::Collider)
					ImGui::TextWrapped("Collider mode supports exactly one box, sphere or capsule on this entity. Mesh and 2D colliders are unsupported.");
				if (m_Context->IsRunning() && selected.size() == 1)
					ImGui::Text("Current weight: %.3f", m_Context->GetAudioZoneWeight(selected.front()));
			});

		DrawComponentSection<AudioSurfaceComponent>(m_Context, entityIDs, "Audio Surface", EditorResources::AudioIcon,
			[this](AudioSurfaceComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();
				static auto s_MaterialNames = AcousticMaterialNames;
				int acousticMaterial = static_cast<int>(firstComponent.Material);
				const bool mixedMaterial = IsSelectionInconsistent<AcousticMaterial>(m_Context, selectedEntities, [](Entity entity)
				{
					return entity.GetComponent<AudioSurfaceComponent>().Material;
				});
				{
					ImGuiEx::ScopedItemFlags mixedFlag(ImGuiItemFlags_MixedValue, mixedMaterial);
					if (ImGuiEx::PropertyDropdown("Acoustic Material", s_MaterialNames.data(), static_cast<int>(AcousticMaterialCount), &acousticMaterial, "", false))
					{
						ApplyToSelection<AudioSurfaceComponent>(m_Context, selectedEntities, [acousticMaterial](AudioSurfaceComponent& component, Entity)
						{
							component.Material = static_cast<AcousticMaterial>(acousticMaterial);
						});
					}
				}
				auto property = [&]<typename T>(const char* label, T AudioSurfaceComponent::* member, auto draw)
				{
					T value = firstComponent.*member;
					const bool mixed = IsSelectionInconsistent<T>(m_Context, selectedEntities, [member](Entity entity)
					{
						return entity.GetComponent<AudioSurfaceComponent>().*member;
					});
					ImGuiEx::ScopedItemFlags flags(ImGuiItemFlags_MixedValue, mixed);
					if (draw(label, value))
						ApplyToSelection<AudioSurfaceComponent>(m_Context, selectedEntities, [member, value](AudioSurfaceComponent& component, Entity)
						{
							component.*member = value;
						});
				};
				property("Physics Sounds", &AudioSurfaceComponent::PhysicsSounds, [](const char* label, bool& value)
				{
					return ImGuiEx::Property(label, value, "Enables impacts and motion sounds when this entity is the dynamic body.", false);
				});
				property("Auto Footsteps", &AudioSurfaceComponent::AutoFootsteps, [](const char* label, bool& value)
				{
					return ImGuiEx::Property(label, value, "Ground query with distance-based cadence. Disable when calling Audio.PlayFootstep from a script.", false);
				});
				property("Stride Length (m)", &AudioSurfaceComponent::StrideLength, [](const char* label, float& value)
				{
					return ImGuiEx::Property(label, value, 0.05f, 0.05f, 100.0f, "", false);
				});
				property("Ground Probe (m)", &AudioSurfaceComponent::GroundProbeDistance, [](const char* label, float& value)
				{
					return ImGuiEx::Property(label, value, 0.05f, 0.01f, 100.0f, "Distance below the entity origin to search for a walkable 3D collider.", false);
				});
				property("Footstep Weight (kg)", &AudioSurfaceComponent::FootstepWeight, [](const char* label, float& value)
				{
					return ImGuiEx::Property(label, value, 1.0f, 0.01f, 10000.0f, "", false);
				});
				ImGuiEx::EndPropertyGrid();
				auto picker = [&](const char* label, AudioEventRef AudioSurfaceComponent::* member)
				{
					auto reference = firstComponent.*member;
					const bool mixed = IsSelectionInconsistent<std::string>(m_Context, selectedEntities, [member](Entity entity)
					{
						return (entity.GetComponent<AudioSurfaceComponent>().*member).GetReference();
					});
					if (ImGuiEx::SurfaceEventPicker(label, reference, true, mixed))
						ApplyToSelection<AudioSurfaceComponent>(m_Context, selectedEntities, [member, &reference](AudioSurfaceComponent& component, Entity)
						{
							component.*member = reference;
						});
				};
				picker("Footstep Override", &AudioSurfaceComponent::FootstepOverride);
				picker("Impact Override", &AudioSurfaceComponent::ImpactOverride);
				ImGui::TextWrapped("Overrides the collider acoustic material and the project surface table events. The collider underfoot "
								   "supplies footstep overrides. Acoustic material changes apply on Play; physics audio uses current surface tags.");
			});

		DrawComponentSection<AudioListenerComponent>(m_Context, entityIDs, "Audio Listener", EditorResources::AudioListenerIcon,
			[this](AudioListenerComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();
				if (ImGuiEx::Property("Listener Index", firstComponent.ListenerIndex, 0, AudioListener::MaxListeners - 1,
					"Active listeners need unique indices. On a conflict, the lowest entity UUID wins."))
				{
					firstComponent.ListenerIndex = std::clamp(firstComponent.ListenerIndex, 0, AudioListener::MaxListeners - 1);
					ApplyToSelection<AudioListenerComponent>(m_Context, selectedEntities, [&firstComponent](AudioListenerComponent& component, Entity)
					{
						component.ListenerIndex = firstComponent.ListenerIndex;
					});
				}
				if (ImGuiEx::PropertySlider("Weight", firstComponent.Weight, 0.0f, 1.0f))
				{
					ApplyToSelection<AudioListenerComponent>(m_Context, selectedEntities, [&firstComponent](AudioListenerComponent& component, Entity)
					{
						component.Weight = firstComponent.Weight;
					});
				}
				if (ImGuiEx::Property("Use Attenuation Target", firstComponent.UseAttenuationTarget))
				{
					ApplyToSelection<AudioListenerComponent>(m_Context, selectedEntities, [&firstComponent](AudioListenerComponent& component, Entity)
					{
						component.UseAttenuationTarget = firstComponent.UseAttenuationTarget;
					});
				}
				if (firstComponent.UseAttenuationTarget && ImGuiEx::PropertyEntityReference("Attenuation Target", firstComponent.AttenuationTarget,
					m_Context, "Sounds pan at this listener and attenuate at the target. A missing target uses the listener position. Prefabs retain only targets within their own hierarchy."))
				{
					ApplyToSelection<AudioListenerComponent>(m_Context, selectedEntities, [&firstComponent](AudioListenerComponent& component, Entity)
					{
						component.AttenuationTarget = firstComponent.AttenuationTarget;
					});
				}
				ImGuiEx::EndPropertyGrid();
				ImGui::TextWrapped("Weights blend listeners. Ray-traced acoustics use the highest-weight listener (lowest index on a tie).");
			});

		DrawComponentSection<StaticMeshComponent>(m_Context, entityIDs, "Static Mesh", EditorResources::StaticMeshIcon,
			[this](StaticMeshComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();

				AssetHandle meshHandle = firstComponent.StaticMesh;
				const bool mixedMesh = selectedEntities.size() > 1 && IsSelectionInconsistent<AssetHandle>(m_Context, selectedEntities, [](Entity entity)
				{
					return entity.GetComponent<StaticMeshComponent>().StaticMesh;
				});
				ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixedMesh);
				if (ImGuiEx::PropertyAssetReference<MeshSource>("Mesh", meshHandle, "Static Mesh accepts mesh source assets"))
				{
					firstComponent.StaticMesh = meshHandle;
					ApplyToSelection<StaticMeshComponent>(m_Context, selectedEntities, [meshHandle](StaticMeshComponent& component, Entity)
					{
						component.StaticMesh = meshHandle;
					});
				}
				ImGui::PopItemFlag();

				AssetHandle materialHandle = firstComponent.MaterialTable && firstComponent.MaterialTable->HasMaterial(0) ? firstComponent.MaterialTable->GetMaterial(0) : AssetHandle{};
				// A primitive's built-in default material is not a registered asset; show None, not a red Invalid.
				if (materialHandle && !AssetManager::IsAssetHandleValid(materialHandle))
					materialHandle = 0;
				const bool mixedMaterial = selectedEntities.size() > 1 && IsSelectionInconsistent<AssetHandle>(m_Context, selectedEntities, [](Entity entity)
				{
					auto& smc = entity.GetComponent<StaticMeshComponent>();
					return (smc.MaterialTable && smc.MaterialTable->HasMaterial(0)) ? smc.MaterialTable->GetMaterial(0) : AssetHandle{};
				});
				ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixedMaterial);
				if (ImGuiEx::PropertyAssetReference<MaterialAsset>("Material", materialHandle, "Static Mesh material override expects a material asset"))
				{
					if (!firstComponent.MaterialTable)
						firstComponent.MaterialTable = Ref<MaterialTable>::Create();
					if (materialHandle)
						firstComponent.MaterialTable->SetMaterial(0, materialHandle);
					else
						firstComponent.MaterialTable->ClearMaterial(0);
					ApplyToSelection<StaticMeshComponent>(m_Context, selectedEntities, [materialHandle](StaticMeshComponent& component, Entity)
					{
						if (!component.MaterialTable)
							component.MaterialTable = Ref<MaterialTable>::Create();
						if (materialHandle)
							component.MaterialTable->SetMaterial(0, materialHandle);
						else
							component.MaterialTable->ClearMaterial(0);
					});
				}
				ImGui::PopItemFlag();

				ImGuiEx::EndPropertyGrid();

				DrawStaticMeshMaterialSlots(firstComponent, selectedEntities);
			});

		DrawComponentSection<SkeletonComponent>(m_Context, entityIDs, "Skeleton", EditorResources::SkeletonIcon,
			[this](SkeletonComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();

				AssetHandle skeletonHandle = firstComponent.SkeletonAsset;
				if (ImGuiEx::PropertyAssetReference<Skeleton>("Skeleton", skeletonHandle, "The skeleton asset (.lskel) whose bones skin this mesh"))
				{
					firstComponent.SkeletonAsset = skeletonHandle;
					ApplyToSelection<SkeletonComponent>(m_Context, selectedEntities, [skeletonHandle](SkeletonComponent& component, Entity)
					{
						component.SkeletonAsset = skeletonHandle;
					});
				}

				std::string debugBone = firstComponent.DebugBone;
				if (ImGuiEx::Property("Debug Bone", debugBone, "Bone to bend for testing, by name (editor preview until animation exists)"))
				{
					firstComponent.DebugBone = debugBone;
					ApplyToSelection<SkeletonComponent>(m_Context, selectedEntities, [debugBone](SkeletonComponent& component, Entity)
					{
						component.DebugBone = debugBone;
					});
				}

				float debugBend = firstComponent.DebugBendDegrees;
				if (ImGuiEx::Property("Debug Bend", debugBend, 1.0f, -180.0f, 180.0f, "Degrees about the debug bone's local X axis"))
				{
					firstComponent.DebugBendDegrees = debugBend;
					ApplyToSelection<SkeletonComponent>(m_Context, selectedEntities, [debugBend](SkeletonComponent& component, Entity)
					{
						component.DebugBendDegrees = debugBend;
					});
				}

				AssetHandle clipHandle = firstComponent.Clip;
				if (ImGuiEx::PropertyAssetReference<Animation>("Clip", clipHandle, "The animation clip (.lanim) to play. Changing it while playing cross-fades."))
				{
					const bool fade = firstComponent.Clip != 0 && firstComponent.PlayOnStart;
					ApplyToSelection<SkeletonComponent>(m_Context, selectedEntities, [clipHandle, fade](SkeletonComponent& component, Entity)
					{
						component.CrossFadeTo(clipHandle, fade ? component.FadeSeconds : 0.0f);
						component.State.PreviewOverride = false;
					});
				}

				AssetHandle graphHandle = firstComponent.Graph;
				if (ImGuiEx::PropertyAssetReference<AnimationGraph>("Graph", graphHandle, "An animation graph (.lanimgraph) that chooses the clip from parameters. Replaces Clip."))
				{
					firstComponent.Graph = graphHandle;
					ApplyToSelection<SkeletonComponent>(m_Context, selectedEntities, [graphHandle](SkeletonComponent& component, Entity)
					{
						component.Graph = graphHandle;
						component.State = AnimatorState(); // restart from the graph's default state
					});
				}

				const auto editBool = [&](const char* label, bool SkeletonComponent::* member, const char* help)
				{
					bool value = firstComponent.*member;
					if (ImGuiEx::Property(label, value, help))
					{
						firstComponent.*member = value;
						ApplyToSelection<SkeletonComponent>(m_Context, selectedEntities, [value, member](SkeletonComponent& component, Entity)
						{
							component.*member = value;
							component.State.PreviewOverride = false; // an authored setting changed: the preview follows it again
						});
					}
				};
				const auto editFloat = [&](const char* label, float SkeletonComponent::* member, float step, float low, float high, const char* help)
				{
					float value = firstComponent.*member;
					if (ImGuiEx::Property(label, value, step, low, high, help))
					{
						firstComponent.*member = value;
						ApplyToSelection<SkeletonComponent>(m_Context, selectedEntities, [value, member](SkeletonComponent& component, Entity)
						{
							component.*member = value;
							component.State.PreviewOverride = false; // an authored setting changed: the preview follows it again
						});
					}
				};
				editBool("Play On Start", &SkeletonComponent::PlayOnStart, "Play when the scene starts; off holds the pose at Start Time until something plays it");
				editBool("Loop", &SkeletonComponent::Loop, "Wrap at the end of the clip instead of holding the last pose");
				editFloat("Speed", &SkeletonComponent::Speed, 0.05f, -4.0f, 4.0f, "Playback rate, negative plays backwards");
				editFloat("Start Time", &SkeletonComponent::StartTime, 0.01f, 0.0f, 3600.0f, "Seconds into the clip where playback starts; scrubs the pose in the editor while Play On Start is off");
				editFloat("Fade Seconds", &SkeletonComponent::FadeSeconds, 0.01f, 0.0f, 10.0f, "Cross-fade length when the clip is changed");
				editBool("Root Motion", &SkeletonComponent::RootMotion, "While the scene runs, move the entity by the root bone's horizontal travel instead of animating it in place");
				std::string rootBone = firstComponent.RootBone;
				if (ImGuiEx::Property("Root Bone", rootBone, "Bone whose travel is the root motion; empty uses the first bone with a position track"))
				{
					firstComponent.RootBone = rootBone;
					ApplyToSelection<SkeletonComponent>(m_Context, selectedEntities, [rootBone](SkeletonComponent& component, Entity)
					{
						component.RootBone = rootBone;
					});
				}

				ImGuiEx::EndPropertyGrid();
				DrawSkeletonAnimationTools(firstComponent);
			});

		DrawComponentSection<DirectionalLightComponent>(m_Context, entityIDs, "Directional Light", EditorResources::DirectionalLightIcon,
			[this](DirectionalLightComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();

				if (ImGuiEx::PropertyColor("Radiance", firstComponent.Radiance))
				{
					ApplyToSelection<DirectionalLightComponent>(m_Context, selectedEntities, [&firstComponent](DirectionalLightComponent& component, Entity)
					{
						component.Radiance = firstComponent.Radiance;
					});
				}

				if (ImGuiEx::Property("Intensity", firstComponent.Intensity, 0.1f, 0.0f, 100.0f))
				{
					ApplyToSelection<DirectionalLightComponent>(m_Context, selectedEntities, [&firstComponent](DirectionalLightComponent& component, Entity)
					{
						component.Intensity = firstComponent.Intensity;
					});
				}

				{
					static const char* unitNames[] = { "Unitless", "Lux" };
					static const LightUnit unitValues[] = { LightUnit::Unitless, LightUnit::Lux };
					int32_t current = 0;
					for (int32_t i = 0; i < 2; i++) if (unitValues[i] == firstComponent.Unit) current = i;
					if (ImGuiEx::PropertyDropdown("Unit", unitNames, 2, &current))
					{
						firstComponent.Unit = unitValues[current];
						ApplyToSelection<DirectionalLightComponent>(m_Context, selectedEntities, [&firstComponent](DirectionalLightComponent& component, Entity)
						{
							component.Unit = firstComponent.Unit;
						});
					}
				}

				if (ImGuiEx::Property("Use Color Temperature", firstComponent.UseColorTemperature))
				{
					ApplyToSelection<DirectionalLightComponent>(m_Context, selectedEntities, [&firstComponent](DirectionalLightComponent& component, Entity)
					{
						component.UseColorTemperature = firstComponent.UseColorTemperature;
					});
				}

				if (ImGuiEx::Property("Temperature (K)", firstComponent.ColorTemperature, 50.0f, 1000.0f, 15000.0f))
				{
					ApplyToSelection<DirectionalLightComponent>(m_Context, selectedEntities, [&firstComponent](DirectionalLightComponent& component, Entity)
					{
						component.ColorTemperature = firstComponent.ColorTemperature;
					});
				}

				if (ImGuiEx::Property("Shadow Amount", firstComponent.ShadowAmount, 0.01f, 0.0f, 1.0f))
				{
					ApplyToSelection<DirectionalLightComponent>(m_Context, selectedEntities, [&firstComponent](DirectionalLightComponent& component, Entity)
					{
						component.ShadowAmount = firstComponent.ShadowAmount;
					});
				}

				if (ImGuiEx::Property("Cast Shadows", firstComponent.CastShadows))
				{
					ApplyToSelection<DirectionalLightComponent>(m_Context, selectedEntities, [&firstComponent](DirectionalLightComponent& component, Entity)
					{
						component.CastShadows = firstComponent.CastShadows;
					});
				}

				if (ImGuiEx::Property("Soft Shadows", firstComponent.SoftShadows))
				{
					ApplyToSelection<DirectionalLightComponent>(m_Context, selectedEntities, [&firstComponent](DirectionalLightComponent& component, Entity)
					{
						component.SoftShadows = firstComponent.SoftShadows;
					});
				}

				if (ImGuiEx::Property("Light Size", firstComponent.LightSize, 0.01f, 0.0f, 10.0f))
				{
					ApplyToSelection<DirectionalLightComponent>(m_Context, selectedEntities, [&firstComponent](DirectionalLightComponent& component, Entity)
					{
						component.LightSize = firstComponent.LightSize;
					});
				}

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponentSection<PointLightComponent>(m_Context, entityIDs, "Point Light", EditorResources::PointLightIcon,
			[this](PointLightComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();

				if (ImGuiEx::PropertyColor("Radiance", firstComponent.Radiance))
				{
					ApplyToSelection<PointLightComponent>(m_Context, selectedEntities, [&firstComponent](PointLightComponent& component, Entity)
					{
						component.Radiance = firstComponent.Radiance;
					});
				}

				if (ImGuiEx::Property("Intensity", firstComponent.Intensity, 0.1f, 0.0f, 100000.0f))
				{
					ApplyToSelection<PointLightComponent>(m_Context, selectedEntities, [&firstComponent](PointLightComponent& component, Entity)
					{
						component.Intensity = firstComponent.Intensity;
					});
				}

				{
					static const char* unitNames[] = { "Unitless", "Lumens", "Candela" };
					static const LightUnit unitValues[] = { LightUnit::Unitless, LightUnit::Lumens, LightUnit::Candela };
					int32_t current = 0;
					for (int32_t i = 0; i < 3; i++) if (unitValues[i] == firstComponent.Unit) current = i;
					if (ImGuiEx::PropertyDropdown("Unit", unitNames, 3, &current))
					{
						firstComponent.Unit = unitValues[current];
						ApplyToSelection<PointLightComponent>(m_Context, selectedEntities, [&firstComponent](PointLightComponent& component, Entity)
						{
							component.Unit = firstComponent.Unit;
						});
					}
				}

				if (ImGuiEx::Property("Use Color Temperature", firstComponent.UseColorTemperature))
				{
					ApplyToSelection<PointLightComponent>(m_Context, selectedEntities, [&firstComponent](PointLightComponent& component, Entity)
					{
						component.UseColorTemperature = firstComponent.UseColorTemperature;
					});
				}

				if (ImGuiEx::Property("Temperature (K)", firstComponent.ColorTemperature, 50.0f, 1000.0f, 15000.0f))
				{
					ApplyToSelection<PointLightComponent>(m_Context, selectedEntities, [&firstComponent](PointLightComponent& component, Entity)
					{
						component.ColorTemperature = firstComponent.ColorTemperature;
					});
				}

				if (ImGuiEx::Property("Radius", firstComponent.Radius, 0.1f, 0.0f, 1000.0f))
				{
					ApplyToSelection<PointLightComponent>(m_Context, selectedEntities, [&firstComponent](PointLightComponent& component, Entity)
					{
						component.Radius = firstComponent.Radius;
					});
				}

				if (ImGuiEx::Property("Light Size", firstComponent.LightSize, 0.01f, 0.0f, 10.0f))
				{
					ApplyToSelection<PointLightComponent>(m_Context, selectedEntities, [&firstComponent](PointLightComponent& component, Entity)
					{
						component.LightSize = firstComponent.LightSize;
					});
				}

				if (ImGuiEx::Property("Cast Shadows", firstComponent.CastsShadows))
				{
					ApplyToSelection<PointLightComponent>(m_Context, selectedEntities, [&firstComponent](PointLightComponent& component, Entity)
					{
						component.CastsShadows = firstComponent.CastsShadows;
					});
				}

				if (ImGuiEx::Property("Soft Shadows", firstComponent.SoftShadows))
				{
					ApplyToSelection<PointLightComponent>(m_Context, selectedEntities, [&firstComponent](PointLightComponent& component, Entity)
					{
						component.SoftShadows = firstComponent.SoftShadows;
					});
				}

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponentSection<SpotLightComponent>(m_Context, entityIDs, "Spot Light", EditorResources::SpotLightIcon,
			[this](SpotLightComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();

				if (ImGuiEx::PropertyColor("Radiance", firstComponent.Radiance))
				{
					ApplyToSelection<SpotLightComponent>(m_Context, selectedEntities, [&firstComponent](SpotLightComponent& component, Entity)
					{
						component.Radiance = firstComponent.Radiance;
					});
				}

				if (ImGuiEx::Property("Intensity", firstComponent.Intensity, 0.1f, 0.0f, 100000.0f))
				{
					ApplyToSelection<SpotLightComponent>(m_Context, selectedEntities, [&firstComponent](SpotLightComponent& component, Entity)
					{
						component.Intensity = firstComponent.Intensity;
					});
				}

				{
					static const char* unitNames[] = { "Unitless", "Lumens", "Candela" };
					static const LightUnit unitValues[] = { LightUnit::Unitless, LightUnit::Lumens, LightUnit::Candela };
					int32_t current = 0;
					for (int32_t i = 0; i < 3; i++) if (unitValues[i] == firstComponent.Unit) current = i;
					if (ImGuiEx::PropertyDropdown("Unit", unitNames, 3, &current))
					{
						firstComponent.Unit = unitValues[current];
						ApplyToSelection<SpotLightComponent>(m_Context, selectedEntities, [&firstComponent](SpotLightComponent& component, Entity)
						{
							component.Unit = firstComponent.Unit;
						});
					}
				}

				if (ImGuiEx::Property("Use Color Temperature", firstComponent.UseColorTemperature))
				{
					ApplyToSelection<SpotLightComponent>(m_Context, selectedEntities, [&firstComponent](SpotLightComponent& component, Entity)
					{
						component.UseColorTemperature = firstComponent.UseColorTemperature;
					});
				}

				if (ImGuiEx::Property("Temperature (K)", firstComponent.ColorTemperature, 50.0f, 1000.0f, 15000.0f))
				{
					ApplyToSelection<SpotLightComponent>(m_Context, selectedEntities, [&firstComponent](SpotLightComponent& component, Entity)
					{
						component.ColorTemperature = firstComponent.ColorTemperature;
					});
				}

				if (ImGuiEx::Property("Range", firstComponent.Range, 0.1f, 0.0f, 1000.0f))
				{
					ApplyToSelection<SpotLightComponent>(m_Context, selectedEntities, [&firstComponent](SpotLightComponent& component, Entity)
					{
						component.Range = firstComponent.Range;
					});
				}

				if (ImGuiEx::Property("Angle", firstComponent.Angle, 1.0f, 0.0f, 90.0f))
				{
					ApplyToSelection<SpotLightComponent>(m_Context, selectedEntities, [&firstComponent](SpotLightComponent& component, Entity)
					{
						component.Angle = firstComponent.Angle;
					});
				}

				if (ImGuiEx::Property("Angle Attenuation", firstComponent.AngleAttenuation, 0.01f, 0.0f, 10.0f))
				{
					ApplyToSelection<SpotLightComponent>(m_Context, selectedEntities, [&firstComponent](SpotLightComponent& component, Entity)
					{
						component.AngleAttenuation = firstComponent.AngleAttenuation;
					});
				}

				if (ImGuiEx::Property("Light Size", firstComponent.Falloff, 0.01f, 0.0f, 10.0f))
				{
					ApplyToSelection<SpotLightComponent>(m_Context, selectedEntities, [&firstComponent](SpotLightComponent& component, Entity)
					{
						component.Falloff = firstComponent.Falloff;
					});
				}

				if (ImGuiEx::Property("Cast Shadows", firstComponent.CastsShadows))
				{
					ApplyToSelection<SpotLightComponent>(m_Context, selectedEntities, [&firstComponent](SpotLightComponent& component, Entity)
					{
						component.CastsShadows = firstComponent.CastsShadows;
					});
				}

				if (ImGuiEx::Property("Soft Shadows", firstComponent.SoftShadows))
				{
					ApplyToSelection<SpotLightComponent>(m_Context, selectedEntities, [&firstComponent](SpotLightComponent& component, Entity)
					{
						component.SoftShadows = firstComponent.SoftShadows;
					});
				}

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponentSection<AreaLightComponent>(m_Context, entityIDs, "Area Light", EditorResources::PointLightIcon,
			[this](AreaLightComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();

				{
					static const char* shapeNames[] = { "Rectangle", "Disk", "Sphere", "Tube" };
					int32_t current = static_cast<int32_t>(firstComponent.Shape);
					if (ImGuiEx::PropertyDropdown("Shape", shapeNames, 4, &current))
					{
						firstComponent.Shape = static_cast<AreaLightShape>(current);
						ApplyToSelection<AreaLightComponent>(m_Context, selectedEntities, [&firstComponent](AreaLightComponent& component, Entity)
						{
							component.Shape = firstComponent.Shape;
						});
					}
				}

				if (ImGuiEx::PropertyColor("Radiance", firstComponent.Radiance))
				{
					ApplyToSelection<AreaLightComponent>(m_Context, selectedEntities, [&firstComponent](AreaLightComponent& component, Entity)
					{
						component.Radiance = firstComponent.Radiance;
					});
				}

				if (ImGuiEx::Property("Intensity", firstComponent.Intensity, 0.1f, 0.0f, 100000.0f))
				{
					ApplyToSelection<AreaLightComponent>(m_Context, selectedEntities, [&firstComponent](AreaLightComponent& component, Entity)
					{
						component.Intensity = firstComponent.Intensity;
					});
				}

				{
					static const char* unitNames[] = { "Unitless (luminance)", "Lumens" };
					static const LightUnit unitValues[] = { LightUnit::Unitless, LightUnit::Lumens };
					int32_t current = firstComponent.Unit == LightUnit::Lumens ? 1 : 0;
					if (ImGuiEx::PropertyDropdown("Unit", unitNames, 2, &current))
					{
						firstComponent.Unit = unitValues[current];
						ApplyToSelection<AreaLightComponent>(m_Context, selectedEntities, [&firstComponent](AreaLightComponent& component, Entity)
						{
							component.Unit = firstComponent.Unit;
						});
					}
				}

				if (ImGuiEx::Property("Use Color Temperature", firstComponent.UseColorTemperature))
				{
					ApplyToSelection<AreaLightComponent>(m_Context, selectedEntities, [&firstComponent](AreaLightComponent& component, Entity)
					{
						component.UseColorTemperature = firstComponent.UseColorTemperature;
					});
				}

				if (ImGuiEx::Property("Temperature (K)", firstComponent.ColorTemperature, 50.0f, 1000.0f, 15000.0f))
				{
					ApplyToSelection<AreaLightComponent>(m_Context, selectedEntities, [&firstComponent](AreaLightComponent& component, Entity)
					{
						component.ColorTemperature = firstComponent.ColorTemperature;
					});
				}

				if (firstComponent.Shape == AreaLightShape::Rect)
				{
					if (ImGuiEx::Property("Width", firstComponent.Width, 0.01f, 0.0f, 100.0f))
					{
						ApplyToSelection<AreaLightComponent>(m_Context, selectedEntities, [&firstComponent](AreaLightComponent& component, Entity)
						{
							component.Width = firstComponent.Width;
						});
					}

					if (ImGuiEx::Property("Height", firstComponent.Height, 0.01f, 0.0f, 100.0f))
					{
						ApplyToSelection<AreaLightComponent>(m_Context, selectedEntities, [&firstComponent](AreaLightComponent& component, Entity)
						{
							component.Height = firstComponent.Height;
						});
					}
				}
				else
				{
					if (firstComponent.Shape == AreaLightShape::Tube && ImGuiEx::Property("Length", firstComponent.Length, 0.01f, 0.0f, 100.0f))
					{
						ApplyToSelection<AreaLightComponent>(m_Context, selectedEntities, [&firstComponent](AreaLightComponent& component, Entity)
						{
							component.Length = firstComponent.Length;
						});
					}

					if (ImGuiEx::Property("Radius", firstComponent.Radius, 0.01f, 0.0f, 50.0f))
					{
						ApplyToSelection<AreaLightComponent>(m_Context, selectedEntities, [&firstComponent](AreaLightComponent& component, Entity)
						{
							component.Radius = firstComponent.Radius;
						});
					}
				}

				// Spheres and tubes emit all around; only flat shapes have a back side.
				const bool flatShape = firstComponent.Shape == AreaLightShape::Rect || firstComponent.Shape == AreaLightShape::Disk;
				if (flatShape && ImGuiEx::Property("Two Sided", firstComponent.TwoSided))
				{
					ApplyToSelection<AreaLightComponent>(m_Context, selectedEntities, [&firstComponent](AreaLightComponent& component, Entity)
					{
						component.TwoSided = firstComponent.TwoSided;
					});
				}

				if (ImGuiEx::Property("Range", firstComponent.Range, 0.1f, 0.0f, 1000.0f))
				{
					ApplyToSelection<AreaLightComponent>(m_Context, selectedEntities, [&firstComponent](AreaLightComponent& component, Entity)
					{
						component.Range = firstComponent.Range;
					});
				}

				if (ImGuiEx::Property("Cast Shadows", firstComponent.CastsShadows,
					"Takes shadow atlas tiles: one per lit side of a rectangle or disk, six for a sphere or tube. A mesh placed inside the light's surface shadows it too; turn off that mesh's shadow casting."))
				{
					ApplyToSelection<AreaLightComponent>(m_Context, selectedEntities, [&firstComponent](AreaLightComponent& component, Entity)
					{
						component.CastsShadows = firstComponent.CastsShadows;
					});
				}

				{
					ImGuiEx::ScopedDisable noShadows(!firstComponent.CastsShadows);
					if (ImGuiEx::Property("Soft Shadows", firstComponent.SoftShadows,
						"Contact-hardening penumbra sized by the light's shape. Needs the PCSS shadow filter (Scene Renderer panel, Shadows > Filter); the other filters use a fixed blur."))
					{
						ApplyToSelection<AreaLightComponent>(m_Context, selectedEntities, [&firstComponent](AreaLightComponent& component, Entity)
						{
							component.SoftShadows = firstComponent.SoftShadows;
						});
					}
				}

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponentSection<SkyLightComponent>(m_Context, entityIDs, "Sky Light", EditorResources::SkyLightIcon,
			[this](SkyLightComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool isMultiEdit)
			{
				ImGuiEx::BeginPropertyGrid();

				AssetHandle environmentHandle = firstComponent.SceneEnvironment;
				const bool mixedEnvironment = isMultiEdit && IsSelectionInconsistent<AssetHandle>(m_Context, selectedEntities, [](Entity entity)
				{
					return entity.GetComponent<SkyLightComponent>().SceneEnvironment;
				});
				ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixedEnvironment);
				if (ImGuiEx::PropertyAssetReference<Environment>("Environment Map", environmentHandle, "Sky Light only accepts environment map assets"))
				{
					firstComponent.SceneEnvironment = environmentHandle;
					firstComponent.DynamicSky = !environmentHandle;
					ApplyToSelection<SkyLightComponent>(m_Context, selectedEntities, [environmentHandle](SkyLightComponent& component, Entity)
					{
						component.SceneEnvironment = environmentHandle;
						component.DynamicSky = !environmentHandle;
					});
				}
				ImGui::PopItemFlag();

				if (ImGuiEx::Property("Intensity", firstComponent.Intensity, 0.01f, 0.0f, 10.0f))
				{
					ApplyToSelection<SkyLightComponent>(m_Context, selectedEntities, [&firstComponent](SkyLightComponent& component, Entity)
					{
						component.Intensity = firstComponent.Intensity;
					});
				}

				if (ImGuiEx::Property("Rotation", firstComponent.Rotation, 0.5f, -180.0f, 180.0f, "Turns the sky, and the light it gives, about the vertical axis (degrees)"))
				{
					ApplyToSelection<SkyLightComponent>(m_Context, selectedEntities, [&firstComponent](SkyLightComponent& component, Entity)
					{
						component.Rotation = firstComponent.Rotation;
					});
				}

				bool lodChanged = false;
				Ref<AssetManagerBase> assetManager = Project::GetAssetManager();
				if (firstComponent.SceneEnvironment && assetManager && assetManager->IsAssetHandleValid(firstComponent.SceneEnvironment))
				{
					AsyncAssetResult<Asset> environmentResult = assetManager->GetAssetAsync(firstComponent.SceneEnvironment);
					Ref<Asset> environmentAsset = environmentResult.Asset;
					Ref<Environment> environment = environmentResult.IsReady && environmentAsset && environmentAsset->GetAssetType() == AssetType::EnvMap ? environmentAsset.As<Environment>() : nullptr;
					if (environment && environment->RadianceMap)
					{
						const float maxLod = static_cast<float>(environment->RadianceMap->GetMipLevelCount());
						lodChanged = ImGuiEx::PropertySlider("Lod", firstComponent.Lod, 0.0f, maxLod);
					}
					else
					{
						ImGuiEx::BeginDisabled();
						ImGuiEx::PropertySlider("Lod", firstComponent.Lod, 0.0f, 10.0f);
						ImGuiEx::EndDisabled();
					}
				}

				if (lodChanged)
				{
					ApplyToSelection<SkyLightComponent>(m_Context, selectedEntities, [&firstComponent](SkyLightComponent& component, Entity)
					{
						component.Lod = firstComponent.Lod;
					});
				}

				if (ImGuiEx::Property("Dynamic Sky", firstComponent.DynamicSky))
				{
					if (firstComponent.DynamicSky)
						firstComponent.SceneEnvironment = 0;

					ApplyToSelection<SkyLightComponent>(m_Context, selectedEntities, [&firstComponent](SkyLightComponent& component, Entity)
					{
						component.DynamicSky = firstComponent.DynamicSky;
						if (component.DynamicSky)
							component.SceneEnvironment = 0;
					});
				}

				if (ImGuiEx::Property("Turbidity", firstComponent.TurbidityAzimuthInclination.x, 0.01f, 0.0f, 20.0f))
				{
					ApplyToSelection<SkyLightComponent>(m_Context, selectedEntities, [&firstComponent](SkyLightComponent& component, Entity)
					{
						component.TurbidityAzimuthInclination.x = firstComponent.TurbidityAzimuthInclination.x;
					});
				}

				if (ImGuiEx::Property("Azimuth", firstComponent.TurbidityAzimuthInclination.y, 0.01f, -360.0f, 360.0f))
				{
					ApplyToSelection<SkyLightComponent>(m_Context, selectedEntities, [&firstComponent](SkyLightComponent& component, Entity)
					{
						component.TurbidityAzimuthInclination.y = firstComponent.TurbidityAzimuthInclination.y;
					});
				}

				if (ImGuiEx::Property("Inclination", firstComponent.TurbidityAzimuthInclination.z, 0.01f, -180.0f, 180.0f))
				{
					ApplyToSelection<SkyLightComponent>(m_Context, selectedEntities, [&firstComponent](SkyLightComponent& component, Entity)
					{
						component.TurbidityAzimuthInclination.z = firstComponent.TurbidityAzimuthInclination.z;
					});
				}

				ImGuiEx::EndPropertyGrid();
			});

	}

} // namespace Lux
