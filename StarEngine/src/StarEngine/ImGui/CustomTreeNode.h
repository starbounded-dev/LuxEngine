#pragma once

#include "StarEngine/Core/Ref.h"

#include <imgui.h>

namespace StarEngine {
	class Texture2D;
}

namespace ImGui {
	bool TreeNodeWithIcon(StarEngine::Ref<StarEngine::Texture2D> icon, ImGuiID id, ImGuiTreeNodeFlags flags, const char* label, const char* label_end, ImColor iconTint = IM_COL32_WHITE);
	bool TreeNodeWithIcon(StarEngine::Ref<StarEngine::Texture2D> icon, const void* ptr_id, ImGuiTreeNodeFlags flags, ImColor iconTint, const char* fmt, ...);
	bool TreeNodeWithIcon(StarEngine::Ref<StarEngine::Texture2D> icon, const char* label, ImGuiTreeNodeFlags flags, ImColor iconTint = IM_COL32_WHITE);

} // namespace UI
