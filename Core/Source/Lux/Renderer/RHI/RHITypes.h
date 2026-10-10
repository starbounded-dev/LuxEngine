// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <cstdint>
#include <string_view>

// Renderer vocabulary that engine, editor and serialization code may name. Backend types (NVRHI
// today, NRI after the migration) stay inside renderer .cpp files; NVRHIInterop.h converts.
//
// Values mirror NVRHI's exactly. Shader reflection and ShaderPack files store ShaderStage raw, and
// the shader cache stores its string form, so neither the values nor the strings may change.

#define LUX_RHI_ENUM_FLAG_OPERATORS(T) \
	constexpr T operator|(T a, T b) { return T(uint32_t(a) | uint32_t(b)); } \
	constexpr T operator&(T a, T b) { return T(uint32_t(a) & uint32_t(b)); } \
	constexpr T operator~(T a) { return T(~uint32_t(a)); } \
	constexpr T& operator|=(T& a, T b) { return a = a | b; } \
	constexpr T& operator&=(T& a, T b) { return a = a & b; } \
	constexpr bool operator!(T a) { return uint32_t(a) == 0; } \
	constexpr bool operator==(T a, uint32_t b) { return uint32_t(a) == b; } \
	constexpr bool operator!=(T a, uint32_t b) { return uint32_t(a) != b; }

namespace Lux {

	enum class ShaderStage : uint16_t
	{
		None = 0x0000,

		Compute = 0x0020,

		Vertex = 0x0001,
		Hull = 0x0002,
		Domain = 0x0004,
		Geometry = 0x0008,
		Pixel = 0x0010,
		Amplification = 0x0040,
		Mesh = 0x0080,
		AllGraphics = 0x00DF,

		RayGeneration = 0x0100,
		AnyHit = 0x0200,
		ClosestHit = 0x0400,
		Miss = 0x0800,
		Intersection = 0x1000,
		Callable = 0x2000,
		AllRayTracing = 0x3F00,

		All = 0x3FFF,
	};

	LUX_RHI_ENUM_FLAG_OPERATORS(ShaderStage)

	// Strings are the shader cache's on-disk spelling. Unknown strings map to None.
	const char* ShaderStageToString(ShaderStage stage);
	ShaderStage ShaderStageFromString(std::string_view string);

	enum class TextureDimension : uint8_t
	{
		Unknown,
		Texture1D,
		Texture1DArray,
		Texture2D,
		Texture2DArray,
		TextureCube,
		TextureCubeArray,
		Texture2DMS,
		Texture2DMSArray,
		Texture3D
	};

	// Unlike nvrhi::TextureSubresourceSet, a default range covers every mip and layer.
	struct TextureSubresourceRange
	{
		static constexpr uint32_t AllMips = ~0u;
		static constexpr uint32_t AllLayers = ~0u;

		uint32_t BaseMip = 0;
		uint32_t MipCount = AllMips;
		uint32_t BaseLayer = 0;
		uint32_t LayerCount = AllLayers;

		constexpr bool operator==(const TextureSubresourceRange&) const = default;
	};

	inline constexpr TextureSubresourceRange AllSubresources{};

	// Descriptor-set numbers a pipeline layout has at most (0 to MaxDescriptorSets - 1).
	inline constexpr uint32_t MaxDescriptorSets = 8;

	// Same order as nvrhi::CommandQueue.
	enum class GPUQueue : uint8_t
	{
		Graphics = 0,
		Compute,
		Copy,

		Count
	};

	enum class ResourceState : uint32_t
	{
		Unknown = 0,
		Common = 0x00000001,
		ConstantBuffer = 0x00000002,
		VertexBuffer = 0x00000004,
		IndexBuffer = 0x00000008,
		IndirectArgument = 0x00000010,
		ShaderResource = 0x00000020,
		UnorderedAccess = 0x00000040,
		RenderTarget = 0x00000080,
		DepthWrite = 0x00000100,
		DepthRead = 0x00000200,
		StreamOut = 0x00000400,
		CopyDest = 0x00000800,
		CopySource = 0x00001000,
		ResolveDest = 0x00002000,
		ResolveSource = 0x00004000,
		Present = 0x00008000,
		AccelStructRead = 0x00010000,
		AccelStructWrite = 0x00020000,
		AccelStructBuildInput = 0x00040000,
		AccelStructBuildBlas = 0x00080000,
		ShadingRateSurface = 0x00100000,
		OpacityMicromapWrite = 0x00200000,
		OpacityMicromapBuildInput = 0x00400000,
		ConvertCoopVecMatrixInput = 0x00800000,
		ConvertCoopVecMatrixOutput = 0x01000000,
	};

	LUX_RHI_ENUM_FLAG_OPERATORS(ResourceState)

	// The GPU's indexed indirect-draw record (VkDrawIndexedIndirectCommand layout).
	struct DrawIndexedIndirectCommand
	{
		uint32_t IndexCount = 0;
		uint32_t InstanceCount = 1;
		uint32_t FirstIndex = 0;
		int32_t VertexOffset = 0;
		uint32_t FirstInstance = 0;
	};

	static_assert(sizeof(DrawIndexedIndirectCommand) == 20);

	// Byte-for-byte stand-in for the VkDescriptorBufferInfo that shader reflection used to serialize.
	// Kept only so cached reflection and ShaderPack files keep their layout; nothing reads it.
	struct LegacyDescriptorBufferInfo
	{
		uint64_t Buffer = 0;
		uint64_t Offset = 0;
		uint64_t Range = 0;
	};

	static_assert(sizeof(LegacyDescriptorBufferInfo) == 24);

}
