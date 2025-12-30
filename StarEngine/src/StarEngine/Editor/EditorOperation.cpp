#include "sepch.h"
#include "EditorOperation.h"

namespace StarEngine {
	/* TODO: This is not correct -- needs an owning scratch buffer to swap. */
	RawPropertyState::RawPropertyState(void* restoreIn, void* copyIn, size_t sizeIn) :
			restore(restoreIn), copy(copyIn), size(sizeIn) {}

	void RawPropertyState::Apply()
	{
		auto restore_p = static_cast<char*>(restore);
		auto copy_p = static_cast<char*>(copy);

		std::swap_ranges(restore_p, restore_p + size, copy_p);
	}

	EditorStack EditorStack::s_Instance;

	EditorStack& EditorStack::Get() { return s_Instance; }

	// TODO: Limit undo stack size.
	void EditorStack::Push(StatePtr&& state)
	{
		// Erase the future if you break the past.
		if (m_Head < m_Stack.size())
			m_Stack.resize(m_Head);

		m_Head++;
		m_Stack.emplace_back(std::move(state));

		// HZ_CORE_INFO("Pushed undo stack head to {}/{}", m_Head, m_Stack.size());
	}

	void EditorStack::Pop() {
		if (!m_Head) return;

		m_Stack[--m_Head]->Apply();

		// HZ_CORE_INFO("Popped undo stack head; now {}/{}", m_Head, m_Stack.size());
	}

	void EditorStack::Advance()
	{
		if (m_Head == m_Stack.size()) return;

		m_Stack[m_Head++]->Apply();

		// HZ_CORE_INFO("Applied undo stack head; now {}/{}", m_Head, m_Stack.size());
	}
}
