using System;
using Lux;

namespace LuxSample
{
	// Attach alongside AudioSourceComponent. The demo scene already has both attached.
	public class AudioDemo : Entity
	{
		public string StringsBank = "Audio/SampleProject/Build/Desktop/Master.strings.bank";
		public string MasterBank = "Audio/SampleProject/Build/Desktop/Master.bank";
		public string EventPath = "event:/Fart";
		// The native backend plays this Sound asset; the FMOD build plays EventPath from the banks.
		public Sound Sound;
		public bool PlayOnStart = true;
		public float Volume = 0.7f;

		private AudioSourceComponent m_Source;
		private EventInstance m_Instance;
		private bool m_Ready;

		void OnCreate()
		{
			try
			{
				bool native = Audio.PlaysSoundAssets;
				if (native && Sound == null)
				{
					Log.Error("Audio Demo: assign this script's Sound (the native backend plays Sound assets).");
					return;
				}
				// Relative bank paths start at Assets. Loading the same file twice is safe.
				if (!native && (!Audio.LoadBank(MasterBank) || !Audio.LoadBank(StringsBank)))
				{
					Log.Error("Audio Demo: bank loading failed. Check Project Settings > Audio and the bank paths on this script.");
					return;
				}
				m_Source = GetComponent<AudioSourceComponent>();
				if (m_Source == null)
				{
					Log.Error("Audio Demo requires an Audio Source component on the same entity.");
					return;
				}
				// CreateInstance validates the event and gives us an independently owned voice.
				m_Instance = native ? Audio.CreateInstance(Sound) : Audio.CreateInstance(EventPath);
				m_Instance.SetVolume(Volume);
				m_Instance.Stopped += () => Log.Info("Audio Demo: independent instance stopped.");
				m_Instance.Marker += marker => Log.Info($"Audio Demo marker: {marker}");
				if (!native)
					m_Source.SetEvent(EventPath); // the native source already names its Sound in the scene
				m_Source.Volume = Volume;
				m_Ready = true;
				if (PlayOnStart)
					m_Source.Play();
				Log.Info("Audio Demo ready: K=replay source, L=one-shot, I=independent instance, P=pause source, O=stop source + instance. Right mouse + WASD moves the listener.");
			}
			catch (Exception exception)
			{
				m_Instance?.Dispose();
				m_Instance = null;
				Log.Error($"Audio Demo could not start: {exception.Message}");
			}
		}

		void OnUpdate(float ts)
		{
			if (!m_Ready)
				return;
			if (!m_Instance.IsValid)
			{
				m_Ready = false;
				Log.Warn("Audio Demo: banks were reloaded. Stop Play and start again to recreate the audio instances.");
				return;
			}
			// Entity is at the scene root, so Translation is also its world position.
			m_Instance.Set3DAttributes(Translation, Vector3.Zero, new Vector3(0, 0, -1), new Vector3(0, 1, 0));
			if (Input.IsKeyPressed(KeyCode.K))
				m_Source.Restart();
			if (Input.IsKeyPressed(KeyCode.L))
			{
				if (Sound != null && Audio.PlaysSoundAssets)
					Audio.PlayOneShot(Sound, Translation);
				else
					Audio.PlayOneShot(EventPath, Translation);
			}
			if (Input.IsKeyPressed(KeyCode.I))
				m_Instance.Start();
			if (Input.IsKeyPressed(KeyCode.P))
				m_Source.IsPaused = !m_Source.IsPaused;
			if (Input.IsKeyPressed(KeyCode.O))
			{
				m_Source.Stop(false);
				m_Instance.Stop(false);
			}
		}

		void OnDestroy()
		{
			// The scene owns the component voice; this script owns only the separate instance.
			m_Ready = false;
			m_Instance?.Dispose();
			m_Instance = null;
		}
	}
}
