#pragma once

#include <string>

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
		void PushCopy(T* /*target*/, const T& /*previousValue*/) { MarkSceneEdited("Edit"); }

		// Flag a scene-modifying edit. `label` names the action for the Undo/Redo menu ("Move",
		// "Delete Entity", …); pass a string literal (it is copied immediately).
		void MarkSceneEdited(const char* label = nullptr)
		{
			m_SceneEditPending = true;
			if (label)
				m_PendingLabel = label;
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

		bool m_SceneEditPending = false;
		std::string m_PendingLabel;
	};

}
