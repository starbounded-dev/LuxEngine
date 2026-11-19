// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using Lux;

namespace PlayTest
{
	// Particles from C#: play, burst, stop, pause and clear a system (M13, G1d). The scene's system does not play on awake,
	// holds particles for 30 seconds and releases 20 a second. Phases wait in seconds, so the frame rate does not matter.
	public class ParticleCheck : Entity
	{
		int m_Frame;
		int m_Phase;
		float m_Time;
		int m_CountAtStop, m_CountAtPause;

		void OnUpdate(float ts)
		{
			m_Frame++;
			var particles = GetComponent<ParticleSystemComponent>();
			if (particles == null)
			{
				if (m_Frame == 1)
					Log.Error("ParticleCheck: no ParticleSystemComponent");
				return;
			}
			m_Time += ts;

			switch (m_Phase)
			{
				case 0:
					if (m_Frame < 2)
						return;
					bool ok = particles.ParticleCount == 0 && !particles.IsPlaying && particles.Looping && particles.EmissionRate == 20.0f;
					particles.Emit(10);
					ok &= particles.ParticleCount == 10 && particles.IsPlaying;
					Report.Result("ParticleEmit", ok, $"a system that does not play on awake: Emit(10) gives {particles.ParticleCount} particles (expected 10), playing {particles.IsPlaying}");
					particles.Play();
					m_Phase = 1;
					m_Time = 0.0f;
					break;
				case 1:
					if (m_Time < 1.0f)
						return;
					// Twenty a second for a second: about twenty more than the burst's ten.
					int count = particles.ParticleCount;
					Report.Result("ParticlePlay", count >= 24 && particles.IsPlaying, $"Play() releases at the rate: {count} particles after a second (expected 24 or more with the burst of 10)");
					particles.Stop();
					m_CountAtStop = particles.ParticleCount;
					m_Phase = 2;
					m_Time = 0.0f;
					break;
				case 2:
					if (m_Time < 0.5f)
						return;
					Report.Result("ParticleStop", particles.ParticleCount == m_CountAtStop && particles.IsPlaying,
						$"Stop() releases nothing new and keeps the particles in the air: {particles.ParticleCount}, expected {m_CountAtStop}");
					particles.Play();
					particles.Pause();
					m_CountAtPause = particles.ParticleCount;
					m_Phase = 3;
					m_Time = 0.0f;
					break;
				case 3:
					if (m_Time < 0.3f)
						return;
					bool cleared = particles.ParticleCount == m_CountAtPause && !particles.IsPlaying;
					particles.Play();
					particles.Stop();
					particles.Clear();
					cleared &= particles.ParticleCount == 0;
					particles.EmissionRate = 5.0f;
					particles.Looping = false;
					cleared &= particles.EmissionRate == 5.0f && !particles.Looping;
					Report.Result("ParticleClear", cleared, $"Pause() holds {m_CountAtPause} particles, Clear() leaves {particles.ParticleCount} (expected 0), settings {particles.EmissionRate} / {particles.Looping}");
					m_Phase = 4;
					break;
			}
		}
	}
}
