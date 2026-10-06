// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "NVRHIInterop.h"

#include "Lux/Renderer/Image.h"

#include <cstddef>

#include <vulkan/vulkan.h>

namespace Lux {

	// The casts in NVRHIInterop.h are only valid while every value matches.
#define LUX_RHI_ASSERT_SAME(LuxEnum, NVRHIEnum, LuxValue, NVRHIValue) \
	static_assert(static_cast<uint64_t>(LuxEnum::LuxValue) == static_cast<uint64_t>(NVRHIEnum::NVRHIValue), #LuxEnum "::" #LuxValue " must equal " #NVRHIEnum "::" #NVRHIValue)

	static_assert(sizeof(ShaderStage) == sizeof(nvrhi::ShaderType));
	LUX_RHI_ASSERT_SAME(ShaderStage, nvrhi::ShaderType, None, None);
	LUX_RHI_ASSERT_SAME(ShaderStage, nvrhi::ShaderType, Compute, Compute);
	LUX_RHI_ASSERT_SAME(ShaderStage, nvrhi::ShaderType, Vertex, Vertex);
	LUX_RHI_ASSERT_SAME(ShaderStage, nvrhi::ShaderType, Hull, Hull);
	LUX_RHI_ASSERT_SAME(ShaderStage, nvrhi::ShaderType, Domain, Domain);
	LUX_RHI_ASSERT_SAME(ShaderStage, nvrhi::ShaderType, Geometry, Geometry);
	LUX_RHI_ASSERT_SAME(ShaderStage, nvrhi::ShaderType, Pixel, Pixel);
	LUX_RHI_ASSERT_SAME(ShaderStage, nvrhi::ShaderType, Amplification, Amplification);
	LUX_RHI_ASSERT_SAME(ShaderStage, nvrhi::ShaderType, Mesh, Mesh);
	LUX_RHI_ASSERT_SAME(ShaderStage, nvrhi::ShaderType, AllGraphics, AllGraphics);
	LUX_RHI_ASSERT_SAME(ShaderStage, nvrhi::ShaderType, RayGeneration, RayGeneration);
	LUX_RHI_ASSERT_SAME(ShaderStage, nvrhi::ShaderType, AnyHit, AnyHit);
	LUX_RHI_ASSERT_SAME(ShaderStage, nvrhi::ShaderType, ClosestHit, ClosestHit);
	LUX_RHI_ASSERT_SAME(ShaderStage, nvrhi::ShaderType, Miss, Miss);
	LUX_RHI_ASSERT_SAME(ShaderStage, nvrhi::ShaderType, Intersection, Intersection);
	LUX_RHI_ASSERT_SAME(ShaderStage, nvrhi::ShaderType, Callable, Callable);
	LUX_RHI_ASSERT_SAME(ShaderStage, nvrhi::ShaderType, AllRayTracing, AllRayTracing);
	LUX_RHI_ASSERT_SAME(ShaderStage, nvrhi::ShaderType, All, All);

	static_assert(sizeof(TextureDimension) == sizeof(nvrhi::TextureDimension));
	LUX_RHI_ASSERT_SAME(TextureDimension, nvrhi::TextureDimension, Unknown, Unknown);
	LUX_RHI_ASSERT_SAME(TextureDimension, nvrhi::TextureDimension, Texture1D, Texture1D);
	LUX_RHI_ASSERT_SAME(TextureDimension, nvrhi::TextureDimension, Texture1DArray, Texture1DArray);
	LUX_RHI_ASSERT_SAME(TextureDimension, nvrhi::TextureDimension, Texture2D, Texture2D);
	LUX_RHI_ASSERT_SAME(TextureDimension, nvrhi::TextureDimension, Texture2DArray, Texture2DArray);
	LUX_RHI_ASSERT_SAME(TextureDimension, nvrhi::TextureDimension, TextureCube, TextureCube);
	LUX_RHI_ASSERT_SAME(TextureDimension, nvrhi::TextureDimension, TextureCubeArray, TextureCubeArray);
	LUX_RHI_ASSERT_SAME(TextureDimension, nvrhi::TextureDimension, Texture2DMS, Texture2DMS);
	LUX_RHI_ASSERT_SAME(TextureDimension, nvrhi::TextureDimension, Texture2DMSArray, Texture2DMSArray);
	LUX_RHI_ASSERT_SAME(TextureDimension, nvrhi::TextureDimension, Texture3D, Texture3D);

	static_assert(sizeof(GPUQueue) == sizeof(nvrhi::CommandQueue));
	LUX_RHI_ASSERT_SAME(GPUQueue, nvrhi::CommandQueue, Graphics, Graphics);
	LUX_RHI_ASSERT_SAME(GPUQueue, nvrhi::CommandQueue, Compute, Compute);
	LUX_RHI_ASSERT_SAME(GPUQueue, nvrhi::CommandQueue, Copy, Copy);
	LUX_RHI_ASSERT_SAME(GPUQueue, nvrhi::CommandQueue, Count, Count);

	static_assert(sizeof(ResourceState) == sizeof(nvrhi::ResourceStates));
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, Unknown, Unknown);
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, Common, Common);
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, ConstantBuffer, ConstantBuffer);
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, VertexBuffer, VertexBuffer);
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, IndexBuffer, IndexBuffer);
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, IndirectArgument, IndirectArgument);
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, ShaderResource, ShaderResource);
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, UnorderedAccess, UnorderedAccess);
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, RenderTarget, RenderTarget);
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, DepthWrite, DepthWrite);
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, DepthRead, DepthRead);
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, StreamOut, StreamOut);
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, CopyDest, CopyDest);
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, CopySource, CopySource);
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, ResolveDest, ResolveDest);
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, ResolveSource, ResolveSource);
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, Present, Present);
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, AccelStructRead, AccelStructRead);
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, AccelStructWrite, AccelStructWrite);
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, AccelStructBuildInput, AccelStructBuildInput);
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, AccelStructBuildBlas, AccelStructBuildBlas);
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, ShadingRateSurface, ShadingRateSurface);
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, OpacityMicromapWrite, OpacityMicromapWrite);
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, OpacityMicromapBuildInput, OpacityMicromapBuildInput);
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, ConvertCoopVecMatrixInput, ConvertCoopVecMatrixInput);
	LUX_RHI_ASSERT_SAME(ResourceState, nvrhi::ResourceStates, ConvertCoopVecMatrixOutput, ConvertCoopVecMatrixOutput);

#undef LUX_RHI_ASSERT_SAME

	static_assert(TextureSubresourceRange::AllMips == nvrhi::TextureSubresourceSet::AllMipLevels);
	static_assert(TextureSubresourceRange::AllLayers == nvrhi::TextureSubresourceSet::AllArraySlices);

	// Indirect-draw buffers are written as DrawIndexedIndirectCommand and read by the GPU (and by
	// nvrhi's drawIndexedIndirect) with the VkDrawIndexedIndirectCommand layout.
	static_assert(sizeof(DrawIndexedIndirectCommand) == sizeof(nvrhi::DrawIndexedIndirectArguments));
	static_assert(sizeof(DrawIndexedIndirectCommand) == sizeof(VkDrawIndexedIndirectCommand));
	static_assert(offsetof(DrawIndexedIndirectCommand, IndexCount) == offsetof(VkDrawIndexedIndirectCommand, indexCount));
	static_assert(offsetof(DrawIndexedIndirectCommand, InstanceCount) == offsetof(VkDrawIndexedIndirectCommand, instanceCount));
	static_assert(offsetof(DrawIndexedIndirectCommand, FirstIndex) == offsetof(VkDrawIndexedIndirectCommand, firstIndex));
	static_assert(offsetof(DrawIndexedIndirectCommand, VertexOffset) == offsetof(VkDrawIndexedIndirectCommand, vertexOffset));
	static_assert(offsetof(DrawIndexedIndirectCommand, FirstInstance) == offsetof(VkDrawIndexedIndirectCommand, firstInstance));

	// Shader reflection serializes this where it used to write a VkDescriptorBufferInfo.
	static_assert(sizeof(LegacyDescriptorBufferInfo) == sizeof(VkDescriptorBufferInfo));
	static_assert(alignof(LegacyDescriptorBufferInfo) == alignof(VkDescriptorBufferInfo));

	nvrhi::SamplerAddressMode ToNVRHI(TextureWrap wrap)
	{
		switch (wrap)
		{
			case TextureWrap::Clamp:  return nvrhi::SamplerAddressMode::Clamp;
			case TextureWrap::Repeat: return nvrhi::SamplerAddressMode::Repeat;
			case TextureWrap::None:   break;
		}

		LUX_CORE_ASSERT(false, "Unknown wrap mode");
		return nvrhi::SamplerAddressMode::Clamp;
	}

}
