using System;
using Lux;

namespace LuxSample
{
	// A repeatable camera move for recording the demo. Put this on an entity at the origin with no
	// rotation and give it child entities as waypoints, in order. C starts and stops the move; the
	// target camera glides through the waypoints on a smooth curve while looking at LookAt.
	// Keep hands off the mouse while it plays: Fly Camera re-applies its own look when you move it.
	public class CameraPath : Entity
	{
		public string Target = "Listener Camera - RMB + WASD";
		public string LookAt = "Music Emitter - MusicAmbiance";
		public float SecondsPerPoint = 4.0f;
		public bool Loop = true;
		public bool PlayOnStart = false;

		private Entity m_Target;
		private Entity m_LookAt;
		private Entity[] m_Points;
		private float m_Time;
		private bool m_Playing;

		void OnCreate()
		{
			m_Target = FindEntityByName(Target);
			m_LookAt = string.IsNullOrEmpty(LookAt) ? null : FindEntityByName(LookAt);
			m_Points = Children;
			if (m_Target == null || m_Points.Length < 2)
			{
				Log.Error($"Camera Path needs a target named '{Target}' and at least two child waypoints.");
				m_Points = Array.Empty<Entity>();
				return;
			}
			m_Playing = PlayOnStart;
			Log.Info("Camera Path ready: C starts and stops the camera move.");
		}

		void OnUpdate(float ts)
		{
			if (m_Points.Length < 2)
				return;
			if (Input.IsKeyPressed(KeyCode.C))
			{
				m_Playing = !m_Playing;
				if (m_Playing && !Loop && m_Time >= Segments())
					m_Time = 0.0f;
			}
			if (!m_Playing)
				return;

			m_Time += SecondsPerPoint > 0.0f ? ts / SecondsPerPoint : 0.0f;
			if (m_Time >= Segments())
			{
				if (Loop)
					m_Time -= Segments();
				else
				{
					m_Time = Segments();
					m_Playing = false;
				}
			}

			int segment = Math.Min((int)m_Time, Segments() - 1);
			float t = m_Time - segment;
			Vector3 position = CatmullRom(Point(segment - 1), Point(segment), Point(segment + 1), Point(segment + 2), t);
			m_Target.Translation = position;

			Vector3 focus = m_LookAt != null ? m_LookAt.Translation : Point(segment + 1);
			Vector3 direction = focus + (position * -1.0f);
			float length = MathF.Sqrt(direction.X * direction.X + direction.Y * direction.Y + direction.Z * direction.Z);
			if (length > 0.0001f)
			{
				direction = direction * (1.0f / length);
				// Inverse of Fly Camera's basis: forward = (-sin yaw cos pitch, sin pitch, -cos yaw cos pitch).
				float pitch = MathF.Asin(Math.Clamp(direction.Y, -1.0f, 1.0f));
				float yaw = MathF.Atan2(-direction.X, -direction.Z);
				m_Target.Rotation = new Vector3(pitch, yaw, 0.0f);
			}
		}

		private int Segments() => Loop ? m_Points.Length : m_Points.Length - 1;

		private Vector3 Point(int index)
		{
			int count = m_Points.Length;
			index = Loop ? ((index % count) + count) % count : Math.Clamp(index, 0, count - 1);
			return m_Points[index].Translation;
		}

		private static Vector3 CatmullRom(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t)
		{
			float t2 = t * t;
			float t3 = t2 * t;
			return (p1 * 2.0f + (p2 + p0 * -1.0f) * t + (p0 * 2.0f + p1 * -5.0f + p2 * 4.0f + p3 * -1.0f) * t2
				+ (p1 * 3.0f + p0 * -1.0f + p2 * -3.0f + p3) * t3) * 0.5f;
		}
	}
}
