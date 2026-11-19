// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Particles/ParticleSystem.h"

#include <imgui.h>

// ImGui widgets for a particle system's curves and gradient (docs/PARTICLES_PLAN.md, E10). Main thread only.
//
// Both draw a box and edit by dragging: a key is picked up with the left button, moved along the curve (the first and last keys
// move up and down only), added by double-clicking an empty place and removed with the right button (the last one stays). A change
// is reported to ImGui as an edit of the item, so the editor's undo sees it like any property.
namespace Lux::ParticleWidgets
{
	// A curve over a particle's life, values from 0 to maxValue. Returns true on the frames it changed. size.x <= 0 fills the width.
	bool Curve(const char* id, Particles::Curve& curve, float maxValue, ImVec2 size);

	// A color (with alpha) over a particle's life: a bar with a marker per key, and the selected key's color and time below it.
	bool Gradient(const char* id, Particles::Gradient& gradient, float width);

	// The same as rows of a property grid: the label on the left, the widget on the right.
	bool PropertyCurve(const char* label, Particles::Curve& curve, float maxValue, const char* helpText = "");
	bool PropertyGradient(const char* label, Particles::Gradient& gradient, const char* helpText = "");
}
