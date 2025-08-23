#pragma once

#include "StarEngine/Core/Base.h"
#include "StarEngine/Scene/Scene.h"
#include "StarEngine/Scene/Entity.h"

#include "StarEngine/Editor/SelectionManager.h"


namespace StarEngine {

	class SceneHierarchyPanel
	{
	public:
		SceneHierarchyPanel() = default;
		SceneHierarchyPanel(const Ref<Scene>& scene);

		void SetContext(const Ref<Scene>& scene);

		void OnImGuiRender();

		Entity GetSelectedEntity() const { return m_EntitySelectionContext; }
		void SetSelectedEntity(Entity entity);
	public:
		template<typename TPrimitive, typename TComponent, typename GetOtherFunc>
		bool IsInconsistentPrimitive(GetOtherFunc func)
		{
			const auto& entities = SelectionManager::GetSelections(m_SelectionContext);

			if (entities.size() < 2)
				return false;

			Entity firstEntity = m_Context->GetEntityWithUUID(entities[0]);
			const TPrimitive& first = func(firstEntity.GetComponent<TComponent>());

			for (size_t i = 1; i < entities.size(); i++)
			{
				Entity entity = m_Context->GetEntityWithUUID(entities[i]);

				if (!entity.HasComponent<TComponent>())
					continue;

				const auto& otherValue = func(entity.GetComponent<TComponent>());
				if (otherValue != first)
					return true;
			}

			return false;
		}
	private:
		template<typename T>
		void DisplayAddComponentEntry(const std::string& entryName);

		void DrawEntityNode(Entity entity);
		void DrawComponents(Entity entity);
	private:
		Ref<Scene> m_Context;
		Entity m_EntitySelectionContext;
		SelectionContext m_SelectionContext;
		static SelectionContext s_ActiveSelectionContext;
	};

}
