// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Editor/EditorPanel.h"
#include "Lux/Editor/SelectionManager.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Scene/Entity.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Lux {

	class SceneHierarchyPanel : public EditorPanel
	{
	public:
		SceneHierarchyPanel() = default;
		SceneHierarchyPanel(const Ref<Scene>& scene, SelectionContext selectionContext = SelectionContext::Scene, bool isWindow = true);

		void SetContext(const Ref<Scene>& scene);
		virtual void SetSceneContext(const Ref<Scene>& context) override { SetContext(context); }
		Ref<Scene> GetSceneContext() const { return m_Context; }

		virtual void OnImGuiRender(bool& isOpen) override;
		virtual void OnEvent(Event& e) override;

		Entity GetSelectedEntity() const;
		std::vector<Entity> GetSelectedEntities() const;
		void SetSelectedEntity(Entity entity);
		static SelectionContext GetActiveSelectionContext() { return s_ActiveSelectionContext; }

		// Invoked by the material slots' Edit buttons; the editor opens the Material Editor.
		void SetOpenMaterialCallback(std::function<void(AssetHandle)> callback) { m_OpenMaterialCallback = std::move(callback); }
	private:
		void PruneInvalidSelection();
		void QueueEntityDeletion(const std::vector<UUID>& entityIDs);
		void FlushQueuedEntityDeletions();
		void DrawEntityCreateMenu(Entity parent = {});
		void DrawEntityNode(Entity entity, const std::string& searchFilter = {});
		void DrawComponents(const std::vector<UUID>& entityIDs);
		void DrawStaticMeshMaterialSlots(struct StaticMeshComponent& component, const std::vector<UUID>& selectedEntities);

		// The FMOD Studio event assignment for an Audio Source. Lists what the loaded banks
		// describe, so it is empty until the project's banks are built.
		void DrawAudioEventPicker(struct AudioSourceComponent& component, const std::vector<UUID>& selectedEntities);

		// Per-entity variation of a shared event: name/value pairs applied when its instance is
		// created. First-selected entity only, since the list is variable-length.
		void DrawAudioParameterOverrides(struct AudioSourceComponent& component);

		// Editor-only picker state: which bank the event list is filtered to, and the search inside
		// the event dropdown. Follows the assigned event when there is one.
		std::string m_AudioEventBankFilter;
		std::string m_AudioEventSearch;
		UUID m_AudioEventPickerEntity = 0;
		std::string m_AudioEventPickerGuid;
		uint64_t m_AudioEventPickerGeneration = 0;
		bool TagSearchRecursive(Entity entity, std::string_view searchFilter, uint32_t maxSearchDepth, uint32_t currentDepth = 1);
	private:
		Ref<Scene> m_Context;
		std::string m_SearchString;
		SelectionContext m_SelectionContext = SelectionContext::Scene;
		bool m_IsWindow = true;
		bool m_IsHierarchyFocused = false;
		bool m_IsHierarchyOrPropertiesFocused = false;
		bool m_ActivateSearchWidget = false;
		std::vector<UUID> m_QueuedEntityDeletions;
		std::function<void(AssetHandle)> m_OpenMaterialCallback;

		static SelectionContext s_ActiveSelectionContext;
	};

}
