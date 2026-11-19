// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ParticleEditorPanel.h"

#include "Lux/Editor/ParticleWidgets.h"
#include "Lux/Particles/ParticleSystem.h"
#include "Lux/Scene/Components.h"
#include "Lux/Scene/Scene.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>

namespace Lux
{
	// Runs inside the editor (LUX_PARTICLE_SELFTEST, tests/editor/run_particles.py) with a private ImGui context: the widgets are driven with
	// real mouse events (add, drag, remove a key) and the panel is rendered in each of its states, so a mismatched Begin/End or a repeated ID
	// stops here rather than in someone's editor.
	bool ParticleEditorPanel::RunSelfTests(std::vector<std::string>& failures)
	{
		ImGuiContext* previous = ImGui::GetCurrentContext();
		ImGuiContext* context = ImGui::CreateContext(previous->IO.Fonts);
		ImGui::SetCurrentContext(context);
		const auto check = [&](bool ok, const std::string& what)
		{
			if (!ok)
				failures.push_back(what);
		};
		try
		{
			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard;
			io.BackendFlags = previous->IO.BackendFlags & ImGuiBackendFlags_RendererHasTextures;
			io.DisplaySize = { 1280.0f, 800.0f };

			// A window to draw in; the body runs inside it.
			const auto frame = [&](const std::function<void()>& body)
			{
				ImGui::NewFrame();
				ImGui::SetNextWindowPos({ 20.0f, 20.0f });
				ImGui::SetNextWindowSize({ 560.0f, 420.0f });
				ImGui::Begin("Test", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
				body();
				ImGui::End();
				ImGui::Render();
			};
			const auto close = [](float a, float b, float tolerance) { return std::abs(a - b) <= tolerance; };

			// -- the curve ----------------------------------------------------------------------
			{
				Particles::Curve curve = Particles::Curve::Linear(0.0f, 1.0f);
				ImVec2 min{}, max{};
				bool changed = false;
				const auto curveFrame = [&]()
				{
					frame([&]
					{
						changed = ParticleWidgets::Curve("curve", curve, 2.0f, { 400.0f, 120.0f });
						min = ImGui::GetItemRectMin();
						max = ImGui::GetItemRectMax();
					});
				};
				// A point on the curve, kept one pixel inside the box (its edge is not part of the item).
				const auto at = [&](float t, float value) { return ImVec2(std::clamp(min.x + t * (max.x - min.x), min.x + 1.0f, max.x - 1.0f), std::clamp(max.y - value / 2.0f * (max.y - min.y), min.y + 1.0f, max.y - 1.0f)); };
				const auto move = [&](ImVec2 p) { io.AddMousePosEvent(p.x, p.y); curveFrame(); };
				const auto press = [&](int button) { io.AddMouseButtonEvent(button, true); curveFrame(); };
				const auto release = [&](int button) { io.AddMouseButtonEvent(button, false); curveFrame(); };
				curveFrame();
				curveFrame();
				check(max.x - min.x > 300.0f && curve.Count == 2, "the curve widget must take its size and leave the curve alone");

				// Double-click an empty place: a key is added there.
				move(at(0.5f, 1.5f));
				press(0); release(0); press(0);
				const bool addedChanged = changed;
				release(0);
				check(curve.Count == 3 && close(curve.Keys[1].T, 0.5f, 0.02f) && close(curve.Keys[1].Value, 1.5f, 0.05f), std::format("a double click must add a key where it lands (count {}, T {:.2f}, value {:.2f})", curve.Count, curve.Keys[1].T, curve.Keys[1].Value));
				check(addedChanged, "adding a key must report a change");

				// Drag it.
				move(at(0.5f, 1.5f));
				press(0);
				move(at(0.7f, 0.5f));
				move(at(0.7f, 0.5f));
				release(0);
				check(curve.Count == 3 && close(curve.Keys[1].T, 0.7f, 0.02f) && close(curve.Keys[1].Value, 0.5f, 0.05f), std::format("dragging a key must move it (T {:.2f}, value {:.2f})", curve.Keys[1].T, curve.Keys[1].Value));

				// The first key moves up and down only.
				move(at(0.0f, 0.0f));
				press(0);
				move(at(0.4f, 1.0f));
				move(at(0.4f, 1.0f));
				release(0);
				check(curve.Keys[0].T == 0.0f && close(curve.Keys[0].Value, 1.0f, 0.05f), std::format("the first key must keep its time (T {:.2f}, value {:.2f})", curve.Keys[0].T, curve.Keys[0].Value));

				// A key is dragged between its neighbours, never past them.
				move(at(0.7f, 0.5f));
				press(0);
				move(at(1.0f, 0.5f));
				move(at(1.0f, 0.5f));
				release(0);
				check(curve.Keys[1].T < curve.Keys[2].T && curve.Keys[1].T > curve.Keys[0].T, "a key must stay between its neighbours");

				// Right-click removes it; the last key stays.
				const ImVec2 keyPosition = at(curve.Keys[1].T, curve.Keys[1].Value);
				move(keyPosition);
				press(1);
				release(1);
				check(curve.Count == 2, std::format("a right click must remove a key (count {})", curve.Count));
				for (int i = 0; i < 4; i++)
				{
					move(at(curve.Keys[0].T, curve.Keys[0].Value));
					press(1);
					release(1);
				}
				check(curve.Count == 1, std::format("the last key must stay (count {})", curve.Count));
			}

			// -- the gradient --------------------------------------------------------------------
			{
				Particles::Gradient gradient = Particles::Gradient::Fade(glm::vec4(1.0f, 0.5f, 0.0f, 1.0f));
				ImVec2 origin{};
				const float width = 400.0f;
				bool changed = false;
				const auto gradientFrame = [&]()
				{
					frame([&]
					{
						origin = ImGui::GetCursorScreenPos();
						changed = ParticleWidgets::Gradient("gradient", gradient, width);
					});
				};
				const auto at = [&](float t) { return ImVec2(std::clamp(origin.x + t * width, origin.x + 1.0f, origin.x + width - 1.0f), origin.y + ImGui::GetFrameHeight() * 0.5f); };
				const auto move = [&](ImVec2 p) { io.AddMousePosEvent(p.x, p.y); gradientFrame(); };
				const auto press = [&](int button) { io.AddMouseButtonEvent(button, true); gradientFrame(); };
				const auto release = [&](int button) { io.AddMouseButtonEvent(button, false); gradientFrame(); };
				gradientFrame();
				gradientFrame();
				check(gradient.Count == 2, "the gradient widget must leave the gradient alone");

				move(at(0.5f));
				press(0); release(0); press(0);
				release(0);
				check(gradient.Count == 3 && close(gradient.Keys[1].T, 0.5f, 0.02f) && close(gradient.Keys[1].Color.a, 0.5f, 0.05f),
					std::format("a double click on the bar must add a key with the color there (count {}, alpha {:.2f})", gradient.Count, gradient.Keys[1].Color.a));

				move(at(0.5f));
				press(0);
				move(at(0.25f));
				move(at(0.25f));
				release(0);
				check(close(gradient.Keys[1].T, 0.25f, 0.02f), std::format("dragging a marker must move it (T {:.2f})", gradient.Keys[1].T));

				move(at(1.0f));
				press(0);
				move(at(0.5f));
				move(at(0.5f));
				release(0);
				check(gradient.Keys[2].T == 1.0f, "the last marker must keep its time");

				move(at(0.25f));
				press(1);
				release(1);
				check(gradient.Count == 2, std::format("a right click must remove a marker (count {})", gradient.Count));
				(void)changed;
			}

			// -- the panel -----------------------------------------------------------------------
			{
				Ref<Scene> scene = Ref<Scene>::Create();
				Entity plain = scene->CreateEntity("Plain");
				Entity sparks = scene->CreateEntity("Sparks");
				sparks.AddComponent<ParticleSystemComponent>().Settings.Mode = Particles::RenderMode::Sprite2D;

				ParticleEditorPanel panel;
				panel.SetSceneContext(scene);
				bool open = true;
				const auto panelFrame = [&]
				{
					ImGui::NewFrame();
					panel.OnImGuiRender(open);
					ImGui::Render();
				};

				// Nothing selected, an entity without a system, a closed panel: no preview.
				panelFrame();
				check(panel.GetPreviewEntity() == UUID(0), "no target, no preview");
				panel.SetTarget(plain);
				panelFrame();
				check(panel.GetPreviewEntity() == UUID(0), "an entity without a particle system has no preview");

				// A system: previewed while the panel is open; the scene plays it.
				panel.SetTarget(sparks);
				panelFrame();
				check(panel.GetPreviewEntity() == sparks.GetUUID(), "the selected system must be the preview");
				scene->SetParticlePreviewEntity(panel.GetPreviewEntity());
				for (int i = 0; i < 60; i++)
				{
					scene->UpdateParticles(Timestep(1.0f / 60.0f), true);
					panelFrame();
				}
				const Particles::State* state = scene->GetParticleState(sparks);
				check(state && state->Count() > 5, std::format("the previewed system must play in the editor (count {})", state ? state->Count() : 0));

				// Every state of the panel draws without leaving a scope open: paused, dropped, a one-shot that has ended.
				Particles::State* live = scene->GetParticleState(sparks);
				if (live)
				{
					live->Playing = false;
					panelFrame();
					live->Dropped = 7;
					panelFrame();
					live->Playing = true;
					sparks.GetComponent<ParticleSystemComponent>().Settings.Looping = false;
					live->Time = 100.0f;
					panelFrame();
				}
				sparks.GetComponent<ParticleSystemComponent>().Settings.SizeOverLife.Add(0.5f, 2.0f);
				sparks.GetComponent<ParticleSystemComponent>().Settings.ColorOverLife.Add(0.5f, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
				panelFrame();
				open = false;
				panelFrame();
				check(panel.GetPreviewEntity() == UUID(0), "a closed panel must not keep a preview going");
				open = true;
				panelFrame();

				// A system that stops asking for a preview loses its particles.
				scene->SetParticlePreviewEntity(0);
				scene->UpdateParticles(Timestep(0.0f), true);
				check(scene->GetParticleState(sparks) == nullptr, "a system that is no longer previewed must lose its particles");
			}
		}
		catch (const std::exception& error)
		{
			failures.push_back(std::string("exception: ") + error.what());
		}

		ImGui::SetCurrentContext(previous);
		ImGui::DestroyContext(context);
		return failures.empty();
	}
}
