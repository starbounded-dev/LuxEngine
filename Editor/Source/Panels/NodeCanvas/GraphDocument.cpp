// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "GraphDocument.h"

namespace Lux
{
	namespace
	{
		constexpr size_t k_MaxUndoSteps = 128;
	}

	void GraphDocument::PushUndo(std::function<void()> undo, std::function<void()> redo)
	{
		m_History.erase(m_History.begin() + static_cast<ptrdiff_t>(m_UndoCursor), m_History.end());
		m_History.push_back({ std::move(undo), std::move(redo) });
		if (m_History.size() > k_MaxUndoSteps)
			m_History.erase(m_History.begin());
		m_UndoCursor = m_History.size();
	}

	void GraphDocument::Undo()
	{
		if (CanUndo())
			m_History[--m_UndoCursor].Undo();
	}

	void GraphDocument::Redo()
	{
		if (CanRedo())
			m_History[m_UndoCursor++].Redo();
	}
}
