// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "UISystem.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Renderer/FrameRenderPacket.h"
#include "Lux/Renderer/Renderer2D.h"
#include "Lux/Renderer/UI/Font.h"
#include "Lux/Scene/Components.h"
#include "Lux/Scene/Scene.h"

#include <algorithm>

namespace Lux::UI
{
	namespace
	{
		struct Child
		{
			Entity Element;
			const UIElementComponent* Component;
		};

		void ResolveChildren(Scene& scene, Entity parent, const Rect& parentRect, const LayoutGroup& group, ResolvedCanvas& canvas)
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

			std::vector<Rect> rects(children.size());
			if (group.Kind != LayoutKind::None)
			{
				std::vector<LayoutChild> asks(children.size());
				for (size_t i = 0; i < children.size(); ++i)
				{
					asks[i].PreferredSize = ResolveRect(children[i].Component->Rect, parentRect).Size();
					asks[i].Flex = 0.0f;
				}
				rects = LayoutChildren(group, parentRect, asks);
			}
			else
			{
				for (size_t i = 0; i < children.size(); ++i)
					rects[i] = ResolveRect(children[i].Component->Rect, parentRect);
			}

			for (size_t i = 0; i < children.size(); ++i)
			{
				canvas.Elements.push_back({ children[i].Element, { rects[i].Min * canvas.Scale, rects[i].Max * canvas.Scale } });
				ResolveChildren(scene, children[i].Element, rects[i], children[i].Component->Layout, canvas);
			}
		}

		Ref<Font> FindFont(AssetHandle handle)
		{
			TextComponent lookup;
			lookup.FontHandle = handle;
			return Font::GetFontAssetForTextComponent(lookup);
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
			ResolveChildren(scene, entity, canvas.CanvasRect, LayoutGroup{}, canvas);
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

		for (const ResolvedCanvas& canvas : canvases)
		{
			for (const ResolvedElement& resolved : canvas.Elements)
			{
				const Rect& rect = resolved.ScreenRect;
				const glm::vec2 size = rect.Size();

				if (resolved.Element.HasComponent<UIImageComponent>() && size.x > 0.0f && size.y > 0.0f)
				{
					const UIImageComponent& image = resolved.Element.GetComponent<UIImageComponent>();
					Draw2DItem item;
					item.Transform = glm::translate(glm::mat4(1.0f), glm::vec3(rect.Center(), 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(size, 1.0f));
					item.Color = image.Color;
					item.Texture = image.Texture ? AssetManager::GetAsset<Texture2D>(image.Texture) : nullptr;
					item.Type = item.Texture ? Draw2DItem::Kind::TexturedQuad : Draw2DItem::Kind::Quad;
					item.PointFilter = image.PointFilter;
					packet.UI.push_back(std::move(item));
				}

				if (resolved.Element.HasComponent<UITextComponent>())
				{
					const UITextComponent& text = resolved.Element.GetComponent<UITextComponent>();
					const Ref<Font> font = FindFont(text.FontHandle);
					const float emPixels = text.Size * canvas.Scale;
					if (text.Text.empty() || !font || emPixels <= 0.0f)
						continue;

					// Text lays out in em units, one em tall, then the transform scales it to pixels.
					const float wrapWidth = text.Wrap ? size.x / emPixels : 1.0e9f;
					const glm::vec2 measured = Renderer2D::MeasureString(text.Text, font, wrapWidth, text.LineSpacing, 0.0f);
					const glm::vec2 block = measured * emPixels;

					float left = rect.Min.x;
					if (text.AlignH == UITextAlignH::Center)
						left = rect.Center().x - block.x * 0.5f;
					else if (text.AlignH == UITextAlignH::Right)
						left = rect.Max.x - block.x;

					float top = rect.Max.y;
					if (text.AlignV == UITextAlignV::Middle)
						top = rect.Center().y + block.y * 0.5f;
					else if (text.AlignV == UITextAlignV::Bottom)
						top = rect.Min.y + block.y;

					Draw2DItem item;
					item.Type = Draw2DItem::Kind::Text;
					item.Transform = glm::translate(glm::mat4(1.0f), glm::vec3(left, top - Renderer2D::GetStringAscent(font) * emPixels, 0.0f))
						* glm::scale(glm::mat4(1.0f), glm::vec3(emPixels));
					item.Color = text.Color;
					item.FontAsset = font;
					item.Text = text.Text;
					item.MaxWidth = wrapWidth;
					item.LineSpacing = text.LineSpacing;
					packet.UI.push_back(std::move(item));
				}
			}
		}
	}
}
