// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Editor/EditorPanel.h"
#include "Lux/Scene/Entity.h"

#include <string>
#include <vector>

namespace Lux
{
	// Plays and edits the selected entity's particle system (docs/PARTICLES_PLAN.md, E10): play, pause, restart and step it in the
	// editor without entering Play, watch how many particles are alive, and shape its size, speed and color over life with
	// curves and a gradient. The inspector holds the rest of the settings. Main thread only.
	class ParticleEditorPanel : public EditorPanel
	{
	public:
		void OnImGuiRender(bool& isOpen) override;
		void SetSceneContext(const Ref<Scene>& context) override { m_Scene = context; m_Target = {}; }
		void OnProjectChanged(const Ref<Project>&) override { m_Target = {}; }

		// The entity to show (or none): the selection in the Edit state. Called every frame.
		void SetTarget(Entity entity) { m_Target = entity; }

		// The system the scene should play as a preview: the target's, while the panel is open and Preview is on; 0 for none.
		UUID GetPreviewEntity() const;

		// Drives the curve and gradient widgets with mouse events and renders the panel in each of its states, in a private ImGui
		// context. Fills `failures`; true when there are none. Run by LUX_PARTICLE_SELFTEST.
		static bool RunSelfTests(std::vector<std::string>& failures);

	private:
		ParticleSystemComponent* GetSystem() const;

		Ref<Scene> m_Scene;
		Entity m_Target;
		bool m_PanelOpen = false;
		bool m_Preview = true;
	};
}
