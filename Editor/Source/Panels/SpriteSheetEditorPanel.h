// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Asset/Asset.h"
#include "Lux/Editor/EditorPanel.h"

#include <string>

namespace Lux
{
	// Edits a sprite sheet (.lsheet): its texture, the grid that cuts it into frames, each frame's name,
	// pivot and Solid flag (a tile set's collision), and the animation clips that play the frames, with
	// a preview. The same sheet serves sprites, the sprite animator and tilemaps. Every edit is saved at
	// once. Main thread only.
	class SpriteSheetEditorPanel : public EditorPanel
	{
	public:
		void OnImGuiRender(bool& isOpen) override;
		void OnProjectChanged(const Ref<Project>& project) override;
		void OnClose() override { m_RequestFocus = false; }
		void OpenSheet(AssetHandle sheet);

	private:
		AssetHandle m_Sheet = 0;
		bool m_RequestFocus = false;
		int32_t m_SelectedFrame = -1;
		int32_t m_SelectedClip = -1;
		float m_Zoom = 2.0f;
		std::string m_Error;
		std::string m_ClipFramesText; // the selected clip's frames as typed, "0, 1, 2"
		int32_t m_ClipFramesTextClip = -2;
	};
}
