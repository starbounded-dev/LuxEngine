#pragma once

#include "Lux/Editor/EditorPanel.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Scene/Entity.h"

#include <string>
#include <string_view>

namespace Lux {

	class SceneHierarchyPanel : public EditorPanel
	{
	public:
		SceneHierarchyPanel() = default;
		SceneHierarchyPanel(const Ref<Scene>& scene, bool isWindow = true);

		void SetContext(const Ref<Scene>& scene);
		virtual void SetSceneContext(const Ref<Scene>& context) override { SetContext(context); }
		Ref<Scene> GetSceneContext() const { return m_Context; }

		virtual void OnImGuiRender(bool& isOpen) override;
		virtual void OnEvent(Event& e) override;

		Entity GetSelectedEntity() const { return m_SelectionContext; }
		void SetSelectedEntity(Entity entity);
	private:
		template<typename T>
		void DisplayAddComponentEntry(const std::string& entryName);

		void DrawEntityCreateMenu(Entity parent = {});
		void DrawEntityNode(Entity entity, const std::string& searchFilter = {});
		void DrawComponents(Entity entity);
		bool TagSearchRecursive(Entity entity, std::string_view searchFilter, uint32_t maxSearchDepth, uint32_t currentDepth = 1);
	private:
		Ref<Scene> m_Context;
		Entity m_SelectionContext;
		std::string m_SearchString;
		bool m_IsWindow = true;
		bool m_IsHierarchyFocused = false;
		bool m_IsHierarchyOrPropertiesFocused = false;
		bool m_ActivateSearchWidget = false;
	};

}
