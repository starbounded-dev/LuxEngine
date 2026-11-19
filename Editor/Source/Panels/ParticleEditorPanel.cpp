// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ParticleEditorPanel.h"

#include "Lux/Editor/ParticleWidgets.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/Particles/ParticleSystem.h"
#include "Lux/Scene/Components.h"
#include "Lux/Scene/Scene.h"

#include <imgui.h>

#include <format>

namespace Lux
{
	ParticleSystemComponent* ParticleEditorPanel::GetSystem() const
	{
		Entity target = m_Target;
		return m_Scene && target && target.HasComponent<ParticleSystemComponent>() ? &target.GetComponent<ParticleSystemComponent>() : nullptr;
	}

	UUID ParticleEditorPanel::GetPreviewEntity() const
	{
		return m_PanelOpen && m_Preview && GetSystem() ? m_Target.GetUUID() : UUID(0);
	}

	void ParticleEditorPanel::OnImGuiRender(bool& isOpen)
	{
		m_PanelOpen = isOpen;
		if (!isOpen)
			return;

		ImGui::SetNextWindowSize(ImVec2(420.0f, 560.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Particle Editor", &isOpen))
		{
			m_PanelOpen = false;
			ImGui::End();
			return;
		}

		ParticleSystemComponent* system = GetSystem();
		if (!system)
		{
			ImGui::TextDisabled("Select an entity with a Particle System to play and shape it.");
			ImGui::End();
			return;
		}

		const std::string name = m_Target.GetComponent<TagComponent>().Tag;
		ImGui::TextUnformatted(name.c_str());
		ImGui::Checkbox("Preview##particle_preview", &m_Preview);
		ImGui::SetItemTooltip("Play this system in the editor while this panel is open.");

		Particles::State* state = m_Scene->GetParticleState(m_Target);
		if (!m_Preview || !state)
		{
			ImGui::TextDisabled(m_Preview ? "Starting the preview..." : "The preview is off.");
		}
		else
		{
			const glm::mat4 world = m_Scene->GetWorldSpaceTransformMatrix(m_Target);
			if (ImGui::Button(state->Playing ? "Pause##particle_play" : "Play##particle_play"))
			{
				if (state->Playing)
					state->Playing = false;
				else
					Particles::Play(*state, system->Settings);
			}
			ImGui::SameLine();
			if (ImGui::Button("Restart##particle_restart"))
			{
				Particles::Clear(*state);
				state->Emitting = true;
				Particles::Play(*state, system->Settings);
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(state->Playing);
			if (ImGui::Button("Step##particle_step"))
			{
				state->Playing = true;
				Particles::Step(*state, system->Settings, world, Particles::FixedStep);
				state->Playing = false;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Clear##particle_clear"))
				Particles::Clear(*state);

			const Particles::Settings& settings = system->Settings;
			ImGui::Text("Particles %u / %u", state->Count(), std::min(settings.MaxParticles, Particles::Settings::HardMaxParticles));
			ImGui::SameLine();
			ImGui::Text("   Time %.2f / %.2f s%s", state->Time, settings.Duration, settings.Looping ? " (loops)" : "");
			if (state->Dropped > 0)
			{
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "   %u dropped: raise Max Particles", state->Dropped);
			}
		}

		ImGui::Separator();
		ImGui::SeparatorText("Over Life");
		Particles::Settings& settings = system->Settings;
		ImGuiEx::BeginPropertyGrid();
		ParticleWidgets::PropertyCurve("Size", settings.SizeOverLife, 4.0f, "A multiplier on the start size from birth (left) to death (right). Double-click to add a key, right-click a key to remove it.");
		ParticleWidgets::PropertyCurve("Speed", settings.SpeedOverLife, 4.0f, "A multiplier on the particle's speed over its life.");
		ParticleWidgets::PropertyGradient("Color", settings.ColorOverLife, "Multiplied by the start color from birth (left) to death (right).");
		ImGuiEx::Property("Preview In Editor", system->PreviewInEditor, "Also plays this system in the editor when it is not selected.");
		ImGuiEx::EndPropertyGrid();

		ImGui::End();
	}
}
