#include "sepch.h"

#include "SceneHierarchyPanel.h"
#include "StarEngine/Scene/Components.h"

#include "StarEngine/Scripting/ScriptEngine.h"
#include "StarEngine/Scripting/ScriptEntityStorage.hpp"
#include "StarEngine/UI/UI.h"

#include "StarEngine/Asset/AssetManager.h"
#include "StarEngine/Asset/AssetMetadata.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <glm/gtc/type_ptr.hpp>

#include <cstring>

#include "StarEngine/ImGui/PropertyGrid.h"
#include "StarEngine/ImGui/UICore.h"

/* The Microsoft C++ compiler is non-compliant with the C++ standard and needs
 * the following definition to disable a security warning on std::strncpy().
 */
#ifdef _MSVC_LANG
#define _CRT_SECURE_NO_WARNINGS
#endif

namespace StarEngine {

	SceneHierarchyPanel::SceneHierarchyPanel(const Ref<Scene>& context)
	{
		SetContext(context);
	}

	void SceneHierarchyPanel::SetContext(const Ref<Scene>& context)
	{
		m_Context = context;
		m_EntitySelectionContext = {};
	}

	void SceneHierarchyPanel::OnImGuiRender()
	{
		ImGui::Begin("Scene Hierarchy");

		if (m_Context)
		{
			auto view = m_Context->m_Registry.view<entt::entity>();
			for (auto entityID : view)
			{
				Entity entity{ entityID, &(*m_Context) };
				DrawEntityNode(entity);
			}

			if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
				m_SelectionContext = {};

			// Right-click on blank space
			if (ImGui::BeginPopupContextWindow(0, ImGuiPopupFlags_NoOpenOverItems))
			{
				if (ImGui::MenuItem("Create Empty Entity"))
					m_Context->CreateEntity("Empty Entity");

				ImGui::EndPopup();
			}
		}

		ImGui::End();

		ImGui::Begin("Properties");
		if (m_EntitySelectionContext)
		{
			DrawComponents(m_EntitySelectionContext);
		}
		ImGui::End();

		// Declare and initialize entityDeleted here
		bool entityDeleted = false;

		if (entityDeleted)
		{
			m_Context->DestroyEntity(m_EntitySelectionContext);
			m_EntitySelectionContext = {};
		}
	}

	void SceneHierarchyPanel::SetSelectedEntity(Entity entity)
	{
		m_EntitySelectionContext = entity;
	}

	void SceneHierarchyPanel::DrawEntityNode(Entity entity)
	{
		auto& tag = entity.GetComponent<TagComponent>().Tag;

		ImGuiTreeNodeFlags flags = ((m_EntitySelectionContext == entity) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
		flags |= ImGuiTreeNodeFlags_SpanAvailWidth;
		bool opened = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, flags, tag.c_str());
		if (ImGui::IsMouseReleased(0))
		{
			if (ImGui::IsItemHovered())
			{
				m_EntitySelectionContext = entity;
			}
		}

		bool entityDeleted = false;
		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Create Empty Entity"))
			{
				m_EntitySelectionContext = m_Context->CreateEntity("Empty Entity");
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Delete Entity"))
			{
				entityDeleted = true;
			}

			ImGui::EndPopup();
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

		float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
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
		ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f");
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
		ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f");
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
		ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f");
		ImGui::PopItemWidth();

		ImGui::PopStyleVar();

		ImGui::Columns(1);

		ImGui::PopID();
	}

	template<typename T, typename UIFunction>
	static void DrawComponent(const std::string& name, Entity entity, UIFunction uiFunction)
	{
		const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_FramePadding;
		if (entity.HasComponent<T>())
		{
			auto& component = entity.GetComponent<T>();
			ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();

			//auto& entities = SelectionManager::GetSelections(s_ActiveSelectionContext);

			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4, 4 });
			float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
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
				if (ImGui::MenuItem("Remove component"))
					removeComponent = true;

				ImGui::EndPopup();
			}

			if (open)
			{
				//const bool isMultiEdit = entities.size() > 1;
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

			ImGui::EndPopup();
		}

		ImGui::PopItemWidth();

		DrawComponent<TransformComponent>("Transform", entity, [](auto& component)
			{
				DrawVec3Control("Translation", component.Translation);
				glm::vec3 rotation = glm::vec3(glm::degrees(component.GetRotationEuler()));
				DrawVec3Control("Rotation", rotation);
				component.SetRotationEuler(glm::radians(rotation));
				DrawVec3Control("Scale", component.Scale, 1.0f);
			});

		DrawComponent<CameraComponent>("Camera", entity, [](auto& component)
			{
				auto& camera = component.Camera;

				ImGui::Checkbox("Primary", &component.Primary);

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
					if (ImGui::DragFloat("Near", &perspectiveNear))
						camera.SetPerspectiveNearClip(perspectiveNear);

					float perspectiveFar = camera.GetPerspectiveFarClip();
					if (ImGui::DragFloat("Far", &perspectiveFar))
						camera.SetPerspectiveFarClip(perspectiveFar);
				}

				if (camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic)
				{
					float orthoSize = camera.GetOrthographicSize();
					if (ImGui::DragFloat("Size", &orthoSize))
						camera.SetOrthographicSize(orthoSize);

					float orthoNear = camera.GetOrthographicNearClip();
					if (ImGui::DragFloat("Near", &orthoNear))
						camera.SetOrthographicNearClip(orthoNear);

					float orthoFar = camera.GetOrthographicFarClip();
					if (ImGui::DragFloat("Far", &orthoFar))
						camera.SetOrthographicFarClip(orthoFar);

					ImGui::Checkbox("Fixed Aspect Ratio", &component.FixedAspectRatio);
				}
			});

		DrawComponent<ScriptComponent>("Script", entity, [=](ScriptComponent& component) mutable
			{
			UI::BeginPropertyGrid();

			const bool inconsistentScriptClass = IsInconsistentPrimitive<UUID, ScriptComponent>([](const ScriptComponent& other) { return other.ScriptID; });
			//ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, isMultiEdit&& inconsistentScriptClass);
			auto& scriptEngine = ScriptEngine::GetMutable();

			bool isError = !scriptEngine.IsValidScript(component.ScriptID);
			const UI::PropertyAssetReferenceSettings c_AssetRefSettings = { true, false, 0.0f, (isError && !inconsistentScriptClass) ? ImVec4(0.9f, 0.2f, 0.2f, 1.0f) : ImGui::ColorConvertU32ToFloat4(Colors::Theme::text), ImGui::ColorConvertU32ToFloat4(Colors::Theme::textError), true };

			auto oldScriptID = component.ScriptID;

			if (UI::PropertyScriptReference("Script Class", component.ScriptID, c_AssetRefSettings))
			{
				isError = !scriptEngine.IsValidScript(component.ScriptID);

				const auto& selected = SelectionManager::GetSelections(m_SelectionContext); // vector<AssetHandle>

				for (auto handle : selected)
				{
					Entity e = m_Context->GetEntityWithUUID(static_cast<UUID>(handle));
					if (!e) continue;

					auto& sc = e.GetComponent<ScriptComponent>();
					sc.ScriptID = component.ScriptID;

					if (isError)
					{
						bool wasCleared = (sc.ScriptID == 0);
						if (wasCleared)
							sc.ScriptID = oldScriptID;

						if (oldScriptID)
							m_Context->GetScriptStorage().ShutdownEntityStorage(sc.ScriptID, e.GetUUID());

						if (wasCleared)
							sc.ScriptID = 0;
					}
					else
					{
						m_Context->GetScriptStorage().InitializeEntityStorage(sc.ScriptID, e.GetUUID());
					}
				}

			}

			ImGui::PopItemFlag();

			UI::EndPropertyGrid();

			auto& scriptStorage = m_Context->GetScriptStorage();
			/*
			// NOTE(Peter): Editing fields doesn't really work if there's inconsistencies with the script classes...
			if (!isError && !inconsistentScriptClass && scriptStorage.EntityStorage.contains(firstEntity.GetUUID()))
			{
				UI::BeginPropertyGrid();

				auto& entityStorage = scriptStorage.EntityStorage.at(firstEntity.GetUUID());

				for (auto& [fieldID, fieldStorage] : entityStorage.Fields)
				{
					if (fieldStorage.IsArray())
					{
						if (UI::DrawFieldArray(m_Context, fieldStorage.GetName(), fieldStorage))
						{
							/*for (auto entityID : entities)
							{
								Entity entity = m_Context->GetEntityWithUUID(entityID);
								const auto& sc = entity.GetComponent<ScriptComponent>();
								storage->CopyData(firstComponent.ManagedInstance, sc.ManagedInstance);
							}*//*
						}
					}
					else
					{
						if (UI::DrawFieldValue(m_Context, fieldStorage.GetName(), fieldStorage))
						{
							/*for (auto entityID : entities)
							{
								Entity entity = m_Context->GetEntityWithUUID(entityID);
								const auto& sc = entity.GetComponent<ScriptComponent>();
								storage->CopyData(firstComponent.ManagedInstance, sc.ManagedInstance);
							}*//*
						}
					}
				}

				UI::EndPropertyGrid();
			}*/
			});

		DrawComponent<SpriteRendererComponent>("Sprite Renderer", entity, [](auto& component)
			{
				ImGui::ColorEdit4("Color", glm::value_ptr(component.Color));

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
							SE_CORE_WARN("Wrong asset type!");
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

				ImGui::DragFloat("Tiling Factor", &component.TilingFactor, 0.1f, 0.0f, 100.0f);
			});

		DrawComponent<CircleRendererComponent>("Circle Renderer", entity, [](auto& component)
			{
				ImGui::ColorEdit4("Color", glm::value_ptr(component.Color));
				ImGui::DragFloat("Thickness", &component.Thickness, 0.025f, 0.0f, 1.0f);
				ImGui::DragFloat("Fade", &component.Fade, 0.00025f, 0.0f, 1.0f);
			});

		DrawComponent<RigidBody2DComponent>("RigidBody 2D", entity, [](auto& component)
		{
			const char* bodyTypeStrings[] = { "Static", "Dynamic", "Kinematic" };
			int bodyTypeIndex = static_cast<int>(component.BodyType);

			if (ImGui::BeginCombo("Body Type", bodyTypeStrings[bodyTypeIndex]))
			{
				for (int i = 0; i < 3; ++i) // 3 entries, not 2
				{
					bool isSelected = (bodyTypeIndex == i);
					if (ImGui::Selectable(bodyTypeStrings[i], isSelected))
					{
						bodyTypeIndex = i;
						component.BodyType = static_cast<RigidBody2DComponent::Type>(i);
					}
					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			ImGui::Checkbox("Fixed Rotation", &component.FixedRotation);
		});


		DrawComponent<BoxCollider2DComponent>("Box Collider 2D", entity, [](auto& component)
			{
				ImGui::DragFloat2("Offset", glm::value_ptr(component.Offset));
				ImGui::DragFloat2("Size", glm::value_ptr(component.Size));
				ImGui::DragFloat("Density", &component.Density, 0.01f, 0.0f, 1.0f);
				ImGui::DragFloat("Friction", &component.Friction, 0.01f, 0.0f, 1.0f);
			});

		DrawComponent<CircleCollider2DComponent>("Circle Collider 2D", entity, [](auto& component)
			{
				ImGui::DragFloat2("Offset", glm::value_ptr(component.Offset));
				ImGui::DragFloat("Radius", &component.Radius);
				ImGui::DragFloat("Density", &component.Density, 0.01f, 0.0f, 1.0f);
				ImGui::DragFloat("Friction", &component.Friction, 0.01f, 0.0f, 1.0f);
			});

		DrawComponent<TextComponent>("Text Renderer", entity, [](auto& component)
			{
				ImGui::InputTextMultiline("Text String", &component.TextString);
				ImGui::ColorEdit4("Color", glm::value_ptr(component.Color));
				ImGui::DragFloat("Kerning", &component.Kerning, 0.025f);
				ImGui::DragFloat("Line Spacing", &component.LineSpacing, 0.025f);
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
							SE_CORE_WARN("Wrong asset type!");
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

					float volumeMultiplier = config.VolumeMultiplier;
					if (ImGui::SliderFloat("Volume Multiplier", &config.VolumeMultiplier, 0.0f, 2.0f, "%.2f"))
					{
						config.VolumeMultiplier = volumeMultiplier;
					}


					float pitchMultiplier = config.PitchMultiplier;
					if (ImGui::SliderFloat("Pitch Multiplier", &config.PitchMultiplier, 0.0f, 3.0f, "%.2f"))
					{
						config.PitchMultiplier = pitchMultiplier;
					}

					bool playOnAwake = config.PlayOnAwake;
					if (ImGui::Checkbox("Play On Awake", &config.PlayOnAwake))
					{
						config.PlayOnAwake = playOnAwake;
					}

					bool spatialization = config.Spatialization;
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
						ImGui::Checkbox("Looping", &looping);
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
									SE_CORE_WARN("Wrong asset type!");
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

					ImGui::Checkbox("Active", &component.Active);

					float innerAngle = glm::degrees(config.ConeInnerAngle);
					if (ImGui::SliderFloat("Cone Inner Angle", &innerAngle, 0.0f, 360.0f, "%.2f"))
						config.ConeInnerAngle = glm::radians(innerAngle);

					float outerAngle = glm::degrees(config.ConeOuterAngle);
					if (ImGui::SliderFloat("Cone Outer Angle", &outerAngle, 0.0f, 360.0f, "%.2f"))
						config.ConeOuterAngle = glm::radians(outerAngle);

					ImGui::SliderFloat("Cone Outer Gain", &config.ConeOuterGain, 0.0f, 1.0f, "%.2f");
				});


	}

	template<typename T>
	void SceneHierarchyPanel::DisplayAddComponentEntry(const std::string& entryName) {
		if (!m_EntitySelectionContext.HasComponent<T>())
		{
			if (ImGui::MenuItem(entryName.c_str()))
			{
				m_EntitySelectionContext.AddComponent<T>();
				ImGui::CloseCurrentPopup();
			}
		}
	}

}
