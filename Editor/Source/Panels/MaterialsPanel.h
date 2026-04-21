#pragma once

#include "Lux/Editor/EditorPanel.h"
#include "Lux/Renderer/Texture.h"

#include <functional>

namespace Lux {

	class MaterialAsset;
	class SceneHierarchyPanel;

	class MaterialsPanel : public EditorPanel
	{
	public:
		MaterialsPanel(const Ref<SceneHierarchyPanel>& sceneHierarchyPanel, std::function<void(AssetHandle)> openMaterialCallback);
		virtual ~MaterialsPanel() = default;

		virtual void SetSceneContext(const Ref<Scene>& context) override;
		virtual void OnImGuiRender(bool& isOpen) override;

	private:
		void RenderMaterialSlot(uint32_t materialIndex, AssetHandle materialHandle, bool isOverride);
		bool RenderTextureSlot(const char* label, AssetHandle& handle, const Ref<Texture2D>& texture, AssetType expectedType);
		void SaveMaterial(AssetHandle handle, const Ref<MaterialAsset>& materialAsset) const;

	private:
		Ref<Scene> m_Context;
		Ref<SceneHierarchyPanel> m_SceneHierarchyPanel;
		std::function<void(AssetHandle)> m_OpenMaterialCallback;
		Ref<Texture2D> m_CheckerboardTexture;
	};

}
