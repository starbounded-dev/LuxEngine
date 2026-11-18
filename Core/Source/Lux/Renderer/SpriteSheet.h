// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Asset/Asset.h"

#include <glm/glm.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace Lux
{
	enum class SpriteWrapMode : uint8_t
	{
		Once = 0,   // plays to the last frame and stays there
		Loop,
		PingPong    // forward, then back, without repeating the end frames
	};

	// One sprite inside a sheet's texture.
	struct SpriteFrame
	{
		std::string Name;
		// x, y, width, height in texels; y counts down from the top of the image, as in an image editor.
		glm::ivec4 Rect{ 0 };
		// Where the sprite is anchored inside its rectangle: (0,0) bottom left, (1,1) top right.
		glm::vec2 Pivot{ 0.5f, 0.5f };
		// Tile sets: the tile blocks movement (tilemap collision).
		bool Solid = false;
	};

	struct SpriteClip
	{
		std::string Name;
		std::vector<uint32_t> Frames; // indices into the sheet's frames
		float FramesPerSecond = 12.0f;
		SpriteWrapMode Wrap = SpriteWrapMode::Loop;
	};

	// A texture cut into sprites (.lsheet), the named animation clips that play them, and the same
	// frames used as the tiles of a tile set. Sheets are the unit the 2D renderer, the sprite animator
	// and tilemaps share. Lives inside the asset pack, with its texture.
	class SpriteSheet : public Asset
	{
	public:
		static AssetType GetStaticType() { return AssetType::SpriteSheet; }
		AssetType GetAssetType() const override { return GetStaticType(); }

		static constexpr size_t MaxFrames = 16384;
		static constexpr size_t MaxClips = 256;
		static constexpr size_t MaxClipFrames = 4096;
		static constexpr size_t MaxFileBytes = 4 * 1024 * 1024; // the serialized YAML, at save, load and in packs

		AssetHandle Texture = 0;
		// World size of a frame is its size in texels / PixelsPerUnit.
		float PixelsPerUnit = 100.0f;
		// The grid the frames were cut with, kept so the editor can cut again.
		glm::ivec2 CellSize{ 16, 16 };
		glm::ivec2 Spacing{ 0, 0 };
		glm::ivec2 Margin{ 0, 0 };
		std::vector<SpriteFrame> Frames;
		std::vector<SpriteClip> Clips;

		// Replaces the frames with the grid's cells, left to right then top to bottom; cells that would
		// cross the image edge are dropped. A frame that keeps its index keeps its name, pivot and Solid.
		void SliceGrid(glm::ivec2 textureSize);

		// Texture coordinates of a frame: uv0 is the bottom left, uv1 the top right, v = 0 at the
		// bottom of the image (how the 2D renderer samples a texture).
		bool GetFrameUV(uint32_t frame, glm::ivec2 textureSize, glm::vec2& uv0, glm::vec2& uv1) const;

		// Size of a frame in world units, before the entity's scale.
		glm::vec2 GetFrameWorldSize(uint32_t frame) const;

		int32_t FindClip(std::string_view name) const;

		// The frame to show `time` seconds into a clip, after its wrap mode. finished is set for a Once
		// clip that has played to its end.
		static uint32_t SampleClip(const SpriteClip& clip, float time, bool& finished);
		static float GetClipDuration(const SpriteClip& clip);

		// Structural checks only; whether Texture names a texture is the asset system's to say.
		bool Validate(std::string* error = nullptr) const;
		std::string ToYAML() const;
		// Replaces this sheet's settings, or leaves them unchanged and reports why it failed.
		bool FromYAML(const std::string& text);
	};
}
