// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/UUID.h"

#include <string>
#include <unordered_set>
#include <vector>

namespace Lux {

	// Editor undo/redo signal.
	//
	// This is deliberately NOT a value-restore stack that stores raw pointers. The property widgets
	// in ImGuiEx were originally written to call `EditorStack::Get().PushCopy(&value, old)` on every
	// change, but `&value` is very often a stack local (the editor's common copy-edit-writeback idiom:
	// `float n = cam.GetNear(); if (Property("Near", n)) cam.SetNear(n);`). Storing that pointer and
	// dereferencing it on a later Ctrl+Z corrupts the stack. See docs/Editor/Undo-Redo.md.
	//
	// Instead, PushCopy simply *flags that a scene edit happened* — the pointer and value are ignored.
	// EditorLayer polls the flag and, once the active edit finishes, captures a whole-scene snapshot
	// (via SceneSerializer) as the actual undo unit. That is safe (values, not pointers) and covers
	// every field the property widgets touch without hooking each call site.
	//
	// Non-widget edits (gizmo drag, entity delete/duplicate, add/remove component, rename) call
	// MarkSceneEdited() directly at their site.
	//
	// Edit scope: re-serializing every entity per edit froze large scenes for seconds, so each
	// signal also records which entities it may have touched. MarkSceneEdited() is scene-wide
	// (the safe default); MarkEntitiesEdited() names the entities; PushCopy() uses the active
	// ScopedEditTarget (the Inspector's selection) and is scene-wide without one, because a widget
	// in another panel may edit entities it does not name. The commit then snapshots only the
	// named entities, unless any signal since the last commit was scene-wide.
	//
	// Threading: main-thread only. The ImGui widgets and EditorLayer's poll both run on the main
	// thread, so the flag is unsynchronised by design.
	class EditorStack
	{
	public:
		static EditorStack& Get()
		{
			static EditorStack s_Instance;
			return s_Instance;
		}

		// Called by the ImGuiEx property widgets on every change frame (gated by the `UndoDo` macro).
		// The arguments are intentionally ignored — see the class note.
		template<typename T>
		void PushCopy(T* /*target*/, const T& /*previousValue*/) { MarkEditTargetEdited("Edit"); }

		// Flag a scene-modifying edit that may touch any entity or the scene metadata. `label` names
		// the action for the Undo/Redo menu ("Move", "Delete Entity", …); pass a string literal (it
		// is copied immediately).
		void MarkSceneEdited(const char* label = nullptr)
		{
			MarkPending(label);
			m_FullSnapshotRequired = true;
		}

		// Flag an edit confined to these entities' own serialized data.
		void MarkEntitiesEdited(const std::vector<UUID>& entityIDs, const char* label = nullptr)
		{
			MarkPending(label);
			m_EditedEntities.insert(entityIDs.begin(), entityIDs.end());
		}

		// Flag an edit made through a property widget: confined to the active edit target when one
		// is set, otherwise scene-wide.
		void MarkEditTargetEdited(const char* label = nullptr)
		{
			if (m_EditTarget)
				MarkEntitiesEdited(*m_EditTarget, label);
			else
				MarkSceneEdited(label);
		}

		// While alive, property-widget edits are attributed to `entityIDs` (which must outlive it).
		// Only set this where every widget drawn edits nothing but those entities.
		class ScopedEditTarget
		{
		public:
			explicit ScopedEditTarget(const std::vector<UUID>& entityIDs)
				: m_Previous(Get().m_EditTarget)
			{
				Get().m_EditTarget = &entityIDs;
			}
			~ScopedEditTarget() { Get().m_EditTarget = m_Previous; }

			ScopedEditTarget(const ScopedEditTarget&) = delete;
			ScopedEditTarget& operator=(const ScopedEditTarget&) = delete;

		private:
			const std::vector<UUID>* m_Previous;
		};

		// Scope of the edits signalled since the last ClearEditScope(); read by the undo commit.
		bool IsFullSnapshotRequired() const { return m_FullSnapshotRequired || m_EditedEntities.empty(); }
		const std::unordered_set<UUID>& GetEditedEntities() const { return m_EditedEntities; }
		void ClearEditScope()
		{
			m_FullSnapshotRequired = false;
			m_EditedEntities.clear();
		}

		// EditorLayer reads this each frame. Peek keeps the flag; Consume clears it.
		bool HasPendingSceneEdit() const { return m_SceneEditPending; }
		bool ConsumeSceneEdit()
		{
			bool pending = m_SceneEditPending;
			m_SceneEditPending = false;
			return pending;
		}

		// The label most recently attached to a pending edit; cleared on read. Defaults to "Edit".
		std::string ConsumeLabel()
		{
			std::string label = m_PendingLabel.empty() ? std::string("Edit") : m_PendingLabel;
			m_PendingLabel.clear();
			return label;
		}

	private:
		EditorStack() = default;

		void MarkPending(const char* label)
		{
			m_SceneEditPending = true;
			if (label)
				m_PendingLabel = label;
		}

		bool m_SceneEditPending = false;
		std::string m_PendingLabel;
		bool m_FullSnapshotRequired = false;
		std::unordered_set<UUID> m_EditedEntities;
		const std::vector<UUID>* m_EditTarget = nullptr;
	};

}
