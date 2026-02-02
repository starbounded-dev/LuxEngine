#pragma once

struct ma_engine;

namespace StarEngine {

	class AudioEngine
	{
	public:
		static void Init();
		static void Shutdown();

		static ma_engine* GetEngine() { return s_Engine; }
		static bool ShuttingDownEngine() { return s_ShuttingDown; }

		static bool HasInitializedEngine() { return s_HasInitializedAudioEngine; }
		static void SetInitalizedEngine(bool value) { s_HasInitializedAudioEngine = value; }

	private:
		static ma_engine* s_Engine;
		inline static bool s_HasInitializedAudioEngine = false;
		inline static bool s_ShuttingDown = false;
	};
}
