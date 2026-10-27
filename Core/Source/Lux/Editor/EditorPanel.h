// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Ref.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Project/Project.h"
#include "Lux/Core/Events/Event.h"

namespace Lux::Yaml { class Writer; class Node; }

namespace Lux {

	class EditorPanel : public RefCounted
	{
	public:
		virtual ~EditorPanel() = default;

		virtual void OnImGuiRender(bool& isOpen) = 0;
		virtual void OnEvent(Event& e) {}
		virtual void OnProjectChanged(const Ref<Project>& project){}
		virtual void SetSceneContext(const Ref<Scene>& context){}
		virtual void OnClose() {}

		// What the panel remembers between sessions and in saved layouts, written as keys of a map
		// beside its open state (PanelManager). Store choices the user made (open sections, the
		// current page); a key missing on load keeps the panel's default.
		virtual void SaveState(Yaml::Writer& out) const {}
		virtual void LoadState(const Yaml::Node& state) {}
	};

}
