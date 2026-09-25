using System;
using Lux;

namespace LuxSample
{
	// Attach to an entity with an Audio Portal filling a doorway. Its first child is the visible door
	// panel, which slides toward the portal's -X as it opens: the same side the acoustic shutter
	// retracts to, so what you see and what you hear open together. F toggles the door.
	public class SlidingDoor : Entity
	{
		public float OpenSeconds = 1.2f;
		public bool StartOpen = false;

		private AudioPortalComponent m_Portal;
		private Entity m_Panel;
		private Vector3 m_PanelClosed;
		private float m_Width;
		private float m_Open;
		private float m_Target;

		void OnCreate()
		{
			m_Portal = GetComponent<AudioPortalComponent>();
			if (m_Portal == null)
			{
				Log.Error("Sliding Door requires an Audio Portal component on the same entity.");
				return;
			}
			Entity[] children = Children;
			m_Panel = children.Length > 0 ? children[0] : null;
			if (m_Panel != null)
				m_PanelClosed = m_Panel.Translation;
			m_Width = m_Portal.HalfExtents.X * 2.0f;
			m_Open = m_Target = StartOpen ? 1.0f : 0.0f;
			Apply();
			Log.Info("Sliding Door ready: F opens and closes the door.");
		}

		void OnUpdate(float ts)
		{
			if (m_Portal == null)
				return;
			if (Input.IsKeyPressed(KeyCode.F))
				m_Target = 1.0f - m_Target;
			if (m_Open == m_Target)
				return;

			float step = OpenSeconds > 0.0f ? ts / OpenSeconds : 1.0f;
			m_Open = m_Target > m_Open ? MathF.Min(m_Open + step, m_Target) : MathF.Max(m_Open - step, m_Target);
			Apply();
		}

		private void Apply()
		{
			// Ease the visible slide; the acoustic shutter follows the same eased amount.
			float eased = m_Open * m_Open * (3.0f - 2.0f * m_Open);
			m_Portal.Open = eased;
			if (m_Panel != null)
				m_Panel.Translation = m_PanelClosed + new Vector3(-m_Width * eased, 0.0f, 0.0f);
		}
	}
}
