using System;
using Lux;

namespace LuxSample
{
	// Attach alongside AudioSourceComponent. The demo scene already has both attached.
	public class FmodAudioDemo : Entity
	{
		public string StringsBank = "Audio/SampleProject/Build/Desktop/Master.strings.bank";
		public string MasterBank = "Audio/SampleProject/Build/Desktop/Master.bank";
		public string EventPath = "event:/Fart";
		public bool PlayOnStart = true;
		public float Volume = 0.7f;

		private AudioSourceComponent m_Source;
		private EventInstance m_Instance;
		private bool m_Ready;

		void OnCreate()
		{
			try
			{
				// Relative bank paths start at Assets. Loading the same file twice is safe.
				if (!Audio.LoadBank(MasterBank) || !Audio.LoadBank(StringsBank))
				{
					Log.Error("FMOD Demo: bank loading failed. Check Project Settings > Audio and the bank paths on this script.");
					return;
				}
				m_Source = GetComponent<AudioSourceComponent>();
				if (m_Source == null)
				{
					Log.Error("FMOD Demo requires an Audio Source component on the same entity.");
					return;
				}
				// CreateInstance validates the event and gives us an independently owned voice.
				m_Instance = Audio.CreateInstance(EventPath);
				m_Instance.SetVolume(Volume);
				m_Instance.Stopped += () => Log.Info("FMOD Demo: independent instance stopped.");
				m_Instance.Marker += marker => Log.Info($"FMOD Demo marker: {marker}");
				m_Source.SetEvent(EventPath);
				m_Source.Volume = Volume;
				m_Ready = true;
				if (PlayOnStart)
					m_Source.Play();
				Log.Info("FMOD Demo ready: K=replay source, L=one-shot, I=independent instance, P=pause source, O=stop source + instance. Right mouse + WASD moves the listener.");
			}
			catch (Exception exception)
			{
				m_Instance?.Dispose();
				m_Instance = null;
				Log.Error($"FMOD Demo could not start: {exception.Message}");
			}
		}

		void OnUpdate(float ts)
		{
			if (!m_Ready)
				return;
			if (!m_Instance.IsValid)
			{
				m_Ready = false;
				Log.Warn("FMOD Demo: banks were reloaded. Stop Play and start again to recreate the audio instances.");
				return;
			}
			// Entity is at the scene root, so Translation is also its world position.
			m_Instance.Set3DAttributes(Translation, Vector3.Zero, new Vector3(0, 0, -1), new Vector3(0, 1, 0));
			if (Input.IsKeyPressed(KeyCode.K))
				m_Source.Restart();
			if (Input.IsKeyPressed(KeyCode.L))
				Audio.PlayOneShot(EventPath, Translation);
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
