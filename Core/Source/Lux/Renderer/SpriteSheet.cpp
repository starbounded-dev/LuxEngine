// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "SpriteSheet.h"

#include "Lux/Asset/AssetReference.h"
#include "Lux/Serialization/Yaml.h"

#include <algorithm>
#include <cmath>

namespace Lux
{
	namespace
	{
		constexpr int kVersion = 1;
		constexpr int32_t kMaxTextureSize = 16384;
		constexpr float kMinFramesPerSecond = 0.01f;
		constexpr float kMaxFramesPerSecond = 1000.0f;

		const char* WrapToString(SpriteWrapMode mode)
		{
			switch (mode)
			{
				case SpriteWrapMode::Once: return "Once";
				case SpriteWrapMode::PingPong: return "PingPong";
				default: return "Loop";
			}
		}

		// Integer vectors are written as [x, y(, z, w)]; Yaml only converts float vectors.
		template<glm::length_t L>
		void WriteInts(Yaml::Writer& out, const glm::vec<L, int, glm::defaultp>& value)
		{
			out << Yaml::Flow << Yaml::BeginSeq;
			for (glm::length_t i = 0; i < L; ++i)
				out << value[i];
			out << Yaml::EndSeq;
		}

		template<glm::length_t L>
		glm::vec<L, int, glm::defaultp> ReadInts(const Yaml::Node& node, const glm::vec<L, int, glm::defaultp>& fallback)
		{
			if (!node || !node.IsSequence() || node.size() != static_cast<size_t>(L))
				return fallback;
			glm::vec<L, int, glm::defaultp> value(0);
			glm::length_t i = 0;
			for (const auto& element : node)
				value[i++] = element.as<int>(0);
			return value;
		}

		SpriteWrapMode WrapFromString(const std::string& text)
		{
			if (text == "Once")
				return SpriteWrapMode::Once;
			if (text == "PingPong")
				return SpriteWrapMode::PingPong;
			return SpriteWrapMode::Loop;
		}
	}

	void SpriteSheet::SliceGrid(glm::ivec2 textureSize)
	{
		const glm::ivec2 cell = glm::max(CellSize, glm::ivec2(1));
		const glm::ivec2 spacing = glm::max(Spacing, glm::ivec2(0));
		const glm::ivec2 margin = glm::max(Margin, glm::ivec2(0));
		const glm::ivec2 columnsRows = glm::max((textureSize - margin + spacing) / (cell + spacing), glm::ivec2(0));

		std::vector<SpriteFrame> previous = std::move(Frames);
		Frames.clear();
		const int64_t count = std::min<int64_t>(static_cast<int64_t>(columnsRows.x) * columnsRows.y, MaxFrames);
		Frames.reserve(static_cast<size_t>(count));
		for (int64_t index = 0; index < count; index++)
		{
			SpriteFrame frame;
			if (static_cast<size_t>(index) < previous.size())
			{
				frame.Name = previous[static_cast<size_t>(index)].Name;
				frame.Pivot = previous[static_cast<size_t>(index)].Pivot;
				frame.Solid = previous[static_cast<size_t>(index)].Solid;
			}
			if (frame.Name.empty())
				frame.Name = "frame_" + std::to_string(index);
			const int32_t column = static_cast<int32_t>(index % columnsRows.x);
			const int32_t row = static_cast<int32_t>(index / columnsRows.x);
			frame.Rect = { margin.x + column * (cell.x + spacing.x), margin.y + row * (cell.y + spacing.y), cell.x, cell.y };
			Frames.push_back(std::move(frame));
		}
	}

	bool SpriteSheet::GetFrameUV(uint32_t frame, glm::ivec2 textureSize, glm::vec2& uv0, glm::vec2& uv1) const
	{
		if (frame >= Frames.size() || textureSize.x <= 0 || textureSize.y <= 0)
			return false;

		const glm::ivec4& rect = Frames[frame].Rect;
		const glm::vec2 size(textureSize);
		uv0 = { rect.x / size.x, 1.0f - (rect.y + rect.w) / size.y };
		uv1 = { (rect.x + rect.z) / size.x, 1.0f - rect.y / size.y };
		return true;
	}

	glm::vec2 SpriteSheet::GetFrameWorldSize(uint32_t frame) const
	{
		if (frame >= Frames.size() || PixelsPerUnit <= 0.0f)
			return { 1.0f, 1.0f };
		return glm::vec2(Frames[frame].Rect.z, Frames[frame].Rect.w) / PixelsPerUnit;
	}

	int32_t SpriteSheet::FindClip(std::string_view name) const
	{
		for (size_t i = 0; i < Clips.size(); i++)
		{
			if (Clips[i].Name == name)
				return static_cast<int32_t>(i);
		}
		return -1;
	}

	float SpriteSheet::GetClipDuration(const SpriteClip& clip)
	{
		const size_t count = clip.Frames.size();
		if (count == 0 || clip.FramesPerSecond <= 0.0f)
			return 0.0f;
		const size_t steps = clip.Wrap == SpriteWrapMode::PingPong && count > 1 ? 2 * count - 2 : count;
		return static_cast<float>(steps) / clip.FramesPerSecond;
	}

	uint32_t SpriteSheet::SampleClip(const SpriteClip& clip, float time, bool& finished)
	{
		finished = false;
		const size_t count = clip.Frames.size();
		if (count == 0)
		{
			finished = true;
			return 0;
		}

		const double step = std::floor(static_cast<double>(std::max(time, 0.0f)) * std::max(clip.FramesPerSecond, kMinFramesPerSecond));
		size_t index = 0;
		switch (clip.Wrap)
		{
			case SpriteWrapMode::Once:
				index = step >= static_cast<double>(count) ? count - 1 : static_cast<size_t>(step);
				finished = step >= static_cast<double>(count);
				break;
			case SpriteWrapMode::Loop:
				index = static_cast<size_t>(std::fmod(step, static_cast<double>(count)));
				break;
			case SpriteWrapMode::PingPong:
			{
				if (count == 1)
					break;
				const size_t period = 2 * count - 2;
				const size_t position = static_cast<size_t>(std::fmod(step, static_cast<double>(period)));
				index = position < count ? position : period - position;
				break;
			}
		}
		return clip.Frames[index];
	}

	bool SpriteSheet::Validate(std::string* error) const
	{
		const auto fail = [error](const char* message)
		{
			if (error)
				*error = message;
			return false;
		};
		if (!(PixelsPerUnit > 0.0f) || !std::isfinite(PixelsPerUnit))
			return fail("pixels per unit must be above zero");
		if (CellSize.x < 1 || CellSize.y < 1 || CellSize.x > kMaxTextureSize || CellSize.y > kMaxTextureSize)
			return fail("the cell size must be between 1 and 16384");
		if (Spacing.x < 0 || Spacing.y < 0 || Margin.x < 0 || Margin.y < 0)
			return fail("spacing and margin cannot be negative");
		if (Frames.size() > MaxFrames)
			return fail("a sheet holds at most 16384 frames");
		for (const SpriteFrame& frame : Frames)
		{
			if (frame.Rect.z < 1 || frame.Rect.w < 1 || frame.Rect.x < 0 || frame.Rect.y < 0
				|| frame.Rect.x + frame.Rect.z > kMaxTextureSize || frame.Rect.y + frame.Rect.w > kMaxTextureSize)
				return fail("a frame has an empty or out-of-range rectangle");
			if (!std::isfinite(frame.Pivot.x) || !std::isfinite(frame.Pivot.y))
				return fail("a frame has a pivot that is not a number");
		}
		if (Clips.size() > MaxClips)
			return fail("a sheet holds at most 256 clips");
		for (size_t i = 0; i < Clips.size(); i++)
		{
			const SpriteClip& clip = Clips[i];
			if (clip.Name.empty())
				return fail("a clip has no name");
			for (size_t other = 0; other < i; other++)
			{
				if (Clips[other].Name == clip.Name)
					return fail("two clips share a name");
			}
			if (clip.Frames.size() > MaxClipFrames)
				return fail("a clip holds at most 4096 frames");
			if (!(clip.FramesPerSecond >= kMinFramesPerSecond && clip.FramesPerSecond <= kMaxFramesPerSecond))
				return fail("a clip's frames per second must be between 0.01 and 1000");
			for (const uint32_t frame : clip.Frames)
			{
				if (frame >= Frames.size())
					return fail("a clip names a frame the sheet does not have");
			}
		}
		return true;
	}

	std::string SpriteSheet::ToYAML() const
	{
		Yaml::Writer out;
		out << Yaml::BeginMap << Yaml::Key << "SpriteSheet" << Yaml::Value << Yaml::BeginMap;
		out << Yaml::Key << "Version" << Yaml::Value << kVersion;
		out << Yaml::Key << "Texture" << Yaml::Value;
		AssetReference::WriteValue(out, Texture);
		out << Yaml::Key << "PixelsPerUnit" << Yaml::Value << PixelsPerUnit;
		out << Yaml::Key << "CellSize" << Yaml::Value;
		WriteInts(out, CellSize);
		out << Yaml::Key << "Spacing" << Yaml::Value;
		WriteInts(out, Spacing);
		out << Yaml::Key << "Margin" << Yaml::Value;
		WriteInts(out, Margin);

		out << Yaml::Key << "Frames" << Yaml::Value << Yaml::BeginSeq;
		for (const SpriteFrame& frame : Frames)
		{
			out << Yaml::BeginMap;
			out << Yaml::Key << "Name" << Yaml::Value << frame.Name;
			out << Yaml::Key << "Rect" << Yaml::Value;
			WriteInts(out, frame.Rect);
			out << Yaml::Key << "Pivot" << Yaml::Value << frame.Pivot;
			if (frame.Solid)
				out << Yaml::Key << "Solid" << Yaml::Value << true;
			out << Yaml::EndMap;
		}
		out << Yaml::EndSeq;

		out << Yaml::Key << "Clips" << Yaml::Value << Yaml::BeginSeq;
		for (const SpriteClip& clip : Clips)
		{
			out << Yaml::BeginMap;
			out << Yaml::Key << "Name" << Yaml::Value << clip.Name;
			out << Yaml::Key << "FramesPerSecond" << Yaml::Value << clip.FramesPerSecond;
			out << Yaml::Key << "Wrap" << Yaml::Value << WrapToString(clip.Wrap);
			out << Yaml::Key << "Frames" << Yaml::Value << Yaml::BeginSeq;
			for (const uint32_t frame : clip.Frames)
				out << frame;
			out << Yaml::EndSeq;
			out << Yaml::EndMap;
		}
		out << Yaml::EndSeq;

		out << Yaml::EndMap << Yaml::EndMap;
		return out.c_str();
	}

	bool SpriteSheet::FromYAML(const std::string& text)
	{
		try
		{
			const auto node = Yaml::Load(text)["SpriteSheet"];
			if (!node || !node.IsMap())
				throw std::runtime_error("missing SpriteSheet map");
			if (node["Version"].as<int>(kVersion) != kVersion)
				throw std::runtime_error("unsupported version");

			SpriteSheet parsed;
			parsed.Texture = AssetReference::ReadValue(node["Texture"]);
			parsed.PixelsPerUnit = node["PixelsPerUnit"].as<float>(100.0f);
			parsed.CellSize = ReadInts(node["CellSize"], glm::ivec2(16));
			parsed.Spacing = ReadInts(node["Spacing"], glm::ivec2(0));
			parsed.Margin = ReadInts(node["Margin"], glm::ivec2(0));

			if (const auto frames = node["Frames"])
			{
				if (!frames.IsSequence() || frames.size() > MaxFrames)
					throw std::runtime_error("Frames must be a list of at most 16384 frames");
				for (const auto& frameNode : frames)
				{
					SpriteFrame frame;
					frame.Name = frameNode["Name"].as<std::string>("");
					frame.Rect = ReadInts(frameNode["Rect"], glm::ivec4(0));
					frame.Pivot = frameNode["Pivot"].as<glm::vec2>(glm::vec2(0.5f));
					frame.Solid = frameNode["Solid"].as<bool>(false);
					parsed.Frames.push_back(std::move(frame));
				}
			}
			if (const auto clips = node["Clips"])
			{
				if (!clips.IsSequence() || clips.size() > MaxClips)
					throw std::runtime_error("Clips must be a list of at most 256 clips");
				for (const auto& clipNode : clips)
				{
					SpriteClip clip;
					clip.Name = clipNode["Name"].as<std::string>("");
					clip.FramesPerSecond = clipNode["FramesPerSecond"].as<float>(12.0f);
					clip.Wrap = WrapFromString(clipNode["Wrap"].as<std::string>("Loop"));
					if (const auto clipFrames = clipNode["Frames"])
					{
						if (!clipFrames.IsSequence() || clipFrames.size() > MaxClipFrames)
							throw std::runtime_error("a clip's Frames must be a list of at most 4096 indices");
						for (const auto& frame : clipFrames)
							clip.Frames.push_back(frame.as<uint32_t>(0));
					}
					parsed.Clips.push_back(std::move(clip));
				}
			}

			std::string error;
			if (!parsed.Validate(&error))
				throw std::runtime_error(error);
			Texture = parsed.Texture;
			PixelsPerUnit = parsed.PixelsPerUnit;
			CellSize = parsed.CellSize;
			Spacing = parsed.Spacing;
			Margin = parsed.Margin;
			Frames = std::move(parsed.Frames);
			Clips = std::move(parsed.Clips);
			return true;
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR_TAG("Assets", "Invalid sprite sheet: {}", error.what());
			return false;
		}
	}
}
