#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Editor/EditorPanel.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Scene/Entity.h"

namespace Lux {

	class SceneHierarchyPanel : public EditorPanel
	{
	public:
		SceneHierarchyPanel() = default;
		SceneHierarchyPanel(const Ref<Scene>& scene);

		void SetContext(const Ref<Scene>& scene);
		virtual void SetSceneContext(const Ref<Scene>& context) override { SetContext(context); }

		virtual void OnImGuiRender(bool& isOpen) override;

		Entity GetSelectedEntity() const { return m_SelectionContext; }
		void SetSelectedEntity(Entity entity);
	private:
		template<typename T>
		void DisplayAddComponentEntry(const std::string& entryName);

		void DrawEntityNode(Entity entity);
		void DrawComponents(Entity entity);
	private:
		Ref<Scene> m_Context;
		Entity m_SelectionContext;
	};

}
