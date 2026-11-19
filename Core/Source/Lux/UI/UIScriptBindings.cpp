// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "UIScriptBindings.h"

#include "Lux/Core/Application.h"
#include "Lux/Scene/Components.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Scripting/ScriptEngine.h"

#include <Coral/Assembly.hpp>
#include <Coral/String.hpp>
#include <Coral/Type.hpp>

#include <cmath>

namespace Lux
{
	namespace
	{
		// The values of the C# enums of the same names, in the same order.
		enum class FloatField : int32_t { WidgetValue = 0, WidgetMin, WidgetMax, ScrollOffset, TextSize };
		enum class BoolField : int32_t { ElementVisible = 0, WidgetInteractable, WidgetChecked, WidgetWholeNumbers, WidgetHovered, WidgetPressed, WidgetFocused, CanvasEnabled };
		enum class VectorField : int32_t { ImageColor = 0, TextColor, AnchorMin, AnchorMax, OffsetMin, OffsetMax };
		enum class TextField : int32_t { Text = 0, Placeholder };

		Coral::Type* s_EventsType = nullptr;

		Scene* CurrentScene()
		{
			if (!Application::IsMainThread())
			{
				LUX_CORE_ERROR_TAG("ScriptEngine", "UI scripting calls must run on the main thread");
				return nullptr;
			}
			return ScriptEngine::GetInstance().GetCurrentScene().Raw();
		}

		Entity EntityOf(uint64_t id)
		{
			Scene* scene = CurrentScene();
			if (!scene)
				return {};
			Entity entity = scene->TryGetEntityWithUUID(id);
			if (!entity)
				LUX_CONSOLE_LOG_ERROR("A script used entity {} after it was destroyed (or it never existed)", id);
			return entity;
		}

		template<typename TComponent>
		TComponent* ComponentOf(Entity entity)
		{
			if (!entity)
				return nullptr;
			TComponent* component = entity.TryGetComponent<TComponent>();
			if (!component)
				LUX_CONSOLE_LOG_ERROR("Entity {} ({}) does not have that UI component", (uint64_t)entity.GetUUID(), entity.GetName());
			return component;
		}

		float* FloatOf(Entity entity, FloatField field)
		{
			switch (field)
			{
				case FloatField::WidgetValue: if (auto* c = ComponentOf<UIWidgetComponent>(entity)) return &c->Value; break;
				case FloatField::WidgetMin: if (auto* c = ComponentOf<UIWidgetComponent>(entity)) return &c->MinValue; break;
				case FloatField::WidgetMax: if (auto* c = ComponentOf<UIWidgetComponent>(entity)) return &c->MaxValue; break;
				case FloatField::ScrollOffset: if (auto* c = ComponentOf<UIWidgetComponent>(entity)) return &c->ScrollOffset; break;
				case FloatField::TextSize: if (auto* c = ComponentOf<UITextComponent>(entity)) return &c->Size; break;
			}
			return nullptr;
		}

		// A bool field, read-only ones (hover, press, focus) included: the writer passes null for them.
		bool* BoolOf(Entity entity, BoolField field)
		{
			switch (field)
			{
				case BoolField::ElementVisible: if (auto* c = ComponentOf<UIElementComponent>(entity)) return &c->Visible; break;
				case BoolField::WidgetInteractable: if (auto* c = ComponentOf<UIWidgetComponent>(entity)) return &c->Interactable; break;
				case BoolField::WidgetChecked: if (auto* c = ComponentOf<UIWidgetComponent>(entity)) return &c->Checked; break;
				case BoolField::WidgetWholeNumbers: if (auto* c = ComponentOf<UIWidgetComponent>(entity)) return &c->WholeNumbers; break;
				case BoolField::WidgetHovered: if (auto* c = ComponentOf<UIWidgetComponent>(entity)) return &c->Hovered; break;
				case BoolField::WidgetPressed: if (auto* c = ComponentOf<UIWidgetComponent>(entity)) return &c->Pressed; break;
				case BoolField::WidgetFocused: if (auto* c = ComponentOf<UIWidgetComponent>(entity)) return &c->Focused; break;
				case BoolField::CanvasEnabled: if (auto* c = ComponentOf<UICanvasComponent>(entity)) return &c->Enabled; break;
			}
			return nullptr;
		}

		bool IsFinite(float value)
		{
			if (std::isfinite(value))
				return true;
			LUX_CONSOLE_LOG_ERROR("A UI value must be a finite number, not {}", value);
			return false;
		}

		float UI_GetFloat(uint64_t entityID, int32_t field)
		{
			const float* value = FloatOf(EntityOf(entityID), static_cast<FloatField>(field));
			return value ? *value : 0.0f;
		}

		void UI_SetFloat(uint64_t entityID, int32_t field, float value)
		{
			if (!IsFinite(value))
				return;
			if (float* target = FloatOf(EntityOf(entityID), static_cast<FloatField>(field)))
				*target = value;
		}

		int32_t UI_GetBool(uint64_t entityID, int32_t field)
		{
			const bool* value = BoolOf(EntityOf(entityID), static_cast<BoolField>(field));
			return value && *value ? 1 : 0;
		}

		void UI_SetBool(uint64_t entityID, int32_t field, int32_t value)
		{
			const BoolField kind = static_cast<BoolField>(field);
			if (kind == BoolField::WidgetHovered || kind == BoolField::WidgetPressed || kind == BoolField::WidgetFocused)
			{
				LUX_CONSOLE_LOG_ERROR("Hovered, Pressed and Focused follow the pointer and keyboard; a script cannot set them (use Focus())");
				return;
			}
			if (bool* target = BoolOf(EntityOf(entityID), kind))
				*target = value != 0;
		}

		// Colors are RGBA; the two-component fields use x and y.
		glm::vec4* VectorOf(Entity entity, VectorField field, glm::vec4& scratch)
		{
			switch (field)
			{
				case VectorField::ImageColor: if (auto* c = ComponentOf<UIImageComponent>(entity)) return &c->Color; break;
				case VectorField::TextColor: if (auto* c = ComponentOf<UITextComponent>(entity)) return &c->Color; break;
				default:
				{
					auto* c = ComponentOf<UIElementComponent>(entity);
					if (!c)
						break;
					glm::vec2* target = field == VectorField::AnchorMin ? &c->Rect.AnchorMin : field == VectorField::AnchorMax ? &c->Rect.AnchorMax
						: field == VectorField::OffsetMin ? &c->Rect.OffsetMin : &c->Rect.OffsetMax;
					scratch = { target->x, target->y, 0.0f, 0.0f };
					return &scratch;
				}
			}
			return nullptr;
		}

		void UI_GetVector(uint64_t entityID, int32_t field, glm::vec4* out)
		{
			glm::vec4 scratch;
			const glm::vec4* value = VectorOf(EntityOf(entityID), static_cast<VectorField>(field), scratch);
			*out = value ? *value : glm::vec4(0.0f);
		}

		void UI_SetVector(uint64_t entityID, int32_t field, glm::vec4* in)
		{
			if (!IsFinite(in->x) || !IsFinite(in->y) || !IsFinite(in->z) || !IsFinite(in->w))
				return;
			Entity entity = EntityOf(entityID);
			const VectorField kind = static_cast<VectorField>(field);
			glm::vec4 scratch;
			glm::vec4* target = VectorOf(entity, kind, scratch);
			if (!target)
				return;
			if (kind == VectorField::ImageColor || kind == VectorField::TextColor)
			{
				*target = *in;
				return;
			}
			auto& rect = entity.GetComponent<UIElementComponent>().Rect;
			glm::vec2& destination = kind == VectorField::AnchorMin ? rect.AnchorMin : kind == VectorField::AnchorMax ? rect.AnchorMax
				: kind == VectorField::OffsetMin ? rect.OffsetMin : rect.OffsetMax;
			destination = { in->x, in->y };
		}

		std::string* TextOf(Entity entity, TextField field)
		{
			if (field == TextField::Placeholder)
			{
				if (auto* c = ComponentOf<UIWidgetComponent>(entity))
					return &c->Placeholder;
				return nullptr;
			}
			if (auto* c = ComponentOf<UITextComponent>(entity))
				return &c->Text;
			return nullptr;
		}

		Coral::String UI_GetText(uint64_t entityID, int32_t field)
		{
			const std::string* text = TextOf(EntityOf(entityID), static_cast<TextField>(field));
			return Coral::String::New(text ? *text : std::string());
		}

		void UI_SetText(uint64_t entityID, int32_t field, Coral::String text)
		{
			if (std::string* target = TextOf(EntityOf(entityID), static_cast<TextField>(field)))
			{
				*target = text;
				// A text input's caret goes to the end of what a script put there.
				if (auto* widget = EntityOf(entityID).TryGetComponent<UIWidgetComponent>(); widget && static_cast<TextField>(field) == TextField::Text)
					widget->CaretIndex = -1;
			}
		}

		// What a click does, from a script: a button raises Clicked, a toggle flips and raises Toggled.
		void UI_Click(uint64_t entityID)
		{
			Entity entity = EntityOf(entityID);
			Scene* scene = CurrentScene();
			auto* widget = ComponentOf<UIWidgetComponent>(entity);
			if (!widget || !scene)
				return;
			if (!widget->Interactable)
				return;
			const UUID id = entity.GetUUID();
			if (widget->Kind == UIWidgetKind::Button)
			{
				scene->QueueUIEvent({ UI::EventKind::Click, id, 0.0f, false });
			}
			else if (widget->Kind == UIWidgetKind::Toggle)
			{
				widget->Checked = !widget->Checked;
				scene->QueueUIEvent({ UI::EventKind::Toggled, id, 0.0f, widget->Checked });
			}
			else
			{
				LUX_CONSOLE_LOG_ERROR("Click() works on a button or a toggle; '{}' is another kind of widget", entity.GetName());
			}
		}

		// Moves the keyboard and gamepad focus to a widget, as if navigation had reached it.
		void UI_Focus(uint64_t entityID)
		{
			Entity entity = EntityOf(entityID);
			Scene* scene = CurrentScene();
			if (!entity || !scene)
				return;
			const auto* widget = ComponentOf<UIWidgetComponent>(entity);
			if (!widget || !widget->Interactable)
				return;
			UI::InteractionState& state = scene->GetUIState();
			state.Focus = entity.GetUUID();
			state.CaretTime = 0.0f;
		}
	}

	void UIScriptBindings::Register(Coral::ManagedAssembly& assembly)
	{
#define LUX_ADD_UI_CALL(Name) assembly.AddInternalCall("Lux.InternalCalls", #Name, reinterpret_cast<void*>(&Name))
		LUX_ADD_UI_CALL(UI_GetFloat);
		LUX_ADD_UI_CALL(UI_SetFloat);
		LUX_ADD_UI_CALL(UI_GetBool);
		LUX_ADD_UI_CALL(UI_SetBool);
		LUX_ADD_UI_CALL(UI_GetVector);
		LUX_ADD_UI_CALL(UI_SetVector);
		LUX_ADD_UI_CALL(UI_GetText);
		LUX_ADD_UI_CALL(UI_SetText);
		LUX_ADD_UI_CALL(UI_Focus);
		LUX_ADD_UI_CALL(UI_Click);
#undef LUX_ADD_UI_CALL
	}

	void UIScriptBindings::Bind(Coral::ManagedAssembly& assembly)
	{
		s_EventsType = &assembly.GetLocalType("Lux.UIEvents");
		if (!*s_EventsType)
			LUX_CORE_ERROR_TAG("ScriptEngine", "ScriptCore has no Lux.UIEvents: UI events will not reach scripts");
	}

	void UIScriptBindings::Dispatch(Scene& scene)
	{
		const std::vector<UI::Event> events = scene.GetUIEvents(); // a copy: a handler may change the UI
		if (events.empty() || !s_EventsType || !*s_EventsType)
			return;

		for (const UI::Event& event : events)
		{
			std::string text;
			if (event.Kind == UI::EventKind::TextChanged || event.Kind == UI::EventKind::Submit)
			{
				if (Entity entity = scene.TryGetEntityWithUUID(event.Element); entity && entity.HasComponent<UITextComponent>())
					text = entity.GetComponent<UITextComponent>().Text;
			}
			Coral::String argument = Coral::String::New(text);
			s_EventsType->InvokeStaticMethod("Dispatch", static_cast<uint64_t>(event.Element), static_cast<int32_t>(event.Kind), event.Value, static_cast<int32_t>(event.Checked), argument);
			Coral::String::Free(argument);
		}
	}

	void UIScriptBindings::Reset()
	{
		if (s_EventsType && *s_EventsType)
			s_EventsType->InvokeStaticMethod("Reset");
	}

	void UIScriptBindings::Shutdown()
	{
		s_EventsType = nullptr;
	}
}
