#include "lpch.h"
#include "SelectionManager.h"

#include "Lux/Core/Application.h"
#include "Lux/Core/Events/SceneEvents.h"
#include "Lux/Scene/Entity.h"

#include <algorithm>

namespace Lux {

	void SelectionManager::Select(SelectionContext context, UUID selectionID)
	{
		auto& selections = s_Contexts[context];
		if (std::find(selections.begin(), selections.end(), selectionID) != selections.end())
			return;

		selections.push_back(selectionID);
		Application::Get().DispatchEvent<SelectionChangedEvent>(context, selectionID, true);
	}

	bool SelectionManager::IsSelected(UUID selectionID)
	{
		for (const auto& [context, selections] : s_Contexts)
		{
			if (std::find(selections.begin(), selections.end(), selectionID) != selections.end())
				return true;
		}

		return false;
	}

	bool SelectionManager::IsSelected(SelectionContext context, UUID selectionID)
	{
		const auto& selections = s_Contexts[context];
		return std::find(selections.begin(), selections.end(), selectionID) != selections.end();
	}

	bool SelectionManager::IsEntityOrAncestorSelected(const Entity entity)
	{
		Entity current = entity;
		while (current)
		{
			if (IsSelected(current.GetUUID()))
				return true;

			current = current.GetParent();
		}

		return false;
	}

	bool SelectionManager::IsEntityOrAncestorSelected(SelectionContext context, const Entity entity)
	{
		Entity current = entity;
		while (current)
		{
			if (IsSelected(context, current.GetUUID()))
				return true;

			current = current.GetParent();
		}

		return false;
	}

	void SelectionManager::Deselect(UUID selectionID)
	{
		for (auto& [context, selections] : s_Contexts)
		{
			const auto it = std::find(selections.begin(), selections.end(), selectionID);
			if (it == selections.end())
				continue;

			Application::Get().DispatchEvent<SelectionChangedEvent>(context, selectionID, false);
			selections.erase(it);
			return;
		}
	}

	void SelectionManager::Deselect(SelectionContext context, UUID selectionID)
	{
		auto& selections = s_Contexts[context];
		const auto it = std::find(selections.begin(), selections.end(), selectionID);
		if (it == selections.end())
			return;

		Application::Get().DispatchEvent<SelectionChangedEvent>(context, selectionID, false);
		selections.erase(it);
	}

	void SelectionManager::DeselectAll()
	{
		for (auto& [context, selections] : s_Contexts)
		{
			for (UUID selectionID : selections)
				Application::Get().DispatchEvent<SelectionChangedEvent>(context, selectionID, false);

			selections.clear();
		}
	}

	void SelectionManager::DeselectAll(SelectionContext context)
	{
		auto& selections = s_Contexts[context];
		for (UUID selectionID : selections)
			Application::Get().DispatchEvent<SelectionChangedEvent>(context, selectionID, false);

		selections.clear();
	}

	UUID SelectionManager::GetSelection(SelectionContext context, size_t index)
	{
		const auto& selections = s_Contexts[context];
		LUX_CORE_ASSERT(index < selections.size(), "Selection index out of range!");
		return selections[index];
	}

	size_t SelectionManager::GetSelectionCount(SelectionContext context)
	{
		return s_Contexts[context].size();
	}

}
