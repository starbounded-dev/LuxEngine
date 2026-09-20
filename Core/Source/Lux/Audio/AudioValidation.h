// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once
#include "AudioEventRef.h"
#include <filesystem>
#include <vector>
#include <string>
namespace Lux
{
	class Project;
	class Scene;
	enum class AudioValidationSeverity { Warning, Error };
	enum class AudioReferenceKind { Event, Loop, Snapshot };
	struct AudioValidationIssue
	{
		AudioValidationSeverity Severity;
		std::string Location, Message;
	};
	struct AudioValidationReference
	{
		AudioEventRef Event;
		std::string Location;
		AudioReferenceKind Kind = AudioReferenceKind::Event;
	};
	struct AudioValidationBank
	{
		std::string Name;
		uint64_t Bytes = 0;
	};
	struct AudioValidationReport
	{
		std::vector<AudioValidationIssue> Issues;
		std::vector<AudioValidationBank> Banks;
		uint64_t BankBytes = 0;
		size_t ReferencedEvents = 0, CatalogEvents = 0;
		bool HasErrors() const;
		void Log() const;
	};
	class AudioValidation
	{
	public:
		// Read-only on the main thread. Includes all scene/prefab/table assets, plus unsaved context.
		static AudioValidationReport ValidateProject(const Project& project, Scene* current = nullptr);
		// Uses a separate NOSOUND FMOD system; never unloads the application's banks.
		static AudioValidationReport ValidateBanks(const std::filesystem::path& directory,
			const std::vector<AudioValidationReference>& references, const std::vector<std::string>& buses = {});
	};
}
