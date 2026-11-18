// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Animation/Animation.h"

#include <string>
#include <vector>

namespace Lux
{
	// Events are authored in a text file beside the model, `<model>.events.yaml`, and baked into the clips when
	// they are extracted (so they survive re-extraction and diff in version control):
	//
	//   AnimationEvents:
	//     Version: 1
	//     Clips:
	//       - Clip: Walk              # the clip's name in the model; a "Model|" prefix is optional
	//         Events:
	//           - Time: 0.25          # seconds
	//             Name: Footstep
	//             Argument: left      # optional
	//             Sound: 1234         # optional, a Sound asset handle played at the entity
	//
	// Sets the events of every clip the file names; a clip it does not name keeps what it has. Returns false and
	// changes nothing when the text is not a valid events file. A clip name that matches nothing, or an event
	// outside its clip, is reported in `warnings` (when given) and skipped.
	bool ApplyAnimationEvents(const std::string& yaml, std::vector<Ref<Animation>>& clips, std::vector<std::string>* warnings = nullptr);
}
