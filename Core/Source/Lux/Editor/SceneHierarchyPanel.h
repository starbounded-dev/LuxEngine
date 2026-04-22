#pragma once

#include "Lux/Editor/EditorPanel.h"
#include "Lux/Editor/SelectionManager.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Scene/Entity.h"

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
	private:
		void PruneInvalidSelection();
		void DrawEntityCreateMenu(Entity parent = {});
		void DrawEntityNode(Entity entity, const std::string& searchFilter = {});
		void DrawComponents(const std::vector<UUID>& entityIDs);
		bool TagSearchRecursive(Entity entity, std::string_view searchFilter, uint32_t maxSearchDepth, uint32_t currentDepth = 1);
	private:
		Ref<Scene> m_Context;
		std::string m_SearchString;
		SelectionContext m_SelectionContext = SelectionContext::Scene;
		bool m_IsWindow = true;
		bool m_IsHierarchyFocused = false;
		bool m_IsHierarchyOrPropertiesFocused = false;
		bool m_ActivateSearchWidget = false;

		static SelectionContext s_ActiveSelectionContext;
	};

}
