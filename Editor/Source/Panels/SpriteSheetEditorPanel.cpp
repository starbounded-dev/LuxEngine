// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "SpriteSheetEditorPanel.h"

#include "Lux/Asset/AssetImporter.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiCore.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/ImGuiUtilities.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/SpriteSheet.h"
#include "Lux/Renderer/Texture.h"
#include "Lux/Utilities/StringUtils.h"

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

namespace Lux
{
	namespace
	{
		constexpr float kMinZoom = 0.25f;
		constexpr float kMaxZoom = 16.0f;
		constexpr float kPreviewSize = 128.0f;

		std::string FramesToText(const std::vector<uint32_t>& frames)
		{
			std::string text;
			for (size_t i = 0; i < frames.size(); i++)
			{
				if (i)
					text += ", ";
				text += std::to_string(frames[i]);
			}
			return text;
		}

		// "0, 1, 4-6" -> 0 1 4 5 6. Returns false for text that is not a list of indices below frameCount.
		bool TextToFrames(const std::string& text, size_t frameCount, std::vector<uint32_t>& out)
		{
			out.clear();
			const char* cursor = text.data();
			const char* const end = text.data() + text.size();
			const auto skipSeparators = [&]()
			{
				while (cursor < end && (*cursor == ',' || *cursor == ' ' || *cursor == '\t'))
					++cursor;
			};
			const auto readNumber = [&](uint32_t& value)
			{
				const auto result = std::from_chars(cursor, end, value);
				if (result.ec != std::errc())
					return false;
				cursor = result.ptr;
				return true;
			};

			skipSeparators();
			while (cursor < end)
			{
				uint32_t first = 0;
				if (!readNumber(first))
					return false;
				uint32_t last = first;
				if (cursor < end && *cursor == '-')
				{
					++cursor;
					if (!readNumber(last) || last < first)
						return false;
				}
				if (last >= frameCount || out.size() + (last - first + 1) > SpriteSheet::MaxClipFrames)
					return false;
				for (uint32_t frame = first; frame <= last; frame++)
					out.push_back(frame);
				skipSeparators();
			}
			return true;
		}
	}

	void SpriteSheetEditorPanel::OpenSheet(AssetHandle sheet)
	{
		m_Sheet = sheet;
		m_RequestFocus = true;
		m_SelectedFrame = -1;
		m_SelectedClip = -1;
		m_ClipFramesTextClip = -2;
		m_Error.clear();
	}

	void SpriteSheetEditorPanel::OnProjectChanged(const Ref<Project>&)
	{
		OnClose();
		m_Sheet = 0;
		m_SelectedFrame = m_SelectedClip = -1;
		m_ClipFramesTextClip = -2;
		m_Error.clear();
	}

	void SpriteSheetEditorPanel::OnImGuiRender(bool& isOpen)
	{
		if (!isOpen)
			return;
		if (m_RequestFocus)
		{
			ImGui::SetNextWindowFocus();
			m_RequestFocus = false;
		}
		ImGui::SetNextWindowSize(ImVec2(980.0f, 620.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Sprite Sheet Editor", &isOpen))
		{
			ImGui::End();
			return;
		}

		Ref<SpriteSheet> sheet = m_Sheet && Project::GetEditorAssetManager() && AssetManager::GetAssetType(m_Sheet) == AssetType::SpriteSheet
			? AssetManager::GetAsset<SpriteSheet>(m_Sheet) : nullptr;
		if (!sheet)
		{
			ImGui::TextDisabled("Double-click a sprite sheet (.lsheet) in the Content Browser to edit it.");
			ImGui::End();
			return;
		}

		const Ref<Texture2D> texture = sheet->Texture ? AssetManager::GetAsset<Texture2D>(sheet->Texture) : nullptr;
		const glm::ivec2 textureSize = texture ? glm::ivec2(texture->GetSize()) : glm::ivec2(0);
		bool changed = false;
		const int32_t frameCount = static_cast<int32_t>(sheet->Frames.size());
		m_SelectedFrame = std::min(m_SelectedFrame, frameCount - 1);
		m_SelectedClip = std::min(m_SelectedClip, static_cast<int32_t>(sheet->Clips.size()) - 1);

		if (!m_Error.empty())
		{
			ImGuiEx::ScopedColour errorColour(ImGuiCol_Text, Colors::Theme::textError);
			ImGui::TextWrapped("%s", m_Error.c_str());
		}

		if (ImGui::BeginTable("##sprite_sheet_layout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
		{
			ImGui::TableSetupColumn("Image", ImGuiTableColumnFlags_WidthStretch, 1.4f);
			ImGui::TableSetupColumn("Settings", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableNextRow();

			// ---- The texture with the frame rectangles over it --------------------------------------------
			ImGui::TableSetColumnIndex(0);
			ImGui::SliderFloat("Zoom##sheet_zoom", &m_Zoom, kMinZoom, kMaxZoom, "%.2fx", ImGuiSliderFlags_Logarithmic);
			if (ImGui::BeginChild("##sheet_image", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
			{
				if (!texture)
					ImGui::TextDisabled("Choose a texture in the settings.");
				else
				{
					const ImVec2 size(textureSize.x * m_Zoom, textureSize.y * m_Zoom);
					const ImVec2 origin = ImGui::GetCursorScreenPos();
					// The texture is uploaded with its bottom row first, so it is drawn with v flipped.
					ImGui::Image(ImGuiEx::GetTextureID(texture), size, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
					const bool hovered = ImGui::IsItemHovered();
					ImDrawList* drawList = ImGui::GetWindowDrawList();
					for (int32_t i = 0; i < frameCount; i++)
					{
						const glm::ivec4& rect = sheet->Frames[static_cast<size_t>(i)].Rect;
						const ImVec2 min(origin.x + rect.x * m_Zoom, origin.y + rect.y * m_Zoom);
						const ImVec2 max(min.x + rect.z * m_Zoom, min.y + rect.w * m_Zoom);
						const bool selected = i == m_SelectedFrame;
						drawList->AddRect(min, max, selected ? Colors::Theme::accent : Colors::Theme::muted, 0.0f, 0, selected ? 2.0f : 1.0f);
					}
					if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						const ImVec2 mouse = ImGui::GetMousePos();
						const glm::vec2 texel((mouse.x - origin.x) / m_Zoom, (mouse.y - origin.y) / m_Zoom);
						for (int32_t i = frameCount - 1; i >= 0; i--)
						{
							const glm::ivec4& rect = sheet->Frames[static_cast<size_t>(i)].Rect;
							if (texel.x >= rect.x && texel.x < rect.x + rect.z && texel.y >= rect.y && texel.y < rect.y + rect.w)
							{
								m_SelectedFrame = i;
								break;
							}
						}
					}
				}
			}
			ImGui::EndChild();

			// ---- Settings ------------------------------------------------------------------------------------
			ImGui::TableSetColumnIndex(1);
			if (ImGui::BeginChild("##sheet_settings", ImVec2(0.0f, 0.0f)))
			{
				ImGuiEx::BeginPropertyGrid();
				AssetHandle textureHandle = sheet->Texture;
				if (ImGuiEx::PropertyAssetReference<Texture2D>("Texture", textureHandle, "The image the frames are cut from.", nullptr, {}, false))
				{
					sheet->Texture = textureHandle;
					changed = true;
				}
				if (ImGuiEx::Property("Pixels Per Unit", sheet->PixelsPerUnit, 1.0f, 1.0f, 4096.0f, "A frame's world size is its size in texels divided by this.", false))
					changed = true;
				ImGuiEx::EndPropertyGrid();

				if (ImGui::CollapsingHeader("Grid", ImGuiTreeNodeFlags_DefaultOpen))
				{
					changed |= ImGui::DragInt2("Cell Size##sheet_cell", &sheet->CellSize.x, 1.0f, 1, 16384);
					changed |= ImGui::DragInt2("Spacing##sheet_spacing", &sheet->Spacing.x, 1.0f, 0, 16384);
					changed |= ImGui::DragInt2("Margin##sheet_margin", &sheet->Margin.x, 1.0f, 0, 16384);
					ImGuiEx::ScopedDisable noTexture(!texture);
					if (ImGui::Button("Cut Into Frames##sheet_slice"))
					{
						sheet->SliceGrid(textureSize);
						m_SelectedFrame = std::min(m_SelectedFrame, static_cast<int32_t>(sheet->Frames.size()) - 1);
						changed = true;
					}
					ImGui::SetItemTooltip("Replaces the frame rectangles with this grid. Frames keep their name, pivot and Solid flag by index.");
				}

				if (ImGui::CollapsingHeader("Frames", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::Text("%d frames", frameCount);
					if (ImGui::BeginChild("##sheet_frame_list", ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 7.0f), ImGuiChildFlags_Borders))
					{
						ImGuiListClipper clipper;
						clipper.Begin(frameCount);
						while (clipper.Step())
						{
							for (int32_t i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
							{
								const std::string label = std::format("{}: {}##frame_{}", i, sheet->Frames[static_cast<size_t>(i)].Name, i);
								if (ImGui::Selectable(label.c_str(), i == m_SelectedFrame))
									m_SelectedFrame = i;
							}
						}
					}
					ImGui::EndChild();

					if (m_SelectedFrame >= 0)
					{
						SpriteFrame& frame = sheet->Frames[static_cast<size_t>(m_SelectedFrame)];
						ImGuiEx::BeginPropertyGrid();
						changed |= ImGuiEx::Property("Name", frame.Name, "", false);
						changed |= ImGuiEx::Property("Pivot", frame.Pivot, 0.01f, 0.0f, 1.0f, "Where the sprite is anchored: (0,0) bottom left, (1,1) top right.", false);
						changed |= ImGuiEx::Property("Solid", frame.Solid, "A tile that blocks movement: tilemap collision uses it.", false);
						ImGuiEx::EndPropertyGrid();
						changed |= ImGui::DragInt4("Rect##frame_rect", &frame.Rect.x, 1.0f, 0, 16384);
					}
				}

				if (ImGui::CollapsingHeader("Clips", ImGuiTreeNodeFlags_DefaultOpen))
				{
					if (ImGui::Button("Add Clip##sheet_add_clip") && sheet->Clips.size() < SpriteSheet::MaxClips)
					{
						SpriteClip clip;
						for (size_t number = sheet->Clips.size() + 1;; number++)
						{
							clip.Name = "Clip " + std::to_string(number);
							if (sheet->FindClip(clip.Name) < 0)
								break;
						}
						if (m_SelectedFrame >= 0)
							clip.Frames.push_back(static_cast<uint32_t>(m_SelectedFrame));
						sheet->Clips.push_back(std::move(clip));
						m_SelectedClip = static_cast<int32_t>(sheet->Clips.size()) - 1;
						changed = true;
					}
					ImGui::SameLine();
					{
						ImGuiEx::ScopedDisable noClip(m_SelectedClip < 0);
						if (ImGui::Button("Remove Clip##sheet_remove_clip"))
						{
							sheet->Clips.erase(sheet->Clips.begin() + m_SelectedClip);
							m_SelectedClip = std::min(m_SelectedClip, static_cast<int32_t>(sheet->Clips.size()) - 1);
							changed = true;
						}
					}

					for (int32_t i = 0; i < static_cast<int32_t>(sheet->Clips.size()); i++)
					{
						const std::string label = std::format("{}##clip_{}", sheet->Clips[static_cast<size_t>(i)].Name, i);
						if (ImGui::Selectable(label.c_str(), i == m_SelectedClip))
							m_SelectedClip = i;
					}

					if (m_SelectedClip >= 0)
					{
						SpriteClip& clip = sheet->Clips[static_cast<size_t>(m_SelectedClip)];
						ImGuiEx::BeginPropertyGrid();
						changed |= ImGuiEx::Property("Clip Name", clip.Name, "", false);
						changed |= ImGuiEx::Property("Frames Per Second", clip.FramesPerSecond, 0.5f, 0.01f, 1000.0f, "", false);
						const char* wraps[] = { "Once", "Loop", "Ping Pong" };
						int32_t wrap = static_cast<int32_t>(clip.Wrap);
						if (ImGuiEx::PropertyDropdown("Wrap", wraps, 3, &wrap, "", false))
						{
							clip.Wrap = static_cast<SpriteWrapMode>(wrap);
							changed = true;
						}
						ImGuiEx::EndPropertyGrid();

						// The frame list is edited as text ("0, 1, 4-6") and read back when the field is left.
						if (m_ClipFramesTextClip != m_SelectedClip)
						{
							m_ClipFramesText = FramesToText(clip.Frames);
							m_ClipFramesTextClip = m_SelectedClip;
						}
						ImGui::InputText("Frames##clip_frames", &m_ClipFramesText);
						if (ImGui::IsItemDeactivatedAfterEdit())
						{
							std::vector<uint32_t> frames;
							if (TextToFrames(m_ClipFramesText, sheet->Frames.size(), frames))
							{
								clip.Frames = std::move(frames);
								changed = true;
							}
							else
								m_Error = "Frames: type frame numbers below " + std::to_string(sheet->Frames.size()) + ", like 0, 1, 4-6.";
							m_ClipFramesText = FramesToText(clip.Frames);
						}
						ImGui::SetItemTooltip("Frame numbers in play order, ranges allowed: 0, 1, 4-6");
						{
							ImGuiEx::ScopedDisable noFrame(m_SelectedFrame < 0 || clip.Frames.size() >= SpriteSheet::MaxClipFrames);
							if (ImGui::Button("Append Selected Frame##clip_append"))
							{
								clip.Frames.push_back(static_cast<uint32_t>(m_SelectedFrame));
								m_ClipFramesText = FramesToText(clip.Frames);
								changed = true;
							}
						}

						// A looping preview of the clip.
						if (texture && !clip.Frames.empty())
						{
							bool finished = false;
							const float elapsed = static_cast<float>(ImGui::GetTime());
							const float time = clip.Wrap == SpriteWrapMode::Once ? std::fmod(elapsed, SpriteSheet::GetClipDuration(clip) + 0.5f) : elapsed;
							const uint32_t frame = SpriteSheet::SampleClip(clip, time, finished);
							glm::vec2 uv0, uv1;
							if (sheet->GetFrameUV(frame, textureSize, uv0, uv1))
							{
								const glm::vec2 frameSize(sheet->Frames[frame].Rect.z, sheet->Frames[frame].Rect.w);
								const float scale = kPreviewSize / std::max(frameSize.x, frameSize.y);
								ImGui::Image(ImGuiEx::GetTextureID(texture), ImVec2(frameSize.x * scale, frameSize.y * scale), ImVec2(uv0.x, uv1.y), ImVec2(uv1.x, uv0.y));
							}
						}
					}
				}
			}
			ImGui::EndChild();
			ImGui::EndTable();
		}

		if (changed)
		{
			m_Error.clear();
			std::string error;
			if (!sheet->Validate(&error))
				m_Error = "Not saved: " + error + ".";
			else if (!AssetImporter::TrySerialize(sheet.As<Asset>()))
				m_Error = "Saving " + Utils::PathToUTF8(Project::GetEditorAssetManager()->GetMetadata(m_Sheet).FilePath) + " failed. See the Log.";
		}

		ImGui::End();
	}
}
