#include "lpch.h"

#include "SceneHierarchyPanel.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Asset/AssetMetadata.h"
#include "Lux/Core/Events/KeyEvent.h"
#include "Lux/Core/Events/MouseEvent.h"
#include "Lux/Core/Input.h"
#include "Lux/Editor/EditorResources.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/MeshFactory.h"
#include "Lux/Renderer/SceneEnvironment.h"
#include "Lux/Renderer/UI/Font.h"
#include "Lux/Scene/Components.h"
#include "Lux/Scene/Prefab.h"
#include "Lux/Scripting/ScriptEngine.h"

#include <entt/entt.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <optional>
#include <vector>

namespace Lux {

	SelectionContext SceneHierarchyPanel::s_ActiveSelectionContext = SelectionContext::Scene;

	namespace {

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

		std::string GetAssetDisplayLabel(AssetHandle handle, const std::optional<AssetType>& expectedType)
		{
			if (handle == 0)
				return "None";

			if (!AssetManager::IsAssetHandleValid(handle))
				return "Invalid";

			if (expectedType.has_value() && AssetManager::GetAssetType(handle) != *expectedType)
				return "Invalid";

			Ref<Project> activeProject = Project::GetActive();
			if (!activeProject)
				return std::to_string((uint64_t)handle);

			const AssetMetadata& metadata = activeProject->GetEditorAssetManager()->GetMetadata(handle);
			if (metadata.FilePath.empty())
				return std::to_string((uint64_t)handle);

			return metadata.FilePath.filename().string();
		}

		bool DrawAssetReferenceProperty(const char* label, AssetHandle& handle, const std::optional<AssetType>& expectedType, const char* wrongTypeWarning = nullptr)
		{
			bool modified = false;

			ImGui::PushID(label);
			ImGui::TextUnformatted(label);
			ImGui::NextColumn();
			ImGui::PushItemWidth(-1.0f);

			const std::string buttonLabel = GetAssetDisplayLabel(handle, expectedType);
			const bool showClearButton = handle != 0;

			float clearButtonWidth = 0.0f;
			if (showClearButton)
			{
				const float buttonSize = ImGui::GetFrameHeight();
				clearButtonWidth = buttonSize + ImGui::GetStyle().ItemSpacing.x;
			}

			const ImVec2 buttonSize(ImGui::GetContentRegionAvail().x - clearButtonWidth, 0.0f);
			ImGui::Button(buttonLabel.c_str(), buttonSize);
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
				{
					if (payload->DataSize >= sizeof(AssetHandle))
					{
						const AssetHandle droppedHandle = *(const AssetHandle*)payload->Data;
						const bool typeMatches = !expectedType.has_value() || AssetManager::GetAssetType(droppedHandle) == *expectedType;
						if (typeMatches)
						{
							handle = droppedHandle;
							modified = true;
						}
						else if (wrongTypeWarning)
						{
							LUX_CORE_WARN("{}", wrongTypeWarning);
						}
					}
				}
				ImGui::EndDragDropTarget();
			}

			if (showClearButton)
			{
				ImGui::SameLine();
				if (ImGui::Button("X", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
				{
					handle = 0;
					modified = true;
				}
			}

			ImGui::PopItemWidth();
			ImGui::NextColumn();
			ImGui::PopID();

			return modified;
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
			component.Rotation = glm::vec3(0.0f);
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
			changed |= ImGuiEx::Property("##X", values.x, 0.1f, 0.0f, 0.0f, "%.2f");
			ImGui::PopItemWidth();
			ImGui::SameLine();

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
			changed |= ImGuiEx::Property("##Y", values.y, 0.1f, 0.0f, 0.0f, "%.2f");
			ImGui::PopItemWidth();
			ImGui::SameLine();

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
			changed |= ImGuiEx::Property("##Z", values.z, 0.1f, 0.0f, 0.0f, "%.2f");
			ImGui::PopItemWidth();

			ImGui::PopStyleVar();
			ImGui::Columns(1);
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
			const Ref<Texture2D> sectionIcon = icon ? icon : EditorResources::AssetIcon;

			const bool open = ImGuiEx::TreeNodeWithIcon(name, sectionIcon, { 14.0f, 14.0f });
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
			staticMesh.Mesh = meshHandle;
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
				if (ImGui::MenuItem("Cube"))
					createStaticMeshEntity("Cube", MeshFactory::CreateBox({ 1.0f, 1.0f, 1.0f }));

				if (ImGui::MenuItem("Sphere"))
					createStaticMeshEntity("Sphere", MeshFactory::CreateSphere(0.5f));

				if (ImGui::MenuItem("Capsule"))
					createStaticMeshEntity("Capsule", MeshFactory::CreateCapsule(0.5f, 1.0f));

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

		const bool opened = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, flags, "%s", tag.c_str());

		if (ImGui::IsItemClicked())
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
			for (UUID childID : relationship.Children)
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

			for (UUID entityID : entitiesToDelete)
			{
				Entity entityToDelete = m_Context->GetEntityByUUID(entityID);
				if (entityToDelete)
					m_Context->DestroyEntity(entityToDelete);

				SelectionManager::Deselect(s_ActiveSelectionContext, entityID);
			}
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

		{
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);

			if (EditorResources::PencilIcon)
			{
				ImGui::Image(ImGuiEx::GetTextureID(EditorResources::PencilIcon), ImVec2(16.0f, 16.0f));
				ImGui::SameLine(0.0f, 6.0f);
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
			if (ImGui::Button("ADD", ImVec2(addButtonWidth, 0.0f)))
				ImGui::OpenPopup("AddComponentPanel");
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

			if (ImGui::BeginTable("##AddComponentTable", 2, ImGuiTableFlags_SizingFixedFit))
			{
				ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed, 24.0f);
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);

				if (canAddComponent.template operator()<CameraComponent>())
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

				if (canAddComponent.template operator()<ScriptComponent>())
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

				if (canAddComponent.template operator()<TextComponent>())
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

				if (canAddComponent.template operator()<SpriteRendererComponent>())
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

				if (canAddComponent.template operator()<CircleRendererComponent>())
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

				if (canAddComponent.template operator()<RigidBody2DComponent>())
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

				if (canAddComponent.template operator()<BoxCollider2DComponent>())
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

				if (canAddComponent.template operator()<CircleCollider2DComponent>())
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

				if (canAddComponent.template operator()<AudioSourceComponent>())
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

				if (canAddComponent.template operator()<AudioListenerComponent>())
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

				if (canAddComponent.template operator()<StaticMeshComponent>())
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

				if (canAddComponent.template operator()<DirectionalLightComponent>())
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

				if (canAddComponent.template operator()<PointLightComponent>())
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

				if (canAddComponent.template operator()<SpotLightComponent>())
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

				if (canAddComponent.template operator()<SkyLightComponent>())
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
				glm::vec3 rotation = glm::degrees(firstComponent.Rotation);
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
					firstComponent.Rotation = rotationRadians;
					ApplyToSelection<TransformComponent>(m_Context, selectedEntities, [&rotationRadians](TransformComponent& component, Entity)
					{
						component.Rotation = rotationRadians;
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
				std::string className = firstComponent.ClassName;

				ImGuiEx::BeginPropertyGrid();
				if (ImGuiEx::Property("Class", className))
				{
					firstComponent.ClassName = className;
					ApplyToSelection<ScriptComponent>(m_Context, selectedEntities, [&className](ScriptComponent& component, Entity)
					{
						component.ClassName = className;
					});
				}
				ImGuiEx::EndPropertyGrid();

				const bool scriptClassExists = ScriptEngine::EntityClassExists(firstComponent.ClassName);
				if (!scriptClassExists && !firstComponent.ClassName.empty())
					ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.3f, 1.0f), "Script class not found.");

				if (isMultiEdit)
				{
					ImGui::TextDisabled("Script field editing is shown for the first selected entity.");
				}

				if (!scriptClassExists || firstComponent.ClassName.empty())
					return;

				if (m_Context->IsRunning())
				{
					Ref<ScriptInstance> scriptInstance = ScriptEngine::GetEntityScriptInstance(firstEntity.GetUUID());
					if (!scriptInstance)
						return;

					const auto& fields = scriptInstance->GetScriptClass()->GetFields();
					for (const auto& [name, field] : fields)
					{
						if (field.Type != ScriptFieldType::Float)
							continue;

						float data = scriptInstance->GetFieldValue<float>(name);
						if (ImGui::DragFloat(name.c_str(), &data))
							scriptInstance->SetFieldValue(name, data);
					}
				}
				else
				{
					Ref<ScriptClass> entityClass = ScriptEngine::GetEntityClass(firstComponent.ClassName);
					if (!entityClass)
						return;

					const auto& fields = entityClass->GetFields();
					auto& entityFields = ScriptEngine::GetScriptFieldMap(firstEntity);

					for (const auto& [name, field] : fields)
					{
						if (field.Type != ScriptFieldType::Float)
							continue;

						if (entityFields.find(name) != entityFields.end())
						{
							ScriptFieldInstance& scriptField = entityFields.at(name);
							float data = scriptField.GetValue<float>();
							if (ImGui::DragFloat(name.c_str(), &data))
								scriptField.SetValue(data);
						}
						else
						{
							float data = 0.0f;
							if (ImGui::DragFloat(name.c_str(), &data))
							{
								ScriptFieldInstance& fieldInstance = entityFields[name];
								fieldInstance.Field = field;
								fieldInstance.SetValue(data);
							}
						}
					}
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
				if (DrawAssetReferenceProperty("Texture", textureHandle, AssetType::Texture, "Sprite Renderer only accepts texture assets"))
				{
					firstComponent.Texture = textureHandle;
					ApplyToSelection<SpriteRendererComponent>(m_Context, selectedEntities, [textureHandle](SpriteRendererComponent& component, Entity)
					{
						component.Texture = textureHandle;
					});
				}

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
				int currentBodyType = (int)firstComponent.Type;
				ImGuiEx::BeginPropertyGrid();

				if (ImGuiEx::PropertyDropdown("Body Type", bodyTypeStrings, 3, &currentBodyType))
				{
					firstComponent.Type = (RigidBody2DComponent::BodyType)currentBodyType;
					ApplyToSelection<RigidBody2DComponent>(m_Context, selectedEntities, [currentBodyType](RigidBody2DComponent& component, Entity)
					{
						component.Type = (RigidBody2DComponent::BodyType)currentBodyType;
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
				if (DrawAssetReferenceProperty("Font", fontHandle, AssetType::Font, "Text component only accepts font assets"))
				{
					firstComponent.FontHandle = fontHandle;
					ApplyToSelection<TextComponent>(m_Context, selectedEntities, [fontHandle](TextComponent& component, Entity)
					{
						component.FontHandle = fontHandle;
					});
				}

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
				if (DrawAssetReferenceProperty("Audio", audioHandle, AssetType::Audio, "Audio Source only accepts audio assets"))
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

				if (component.Audio)
				{
					if (Ref<AudioSource> audioSource = AssetManager::GetAsset<AudioSource>(component.Audio))
					{
						const glm::mat4 inverted = glm::inverse(firstEntity.GetComponent<TransformComponent>().GetTransform());
						const glm::vec3 forward = glm::normalize(glm::vec3(inverted[2]));
						audioSource->SetConfig(config);
						audioSource->SetPosition(glm::vec4(firstEntity.GetComponent<TransformComponent>().Translation, 1.0f));
						audioSource->SetDirection(-forward);
					}
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

				AssetHandle meshHandle = firstComponent.Mesh;
				if (DrawAssetReferenceProperty("Mesh", meshHandle, AssetType::StaticMesh, "Static Mesh only accepts static mesh assets"))
				{
					firstComponent.Mesh = meshHandle;
					ApplyToSelection<StaticMeshComponent>(m_Context, selectedEntities, [meshHandle](StaticMeshComponent& component, Entity)
					{
						component.Mesh = meshHandle;
					});
				}

				AssetHandle materialHandle = firstComponent.MaterialTable;
				if (DrawAssetReferenceProperty("Material", materialHandle, AssetType::Material, "Static Mesh material override expects a material asset"))
				{
					firstComponent.MaterialTable = materialHandle;
					ApplyToSelection<StaticMeshComponent>(m_Context, selectedEntities, [materialHandle](StaticMeshComponent& component, Entity)
					{
						component.MaterialTable = materialHandle;
					});
				}

				if (ImGuiEx::Property("Visible", firstComponent.Visible))
				{
					ApplyToSelection<StaticMeshComponent>(m_Context, selectedEntities, [&firstComponent](StaticMeshComponent& component, Entity)
					{
						component.Visible = firstComponent.Visible;
					});
				}

				if (ImGuiEx::Property("Cast Shadows", firstComponent.CastShadows))
				{
					ApplyToSelection<StaticMeshComponent>(m_Context, selectedEntities, [&firstComponent](StaticMeshComponent& component, Entity)
					{
						component.CastShadows = firstComponent.CastShadows;
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

				if (ImGuiEx::Property("Intensity", firstComponent.Intensity, 0.1f, 0.0f, 100.0f))
				{
					ApplyToSelection<PointLightComponent>(m_Context, selectedEntities, [&firstComponent](PointLightComponent& component, Entity)
					{
						component.Intensity = firstComponent.Intensity;
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

				if (ImGuiEx::Property("Cast Shadows", firstComponent.CastShadows))
				{
					ApplyToSelection<PointLightComponent>(m_Context, selectedEntities, [&firstComponent](PointLightComponent& component, Entity)
					{
						component.CastShadows = firstComponent.CastShadows;
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

				if (ImGuiEx::Property("Intensity", firstComponent.Intensity, 0.1f, 0.0f, 100.0f))
				{
					ApplyToSelection<SpotLightComponent>(m_Context, selectedEntities, [&firstComponent](SpotLightComponent& component, Entity)
					{
						component.Intensity = firstComponent.Intensity;
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

				if (ImGuiEx::Property("Cast Shadows", firstComponent.CastShadows))
				{
					ApplyToSelection<SpotLightComponent>(m_Context, selectedEntities, [&firstComponent](SpotLightComponent& component, Entity)
					{
						component.CastShadows = firstComponent.CastShadows;
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

				AssetHandle environmentHandle = firstComponent.EnvironmentMap;
				const bool mixedEnvironment = isMultiEdit && IsSelectionInconsistent<AssetHandle>(m_Context, selectedEntities, [](Entity entity)
				{
					return entity.GetComponent<SkyLightComponent>().EnvironmentMap;
				});
				ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixedEnvironment);
				if (ImGuiEx::PropertyAssetReference<Environment>("Environment Map", environmentHandle, "Sky Light only accepts environment map assets"))
				{
					firstComponent.EnvironmentMap = environmentHandle;
					firstComponent.DynamicSky = (environmentHandle == 0);
					ApplyToSelection<SkyLightComponent>(m_Context, selectedEntities, [environmentHandle](SkyLightComponent& component, Entity)
					{
						component.EnvironmentMap = environmentHandle;
						component.DynamicSky = (environmentHandle == 0);
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

				if (ImGuiEx::Property("Lod", firstComponent.Lod, 0.01f, 0.0f, 10.0f))
				{
					ApplyToSelection<SkyLightComponent>(m_Context, selectedEntities, [&firstComponent](SkyLightComponent& component, Entity)
					{
						component.Lod = firstComponent.Lod;
					});
				}

				if (ImGuiEx::Property("Dynamic Sky", firstComponent.DynamicSky))
				{
					ApplyToSelection<SkyLightComponent>(m_Context, selectedEntities, [&firstComponent](SkyLightComponent& component, Entity)
					{
						component.DynamicSky = firstComponent.DynamicSky;
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
