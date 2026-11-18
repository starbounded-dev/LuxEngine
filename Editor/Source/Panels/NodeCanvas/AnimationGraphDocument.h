// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "GraphDocument.h"
#include "NodeCanvasCore.h"

#include "Lux/Animation/AnimationGraph.h"

#include <optional>

namespace Lux
{
	struct SkeletonComponent;

	class AnimationGraphDocument final : public GraphDocument
	{
	public:
		static OpenFn MakeFactory();
		AnimationGraphDocument(const Ref<AnimationGraph>& graph, const std::filesystem::path& path);

		AssetHandle Draw() override;
		bool IsDirty() const override { return m_Current != m_Saved; }
		bool Save() override;
		void Discard() override;
		void OnDeactivate() override;
		void SetSceneContext(const Ref<Scene>& scene) override;
		void SaveView(Yaml::Writer& out, bool navigation) const override;
		void LoadView(const Yaml::Node& state, bool navigation) override;

	private:
		friend class NodeCanvasPanel;
		void DrawCanvas(uint32_t activeState);
		void DrawDetails();
		void DrawState();
		void DrawTransition();
		void DrawParameters();
		void DrawPreview(SkeletonComponent& component);
		SkeletonComponent* FindPreview(Scene& scene) const;

		void AddState(const AnimationGraphState* source = nullptr);
		void RemoveState(uint32_t index);
		void RemoveTransition(uint32_t index);
		void RefreshDraft(bool finish);
		bool ValidateDraft();
		void RestoreDraft(const std::string& snapshot);
		void MarkEdited() { m_EditPending = true; }

		Ref<AnimationGraph> m_Draft;
		WeakRef<Scene> m_Scene;
		NodeCanvasCore m_Canvas;
		std::vector<NodeCanvasCore::MinimapRect> m_Minimap;
		std::vector<NodeCanvasCore::SearchItem> m_SearchChoices;
		std::string m_Saved;
		std::string m_Current;
		std::string m_Baseline;
		std::string m_Diagnostic;
		std::optional<AnimationGraphState> m_Clipboard;
		uint32_t m_SelectedState = AnimationGraph::StateNone;
		uint32_t m_SelectedTransition = AnimationGraph::StateNone;
		uint32_t m_InvalidTransition = AnimationGraph::StateNone;
		uint32_t m_PlacedStates = 0;
		uint32_t m_PendingSelection = AnimationGraph::StateNone;
		bool m_EditPending = false;
		bool m_AnyPlaced = false;
		bool m_NarrowDetails = false;
		bool m_FocusTransitionInspector = false;
		bool m_SyncLinkSelection = false;
		bool m_ShowMinimap = true;
	};
}
