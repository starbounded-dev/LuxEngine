// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "MaterialGraphCanvas.h"
#include "Panels/NodeCanvas/GraphDocument.h"

#include "Lux/Renderer/MaterialAsset.h"

namespace Lux
{
	class MaterialPreview;

	class MaterialGraphDocument final : public GraphDocument
	{
	public:
		// Every property the editor can change, so a whole edit can be captured, compared, undone,
		// reverted and checked for unsaved changes in one place.
		struct MaterialState
		{
			glm::vec3 AlbedoColor{ 1.0f };
			float Metalness = 0.0f;
			float Roughness = 0.5f;
			float Emission = 0.0f;
			float Transparency = 1.0f;
			bool CastShadows = true;
			bool UseNormalMap = false;
			AssetHandle AlbedoMap = 0;
			AssetHandle NormalMap = 0;
			AssetHandle MetalnessMap = 0;
			AssetHandle RoughnessMap = 0;
			MaterialSurfaceParameters Surface;
			int32_t AcousticTag = -1;
			AssetHandle Parent = 0;
			std::set<std::string, std::less<>> Overrides;
			std::map<std::string, MaterialGraphParameterValue, std::less<>> GraphParameters;

			bool operator==(const MaterialState& other) const = default;

			static MaterialState Capture(Ref<MaterialAsset> material);
			void Apply(Ref<MaterialAsset> material) const;
		};

		struct PreviewSession : public RefCounted
		{
			~PreviewSession() override;
			Ref<MaterialPreview> Preview;
		};

		static OpenFn MakeFactory();
		MaterialGraphDocument(const Ref<MaterialAsset>& material, const std::filesystem::path& path, Ref<PreviewSession> preview);
		~MaterialGraphDocument() override;
		AssetHandle Draw() override;
		void OnDeactivate() override;
		void SaveView(Yaml::Writer& out, bool navigation) const override;
		void LoadView(const Yaml::Node& state, bool navigation) override;
		bool IsDirty() const override;
		bool Save() override;
		void Discard() override;

		static std::string GraphSnapshot(const Ref<MaterialAsset>& material);
		static void ApplyGraphSnapshot(AssetHandle handle, const std::string& snapshot);

		MaterialGraphCanvas Canvas;
		MaterialState Saved;
		MaterialState EditBaseline;
		MaterialState Propagated;
		std::string SavedGraph;
		bool EditInProgress = false;

	private:
		friend class NodeCanvasPanel;
		void RevertDocument(const Ref<MaterialAsset>& material);
		void TrackEdit(const Ref<MaterialAsset>& material);
		void UI_Toolbar(const Ref<MaterialAsset>& material);
		void UI_Preview(float width);
		void UI_Properties(Ref<MaterialAsset> material);
		void UI_Instance(Ref<MaterialAsset> material);
		void UI_GraphParameters(Ref<MaterialAsset> material);

		Ref<PreviewSession> m_PreviewSession;
		bool m_ShowGraph = false;
		bool m_ShowPreview = true;
		bool m_NarrowPreview = false;
		bool m_Compact = false;
		float m_PreviewFraction = 0.3f;
		AssetHandle m_OpenAfterFrame = 0;
	};
}
