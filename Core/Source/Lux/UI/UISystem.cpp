// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "UISystem.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Core/Input.h"
#include "Lux/Renderer/FrameRenderPacket.h"
#include "Lux/Renderer/Renderer2D.h"
#include "Lux/Renderer/UI/Font.h"
#include "Lux/Scene/Components.h"
#include "Lux/Scene/Scene.h"

#include <algorithm>
#include <cmath>

namespace Lux::UI
{
	namespace
	{
		constexpr float k_ScrollNotchUnits = 60.0f; // UI units one wheel notch moves a scroll list
		constexpr size_t k_NoParent = ~size_t(0);

		struct Child
		{
			Entity Element;
			const UIElementComponent* Component;
		};

		const UIWidgetComponent* FindWidget(Entity entity)
		{
			return entity.HasComponent<UIWidgetComponent>() ? &entity.GetComponent<UIWidgetComponent>() : nullptr;
		}

		Rect Intersect(const Rect& a, const Rect& b)
		{
			return { glm::max(a.Min, b.Min), glm::min(a.Max, b.Max) };
		}

		void ResolveChildren(Scene& scene, Entity parent, size_t parentIndex, const Rect& parentRect, const LayoutGroup& group,
			ResolvedCanvas& canvas, const Rect* clip)
		{
			std::vector<Child> children;
			for (const UUID id : parent.Children())
			{
				Entity child = scene.TryGetEntityWithUUID(id);
				if (!child || !child.HasComponent<UIElementComponent>())
					continue;
				const UIElementComponent& element = child.GetComponent<UIElementComponent>();
				if (element.Visible && scene.IsEntityActiveInHierarchy(child))
					children.push_back({ child, &element });
			}

			const UIWidgetComponent* widget = FindWidget(parent);
			const bool scrolls = widget && widget->Kind == UIWidgetKind::ScrollList;

			std::vector<LayoutChild> asks;
			if (group.Kind != LayoutKind::None || scrolls)
			{
				asks.resize(children.size());
				for (size_t i = 0; i < children.size(); ++i)
					asks[i].PreferredSize = ResolveRect(children[i].Component->Rect, parentRect).Size();
			}

			// A scroll list lays its children out in a content rectangle at least as tall as the list, moved up by
			// the scroll offset; what falls outside the list is clipped.
			Rect contentRect = parentRect;
			Rect childClip;
			const Rect* clipForChildren = clip;
			if (scrolls)
			{
				const float viewHeight = parentRect.Size().y;
				const float contentHeight = std::max(PreferredGroupSize(group, asks).y, viewHeight);
				const float range = contentHeight - viewHeight;
				const float offset = std::clamp(widget->ScrollOffset, 0.0f, range);
				contentRect = { { parentRect.Min.x, parentRect.Max.y + offset - contentHeight }, { parentRect.Max.x, parentRect.Max.y + offset } };
				if (parentIndex != k_NoParent)
					canvas.Elements[parentIndex].ScrollRange = range;
				const Rect viewport{ parentRect.Min * canvas.Scale, parentRect.Max * canvas.Scale };
				childClip = clip ? Intersect(*clip, viewport) : viewport;
				clipForChildren = &childClip;
			}

			std::vector<Rect> rects(children.size());
			if (group.Kind != LayoutKind::None)
			{
				rects = LayoutChildren(group, contentRect, asks);
			}
			else
			{
				for (size_t i = 0; i < children.size(); ++i)
					rects[i] = ResolveRect(children[i].Component->Rect, contentRect);
			}

			for (size_t i = 0; i < children.size(); ++i)
			{
				ResolvedElement resolved;
				resolved.Element = children[i].Element;
				resolved.ScreenRect = { rects[i].Min * canvas.Scale, rects[i].Max * canvas.Scale };
				if (clipForChildren)
				{
					resolved.Clipped = true;
					resolved.Clip = *clipForChildren;
				}
				canvas.Elements.push_back(std::move(resolved));
				ResolveChildren(scene, children[i].Element, canvas.Elements.size() - 1, rects[i], children[i].Component->Layout, canvas, clipForChildren);
			}
		}

		Ref<Font> FindFont(AssetHandle handle)
		{
			TextComponent lookup;
			lookup.FontHandle = handle;
			return Font::GetFontAssetForTextComponent(lookup);
		}

		// UTF-8 editing on byte offsets.
		size_t PreviousCodepoint(const std::string& text, size_t index)
		{
			if (index == 0)
				return 0;
			size_t at = index - 1;
			while (at > 0 && (static_cast<uint8_t>(text[at]) & 0xC0) == 0x80)
				at--;
			return at;
		}

		size_t NextCodepoint(const std::string& text, size_t index)
		{
			if (index >= text.size())
				return text.size();
			size_t at = index + 1;
			while (at < text.size() && (static_cast<uint8_t>(text[at]) & 0xC0) == 0x80)
				at++;
			return at;
		}

		std::string EncodeUtf8(char32_t c)
		{
			std::string out;
			if (c < 0x80)
			{
				out += static_cast<char>(c);
			}
			else if (c < 0x800)
			{
				out += static_cast<char>(0xC0 | (c >> 6));
				out += static_cast<char>(0x80 | (c & 0x3F));
			}
			else if (c < 0x10000)
			{
				out += static_cast<char>(0xE0 | (c >> 12));
				out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (c & 0x3F));
			}
			else
			{
				out += static_cast<char>(0xF0 | (c >> 18));
				out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
				out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (c & 0x3F));
			}
			return out;
		}

		struct ElementIndex
		{
			const ResolvedElement* Resolved = nullptr;
			UIWidgetComponent* Widget = nullptr;
		};

		bool HitTest(const ResolvedElement& element, const glm::vec2& point)
		{
			return element.ScreenRect.Contains(point) && (!element.Clipped || element.Clip.Contains(point));
		}
	}

	std::vector<ResolvedCanvas> ResolveLayout(Scene& scene, const glm::vec2& screenSize)
	{
		std::vector<ResolvedCanvas> canvases;
		if (screenSize.x < 1.0f || screenSize.y < 1.0f)
			return canvases;

		for (auto entityID : scene.GetAllEntitiesWith<UICanvasComponent>())
		{
			Entity entity{ entityID, &scene };
			const UICanvasComponent& component = entity.GetComponent<UICanvasComponent>();
			if (!component.Enabled || !scene.IsEntityActiveInHierarchy(entity))
				continue;

			ResolvedCanvas canvas;
			canvas.Canvas = entity;
			canvas.Scale = CanvasScale(component.ScaleMode, component.ReferenceResolution, component.ScaleFactor, component.Match, screenSize);
			canvas.CanvasRect = CanvasRect(screenSize, canvas.Scale);
			// A canvas lays out its direct children like a group of no layout: each by its own anchors.
			ResolveChildren(scene, entity, k_NoParent, canvas.CanvasRect, LayoutGroup{}, canvas, nullptr);
			canvases.push_back(std::move(canvas));
		}

		std::stable_sort(canvases.begin(), canvases.end(), [](const ResolvedCanvas& a, const ResolvedCanvas& b)
		{
			return a.Canvas.GetComponent<UICanvasComponent>().SortOrder < b.Canvas.GetComponent<UICanvasComponent>().SortOrder;
		});
		return canvases;
	}

	void CaptureDrawItems(const std::vector<ResolvedCanvas>& canvases, const glm::vec2& screenSize, FrameRenderPacket& packet)
	{
		using Draw2DItem = FrameRenderPacket::Draw2DItem;
		packet.UIScreenSize = screenSize;

		// One quad, cut to the clip rectangle of a scroll list with the texture rectangle cut to match.
		const auto emitQuad = [&packet](const ResolvedElement& element, Rect rect, const glm::vec4& color, const Ref<Texture2D>& texture,
			glm::vec2 uv0, glm::vec2 uv1, bool pointFilter)
		{
			if (rect.Size().x <= 0.0f || rect.Size().y <= 0.0f || color.a <= 0.0f)
				return;
			if (element.Clipped)
			{
				const Rect cut = Intersect(rect, element.Clip);
				if (cut.Max.x <= cut.Min.x || cut.Max.y <= cut.Min.y)
					return;
				const glm::vec2 size = rect.Size();
				const glm::vec2 from = (cut.Min - rect.Min) / size, to = (cut.Max - rect.Min) / size;
				const glm::vec2 u0 = uv0, u1 = uv1;
				uv0 = u0 + (u1 - u0) * from;
				uv1 = u0 + (u1 - u0) * to;
				rect = cut;
			}
			Draw2DItem item;
			item.Transform = glm::translate(glm::mat4(1.0f), glm::vec3(rect.Center(), 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(rect.Size(), 1.0f));
			item.Color = color;
			item.Texture = texture;
			item.Type = texture ? Draw2DItem::Kind::TexturedQuad : Draw2DItem::Kind::Quad;
			item.PointFilter = pointFilter;
			item.UV0 = uv0;
			item.UV1 = uv1;
			packet.UI.push_back(std::move(item));
		};

		struct TextBlock
		{
			bool Valid = false;
			float Left = 0.0f, Top = 0.0f, EmPixels = 0.0f;
			glm::vec2 Size{ 0.0f }; // pixels
			Ref<Font> FontAsset;
		};

		// Lays a string out inside a rectangle by the text's alignment and, unless it is outside a scroll list's clip,
		// emits it. Text is only drawn whole: a line half out of a scroll list is left out.
		const auto emitText = [&packet](const ResolvedElement& element, const UITextComponent& text, const std::string& content,
			const glm::vec4& color, float scale) -> TextBlock
		{
			TextBlock block;
			block.FontAsset = FindFont(text.FontHandle);
			block.EmPixels = text.Size * scale;
			if (!block.FontAsset || block.EmPixels <= 0.0f)
				return block;

			const Rect& rect = element.ScreenRect;
			const float wrapWidth = text.Wrap ? rect.Size().x / block.EmPixels : 1.0e9f;
			const glm::vec2 measured = Renderer2D::MeasureString(content, block.FontAsset, wrapWidth, text.LineSpacing, 0.0f);
			block.Size = measured * block.EmPixels;

			block.Left = rect.Min.x;
			if (text.AlignH == UITextAlignH::Center)
				block.Left = rect.Center().x - block.Size.x * 0.5f;
			else if (text.AlignH == UITextAlignH::Right)
				block.Left = rect.Max.x - block.Size.x;

			block.Top = rect.Max.y;
			if (text.AlignV == UITextAlignV::Middle)
				block.Top = rect.Center().y + block.Size.y * 0.5f;
			else if (text.AlignV == UITextAlignV::Bottom)
				block.Top = rect.Min.y + block.Size.y;
			block.Valid = true;

			if (content.empty())
				return block;
			if (element.Clipped)
			{
				const Rect bounds{ { block.Left, block.Top - block.Size.y }, { block.Left + block.Size.x, block.Top } };
				if (bounds.Min.x < element.Clip.Min.x || bounds.Min.y < element.Clip.Min.y || bounds.Max.x > element.Clip.Max.x || bounds.Max.y > element.Clip.Max.y)
					return block;
			}

			Draw2DItem item;
			item.Type = Draw2DItem::Kind::Text;
			item.Transform = glm::translate(glm::mat4(1.0f), glm::vec3(block.Left, block.Top - Renderer2D::GetStringAscent(block.FontAsset) * block.EmPixels, 0.0f))
				* glm::scale(glm::mat4(1.0f), glm::vec3(block.EmPixels));
			item.Color = color;
			item.FontAsset = block.FontAsset;
			item.Text = content;
			item.MaxWidth = wrapWidth;
			item.LineSpacing = text.LineSpacing;
			packet.UI.push_back(std::move(item));
			return block;
		};

		for (const ResolvedCanvas& canvas : canvases)
		{
			for (const ResolvedElement& resolved : canvas.Elements)
			{
				const Rect& rect = resolved.ScreenRect;
				const glm::vec2 size = rect.Size();
				const UIWidgetComponent* widget = FindWidget(resolved.Element);

				glm::vec4 tint(1.0f);
				if (widget)
				{
					if (!widget->Interactable)
						tint = widget->DisabledTint;
					else if (widget->Pressed)
						tint = widget->PressedTint;
					else if (widget->Hovered)
						tint = widget->HoverTint;
				}

				if (resolved.Element.HasComponent<UIImageComponent>())
				{
					const UIImageComponent& image = resolved.Element.GetComponent<UIImageComponent>();
					const Ref<Texture2D> texture = image.Texture ? AssetManager::GetAsset<Texture2D>(image.Texture) : nullptr;
					const glm::vec4 color = image.Color * tint;
					const glm::vec4& border = image.SliceBorder;
					if (texture && (border.x > 0.0f || border.y > 0.0f || border.z > 0.0f || border.w > 0.0f))
					{
						// 9-slice: corners keep their size, edges and middle stretch. Texture v grows upward like the quad.
						const glm::vec2 texels = glm::max(glm::vec2(texture->GetSize()), glm::vec2(1.0f));
						const float pixels = image.SliceScale * canvas.Scale;
						float left = border.x * pixels, right = border.z * pixels, bottom = border.y * pixels, top = border.w * pixels;
						if (left + right > size.x)
						{
							const float k = size.x / (left + right);
							left *= k;
							right *= k;
						}
						if (bottom + top > size.y)
						{
							const float k = size.y / (bottom + top);
							bottom *= k;
							top *= k;
						}
						const float xs[4] = { rect.Min.x, rect.Min.x + left, rect.Max.x - right, rect.Max.x };
						const float ys[4] = { rect.Min.y, rect.Min.y + bottom, rect.Max.y - top, rect.Max.y };
						const float us[4] = { 0.0f, border.x / texels.x, 1.0f - border.z / texels.x, 1.0f };
						const float vs[4] = { 0.0f, border.y / texels.y, 1.0f - border.w / texels.y, 1.0f };
						for (int row = 0; row < 3; ++row)
						{
							for (int column = 0; column < 3; ++column)
								emitQuad(resolved, { { xs[column], ys[row] }, { xs[column + 1], ys[row + 1] } }, color, texture, { us[column], vs[row] }, { us[column + 1], vs[row + 1] }, image.PointFilter);
						}
					}
					else
					{
						emitQuad(resolved, rect, color, texture, glm::vec2(0.0f), glm::vec2(1.0f), image.PointFilter);
					}
				}

				const glm::vec4 accent = widget ? widget->AccentColor * glm::vec4(glm::vec3(tint), tint.a) : glm::vec4(1.0f);

				// The parts a widget draws itself.
				if (widget && size.x > 0.0f && size.y > 0.0f)
				{
					const float span = widget->MaxValue - widget->MinValue;
					const float fraction = span > 0.0f ? std::clamp((widget->Value - widget->MinValue) / span, 0.0f, 1.0f) : 0.0f;
					switch (widget->Kind)
					{
						case UIWidgetKind::Toggle:
						{
							if (widget->Checked)
							{
								const float inset = std::min(size.x, size.y) * 0.22f;
								emitQuad(resolved, { rect.Min + glm::vec2(inset), rect.Max - glm::vec2(inset) }, accent, nullptr, glm::vec2(0.0f), glm::vec2(1.0f), false);
							}
							break;
						}
						case UIWidgetKind::ProgressBar:
						{
							const float inset = size.y * 0.12f;
							const float width = (size.x - 2.0f * inset) * fraction;
							emitQuad(resolved, { rect.Min + glm::vec2(inset), { rect.Min.x + inset + width, rect.Max.y - inset } }, accent, nullptr, glm::vec2(0.0f), glm::vec2(1.0f), false);
							break;
						}
						case UIWidgetKind::Slider:
						{
							const float handleWidth = size.y * 0.5f;
							const float centerX = rect.Min.x + handleWidth * 0.5f + (size.x - handleWidth) * fraction;
							const float inset = size.y * 0.3f;
							emitQuad(resolved, { { rect.Min.x + inset, rect.Min.y + inset }, { centerX, rect.Max.y - inset } }, accent, nullptr, glm::vec2(0.0f), glm::vec2(1.0f), false);
							emitQuad(resolved, { { centerX - handleWidth * 0.5f, rect.Min.y }, { centerX + handleWidth * 0.5f, rect.Max.y } },
								glm::vec4(1.0f) * glm::vec4(glm::vec3(tint), tint.a), nullptr, glm::vec2(0.0f), glm::vec2(1.0f), false);
							break;
						}
						default:
							break;
					}
				}

				if (resolved.Element.HasComponent<UITextComponent>())
				{
					const UITextComponent& text = resolved.Element.GetComponent<UITextComponent>();
					const bool input = widget && widget->Kind == UIWidgetKind::TextInput;
					const bool showPlaceholder = input && text.Text.empty() && !widget->Focused && !widget->Placeholder.empty();
					glm::vec4 color = text.Color;
					if (widget && !widget->Interactable)
						color *= widget->DisabledTint;
					if (showPlaceholder)
						color.a *= 0.5f;

					const TextBlock block = emitText(resolved, text, showPlaceholder ? widget->Placeholder : text.Text, color, canvas.Scale);

					if (input && widget->Focused && block.Valid)
					{
						// A blinking caret at the caret index: the width of the text before it.
						const size_t caret = widget->CaretIndex < 0 ? text.Text.size() : std::min<size_t>(widget->CaretIndex, text.Text.size());
						const float before = caret == 0 ? 0.0f : Renderer2D::MeasureString(text.Text.substr(0, caret), block.FontAsset, 1.0e9f, text.LineSpacing, 0.0f).x * block.EmPixels;
						if (std::fmod(widget->CaretTime, 1.0f) < 0.6f)
						{
							const float x = block.Left + before;
							const float height = block.EmPixels;
							emitQuad(resolved, { { x, block.Top - height }, { x + std::max(2.0f, canvas.Scale * 2.0f), block.Top } }, accent, nullptr, glm::vec2(0.0f), glm::vec2(1.0f), false);
						}
					}
				}
			}
		}
	}

	std::vector<Event> UpdateInteraction(Scene& scene, InteractionState& state, const glm::vec2& screenSize, const FrameInput& input)
	{
		std::vector<Event> events;
		std::vector<ResolvedCanvas> canvases = ResolveLayout(scene, screenSize);
		const Pointer& mouse = input.Mouse;

		// Every widget, topmost last: later canvases and later elements draw over earlier ones.
		std::vector<ElementIndex> widgets;
		for (ResolvedCanvas& canvas : canvases)
		{
			for (const ResolvedElement& element : canvas.Elements)
			{
				Entity entity = element.Element; // a mutable handle: the widget's runtime state changes below
				if (entity.HasComponent<UIWidgetComponent>())
					widgets.push_back({ &element, &entity.GetComponent<UIWidgetComponent>() });
			}
		}

		for (ElementIndex& entry : widgets)
			entry.Widget->Hovered = false;

		const auto findById = [&widgets](UUID id) -> ElementIndex*
		{
			for (ElementIndex& entry : widgets)
			{
				if (entry.Resolved->Element.GetUUID() == id)
					return &entry;
			}
			return nullptr;
		};

		ElementIndex* hovered = nullptr;
		if (mouse.Present)
		{
			for (auto it = widgets.rbegin(); it != widgets.rend() && !hovered; ++it)
			{
				if (HitTest(*it->Resolved, mouse.Position))
					hovered = &*it;
			}
		}
		if (hovered)
			hovered->Widget->Hovered = true;

		const bool down = mouse.Down && mouse.Present;
		const bool pressedEdge = down && !state.WasDown;
		const bool releasedEdge = !mouse.Down && state.WasDown;
		const bool usable = hovered && hovered->Widget->Interactable;

		if (pressedEdge)
		{
			state.Pressed = usable ? hovered->Resolved->Element.GetUUID() : UUID(0);
			const UUID previousFocus = state.Focus;
			state.Focus = state.Pressed;
			if (state.Focus != previousFocus)
				state.CaretTime = 0.0f;
			if (usable && hovered->Widget->Kind == UIWidgetKind::TextInput && state.Focus != previousFocus)
				hovered->Widget->CaretIndex = -1;
		}

		const auto setSliderFromPointer = [&](ElementIndex& entry)
		{
			UIWidgetComponent& widget = *entry.Widget;
			const Rect& rect = entry.Resolved->ScreenRect;
			const float handleWidth = rect.Size().y * 0.5f;
			const float span = rect.Size().x - handleWidth;
			float fraction = span > 0.0f ? (mouse.Position.x - (rect.Min.x + handleWidth * 0.5f)) / span : 0.0f;
			fraction = std::clamp(fraction, 0.0f, 1.0f);
			float value = widget.MinValue + (widget.MaxValue - widget.MinValue) * fraction;
			if (widget.WholeNumbers)
				value = std::clamp(std::round(value), std::min(widget.MinValue, widget.MaxValue), std::max(widget.MinValue, widget.MaxValue));
			if (value != widget.Value)
			{
				widget.Value = value;
				events.push_back({ EventKind::ValueChanged, entry.Resolved->Element.GetUUID(), value, false });
			}
		};

		if (state.Pressed)
		{
			ElementIndex* pressed = findById(state.Pressed);
			if (!pressed || !pressed->Widget->Interactable)
				state.Pressed = 0;
			else if (down && pressed->Widget->Kind == UIWidgetKind::Slider)
				setSliderFromPointer(*pressed);
		}

		if (releasedEdge || (!mouse.Present && state.WasDown))
		{
			if (state.Pressed && hovered && hovered->Resolved->Element.GetUUID() == state.Pressed && releasedEdge)
			{
				UIWidgetComponent& widget = *hovered->Widget;
				const UUID id = hovered->Resolved->Element.GetUUID();
				if (widget.Kind == UIWidgetKind::Button)
				{
					events.push_back({ EventKind::Click, id, 0.0f, false });
				}
				else if (widget.Kind == UIWidgetKind::Toggle)
				{
					widget.Checked = !widget.Checked;
					events.push_back({ EventKind::Toggled, id, 0.0f, widget.Checked });
				}
			}
			state.Pressed = 0;
		}
		state.WasDown = mouse.Down;

		// The wheel scrolls the topmost scroll list under the pointer; every list's offset stays inside its range.
		if (mouse.Present && mouse.Scroll != 0.0f)
		{
			for (auto it = widgets.rbegin(); it != widgets.rend(); ++it)
			{
				if (it->Widget->Kind == UIWidgetKind::ScrollList && it->Widget->Interactable && HitTest(*it->Resolved, mouse.Position))
				{
					it->Widget->ScrollOffset -= mouse.Scroll * k_ScrollNotchUnits;
					break;
				}
			}
		}
		for (ElementIndex& entry : widgets)
		{
			if (entry.Widget->Kind == UIWidgetKind::ScrollList)
				entry.Widget->ScrollOffset = std::clamp(entry.Widget->ScrollOffset, 0.0f, entry.Resolved->ScrollRange);
			entry.Widget->Pressed = state.Pressed != 0 && entry.Resolved->Element.GetUUID() == state.Pressed && down;
		}

		// Text editing: the focused text input takes the typed characters and the editing keys.
		ElementIndex* focused = state.Focus ? findById(state.Focus) : nullptr;
		if (focused && (!focused->Widget->Interactable || !focused->Resolved->Element.HasComponent<UITextComponent>()))
			focused = nullptr;
		for (ElementIndex& entry : widgets)
			entry.Widget->Focused = &entry == focused;
		if (!focused)
		{
			state.Focus = 0;
			return events;
		}

		UIWidgetComponent& widget = *focused->Widget;
		if (widget.Kind != UIWidgetKind::TextInput)
			return events;

		const UUID id = focused->Resolved->Element.GetUUID();
		Entity focusedEntity = focused->Resolved->Element;
		std::string& text = focusedEntity.GetComponent<UITextComponent>().Text;
		size_t caret = widget.CaretIndex < 0 ? text.size() : std::min<size_t>(widget.CaretIndex, text.size());
		bool changed = false, moved = false;

		for (char32_t character : input.Typed)
		{
			if (character < 32 || character == 127)
				continue;
			const std::string encoded = EncodeUtf8(character);
			if (widget.MaxLength > 0 && text.size() + encoded.size() > static_cast<size_t>(widget.MaxLength))
				continue;
			text.insert(caret, encoded);
			caret += encoded.size();
			changed = moved = true;
		}
		const Keys& keys = input.Edit;
		if (keys.Backspace && caret > 0)
		{
			const size_t from = PreviousCodepoint(text, caret);
			text.erase(from, caret - from);
			caret = from;
			changed = moved = true;
		}
		if (keys.Delete && caret < text.size())
		{
			text.erase(caret, NextCodepoint(text, caret) - caret);
			changed = moved = true;
		}
		if (keys.Left && caret > 0)
		{
			caret = PreviousCodepoint(text, caret);
			moved = true;
		}
		if (keys.Right && caret < text.size())
		{
			caret = NextCodepoint(text, caret);
			moved = true;
		}
		if (keys.Home)
		{
			caret = 0;
			moved = true;
		}
		if (keys.End)
		{
			caret = text.size();
			moved = true;
		}
		widget.CaretIndex = static_cast<int32_t>(caret);
		if (changed)
			events.push_back({ EventKind::TextChanged, id, 0.0f, false });
		if (keys.Enter)
			events.push_back({ EventKind::Submit, id, 0.0f, false });
		if (keys.Escape)
			state.Focus = 0;

		state.CaretTime = moved ? 0.0f : state.CaretTime + input.DeltaTime;
		widget.CaretTime = state.CaretTime;
		if (!state.Focus)
			widget.Focused = false;
		return events;
	}

	Keys ReadEditKeys(float deltaTime, float (&heldTime)[8])
	{
		constexpr float k_RepeatDelay = 0.4f, k_RepeatPeriod = 0.04f;
		const auto acts = [&](int slot, KeyCode key, bool repeats)
		{
			if (Input::IsKeyPressed(key))
			{
				heldTime[slot] = 0.0f;
				return true;
			}
			if (!Input::IsKeyDown(key))
			{
				heldTime[slot] = 0.0f;
				return false;
			}
			if (!repeats)
				return false;
			const float before = heldTime[slot];
			heldTime[slot] += deltaTime;
			return heldTime[slot] > k_RepeatDelay && std::floor((heldTime[slot] - k_RepeatDelay) / k_RepeatPeriod) > std::floor(std::max(before - k_RepeatDelay, 0.0f) / k_RepeatPeriod);
		};

		Keys keys;
		keys.Backspace = acts(0, KeyCode::Backspace, true);
		keys.Delete = acts(1, KeyCode::Delete, true);
		keys.Left = acts(2, KeyCode::Left, true);
		keys.Right = acts(3, KeyCode::Right, true);
		keys.Home = acts(4, KeyCode::Home, false);
		keys.End = acts(5, KeyCode::End, false);
		keys.Enter = acts(6, KeyCode::Enter, false) || Input::IsKeyPressed(KeyCode::KPEnter);
		keys.Escape = acts(7, KeyCode::Escape, false);
		return keys;
	}
}
