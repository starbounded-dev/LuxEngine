#include "lpch.h"

#include "SceneHierarchyPanel.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Asset/AssetMetadata.h"
#include "Lux/Core/Events/KeyEvent.h"
#include "Lux/Core/Events/MouseEvent.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/Scene/Components.h"
#include "Lux/Scene/Prefab.h"
#include "Lux/Renderer/UI/Font.h"
#include "Lux/Scripting/ScriptEngine.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <glm/gtc/type_ptr.hpp>
#include <entt/entt.hpp>

#include <cstring>

namespace Lux {

	SceneHierarchyPanel::SceneHierarchyPanel(const Ref<Scene>& context, bool isWindow)
		: m_IsWindow(isWindow)
	{
		SetContext(context);
	}

	void SceneHierarchyPanel::SetContext(const Ref<Scene>& context)
	{
		m_Context = context;
		m_SelectionContext = {};
		m_SearchString.clear();
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
				Entity entity{ entityID , m_Context.get() };
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
					const UUID draggedEntityID = *(const UUID*)payload->Data;
					Entity draggedEntity = m_Context->GetEntityByUUID(draggedEntityID);
					if (draggedEntity)
						draggedEntity.SetParent({});
				}

				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
				{
					AssetHandle handle = *(AssetHandle*)payload->Data;
					if (AssetManager::GetAssetType(handle) == AssetType::Prefab)
					{
						Ref<Prefab> prefab = AssetManager::GetAsset<Prefab>(handle);
						if (prefab)
							m_SelectionContext = m_Context->InstantiatePrefab(prefab);
					}
				}

				ImGui::EndDragDropTarget();
			}
		}

		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)
			&& ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)
			&& !ImGui::IsAnyItemHovered())
			m_SelectionContext = {};

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
		if (m_SelectionContext)
		{
			DrawComponents(m_SelectionContext);
		}

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
				m_SelectionContext = {};
				return true;
			default:
				return false;
			}
		});
	}

	void SceneHierarchyPanel::SetSelectedEntity(Entity entity)
	{
		m_SelectionContext = entity;
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

			m_SelectionContext = entity;
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
			if (ImGui::MenuItem("Static Mesh"))
			{
				Entity entity = createEntity("Static Mesh");
				entity.AddComponent<StaticMeshComponent>();
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

		if (!ImGuiEx::IsMatchingSearch(tag, searchFilter) && !hasChildMatchingSearch)
			return;

		ImGuiTreeNodeFlags flags = ((m_SelectionContext == entity) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
		flags |= ImGuiTreeNodeFlags_SpanAvailWidth;
		if (hasChildMatchingSearch)
			flags |= ImGuiTreeNodeFlags_DefaultOpen;
		if (!hasChildren)
			flags |= ImGuiTreeNodeFlags_Leaf;
		bool opened = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, flags, tag.c_str());
		if (ImGui::IsItemClicked())
		{
			m_SelectionContext = entity;
		}

		if (ImGui::BeginDragDropSource())
		{
			const UUID entityID = entity.GetUUID();
			ImGui::SetDragDropPayload("SCENE_HIERARCHY_ENTITY", &entityID, sizeof(UUID));
			ImGui::TextUnformatted(tag.c_str());
			ImGui::EndDragDropSource();
		}

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_HIERARCHY_ENTITY"))
			{
				const UUID draggedEntityID = *(const UUID*)payload->Data;
				Entity draggedEntity = m_Context->GetEntityByUUID(draggedEntityID);
				if (draggedEntity && draggedEntity != entity)
					draggedEntity.SetParent(entity);
			}

			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
			{
				AssetHandle handle = *(AssetHandle*)payload->Data;
				if (AssetManager::GetAssetType(handle) == AssetType::Prefab)
				{
					Ref<Prefab> prefab = AssetManager::GetAsset<Prefab>(handle);
					if (prefab)
					{
						Entity instantiated = m_Context->InstantiatePrefab(prefab);
						if (instantiated)
						{
							instantiated.SetParent(entity);
							m_SelectionContext = instantiated;
						}
					}
				}
			}

			ImGui::EndDragDropTarget();
		}

		bool entityDeleted = false;
		if (ImGui::BeginPopupContextItem())
		{
			DrawEntityCreateMenu(entity);
			ImGui::Separator();
			if (ImGui::MenuItem("Delete Entity"))
				entityDeleted = true;

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

		if (entityDeleted)
		{
			m_Context->DestroyEntity(entity);
			if (m_SelectionContext == entity)
				m_SelectionContext = {};
		}
	}

	static void DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 100.0f)
	{
		ImGuiIO& io = ImGui::GetIO();
		auto boldFont = io.Fonts->Fonts[0];

		ImGui::PushID(label.c_str());

		ImGui::Columns(2);
		ImGui::SetColumnWidth(0, columnWidth);
		ImGui::Text(label.c_str());
		ImGui::NextColumn();

		ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });

		// FIX: Font->Scale is a multiplier (~1.0), not pixel height. Font->FontSize is correct.
		float lineHeight = GImGui->Font->Scale + GImGui->Style.FramePadding.y * 2.0f;
		ImVec2 buttonSize = { lineHeight + 3.0f, lineHeight };

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.9f, 0.2f, 0.2f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
		ImGui::PushFont(boldFont);
		if (ImGui::Button("X", buttonSize))
			values.x = resetValue;
		ImGui::PopFont();
		ImGui::PopStyleColor(3);

		ImGui::SameLine();
		ImGuiEx::Property("##X", values.x, 0.1f, 0.0f, 0.0f, "%.2f");
		ImGui::PopItemWidth();
		ImGui::SameLine();

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.3f, 0.8f, 0.3f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
		ImGui::PushFont(boldFont);
		if (ImGui::Button("Y", buttonSize))
			values.y = resetValue;
		ImGui::PopFont();
		ImGui::PopStyleColor(3);

		ImGui::SameLine();
		ImGuiEx::Property("##Y", values.y, 0.1f, 0.0f, 0.0f, "%.2f");
		ImGui::PopItemWidth();
		ImGui::SameLine();

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.2f, 0.35f, 0.9f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
		ImGui::PushFont(boldFont);
		if (ImGui::Button("Z", buttonSize))
			values.z = resetValue;
		ImGui::PopFont();
		ImGui::PopStyleColor(3);

		ImGui::SameLine();
		ImGuiEx::Property("##Z", values.z, 0.1f, 0.0f, 0.0f, "%.2f");
		ImGui::PopItemWidth();

		ImGui::PopStyleVar();

		ImGui::Columns(1);

		ImGui::PopID();
	}

	template<typename T, typename UIFunction>
	static void DrawComponent(const std::string& name, Entity entity, UIFunction uiFunction)
	{
		const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding;
		if (entity.HasComponent<T>())
		{
			auto& component = entity.GetComponent<T>();
			ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();

			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4, 4 });
			// FIX: Font->Scale is a multiplier (~1.0), not pixel height. Font->FontSize is correct.
			float lineHeight = GImGui->Font->Scale + GImGui->Style.FramePadding.y * 2.0f;
			ImGui::Separator();
			bool open = ImGui::TreeNodeEx((void*)typeid(T).hash_code(), treeNodeFlags, name.c_str());
			ImGui::PopStyleVar(
			);
			ImGui::SameLine(contentRegionAvailable.x - lineHeight * 0.5f);
			if (ImGui::Button("+", ImVec2{ lineHeight, lineHeight }))
			{
				ImGui::OpenPopup("ComponentSettings");
			}

			bool removeComponent = false;
			if (ImGui::BeginPopup("ComponentSettings"))
			{
				if constexpr (!std::is_same_v<T, TransformComponent>)
				{
					if (ImGui::MenuItem("Remove component"))
						removeComponent = true;
				}
				else
				{
					ImGui::MenuItem("Remove component", nullptr, false, false);
				}

				if constexpr (std::is_same_v<T, TransformComponent>)
				{
					if (ImGui::MenuItem("Reset transform"))
					{
						component.Translation = glm::vec3(0.0f);
						component.Rotation = glm::vec3(0.0f);
						component.Scale = glm::vec3(1.0f);
					}
				}

				ImGui::EndPopup();
			}

			if (open)
			{
				uiFunction(component);
				ImGui::TreePop();
			}

			if (removeComponent)
				entity.RemoveComponent<T>();
		}
	}

	void SceneHierarchyPanel::DrawComponents(Entity entity)
	{
		if (entity.HasComponent<TagComponent>())
		{
			auto& tag = entity.GetComponent<TagComponent>().Tag;

			char buffer[256];
			memset(buffer, 0, sizeof(buffer));
			strncpy_s(buffer, sizeof(buffer), tag.c_str(), sizeof(buffer));
			if (ImGui::InputText("##Tag", buffer, sizeof(buffer)))
			{
				tag = std::string(buffer);
			}
		}

		ImGui::SameLine();
		ImGui::PushItemWidth(-1);

		if (ImGui::Button("Add Component"))
			ImGui::OpenPopup("AddComponent");

		if (ImGui::BeginPopup("AddComponent"))
		{
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
			DisplayAddComponentEntry<CameraComponent>("Camera");
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
			DisplayAddComponentEntry<ScriptComponent>("Script Component");
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
			DisplayAddComponentEntry<TextComponent>("Text Component");

			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
			if (ImGui::BeginMenu("2D Primitives"))
			{
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
				DisplayAddComponentEntry<SpriteRendererComponent>("Sprite Renderer");
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
				DisplayAddComponentEntry<CircleRendererComponent>("Circle Renderer");

				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
				if (ImGui::BeginMenu("Physics"))
				{
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
					DisplayAddComponentEntry<RigidBody2DComponent>("Rigidbody 2D");
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
					DisplayAddComponentEntry<BoxCollider2DComponent>("Box Collider 2D");
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
					DisplayAddComponentEntry<CircleCollider2DComponent>("Circle Collider 2D");

					ImGui::EndMenu();
				}

				ImGui::EndMenu();
			}
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
			if (ImGui::BeginMenu("Audio"))
			{
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
				DisplayAddComponentEntry<AudioSourceComponent>("Audio Source");
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
				DisplayAddComponentEntry<AudioListenerComponent>("Audio Listener");

				ImGui::EndMenu();
			}

			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
			if (ImGui::BeginMenu("3D"))
			{
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
				DisplayAddComponentEntry<StaticMeshComponent>("Static Mesh");

				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
				if (ImGui::BeginMenu("Lights"))
				{
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
					DisplayAddComponentEntry<DirectionalLightComponent>("Directional Light");
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
					DisplayAddComponentEntry<PointLightComponent>("Point Light");
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
					DisplayAddComponentEntry<SpotLightComponent>("Spot Light");
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
					DisplayAddComponentEntry<SkyLightComponent>("Sky Light");

					ImGui::EndMenu();
				}

				ImGui::EndMenu();
			}

			ImGui::EndPopup();;
		}

		ImGui::PopItemWidth();

		DrawComponent<TransformComponent>("Transform", entity, [](auto& component)
			{
				DrawVec3Control("Translation", component.Translation);
				glm::vec3 rotation = glm::degrees(component.Rotation);
				DrawVec3Control("Rotation", rotation);
				component.Rotation = glm::radians(rotation);
				DrawVec3Control("Scale", component.Scale, 1.0f);
			});

		DrawComponent<CameraComponent>("Camera", entity, [](auto& component)
			{
				auto& camera = component.Camera;

				ImGuiEx::BeginPropertyGrid();
				ImGuiEx::Property("Primary", component.Primary);

				const char* projectionTypeStrings[] = { "Perspective", "Orthographic" };
				const char* currentProjectionTypeString = projectionTypeStrings[(int)camera.GetProjectionType()];
				if (ImGui::BeginCombo("Projection", currentProjectionTypeString))
				{
					for (int i = 0; i < 2; i++)
					{
						bool isSelected = currentProjectionTypeString == projectionTypeStrings[i];
						if (ImGui::Selectable(projectionTypeStrings[i], isSelected))
						{
							currentProjectionTypeString = projectionTypeStrings[i];
							camera.SetProjectionType((SceneCamera::ProjectionType)i);
						}

						if (isSelected)
							ImGui::SetItemDefaultFocus();
					}

					ImGui::EndCombo();
				}

				if (camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective)
				{

					float perspectiveNear = camera.GetPerspectiveNearClip();
					if (ImGuiEx::Property("Near", perspectiveNear))
						camera.SetPerspectiveNearClip(perspectiveNear);

					float perspectiveFar = camera.GetPerspectiveFarClip();
					if (ImGuiEx::Property("Far", perspectiveFar))
						camera.SetPerspectiveFarClip(perspectiveFar);
				}

				if (camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic)
				{
					float orthoSize = camera.GetOrthographicSize();
					if (ImGuiEx::Property("Size", orthoSize))
						camera.SetOrthographicSize(orthoSize);

					float orthoNear = camera.GetOrthographicNearClip();
					if (ImGuiEx::Property("Near", orthoNear))
						camera.SetOrthographicNearClip(orthoNear);

					float orthoFar = camera.GetOrthographicFarClip();
					if (ImGuiEx::Property("Far", orthoFar))
						camera.SetOrthographicFarClip(orthoFar);

					ImGuiEx::Property("Fixed Aspect Ratio", component.FixedAspectRatio);
				}

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponent<ScriptComponent>("Script", entity, [entity, scene = m_Context](auto& component) mutable
			{
				bool scriptClassExists = ScriptEngine::EntityClassExists(component.ClassName);

				static char buffer[64];
				strcpy_s(buffer, sizeof(buffer), component.ClassName.c_str());

				if (!scriptClassExists)
				{
					ImGuiEx::ScopedColour textColor(ImGuiCol_Text, ImVec4(0.9f, 0.2f, 0.3f, 1.0f));

					if (ImGui::InputText("Class", buffer, sizeof(buffer)))
					{
						component.ClassName = buffer;
						return;
					}
				}
				else
				{
					if (ImGui::InputText("Class", buffer, sizeof(buffer)))
					{
						component.ClassName = buffer;
						return;
					}
				}

				// Fields
				bool sceneRunning = scene->IsRunning();
				if (sceneRunning)
				{
					Ref<ScriptInstance> scriptInstance = ScriptEngine::GetEntityScriptInstance(entity.GetUUID());
					if (scriptInstance)
					{
						const auto& fields = scriptInstance->GetScriptClass()->GetFields();
						for (const auto& [name, field] : fields)
						{
							if (field.Type == ScriptFieldType::Float)
							{
								float data = scriptInstance->GetFieldValue<float>(name);
								if (ImGui::DragFloat(name.c_str(), &data))
								{
									scriptInstance->SetFieldValue(name, data);
								}
							}
						}
					}
				}
				else
				{
					if (scriptClassExists)
					{
						Ref<ScriptClass> entityClass = ScriptEngine::GetEntityClass(component.ClassName);
						const auto& fields = entityClass->GetFields();

						auto& entityFields = ScriptEngine::GetScriptFieldMap(entity);
						for (const auto& [name, field] : fields)
						{
							// Field has been set in editor
							if (entityFields.find(name) != entityFields.end())
							{
								ScriptFieldInstance& scriptField = entityFields.at(name);

								// Display control to set it maybe
								if (field.Type == ScriptFieldType::Float)
								{
									float data = scriptField.GetValue<float>();
									if (ImGui::DragFloat(name.c_str(), &data))
										scriptField.SetValue(data);
								}
							}
							else
							{
								// Display control to set it maybe
								if (field.Type == ScriptFieldType::Float)
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
					}
				}
			});

		DrawComponent<SpriteRendererComponent>("Sprite Renderer", entity, [](auto& component)
			{
				ImGuiEx::BeginPropertyGrid();
				ImGuiEx::PropertyColor("Color", component.Color);

				std::string label = "None";
				bool isTextureValid = false;
				if (component.Texture != 0)
				{
					if (AssetManager::IsAssetHandleValid(component.Texture)
						&& AssetManager::GetAssetType(component.Texture) == AssetType::Texture)
					{
						const AssetMetadata& metadata = Project::GetActive()->GetEditorAssetManager()->GetMetadata(component.Texture);
						label = metadata.FilePath.filename().string();
						isTextureValid = true;
					}
					else
					{
						label = "Invalid";
					}
				}

				ImVec2 buttonLabelSize = ImGui::CalcTextSize(label.c_str());
				buttonLabelSize.x += 20.0f;
				float buttonLabelWidth = glm::max<float>(100.0f, buttonLabelSize.x);

				ImGui::Button(label.c_str(), ImVec2(buttonLabelWidth, 0.0f));
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
					{
						AssetHandle handle = *(AssetHandle*)payload->Data;
						if (AssetManager::GetAssetType(handle) == AssetType::Texture)
						{
							component.Texture = handle;
						}
						else
						{
							LUX_CORE_WARN("Wrong asset type!");
						}

					}
					ImGui::EndDragDropTarget();
				}

				if (isTextureValid)
				{
					ImGui::SameLine();
					ImVec2 xLabelSize = ImGui::CalcTextSize("X");
					float buttonSize = xLabelSize.y + ImGui::GetStyle().FramePadding.y * 2.0f;
					if (ImGui::Button("X", ImVec2(buttonSize, buttonSize)))
					{
						component.Texture = 0;
					}
				}

				ImGui::SameLine();
				ImGui::Text("Texture");

				ImGuiEx::Property("Tiling Factor", component.TilingFactor, 0.1f, 0.0f, 100.0f);

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponent<CircleRendererComponent>("Circle Renderer", entity, [](auto& component)
			{
				ImGuiEx::BeginPropertyGrid();
				ImGuiEx::PropertyColor("Color", component.Color);
				ImGuiEx::Property("Thickness", component.Thickness, 0.025f, 0.0f, 1.0f);
				ImGuiEx::Property("Fade", component.Fade, 0.00025f, 0.0f, 1.0f);

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponent<RigidBody2DComponent>("RigidBody 2D", entity, [](auto& component)
			{
				const char* bodyTypeStrings[] = { "Static", "Dynamic", "Kinematic" };
				const char* currentBodyTypeString = bodyTypeStrings[(int)component.Type];
				if (ImGui::BeginCombo("Body Type", currentBodyTypeString))
				{
					// FIX: Was "for (int i = 0; i < 3; i++);" - stray semicolon made
					// the loop body empty (items never populated) and EndCombo was
					// missing entirely, triggering an ImGui assert every frame.
					for (int i = 0; i < 3; i++)
					{
						bool isSelected = (currentBodyTypeString == bodyTypeStrings[i]);
						if (ImGui::Selectable(bodyTypeStrings[i], isSelected))
						{
							currentBodyTypeString = bodyTypeStrings[i];
							component.Type = (RigidBody2DComponent::BodyType)i;
						}
						if (isSelected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}

				ImGuiEx::BeginPropertyGrid();
				ImGuiEx::Property("Fixed Rotation", component.FixedRotation);
				ImGuiEx::EndPropertyGrid();
			});

		DrawComponent<BoxCollider2DComponent>("Box Collider 2D", entity, [](auto& component)
			{
				ImGuiEx::BeginPropertyGrid();
				ImGuiEx::Property("Offset", component.Offset);
				ImGuiEx::Property("Size", component.Size);
				ImGuiEx::Property("Density", component.Density, 0.01f, 0.0f, 1.0f);
				ImGuiEx::Property("Friction", component.Friction, 0.01f, 0.0f, 1.0f);
				ImGuiEx::Property("Restitution", component.Restitution, 0.01f, 0.0f, 1.0f);
				ImGuiEx::Property("Restitution Threshold", component.RestitutionThreshold, 0.01f, 0.0f);

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponent<CircleCollider2DComponent>("Circle Collider 2D", entity, [](auto& component)
			{
				ImGuiEx::BeginPropertyGrid();
				ImGuiEx::Property("Offset", component.Offset);
				ImGuiEx::Property("Radius", component.Radius);
				ImGuiEx::Property("Density", component.Density, 0.01f, 0.0f, 1.0f);
				ImGuiEx::Property("Friction", component.Friction, 0.01f, 0.0f, 1.0f);
				ImGuiEx::Property("Restitution", component.Restitution, 0.01f, 0.0f, 1.0f);
				ImGuiEx::Property("Restitution Threshold", component.RestitutionThreshold, 0.01f, 0.0f);

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponent<TextComponent>("Text Renderer", entity, [](auto& component)
			{
				ImGuiEx::BeginPropertyGrid();
				ImGuiEx::PropertyMultiline("Text String", component.TextString);
				ImGuiEx::PropertyColor("Color", component.Color);
				ImGuiEx::Property("Kerning", component.Kerning, 0.025f);
				ImGuiEx::Property("Line Spacing", component.LineSpacing, 0.025f);

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponent<AudioSourceComponent>("Audio Source", entity, [&entity](AudioSourceComponent& component)
			{
				auto& config = component.Config;

				ImGui::Text("Audio");
				ImGui::NextColumn();
				ImGui::PushItemWidth(-1);

				std::string label = "None";
				bool isAudioValid = false;
				if (component.Audio != 0)
				{
					if (AssetManager::IsAssetHandleValid(component.Audio) && AssetManager::GetAssetType(component.Audio) == AssetType::Audio)
					{
						const AssetMetadata& metadata = Project::GetActive()->GetEditorAssetManager()->GetMetadata(component.Audio);
						label = metadata.FilePath.filename().string();
						isAudioValid = true;
					}
					else
					{
						label = "Invalid";
					}
				}

				ImVec2 buttonLabelSize = ImGui::CalcTextSize(label.c_str());
				buttonLabelSize.x += 20.0f;
				float buttonLabelWidth = std::max<float>(100.0f, buttonLabelSize.x + 4.0f);

				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 6, 6 });
				ImGui::Button(label.c_str(), ImVec2(buttonLabelWidth, 0.0f));
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
					{
						AssetHandle handle = *(AssetHandle*)payload->Data;

						if (AssetManager::GetAssetType(handle) == AssetType::Audio)
						{
							if (component.AudioSourceData.Playlist.size() > 0)
							{
								component.AudioSourceData.Playlist[0] = handle;
							}

							component.Audio = handle;
						}
						else
						{
							LUX_CORE_WARN("Wrong asset type!");
						}
					}
					ImGui::EndDragDropTarget();
				}

				if (isAudioValid)
				{
					ImGui::SameLine();
					ImVec2 xLabelSize = ImGui::CalcTextSize("X");
					float buttonSize = xLabelSize.y + ImGui::GetStyle().FramePadding.y * 2.0f;

					if (ImGui::Button("X", ImVec2(buttonSize, buttonSize)))
					{
						AssetHandle result = 0;
						component.Audio = result;
					}
				}

				ImGui::PopStyleVar();
				ImGui::PopItemWidth();
				ImGui::NextColumn();

				if (component.Audio != 0)
				{
					Ref<AudioSource> audioSource = AssetManager::GetAsset<AudioSource>(component.Audio);

					ImGui::SliderFloat("Volume Multiplier", &config.VolumeMultiplier, 0.0f, 2.0f, "%.2f");

					ImGui::SliderFloat("Pitch Multiplier", &config.PitchMultiplier, 0.0f, 3.0f, "%.2f");

					ImGuiEx::Property("Play On Awake", config.PlayOnAwake);

					if (ImGui::Checkbox("Spatialization", &config.Spatialization))
					{
						audioSource->SetSpatialization(config.Spatialization);
					}

					if (component.AudioSourceData.UsePlaylist)
					{
						// Looping is not available in playlist mode
						bool looping = false;
						audioSource->SetLooping(looping);
						config.Looping = looping;

						ImGui::BeginDisabled();
						ImGuiEx::Property("Looping", looping);
						ImGui::EndDisabled();
					}
					else
					{
						if (ImGui::Checkbox("Looping", &config.Looping))
						{
							audioSource->SetLooping(config.Looping);
						}
					}

					if (component.AudioSourceData.UsePlaylist)
					{
						// Repeat Playlist
						bool repeatPlaylist = component.AudioSourceData.RepeatPlaylist;
						if (ImGui::Checkbox("Repeat Playlist", &repeatPlaylist))
						{
							if (component.AudioSourceData.RepeatAfterSpecificTrackPlays)
								component.AudioSourceData.RepeatAfterSpecificTrackPlays = false;
							component.AudioSourceData.RepeatPlaylist = repeatPlaylist;
						}

						// Loop When Start Index Plays
						bool repeatAfterSpecificTrackPlays = component.AudioSourceData.RepeatAfterSpecificTrackPlays;
						if (ImGui::Checkbox("Loop When Start Index Plays", &repeatAfterSpecificTrackPlays))
						{
							if (component.AudioSourceData.RepeatPlaylist)
								component.AudioSourceData.RepeatPlaylist = false;
							component.AudioSourceData.RepeatAfterSpecificTrackPlays = repeatAfterSpecificTrackPlays;
						}

						// Playlist Start Index
						uint32_t startIndex = component.AudioSourceData.StartIndex;
						uint32_t maxValue = component.AudioSourceData.Playlist.size() > 0 ? (uint32_t)(component.AudioSourceData.Playlist.size() - 1) : 0;
						if (ImGui::SliderInt("Playlist Start Index", (int*)&startIndex, 0, (int)maxValue))
						{
							component.AudioSourceData.StartIndex = startIndex;
						}
					}


					if (config.Spatialization)
					{
						// Attenuation Model Combo
						const char* attenuationTypeStrings[] = { "None", "Inverse", "Linear", "Exponential" };
						int attenuationType = static_cast<int>(config.AttenuationModel);
						if (ImGui::Combo("Attenuation Model", &attenuationType, attenuationTypeStrings, IM_ARRAYSIZE(attenuationTypeStrings)))
						{
							config.AttenuationModel = static_cast<AttenuationModelType>(attenuationType);
						}

						// Roll Off
						ImGui::SliderFloat("Roll Off", &config.RollOff, 0.0f, 10.0f, "%.2f");

						// Min/Max Gain
						ImGui::SliderFloat("Min Gain", &config.MinGain, 0.0f, 1.0f, "%.2f");
						ImGui::SliderFloat("Max Gain", &config.MaxGain, 0.0f, 1.0f, "%.2f");

						// Min/Max Distance
						ImGui::SliderFloat("Min Distance", &config.MinDistance, 0.0f, 100.0f, "%.2f");
						ImGui::SliderFloat("Max Distance", &config.MaxDistance, 0.0f, 100.0f, "%.2f");

						// Cone Angles
						float innerAngle = glm::degrees(config.ConeInnerAngle);
						if (ImGui::SliderFloat("Cone Inner Angle", &innerAngle, 0.0f, 360.0f, "%.2f"))
							config.ConeInnerAngle = glm::radians(innerAngle);

						float outerAngle = glm::degrees(config.ConeOuterAngle);
						if (ImGui::SliderFloat("Cone Outer Angle", &outerAngle, 0.0f, 360.0f, "%.2f"))
							config.ConeOuterAngle = glm::radians(outerAngle);

						// Cone Outer Gain
						ImGui::SliderFloat("Cone Outer Gain", &config.ConeOuterGain, 0.0f, 1.0f, "%.2f");

						// Doppler Factor
						ImGui::SliderFloat("Doppler Factor", &config.DopplerFactor, 0.0f, 10.0f, "%.2f");
					}


					if (component.Audio)
					{
						glm::mat4 inverted = glm::inverse(entity.GetComponent<TransformComponent>().GetTransform());
						glm::vec3 forward = glm::normalize(glm::vec3(inverted[2])); // z axis
						audioSource->SetConfig(config);
						audioSource->SetPosition(glm::vec4(entity.GetComponent<TransformComponent>().Translation, 1.0f));
						audioSource->SetDirection(-forward);
					}
				}

				if (component.AudioSourceData.UsePlaylist)
				{
					if (component.AudioSourceData.Playlist.size() == 0)
						component.AddAudioSource(component.Audio);

					if (component.AudioSourceData.PlaylistCopy.size() > 0 && component.AudioSourceData.Playlist.size() != component.AudioSourceData.PlaylistCopy.size()/* && component.AudioSourceData.ChangedUsingTextureAnimation*/)
					{
						if (component.AudioSourceData.Playlist.size() != component.AudioSourceData.PlaylistCopy.size())
							component.AudioSourceData.Playlist.resize(component.AudioSourceData.PlaylistCopy.size());

						for (uint32_t i = 0; i < component.AudioSourceData.PlaylistCopy.size(); i++)
						{
							if (component.AudioSourceData.Playlist[i] != component.AudioSourceData.PlaylistCopy[i])
								component.AudioSourceData.Playlist[i] = component.AudioSourceData.PlaylistCopy[i];
						}
					}

					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 12, 8 });
					if (ImGui::Button("Add Audio"))
					{
						if (component.Audio)
						{
							AssetHandle tempHandle = 0;
							component.AddAudioSource(tempHandle);
						}
					}
					ImGui::PopStyleVar();

					if (component.AudioSourceData.PlaylistCopy.size() != component.AudioSourceData.Playlist.size())
						component.AudioSourceData.PlaylistCopy.resize(component.AudioSourceData.Playlist.size());

					for (uint32_t i = 0; i < component.AudioSourceData.Playlist.size(); i++)
					{
						if (component.AudioSourceData.PlaylistCopy[i] != component.AudioSourceData.Playlist[i])
							component.AudioSourceData.PlaylistCopy[i] = component.AudioSourceData.Playlist[i];

						ImGui::PushID(i);
						ImGui::Text("Audio");
						ImGui::NextColumn();
						ImGui::PushItemWidth(-1);

						std::string label = "None";
						bool isAudioValid = false;
						if (component.AudioSourceData.Playlist[i] != 0)
						{
							if (AssetManager::IsAssetHandleValid(component.AudioSourceData.Playlist[i]) && AssetManager::GetAssetType(component.AudioSourceData.Playlist[i]) == AssetType::Audio)
							{
								const AssetMetadata& metadata = Project::GetActive()->GetEditorAssetManager()->GetMetadata(component.AudioSourceData.Playlist[i]);
								label = metadata.FilePath.filename().string();
								isAudioValid = true;
							}
							else
							{
								label = "Invalid";
							}
						}

						ImVec2 buttonLabelSize = ImGui::CalcTextSize(label.c_str());
						buttonLabelSize.x += 20.0f;
						float buttonLabelWidth = std::max<float>(100.0f, buttonLabelSize.x + 4.0f);

						ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 6, 6 });
						ImGui::Button(label.c_str(), ImVec2(buttonLabelWidth, 0.0f));
						if (ImGui::BeginDragDropTarget())
						{
							if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
							{
								AssetHandle handle = *(AssetHandle*)payload->Data;

								if (AssetManager::GetAssetType(handle) == AssetType::Audio)
								{
									if (i == 0)
									{
										component.Audio = handle;
									}

									component.AudioSourceData.Playlist[i] = handle;
									component.AudioSourceData.PlaylistCopy[i] = handle;
								}
								else
								{
									LUX_CORE_WARN("Wrong asset type!");
								}
							}
							ImGui::EndDragDropTarget();
						}

						ImGui::SameLine();
						ImVec2 xLabelSize = ImGui::CalcTextSize("X");
						float buttonSize = xLabelSize.y + ImGui::GetStyle().FramePadding.y * 2.0f;

						if (ImGui::Button("X", ImVec2(buttonSize, buttonSize)))
						{
							AssetHandle result = 0;

							if (component.AudioSourceData.PlaylistCopy.size() > 0)
							{
								uint32_t index = 0;

								for (uint32_t j = 0; j < component.AudioSourceData.PlaylistCopy.size(); j++)
								{
									if (component.AudioSourceData.PlaylistCopy[j] == component.AudioSourceData.Playlist[i])
										index = j;
								}

								component.AudioSourceData.PlaylistCopy.erase(component.AudioSourceData.PlaylistCopy.begin() + index);
								component.AudioSourceData.PlaylistCopy.shrink_to_fit();
							}

							component.RemoveAudioSource(component.AudioSourceData.Playlist[i]);

							if (i == 0)
							{
								if (component.AudioSourceData.Playlist.size() > 0)
								{
									if (component.Audio != component.AudioSourceData.Playlist[0] && component.AudioSourceData.Playlist[0] != 0)
									{
										component.Audio = component.AudioSourceData.Playlist[0];
									}
								}

								if (component.AudioSourceData.Playlist.size() == 0)
									component.AudioSourceData.UsePlaylist = false;
							}
						}

						ImGui::PopStyleVar();
						ImGui::PopItemWidth();
						ImGui::NextColumn();
						ImGui::PopID();
					}
				}
			});

		DrawComponent<AudioListenerComponent>("Audio Listener", entity, [](AudioListenerComponent& component)
			{
				auto& config = component.Config;

				ImGuiEx::BeginPropertyGrid();
				ImGuiEx::Property("Active", component.Active);
				ImGuiEx::EndPropertyGrid();

				float innerAngle = glm::degrees(config.ConeInnerAngle);
				if (ImGui::SliderFloat("Cone Inner Angle", &innerAngle, 0.0f, 360.0f, "%.2f"))
					config.ConeInnerAngle = glm::radians(innerAngle);

				float outerAngle = glm::degrees(config.ConeOuterAngle);
				if (ImGui::SliderFloat("Cone Outer Angle", &outerAngle, 0.0f, 360.0f, "%.2f"))
					config.ConeOuterAngle = glm::radians(outerAngle);

				ImGuiEx::BeginPropertyGrid();
				ImGuiEx::PropertySlider("Cone Outer Gain", config.ConeOuterGain, 0.0f, 1.0f);
				ImGuiEx::EndPropertyGrid();
			});

		// ============================================================================
		// 3D Component Inspectors
		// ============================================================================

		DrawComponent<StaticMeshComponent>("Static Mesh", entity, [](StaticMeshComponent& component)
			{
				// Mesh asset selector
				std::string meshLabel = "None";
				if (component.Mesh != 0)
				{
					if (AssetManager::IsAssetHandleValid(component.Mesh)
						&& AssetManager::GetAssetType(component.Mesh) == AssetType::Mesh)
					{
						const AssetMetadata& metadata = Project::GetActive()->GetEditorAssetManager()->GetMetadata(component.Mesh);
						meshLabel = metadata.FilePath.filename().string();
					}
					else
					{
						meshLabel = "Invalid";
					}
				}

				ImVec2 buttonLabelSize = ImGui::CalcTextSize(meshLabel.c_str());
				buttonLabelSize.x += 20.0f;
				float buttonLabelWidth = glm::max<float>(100.0f, buttonLabelSize.x);

				ImGui::Button(meshLabel.c_str(), ImVec2(buttonLabelWidth, 0.0f));
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
					{
						AssetHandle handle = *(AssetHandle*)payload->Data;
						if (AssetManager::GetAssetType(handle) == AssetType::Mesh)
							component.Mesh = handle;
						else
							LUX_CORE_WARN("Wrong asset type! Expected Mesh.");
					}
					ImGui::EndDragDropTarget();
				}
				ImGui::SameLine();
				ImGui::Text("Mesh");

				// Material table asset selector
				std::string materialLabel = "Default";
				if (component.MaterialTable != 0)
				{
					if (AssetManager::IsAssetHandleValid(component.MaterialTable))
					{
						const AssetMetadata& metadata = Project::GetActive()->GetEditorAssetManager()->GetMetadata(component.MaterialTable);
						materialLabel = metadata.FilePath.filename().string();
					}
					else
					{
						materialLabel = "Invalid";
					}
				}

				ImVec2 matButtonLabelSize = ImGui::CalcTextSize(materialLabel.c_str());
				matButtonLabelSize.x += 20.0f;
				float matButtonLabelWidth = glm::max<float>(100.0f, matButtonLabelSize.x);

				ImGui::Button(materialLabel.c_str(), ImVec2(matButtonLabelWidth, 0.0f));
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
					{
						AssetHandle handle = *(AssetHandle*)payload->Data;
						// MaterialTable may not have its own asset type yet - just accept it
						component.MaterialTable = handle;
					}
					ImGui::EndDragDropTarget();
				}
				ImGui::SameLine();
				ImGui::Text("Material Table");

				ImGuiEx::Property("Visible", component.Visible);
				ImGuiEx::Property("Cast Shadows", component.CastShadows);

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponent<DirectionalLightComponent>("Directional Light", entity, [](DirectionalLightComponent& component)
			{
				ImGuiEx::BeginPropertyGrid();
				ImGuiEx::BeginPropertyGrid();
				ImGuiEx::BeginPropertyGrid();
				ImGuiEx::PropertyColor("Radiance", component.Radiance);
				ImGuiEx::Property("Intensity", component.Intensity, 0.1f, 0.0f, 100.0f);
				ImGuiEx::Property("Shadow Amount", component.ShadowAmount, 0.01f, 0.0f, 1.0f);
				ImGuiEx::Property("Cast Shadows", component.CastShadows);

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponent<PointLightComponent>("Point Light", entity, [](PointLightComponent& component)
			{
				ImGuiEx::BeginPropertyGrid();
				ImGuiEx::BeginPropertyGrid();
				ImGuiEx::BeginPropertyGrid();
				ImGuiEx::PropertyColor("Radiance", component.Radiance);
				ImGuiEx::Property("Intensity", component.Intensity, 0.1f, 0.0f, 100.0f);
				ImGuiEx::Property("Radius", component.Radius, 0.1f, 0.0f, 1000.0f);
				ImGuiEx::Property("Falloff", component.Falloff, 0.01f, 0.0f, 10.0f);
				ImGuiEx::Property("Min Radius", component.MinRadius, 0.001f, 0.0f, 1.0f);
				ImGuiEx::Property("Light Size", component.LightSize, 0.01f, 0.0f, 10.0f);
				ImGuiEx::Property("Cast Shadows", component.CastShadows);

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponent<SpotLightComponent>("Spot Light", entity, [](SpotLightComponent& component)
			{
				ImGuiEx::BeginPropertyGrid();
				ImGuiEx::BeginPropertyGrid();
				ImGuiEx::BeginPropertyGrid();
				ImGuiEx::PropertyColor("Radiance", component.Radiance);
				ImGuiEx::Property("Intensity", component.Intensity, 0.1f, 0.0f, 100.0f);
				ImGuiEx::Property("Range", component.Range, 0.1f, 0.0f, 1000.0f);
				ImGuiEx::Property("Angle", component.Angle, 1.0f, 0.0f, 90.0f);
				ImGuiEx::Property("Angle Attenuation", component.AngleAttenuation, 0.01f, 0.0f, 10.0f);
				ImGuiEx::Property("Falloff", component.Falloff, 0.01f, 0.0f, 10.0f);
				ImGuiEx::Property("Cast Shadows", component.CastShadows);

				ImGuiEx::EndPropertyGrid();
			});

		DrawComponent<SkyLightComponent>("Sky Light", entity, [](SkyLightComponent& component)
			{
				// Environment map asset selector
				std::string envLabel = "None";
				if (component.EnvironmentMap != 0)
				{
					if (AssetManager::IsAssetHandleValid(component.EnvironmentMap))
					{
						const AssetMetadata& metadata = Project::GetActive()->GetEditorAssetManager()->GetMetadata(component.EnvironmentMap);
						envLabel = metadata.FilePath.filename().string();
					}
					else
					{
						envLabel = "Invalid";
					}
				}

				ImVec2 buttonLabelSize = ImGui::CalcTextSize(envLabel.c_str());
				buttonLabelSize.x += 20.0f;
				float buttonLabelWidth = glm::max<float>(100.0f, buttonLabelSize.x);

				ImGui::Button(envLabel.c_str(), ImVec2(buttonLabelWidth, 0.0f));
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
					{
						AssetHandle handle = *(AssetHandle*)payload->Data;
						// Accept environment map (HDR texture or environment asset)
						component.EnvironmentMap = handle;
					}
					ImGui::EndDragDropTarget();
				}
				ImGui::SameLine();
				ImGui::Text("Environment Map");

				ImGuiEx::BeginPropertyGrid();
				ImGuiEx::Property("Intensity", component.Intensity, 0.01f, 0.0f, 10.0f);
				ImGuiEx::EndPropertyGrid();
			});


	}

	template<typename T>
	void SceneHierarchyPanel::DisplayAddComponentEntry(const std::string& entryName) {
		if (!m_SelectionContext.HasComponent<T>())
		{
			if (ImGui::MenuItem(entryName.c_str()))
			{
				m_SelectionContext.AddComponent<T>();
				ImGui::CloseCurrentPopup();
			}
		}
	}

}
