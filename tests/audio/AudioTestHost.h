#pragma once
#include "lpch.h"
#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/AudioAccessibility.h"
#include <fmod.hpp>
#include <fmod_studio.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <cassert>
#include <iostream>
// Only host setup is replaced: production surface/event code drives the real FMOD Studio SDK.
namespace Lux
{
	std::shared_ptr<spdlog::logger> Log::s_CoreLogger = spdlog::stdout_color_mt("test");
	std::shared_ptr<spdlog::logger> Log::s_ClientLogger = Log::s_CoreLogger;
	std::shared_ptr<spdlog::logger> Log::s_EditorConsoleLogger = Log::s_CoreLogger;
	FMOD::System* AudioEngine::s_Engine = nullptr;
	FMOD::Studio::System* AudioEngine::s_StudioSystem = nullptr;
	int TestRealVoices = 64;
	void AudioEngine::Init()
	{
		assert(FMOD::Studio::System::create(&s_StudioSystem) == FMOD_OK);
		assert(s_StudioSystem->getCoreSystem(&s_Engine) == FMOD_OK);
		assert(s_Engine->setOutput(FMOD_OUTPUTTYPE_NOSOUND) == FMOD_OK);
		assert(s_Engine->setSoftwareChannels(TestRealVoices) == FMOD_OK);
		assert(s_StudioSystem->initialize(128, FMOD_STUDIO_INIT_NORMAL, FMOD_INIT_NORMAL, nullptr) == FMOD_OK);
		s_HasInitializedAudioEngine = true;
		++s_EventGeneration;
		++s_BankRevision;
	}
	void AudioEngine::Shutdown()
	{
		AudioAccessibility::ReleaseMixer();
		s_HasInitializedAudioEngine = false;
		++s_EventGeneration;
		++s_BankRevision;
		assert(s_StudioSystem->release() == FMOD_OK);
		s_StudioSystem = nullptr;
		s_Engine = nullptr;
	}
	bool AudioEngine::LoadBank(const std::filesystem::path& p)
	{
		FMOD::Studio::Bank* b = nullptr;
		auto r = s_StudioSystem->loadBankFile(p.c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &b);
		++s_BankRevision;
		return r == FMOD_OK;
	}
	std::string AudioEngine::ResolveEventReference(const std::string& ref)
	{
		FMOD_GUID g{};
		if (ref.front() == '{')
		{
			if (FMOD::Studio::parseID(ref.c_str(), &g) != FMOD_OK)
				return {};
		}
		else if (s_StudioSystem->lookupID(ref.c_str(), &g) != FMOD_OK)
			return {};
		char text[64];
		std::snprintf(text, sizeof(text), "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", g.Data1, g.Data2,
					  g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6],
					  g.Data4[7]);
		return text;
	}
} // namespace Lux
using namespace Lux;
void Near(float a, float b)
{
	if (std::abs(a - b) >= 0.0001f)
		std::cerr << "Expected " << b << ", got " << a << std::endl;
	assert(std::abs(a - b) < 0.0001f);
}
void Tick()
{
	auto* s = AudioEngine::GetStudioSystem();
	assert(s->update() == FMOD_OK);
	assert(s->flushCommands() == FMOD_OK);
}
FMOD::Studio::EventDescription* Desc(const char* path)
{
	FMOD::Studio::EventDescription* d = nullptr;
	assert(AudioEngine::GetStudioSystem()->getEvent(path, &d) == FMOD_OK);
	return d;
}
FMOD::Studio::EventInstance* Only(const char* path)
{
	FMOD::Studio::EventInstance* i = nullptr;
	int n = 0;
	assert(Desc(path)->getInstanceList(&i, 1, &n) == FMOD_OK && n == 1);
	return i;
}
std::filesystem::path BankDirectory;
void Load()
{
	auto dir = BankDirectory;
	assert(AudioEngine::LoadBank(dir / "Master.strings.bank"));
	assert(AudioEngine::LoadBank(dir / "Master.bank"));
}

int Count(const char* path)
{
	int n = 0;
	assert(Desc(path)->getInstanceCount(&n) == FMOD_OK);
	return n;
}
