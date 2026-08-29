#include "lpch.h"

#include "SceneHierarchyPanel.h"

#include "Lux/Asset/Asset.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Core/Events/KeyEvent.h"
#include "Lux/Core/Events/MouseEvent.h"
#include "Lux/Core/Input.h"
#include "Lux/Editor/EditorResources.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Renderer/Mesh.h"
#include "Lux/Renderer/SceneEnvironment.h"
#include "Lux/Renderer/UI/Font.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Scene/Prefab.h"
#include "Lux/Scripting/ScriptEngine.h"
#include "Lux/Core/Hash.h"

#include <entt/entt.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace Lux {

	SelectionContext SceneHierarchyPanel::s_ActiveSelectionContext = SelectionContext::Scene;

	namespace {

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

			if (entity.HasComponent<CameraComponent>())           return { LUX_ICON_VIDEO_CAMERA, cool };
			if (entity.HasComponent<DirectionalLightComponent>()) return { LUX_ICON_SUN_O, warm };
			if (entity.HasComponent<PointLightComponent>() ||
			    entity.HasComponent<SpotLightComponent>())        return { LUX_ICON_LIGHTBULB_O, warm };
			if (entity.HasComponent<SkyLightComponent>())         return { LUX_ICON_SUN_O, warm };
			if (entity.HasComponent<StaticMeshComponent>() ||
			    entity.HasComponent<MeshComponent>())             return { LUX_ICON_CUBE, neutral };
			if (entity.HasComponent<TextComponent>())             return { LUX_ICON_FONT, neutral };
			if (entity.HasComponent<SpriteRendererComponent>())   return { LUX_ICON_PICTURE_O, neutral };
			if (entity.HasComponent<AudioSourceComponent>())      return { LUX_ICON_MUSIC, purple };
			if (entity.HasComponent<AudioListenerComponent>())    return { LUX_ICON_VOLUME_UP, purple };
			if (entity.HasComponent<ScriptComponent>())           return { LUX_ICON_CODE, green };
			return { LUX_ICON_DOT_CIRCLE_O, muted };
		}

		// Draws the ImGui control for one script field via its dual-mode FieldStorage (edits the
		// serializable buffer when idle, the live managed field when playing). Returns true if changed.
		bool DrawScriptFieldControl(const std::string& name, DataType type, FieldStorage& storage)
		{
			switch (type)
			{
				case DataType::Float:   { float v = storage.GetValue<float>();     if (ImGui::DragFloat(name.c_str(), &v, 0.1f)) { storage.SetValue(v); return true; } return false; }
				case DataType::Double:  { double v = storage.GetValue<double>();    if (ImGui::DragScalar(name.c_str(), ImGuiDataType_Double, &v, 0.1f)) { storage.SetValue(v); return true; } return false; }
				case DataType::Bool:    { bool v = storage.GetValue<uint32_t>() != 0; if (ImGui::Checkbox(name.c_str(), &v)) { storage.SetValue<uint32_t>(v ? 1u : 0u); return true; } return false; }
				case DataType::SByte:   { int8_t v = storage.GetValue<int8_t>();    if (ImGui::DragScalar(name.c_str(), ImGuiDataType_S8, &v)) { storage.SetValue(v); return true; } return false; }
				case DataType::Byte:    { uint8_t v = storage.GetValue<uint8_t>();  if (ImGui::DragScalar(name.c_str(), ImGuiDataType_U8, &v)) { storage.SetValue(v); return true; } return false; }
				case DataType::Short:   { int16_t v = storage.GetValue<int16_t>();  if (ImGui::DragScalar(name.c_str(), ImGuiDataType_S16, &v)) { storage.SetValue(v); return true; } return false; }
				case DataType::UShort:  { uint16_t v = storage.GetValue<uint16_t>(); if (ImGui::DragScalar(name.c_str(), ImGuiDataType_U16, &v)) { storage.SetValue(v); return true; } return false; }
				case DataType::Int:     { int32_t v = storage.GetValue<int32_t>();  if (ImGui::DragScalar(name.c_str(), ImGuiDataType_S32, &v)) { storage.SetValue(v); return true; } return false; }
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

			const std::filesystem::path relativePath = std::filesystem::path("Meshes") / "Source" / "Default" / filename;
			AssetHandle handle = editorAssetManager->GetAssetHandleFromFilePath(relativePath);
			if (!handle)
			{
				const std::filesystem::path filesystemPath = Project::GetActiveAssetDirectory() / relativePath;
				if (std::filesystem::exists(filesystemPath))
					handle = editorAssetManager->ImportAsset(filesystemPath);
			}

			if (!handle || AssetManager::GetAssetType(handle) != AssetType::MeshSource)
				return 0;

			return handle;
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

		static bool DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 110.0f)
		{
			bool changed = false;

			ImGui::PushID(label.c_str());

			ImGui::Columns(2);
			ImGui::SetColumnWidth(0, columnWidth);
			ImGui::TextUnformatted(label.c_str());
			ImGui::NextColumn();

			ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });

			ImFont* boldFont = ImGui::GetIO().Fonts->Fonts[0];
			const float lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
			const ImVec2 buttonSize = { lineHeight + 3.0f, lineHeight };

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.9f, 0.2f, 0.2f, 1.0f });
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
			ImGui::PushFont(boldFont);
			if (ImGui::Button("X", buttonSize))
			{
				values.x = resetValue;
				changed = true;
			}
			ImGui::PopFont();
			ImGui::PopStyleColor(3);

			ImGui::SameLine();
			changed |= ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f");
			ImGui::PopItemWidth();
			ImGui::SameLine(0.0f, 8.0f);

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.3f, 0.8f, 0.3f, 1.0f });
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
			ImGui::PushFont(boldFont);
			if (ImGui::Button("Y", buttonSize))
			{
				values.y = resetValue;
				changed = true;
			}
			ImGui::PopFont();
			ImGui::PopStyleColor(3);

			ImGui::SameLine();
			changed |= ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f");
			ImGui::PopItemWidth();
			ImGui::SameLine(0.0f, 8.0f);

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.2f, 0.35f, 0.9f, 1.0f });
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
			ImGui::PushFont(boldFont);
			if (ImGui::Button("Z", buttonSize))
			{
				values.z = resetValue;
				changed = true;
			}
			ImGui::PopFont();
			ImGui::PopStyleColor(3);

			ImGui::SameLine();
			changed |= ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f");
			ImGui::PopItemWidth();

			ImGui::PopStyleVar();
			ImGui::Columns(1);
			ImGui::Dummy(ImVec2(0.0f, 5.0f)); // vertical breathing room between vec3 rows
			ImGui::PopID();

			return changed;
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
			const ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();
			(void)icon; // texture section icons dropped for the flat concept look

			// Flat collapsible section: an uppercase label + chevron drawn over an empty-label node,
			// with a faint hover/active wash — no framed header or Hazel texture icon.
			ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(255, 255, 255, 10));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(255, 255, 255, 16));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(255, 255, 255, 22));
			const ImGuiTreeNodeFlags sectionFlags = ImGuiTreeNodeFlags_SpanAvailWidth
				| ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_FramePadding;
			const bool open = ImGui::TreeNodeEx("##section", sectionFlags, "");
			ImGui::PopStyleColor(3);

			{
				const ImRect secRect = ImGuiEx::GetItemRect();
				ImGuiContext& g = *GImGui;
				const float labelX = secRect.Min.x + g.Style.FramePadding.x + g.FontSize + g.Style.ItemInnerSpacing.x;
				const float labelY = secRect.Min.y + (secRect.GetHeight() - ImGui::GetTextLineHeight()) * 0.5f;
				ImGui::GetWindowDrawList()->AddText(ImVec2(labelX, labelY), Colors::Theme::textBrighter,
					Utils::String::ToUpperCopy(name).c_str());
			}

			const float lineHeight = ImGui::GetFrameHeight();

			bool resetComponent = false;
			bool removeComponent = false;

			ImGui::SameLine(contentRegionAvailable.x - lineHeight - 5.0f);
			if (ImGui::InvisibleButton("##ComponentSettings", ImVec2{ lineHeight, lineHeight }))
				ImGui::OpenPopup("ComponentSettings");

			ImGuiEx::DrawButtonImage(EditorResources::GearIcon,
				IM_COL32(160, 160, 160, 200),
				IM_COL32(160, 160, 160, 255),
				IM_COL32(160, 160, 160, 150),
				ImGuiEx::GetItemRect());

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
				m_Context->DestroyEntity(entity);
		}

		PruneInvalidSelection();
	}

	void SceneHierarchyPanel::OnImGuiRender(bool& isOpen)
	{
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

		const float edgeOffset = 4.0f;

		// Uppercase section header with a live entity count, like the concept's sidebar.
		{
			uint32_t entityCount = 0;
			if (m_Context)
			{
				auto view = m_Context->GetAllEntitiesWith<TagComponent>();
				for (auto e : view)
				{
					(void)e;
					entityCount++;
				}
			}
			ImGui::SetCursorPosX(edgeOffset * 3.0f);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Colors::Theme::textDarker));
			ImGui::Text("HIERARCHY  ·  %u", entityCount);
			ImGui::PopStyleColor();
			ImGui::Spacing();
		}

		ImGui::SetCursorPosX(edgeOffset * 3.0f);
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - edgeOffset * 3.0f);
		ImGuiEx::Widgets::SearchWidget(m_SearchString, "Search entities...", &m_ActivateSearchWidget);
		ImGui::Spacing();
		ImGui::Separator();

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
							draggedEntity.SetParent({});
					}
				}

				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
				{
					const AssetHandle handle = *(const AssetHandle*)payload->Data;
					if (AssetManager::GetAssetType(handle) == AssetType::Prefab)
					{
						Ref<Prefab> prefab = AssetManager::GetAsset<Prefab>(handle);
						if (prefab)
							SetSelectedEntity(m_Context->InstantiatePrefab(prefab));
					}
				}

				ImGui::EndDragDropTarget();
			}
		}

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

		if (m_IsWindow)
			ImGui::End();

		if (!isOpen)
			return;

		FlushQueuedEntityDeletions();

		ImGui::Begin("Properties", &isOpen);
		m_IsHierarchyOrPropertiesFocused = m_IsHierarchyFocused || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		DrawComponents(SelectionManager::GetSelections(s_ActiveSelectionContext));
		ImGui::End();
	}

	void SceneHierarchyPanel::OnEvent(Event& e)
	{
		if (!m_IsHierarchyOrPropertiesFocused)
			return;

		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<KeyPressedEvent>([this](KeyPressedEvent& event)
		{
			switch (event.GetKeyCode())
			{
				case Key::F:
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

		if (ImGui::BeginMenu("Create 2D"))
		{
			if (ImGui::MenuItem("Sprite"))
			{
				Entity entity = createEntity("Sprite");
				entity.AddComponent<SpriteRendererComponent>();
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
		auto& tag = entity.GetComponent<TagComponent>().Tag;
		const auto& relationship = entity.GetComponent<RelationshipComponent>();
		const bool hasChildren = !relationship.Children.empty();
		const bool hasChildMatchingSearch = TagSearchRecursive(entity, searchFilter, 10);
		const bool isSelected = SelectionManager::IsSelected(s_ActiveSelectionContext, entity.GetUUID());

		if (!ImGuiEx::IsMatchingSearch(tag, searchFilter) && !hasChildMatchingSearch)
			return;

		ImGuiTreeNodeFlags flags = (isSelected ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
		flags |= ImGuiTreeNodeFlags_SpanAvailWidth;
		if (hasChildMatchingSearch)
			flags |= ImGuiTreeNodeFlags_DefaultOpen;
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
			const EntityIconInfo iconInfo = GetEntityIcon(entity);
			const float arrowWidth = g.FontSize;
			const float iconX = rowRect.Min.x + g.Style.FramePadding.x + arrowWidth + g.Style.ItemInnerSpacing.x;
			const float centerY = rowRect.Min.y + rowRect.GetHeight() * 0.5f;
			ImDrawList* dl = ImGui::GetWindowDrawList();

			const ImU32 iconColor = isSelected ? Colors::Theme::accent : iconInfo.Color;
			const ImVec2 iconSize = ImGui::CalcTextSize(iconInfo.Glyph);
			dl->AddText(ImVec2(iconX, centerY - iconSize.y * 0.5f), iconColor, iconInfo.Glyph);

			const float nameX = iconX + iconSize.x + g.Style.ItemInnerSpacing.x + 2.0f;
			const float nameRight = hasVisToggle ? eyeRect.Min.x - 4.0f : rowRect.Max.x - 4.0f;
			const ImU32 baseName = isSelected ? Colors::Theme::accent : Colors::Theme::text;
			const ImU32 nameColor = (hasVisToggle && !meshVisible) ? Colors::Theme::textDarker : baseName;
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

		if (ImGui::BeginDragDropSource())
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
						draggedEntity.SetParent(entity);
				}
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

			const size_t selectionCount = SelectionManager::GetSelectionCount(s_ActiveSelectionContext);
			const char* deleteLabel = (isSelected && selectionCount > 1) ? "Delete Selected Entities" : "Delete Entity";
			if (ImGui::MenuItem(deleteLabel))
				deleteRequested = true;

			ImGui::EndPopup();
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

			QueueEntityDeletion(entitiesToDelete);
		}
	}

	void SceneHierarchyPanel::DrawComponents(const std::vector<UUID>& entityIDs)
	{
		if (!m_Context || entityIDs.empty())
		{
			ImGui::TextDisabled("Select an entity to inspect its properties.");
			return;
		}

		Entity firstEntity = m_Context->GetEntityByUUID(entityIDs.front());
		if (!firstEntity)
			return;

		const bool isMultiSelect = entityIDs.size() > 1;
		const ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();

		// Inspector-wide: every input renders as a bordered, slightly-inset "field box" (the
		// concept's .field look). Scoped RAII so it unwinds cleanly across this function's returns.
		ImGuiEx::ScopedStyle frameBorderSize(ImGuiStyleVar_FrameBorderSize, 1.0f);
		ImGuiEx::ScopedStyle frameRounding(ImGuiStyleVar_FrameRounding, 3.0f);
		ImGuiEx::ScopedColour frameBg(ImGuiCol_FrameBg, Colors::Theme::backgroundDark);
		ImGuiEx::ScopedColour frameBorder(ImGuiCol_Border, Colors::Theme::muted);

		{
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);

			// Accent status dot before the name, like the concept's inspector header.
			{
				const float dotRadius = 4.0f;
				const ImVec2 cursor = ImGui::GetCursorScreenPos();
				const ImVec2 dotCenter(cursor.x + dotRadius + 2.0f, cursor.y + ImGui::GetFrameHeight() * 0.5f);
				ImGui::GetWindowDrawList()->AddCircleFilled(dotCenter, dotRadius, Colors::Theme::accent);
				ImGui::Dummy(ImVec2(dotRadius * 2.0f + 4.0f, ImGui::GetFrameHeight()));
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

			char buffer[256];
			std::memset(buffer, 0, sizeof(buffer));
			std::strncpy(buffer, tagValue.c_str(), sizeof(buffer) - 1);

			ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
			ImGui::PushItemWidth(contentRegionAvailable.x * 0.55f);
			if (ImGui::InputText("##Tag", buffer, sizeof(buffer)))
			{
				const std::string newName = buffer[0] == 0 ? "Unnamed Entity" : std::string(buffer);
				ApplyToSelection<TagComponent>(m_Context, entityIDs, [&newName](TagComponent& component, Entity)
				{
					component.Tag = newName;
				});
			}
			ImGui::PopItemWidth();
			ImGui::PopFont();

			if (isMultiSelect)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("(%zu selected)", entityIDs.size());
			}

			const float addButtonWidth = 90.0f;
			ImGui::SameLine(contentRegionAvailable.x - addButtonWidth - 6.0f);
			{
				// Lime call-to-action, matching the concept's accent Add button.
				ImGuiEx::ScopedColour addBg(ImGuiCol_Button, Colors::Theme::accent);
				ImGuiEx::ScopedColour addBgH(ImGuiCol_ButtonHovered, Colors::Theme::accent);
				ImGuiEx::ScopedColour addBgA(ImGuiCol_ButtonActive, Colors::Theme::accent);
				ImGuiEx::ScopedColour addTxt(ImGuiCol_Text, Colors::Theme::titlebar);
				ImGuiEx::ScopedColour addBorder(ImGuiCol_Border, Colors::Theme::accent);
				if (ImGui::Button(LUX_ICON_PLUS "  ADD", ImVec2(addButtonWidth, 0.0f)))
					ImGui::OpenPopup("AddComponentPanel");
			}
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		if (ImGui::BeginPopup("AddComponentPanel"))
		{
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

			auto addComponentRow = [this, &entityIDs](const char* label, const Ref<Texture2D>& icon, const std::function<void()>& addCallback)
			{
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
					ImGui::CloseCurrentPopup();
				}
			};

			auto addCategoryHeader = [](const char* label)
			{
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
				const bool canAddText = canAddComponent.template operator()<TextComponent>();
				const bool canAddSpriteRenderer = canAddComponent.template operator()<SpriteRendererComponent>();
				const bool canAddCircleRenderer = canAddComponent.template operator()<CircleRendererComponent>();
				const bool canAddStaticMesh = canAddComponent.template operator()<StaticMeshComponent>();
				const bool canAddRigidBody2D = canAddComponent.template operator()<RigidBody2DComponent>();
				const bool canAddBoxCollider2D = canAddComponent.template operator()<BoxCollider2DComponent>();
				const bool canAddCircleCollider2D = canAddComponent.template operator()<CircleCollider2DComponent>();
				const bool canAddRigidBody = canAddComponent.template operator()<RigidBodyComponent>();
				const bool canAddCharacterController = canAddComponent.template operator()<CharacterControllerComponent>();
				const bool canAddCompoundCollider = canAddComponent.template operator()<CompoundColliderComponent>();
				const bool canAddBoxCollider = canAddComponent.template operator()<BoxColliderComponent>();
				const bool canAddSphereCollider = canAddComponent.template operator()<SphereColliderComponent>();
				const bool canAddCapsuleCollider = canAddComponent.template operator()<CapsuleColliderComponent>();
				const bool canAddMeshCollider = canAddComponent.template operator()<MeshColliderComponent>();
				const bool canAddAudioSource = canAddComponent.template operator()<AudioSourceComponent>();
				const bool canAddAudioListener = canAddComponent.template operator()<AudioListenerComponent>();
				const bool canAddDirectionalLight = canAddComponent.template operator()<DirectionalLightComponent>();
				const bool canAddPointLight = canAddComponent.template operator()<PointLightComponent>();
				const bool canAddSpotLight = canAddComponent.template operator()<SpotLightComponent>();
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

				if (canAddText || canAddSpriteRenderer || canAddCircleRenderer || canAddStaticMesh)
					addCategoryHeader("Rendering");

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

				if (canAddRigidBody2D || canAddBoxCollider2D || canAddCircleCollider2D)
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

				if (canAddAudioSource || canAddAudioListener)
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

				if (canAddDirectionalLight || canAddPointLight || canAddSpotLight || canAddSkyLight)
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

			ImGui::EndPopup();
		}

		DrawComponentSection<PrefabComponent>(m_Context, entityIDs, "Prefab", EditorResources::AssetIcon,
			[this](PrefabComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				ImGuiEx::BeginPropertyGrid();

				uint64_t prefabID = (uint64_t)firstComponent.PrefabID;
				if (ImGuiEx::PropertyInput("Prefab ID", prefabID, 1, 1))
				{
					firstComponent.PrefabID = (AssetHandle)prefabID;
					ApplyToSelection<PrefabComponent>(m_Context, selectedEntities, [&firstComponent](PrefabComponent& component, Entity)
					{
						component.PrefabID = firstComponent.PrefabID;
					});
				}

				uint64_t entityID = (uint64_t)firstComponent.EntityID;
				if (ImGuiEx::PropertyInput("Entity ID", entityID, 1, 1))
				{
					firstComponent.EntityID = (UUID)entityID;
					ApplyToSelection<PrefabComponent>(m_Context, selectedEntities, [&firstComponent](PrefabComponent& component, Entity)
					{
						component.EntityID = firstComponent.EntityID;
					});
				}

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponentSection<TransformComponent>(m_Context, entityIDs, "Transform", EditorResources::TransformIcon,
			[this](TransformComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				glm::vec3 translation = firstComponent.Translation;
				glm::vec3 rotation = glm::degrees(firstComponent.GetRotationEuler());
				glm::vec3 scale = firstComponent.Scale;

				if (DrawVec3Control("Translation", translation))
				{
					firstComponent.Translation = translation;
					ApplyToSelection<TransformComponent>(m_Context, selectedEntities, [&translation](TransformComponent& component, Entity)
					{
						component.Translation = translation;
					});
				}

				if (DrawVec3Control("Rotation", rotation))
				{
					const glm::vec3 rotationRadians = glm::radians(rotation);
					firstComponent.SetRotationEuler(rotationRadians);
					ApplyToSelection<TransformComponent>(m_Context, selectedEntities, [&rotationRadians](TransformComponent& component, Entity)
					{
						component.SetRotationEuler(rotationRadians);
					});
				}

				if (DrawVec3Control("Scale", scale, 1.0f))
				{
					firstComponent.Scale = scale;
					ApplyToSelection<TransformComponent>(m_Context, selectedEntities, [&scale](TransformComponent& component, Entity)
					{
						component.Scale = scale;
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
					float orthoSize = camera.GetOrthographicSize();
					if (ImGuiEx::Property("Size", orthoSize))
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
					if (fs.IsArray())
					{
						ImGui::TextDisabled("%s (array)", fieldMetadata.Name.c_str());
						continue;
					}
					DrawScriptFieldControl(fieldMetadata.Name, fieldMetadata.Type, fs);
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

				if (ImGuiEx::Property("Tiling Factor", firstComponent.TilingFactor, 0.1f, 0.0f, 100.0f))
				{
					ApplyToSelection<SpriteRendererComponent>(m_Context, selectedEntities, [&firstComponent](SpriteRendererComponent& component, Entity)
					{
						component.TilingFactor = firstComponent.TilingFactor;
					});
				}

				ImGuiEx::EndPropertyGrid();
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

				AssetHandle colliderAsset = firstComponent.ColliderAsset;
				if (ImGuiEx::PropertyAssetReference<MeshSource>("Collider Mesh", colliderAsset, "Leave empty to use the entity Static Mesh. Mesh colliders are static in Jolt."))
				{
					firstComponent.ColliderAsset = colliderAsset;
					ApplyToSelection<MeshColliderComponent>(m_Context, selectedEntities, [colliderAsset](MeshColliderComponent& component, Entity)
					{
						component.ColliderAsset = colliderAsset;
					});
				}

				if (ImGuiEx::Property("Submesh Index", firstComponent.SubmeshIndex, 0u))
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
			[this, firstEntity](AudioSourceComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool isMultiEdit) mutable
			{
				if (isMultiEdit)
					ImGui::TextDisabled("Audio source playback controls apply to the first selected entity.");

				auto& component = firstComponent;
				auto& config = component.Config;

				ImGuiEx::BeginPropertyGrid();
				AssetHandle audioHandle = component.Audio;
				const bool mixedAudio = selectedEntities.size() > 1 && IsSelectionInconsistent<AssetHandle>(m_Context, selectedEntities, [](Entity entity)
				{
					return entity.GetComponent<AudioSourceComponent>().Audio;
				});
				ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixedAudio);
				if (ImGuiEx::PropertyAssetReference<AudioFile>("Audio", audioHandle, "Audio Source only accepts audio assets"))
				{
					component.Audio = audioHandle;
					if (!component.AudioSourceData.Playlist.empty())
						component.AudioSourceData.Playlist[0] = audioHandle;

					ApplyToSelection<AudioSourceComponent>(m_Context, selectedEntities, [audioHandle](AudioSourceComponent& audioComponent, Entity)
					{
						audioComponent.Audio = audioHandle;
						if (!audioComponent.AudioSourceData.Playlist.empty())
							audioComponent.AudioSourceData.Playlist[0] = audioHandle;
					});
				}
				ImGui::PopItemFlag();

				if (ImGuiEx::Property("Volume Multiplier", config.VolumeMultiplier, 0.01f, 0.0f, 2.0f))
				{
					ApplyToSelection<AudioSourceComponent>(m_Context, selectedEntities, [&config](AudioSourceComponent& audioComponent, Entity)
					{
						audioComponent.Config.VolumeMultiplier = config.VolumeMultiplier;
					});
				}

				if (ImGuiEx::Property("Pitch Multiplier", config.PitchMultiplier, 0.01f, 0.0f, 3.0f))
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

				if (ImGuiEx::Property("Spatialization", config.Spatialization))
				{
					ApplyToSelection<AudioSourceComponent>(m_Context, selectedEntities, [&config](AudioSourceComponent& audioComponent, Entity)
					{
						audioComponent.Config.Spatialization = config.Spatialization;
					});
				}

				if (!component.AudioSourceData.UsePlaylist)
				{
					if (ImGuiEx::Property("Looping", config.Looping))
					{
						ApplyToSelection<AudioSourceComponent>(m_Context, selectedEntities, [&config](AudioSourceComponent& audioComponent, Entity)
						{
							audioComponent.Config.Looping = config.Looping;
						});
					}
				}

				ImGuiEx::EndPropertyGrid();

				if (config.Spatialization)
				{
					ImGuiEx::BeginPropertyGrid();

					const char* attenuationTypeStrings[] = { "None", "Inverse", "Linear", "Exponential" };
					int attenuationType = static_cast<int>(config.AttenuationModel);
					if (ImGuiEx::PropertyDropdown("Attenuation Model", attenuationTypeStrings, IM_ARRAYSIZE(attenuationTypeStrings), &attenuationType))
					{
						config.AttenuationModel = static_cast<AttenuationModelType>(attenuationType);
						ApplyToSelection<AudioSourceComponent>(m_Context, selectedEntities, [attenuationType](AudioSourceComponent& audioComponent, Entity)
						{
							audioComponent.Config.AttenuationModel = static_cast<AttenuationModelType>(attenuationType);
						});
					}

					if (ImGuiEx::Property("Roll Off", config.RollOff, 0.01f, 0.0f, 10.0f))
					{
						ApplyToSelection<AudioSourceComponent>(m_Context, selectedEntities, [&config](AudioSourceComponent& audioComponent, Entity)
						{
							audioComponent.Config.RollOff = config.RollOff;
						});
					}

					if (ImGuiEx::Property("Min Gain", config.MinGain, 0.01f, 0.0f, 1.0f))
					{
						ApplyToSelection<AudioSourceComponent>(m_Context, selectedEntities, [&config](AudioSourceComponent& audioComponent, Entity)
						{
							audioComponent.Config.MinGain = config.MinGain;
						});
					}

					if (ImGuiEx::Property("Max Gain", config.MaxGain, 0.01f, 0.0f, 1.0f))
					{
						ApplyToSelection<AudioSourceComponent>(m_Context, selectedEntities, [&config](AudioSourceComponent& audioComponent, Entity)
						{
							audioComponent.Config.MaxGain = config.MaxGain;
						});
					}

					if (ImGuiEx::Property("Min Distance", config.MinDistance, 0.01f, 0.0f, 100.0f))
					{
						ApplyToSelection<AudioSourceComponent>(m_Context, selectedEntities, [&config](AudioSourceComponent& audioComponent, Entity)
						{
							audioComponent.Config.MinDistance = config.MinDistance;
						});
					}

					if (ImGuiEx::Property("Max Distance", config.MaxDistance, 0.01f, 0.0f, 100.0f))
					{
						ApplyToSelection<AudioSourceComponent>(m_Context, selectedEntities, [&config](AudioSourceComponent& audioComponent, Entity)
						{
							audioComponent.Config.MaxDistance = config.MaxDistance;
						});
					}

					float innerAngle = glm::degrees(config.ConeInnerAngle);
					if (ImGuiEx::Property("Cone Inner Angle", innerAngle, 0.1f, 0.0f, 360.0f))
					{
						config.ConeInnerAngle = glm::radians(innerAngle);
						ApplyToSelection<AudioSourceComponent>(m_Context, selectedEntities, [innerAngle](AudioSourceComponent& audioComponent, Entity)
						{
							audioComponent.Config.ConeInnerAngle = glm::radians(innerAngle);
						});
					}

					float outerAngle = glm::degrees(config.ConeOuterAngle);
					if (ImGuiEx::Property("Cone Outer Angle", outerAngle, 0.1f, 0.0f, 360.0f))
					{
						config.ConeOuterAngle = glm::radians(outerAngle);
						ApplyToSelection<AudioSourceComponent>(m_Context, selectedEntities, [outerAngle](AudioSourceComponent& audioComponent, Entity)
						{
							audioComponent.Config.ConeOuterAngle = glm::radians(outerAngle);
						});
					}

					if (ImGuiEx::Property("Cone Outer Gain", config.ConeOuterGain, 0.01f, 0.0f, 1.0f))
					{
						ApplyToSelection<AudioSourceComponent>(m_Context, selectedEntities, [&config](AudioSourceComponent& audioComponent, Entity)
						{
							audioComponent.Config.ConeOuterGain = config.ConeOuterGain;
						});
					}

					if (ImGuiEx::Property("Doppler Factor", config.DopplerFactor, 0.01f, 0.0f, 10.0f))
					{
						ApplyToSelection<AudioSourceComponent>(m_Context, selectedEntities, [&config](AudioSourceComponent& audioComponent, Entity)
						{
							audioComponent.Config.DopplerFactor = config.DopplerFactor;
						});
					}

					ImGuiEx::EndPropertyGrid();
				}

			});

		DrawComponentSection<AudioListenerComponent>(m_Context, entityIDs, "Audio Listener", EditorResources::AudioListenerIcon,
			[this](AudioListenerComponent& firstComponent, const std::vector<UUID>& selectedEntities, bool)
			{
				auto& config = firstComponent.Config;

				ImGuiEx::BeginPropertyGrid();

				if (ImGuiEx::Property("Active", firstComponent.Active))
				{
					ApplyToSelection<AudioListenerComponent>(m_Context, selectedEntities, [&firstComponent](AudioListenerComponent& component, Entity)
					{
						component.Active = firstComponent.Active;
					});
				}

				float innerAngle = glm::degrees(config.ConeInnerAngle);
				if (ImGuiEx::Property("Cone Inner Angle", innerAngle, 0.1f, 0.0f, 360.0f))
				{
					config.ConeInnerAngle = glm::radians(innerAngle);
					ApplyToSelection<AudioListenerComponent>(m_Context, selectedEntities, [innerAngle](AudioListenerComponent& component, Entity)
					{
						component.Config.ConeInnerAngle = glm::radians(innerAngle);
					});
				}

				float outerAngle = glm::degrees(config.ConeOuterAngle);
				if (ImGuiEx::Property("Cone Outer Angle", outerAngle, 0.1f, 0.0f, 360.0f))
				{
					config.ConeOuterAngle = glm::radians(outerAngle);
					ApplyToSelection<AudioListenerComponent>(m_Context, selectedEntities, [outerAngle](AudioListenerComponent& component, Entity)
					{
						component.Config.ConeOuterAngle = glm::radians(outerAngle);
					});
				}

				if (ImGuiEx::PropertySlider("Cone Outer Gain", config.ConeOuterGain, 0.0f, 1.0f))
				{
					ApplyToSelection<AudioListenerComponent>(m_Context, selectedEntities, [&config](AudioListenerComponent& component, Entity)
					{
						component.Config.ConeOuterGain = config.ConeOuterGain;
					});
				}

				ImGuiEx::EndPropertyGrid();
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

				if (ImGuiEx::Property("Visible", firstComponent.Visible))
				{
					ApplyToSelection<StaticMeshComponent>(m_Context, selectedEntities, [&firstComponent](StaticMeshComponent& component, Entity)
					{
						component.Visible = firstComponent.Visible;
					});
				}

				ImGuiEx::EndPropertyGrid();
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

				if (ImGuiEx::Property("Falloff", firstComponent.Falloff, 0.01f, 0.0f, 10.0f))
				{
					ApplyToSelection<PointLightComponent>(m_Context, selectedEntities, [&firstComponent](PointLightComponent& component, Entity)
					{
						component.Falloff = firstComponent.Falloff;
					});
				}

				if (ImGuiEx::Property("Min Radius", firstComponent.MinRadius, 0.001f, 0.0f, 1.0f))
				{
					ApplyToSelection<PointLightComponent>(m_Context, selectedEntities, [&firstComponent](PointLightComponent& component, Entity)
					{
						component.MinRadius = firstComponent.MinRadius;
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

				if (ImGuiEx::Property("Falloff", firstComponent.Falloff, 0.01f, 0.0f, 10.0f))
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
