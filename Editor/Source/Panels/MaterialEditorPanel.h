#pragma once

#include "Lux/Editor/EditorPanel.h"
#include "Lux/Renderer/MaterialAsset.h"

#include <filesystem>
#include <optional>

namespace Lux {

	class MaterialEditorPanel : public EditorPanel
	{
	public:
		MaterialEditorPanel();
		virtual ~MaterialEditorPanel() = default;

		void OpenMaterial(AssetHandle handle);
		void OpenMaterial(const std::filesystem::path& path);
		void CloseMaterial();

		virtual void OnImGuiRender(bool& isOpen) override;

	private:
		void RenderMaterialProperties(Ref<MaterialAsset> material);
		void RenderTextureSlot(const char* label, AssetHandle& handle, Ref<Texture2D>& texture, const char* buttonLabel, bool& hasSlot);

		std::optional<AssetHandle> m_CurrentMaterial;
		std::filesystem::path m_CurrentPath;
		bool m_IsDirty = false;
	};

}
