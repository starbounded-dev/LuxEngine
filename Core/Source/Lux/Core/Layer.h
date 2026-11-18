// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Events/Event.h"
#include "Lux/Core/Timestep.h"

#include <string>

namespace Lux {

	class Layer
	{
	public:
		Layer(const std::string& name = "Layer");
		virtual ~Layer();

		virtual void OnAttach() {}
		virtual void OnDetach() {}
		// The main loop has ended and the application is about to shut down. Called once, with the script host, the audio
		// engine and the renderer still alive, which OnDetach no longer has (the application tears the script host down first).
		virtual void OnApplicationQuit() {}
		virtual void OnUpdate(Timestep ts) {}
		virtual void OnImGuiRender() {}
		virtual void OnEvent(Event& event) {}

		inline const std::string& GetName() const { return m_DebugName; }
	protected:
		std::string m_DebugName;
	};

}
