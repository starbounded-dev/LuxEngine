// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using Lux;

namespace PlayTest
{
	// Game UI from C#: reading and changing components, and the events of widgets (M3, U6).
	public class UiCheck : Entity
	{
		int m_Frame;
		int m_Clicks, m_Toggles, m_Cancelled;
		bool m_LastToggle;
		int m_Dropped;
		string m_Detail = "";

		void OnCreate()
		{
			var button = FindEntityByName("UIButton");
			var toggle = FindEntityByName("UIToggle");
			button.GetComponent<UIWidgetComponent>().Clicked += () => m_Clicks++;
			toggle.GetComponent<UIWidgetComponent>().Toggled += on => { m_Toggles++; m_LastToggle = on; };
			// Removed again: must never run.
			System.Action dropped = () => m_Dropped++;
			button.GetComponent<UIWidgetComponent>().Clicked += dropped;
			button.GetComponent<UIWidgetComponent>().Clicked -= dropped;
			UI.Cancelled += () => m_Cancelled++;
		}

		void OnUpdate(float ts)
		{
			m_Frame++;
			var label = FindEntityByName("UILabel");
			var button = FindEntityByName("UIButton");
			var toggle = FindEntityByName("UIToggle");
			var slider = FindEntityByName("UISlider");
			var field = FindEntityByName("UIField");
			var panel = FindEntityByName("UIPanel");

			if (m_Frame == 3)
			{
				// Values from the scene, then written and read back.
				var text = label.GetComponent<UITextComponent>();
				bool ok = text.Text == "Game UI\nsecond line" && text.Size == 64.0f;
				text.Text = "Changed";
				text.Size = 20.0f;
				ok &= text.Text == "Changed" && text.Size == 20.0f;

				var widget = slider.GetComponent<UIWidgetComponent>();
				ok &= widget.Value > 0.59f && widget.Value < 0.61f && widget.MinValue == 0.0f && widget.MaxValue == 1.0f;
				widget.Value = 0.25f;
				widget.MaxValue = 4.0f;
				ok &= widget.Value == 0.25f && widget.MaxValue == 4.0f;

				var image = panel.GetComponent<UIImageComponent>();
				image.Color = new Vector4(1.0f, 0.5f, 0.25f, 1.0f);
				ok &= image.Color.Y == 0.5f && image.Color.Z == 0.25f;

				var element = panel.GetComponent<UIElementComponent>();
				ok &= element.Visible && element.OffsetMin.X == -300.0f && element.AnchorMax.Y == 0.5f;
				element.OffsetMin = new Vector2(-100.0f, -50.0f);
				element.Visible = false;
				ok &= element.OffsetMin.X == -100.0f && element.OffsetMin.Y == -50.0f && !element.Visible;
				element.Visible = true;

				var input = field.GetComponent<UIWidgetComponent>();
				ok &= input.Placeholder == "Name" && field.GetComponent<UITextComponent>().Text == "Hello";
				input.Placeholder = "Your name";
				ok &= input.Placeholder == "Your name" && !input.IsHovered && !input.IsPressed && !input.IsFocused;
				input.Interactable = false;
				ok &= !input.Interactable;
				m_Detail = $"text '{text.Text}', slider {widget.Value}/{widget.MaxValue}, panel offset {element.OffsetMin.X}";
				Report.Result("UIComponents", ok, "reading and writing UI components from a script: " + m_Detail);

				// Events: one click, one toggle (it starts on), none for a disabled button.
				button.GetComponent<UIWidgetComponent>().Click();
				toggle.GetComponent<UIWidgetComponent>().Click();
			}
			else if (m_Frame == 6)
			{
				Report.Result("UIEvents", m_Clicks == 1 && m_Toggles == 1 && !m_LastToggle && m_Dropped == 0
					&& !toggle.GetComponent<UIWidgetComponent>().Checked,
					$"Click() raised {m_Clicks} Clicked and {m_Toggles} Toggled (new state {m_LastToggle}, expected 1, 1, false); a removed handler ran {m_Dropped} times");
				button.GetComponent<UIWidgetComponent>().Interactable = false;
				button.GetComponent<UIWidgetComponent>().Click();
			}
			else if (m_Frame == 9)
			{
				Report.Result("UIDisabled", m_Clicks == 1, $"a disabled button's Click() raised {m_Clicks - 1} events");
			}
		}
	}
}
