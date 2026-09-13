#include "lpch.h"
#include "AudioWidgets.h"
#include "ImGuiUtilities.h"
#include "Lux/Audio/AudioEngine.h"
#include <imgui.h>
#include <algorithm>

namespace Lux::ImGuiEx
{
	bool SurfaceEventPicker(const char* label, AudioEventRef& reference, bool oneShot, bool mixed)
	{
		ScopedID id(label);
		bool changed = false;
		const auto& events = AudioEngine::GetEvents();
		const auto assigned = std::find_if(events.begin(), events.end(), [&](const auto& info) { return info.Guid == reference.Guid; });
		const char* preview = mixed ? "Multiple values" : !reference.IsValid() ? "None" : assigned != events.end() ? assigned->Path.c_str()
			: reference.Path.empty() ? reference.Guid.c_str() : reference.Path.c_str();
		if (ImGui::BeginCombo(label, preview))
		{
			if (ImGui::Selectable("None", !reference.IsValid()))
			{
				reference = {};
				changed = true;
			}
			for (const auto& info : events)
			{
				if (info.IsSnapshot || info.IsOneshot != oneShot)
					continue;
				ScopedID eventID(info.Guid.c_str());
				ScopedID bankID(info.BankName.c_str());
				if (ImGui::Selectable(info.Path.empty() ? info.Guid.c_str() : info.Path.c_str(), info.Guid == reference.Guid))
				{
					reference = { info.Guid, info.Path, info.BankName };
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
		if (!changed && !mixed && reference.IsValid() && (assigned == events.end() || assigned->IsSnapshot || assigned->IsOneshot != oneShot))
			ImGui::TextWrapped("Assigned surface event is unavailable or has the wrong type. Build/load its bank.");
		return changed;
	}
}
