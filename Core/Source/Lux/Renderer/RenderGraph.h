// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Renderer/Image.h"
#include "Lux/Renderer/StorageBufferSet.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace Lux {

	class RenderCommandBuffer;

	class RenderGraph
	{
	public:
		using ResourceHandle = uint32_t;
		using ExecuteCallback = std::function<void()>;
		static constexpr ResourceHandle InvalidResource = UINT32_MAX;
		// Buffers share the handle space with textures: a buffer handle is its index with this bit
		// set. Texture handles stay equal to their index into GetTextures().
		static constexpr ResourceHandle BufferHandleBit = 1u << 30;
		static constexpr bool IsBufferHandle(ResourceHandle resource) { return resource != InvalidResource && (resource & BufferHandleBit) != 0; }

		// How a pass touches a resource. Decides whether the access counts as a read and/or a write
		// for culling, lifetimes and aliasing, and which ResourceState the resource must be in.
		enum class AccessKind : uint8_t
		{
			SampledRead = 0,
			StorageRead,       // read-only storage: UAV for a texture, SRV for a buffer
			StorageWrite,
			StorageReadWrite,
			ColorWrite,
			ColorReadWrite,    // load + store
			DepthWrite,
			DepthReadWrite,    // depth test against existing contents, and write
			DepthReadOnly,     // read-only depth attachment
			CopySource,
			CopyDest,
			IndirectArgs,
			UniformRead
		};

		struct ResourceAccess
		{
			ResourceHandle Resource = InvalidResource;
			AccessKind Kind = AccessKind::SampledRead;
			TextureSubresourceRange Range = AllSubresources;
		};

		static bool AccessReads(AccessKind kind);
		static bool AccessWrites(AccessKind kind);
		static ResourceState AccessState(AccessKind kind, bool isBuffer);

		enum class PassFlags : uint32_t
		{
			None = 0,
			Graphics = BIT(0),
			Compute = BIT(1),
			Transfer = BIT(2),
			SideEffect = BIT(3),
			NeverCull = BIT(4),
			// The pass works only on resources the graph does not model (SSBOs/UBOs,
			// synchronized manually inside the pass), so empty Reads/Writes are expected.
			UntrackedResources = BIT(5)
		};

		enum class DiagnosticSeverity : uint8_t
		{
			Info = 0,
			Warning,
			Error
		};

		enum class DiagnosticCode : uint32_t
		{
			InvalidResource = 0,
			NullTexture,
			InvalidTextureDesc,
			ReadBeforeWrite,
			UnwrittenExternalRead,
			DeadWrite,
			ReadWriteSameResource,
			DuplicatePassName,
			DuplicateTextureName,
			InvalidPassFlags,
			EmptyExecutablePass,
			EmptyMetadataPass,
			AliasLifetimeConflict,
			AliasIncompatibleResource,
			NullBuffer,
			// Debug, explicit barriers on: a pass required a graph resource it did not declare.
			UndeclaredAccess
		};

		struct Diagnostic
		{
			DiagnosticSeverity Severity = DiagnosticSeverity::Info;
			DiagnosticCode Code = DiagnosticCode::InvalidResource;
			uint32_t PassIndex = UINT32_MAX;
			std::string PassName;
			ResourceHandle Resource = InvalidResource;
			std::string ResourceName;
			std::string Message;
		};

		struct TextureDesc
		{
			std::string Name;
			Ref<Image2D> Image;
			ImageFormat Format = ImageFormat::RGBA;
			ImageUsage Usage = ImageUsage::Attachment;
			TextureDimension Dimension = TextureDimension::Texture2D;
			uint32_t Width = 1;
			uint32_t Height = 1;
			uint32_t Mips = 1;
			uint32_t Layers = 1;
			bool Transient = true;
			bool AllowAlias = true;
		};

		// A GPU buffer the graph orders passes around. Always external: never transient, never aliased.
		// Exactly one of Set (resolved per frame on the render thread) and Buffer is expected.
		struct BufferDesc
		{
			std::string Name;
			Ref<StorageBufferSet> Set;
			Ref<StorageBuffer> Buffer;
		};

		struct PassDesc
		{
			std::string Name;
			std::vector<ResourceHandle> Reads;
			std::vector<ResourceHandle> Writes;
			PassFlags Flags = PassFlags::None;
			ExecuteCallback Execute;
			// Always-set pointer to the pass's string-literal name (zero-alloc,
			// unlike Name which is skipped on the per-frame executable path).
			// Used for crash diagnostics in Execute(). Kept after Flags/Execute so
			// the positional aggregate initializers in the self-tests still map to
			// Name/Reads/Writes/Flags.
			const char* DebugName = nullptr;
			// How the pass touches each resource. AddPass adds every access to Reads/Writes, so a pass
			// that declares Accesses need not list its resources twice.
			std::vector<ResourceAccess> Accesses;
		};

		struct ResourceLifetime
		{
			ResourceHandle Resource = InvalidResource;
			uint32_t FirstPass = UINT32_MAX;
			uint32_t LastPass = 0;
			uint32_t AliasIndex = UINT32_MAX;
		};

		struct AliasGroupSummary
		{
			uint32_t AliasIndex = UINT32_MAX;
			std::vector<ResourceHandle> Resources;
			bool Compatible = true;
		};

		struct CompileResult
		{
			std::vector<ResourceLifetime> Lifetimes;
			std::vector<uint32_t> ExecutionOrder;
			std::vector<uint32_t> CulledPasses;
			std::vector<Diagnostic> Diagnostics;
			std::vector<bool> PassNeededMask;
			std::vector<uint32_t> ResourceFirstWriter;
			std::vector<uint32_t> ResourceLastReader;
			std::vector<std::vector<uint32_t>> ResourceConsumers;
			std::vector<AliasGroupSummary> AliasGroups;

			// Requirements Execute() issues at each pass's entry, batched into one commit: the
			// declared accesses of every resource the pass uses in a single state. Pass i's entries are
			// [EntryRequirementOffsets[i], EntryRequirementOffsets[i + 1]).
			struct EntryRequirement
			{
				ResourceHandle Resource = InvalidResource;
				TextureSubresourceRange Range = AllSubresources;
				ResourceState State = ResourceState::Unknown;
			};
			std::vector<EntryRequirement> EntryRequirements;
			std::vector<uint32_t> EntryRequirementOffsets;

			uint32_t ErrorCount = 0;
			uint32_t WarningCount = 0;
			uint32_t InfoCount = 0;
			bool Valid = true;
		};

		void Reset();
		void AddDiagnostic(Diagnostic diagnostic);
		ResourceHandle AddTransientTexture(const TextureDesc& desc);
		ResourceHandle AddExternalBuffer(const BufferDesc& desc);
		uint32_t AddPass(PassDesc desc);
		uint32_t AddPass(PassDesc desc, ExecuteCallback execute);
		CompileResult Compile() const;
		// Folds every field Compile()/Execute() depend on (pass topology + texture
		// metadata) into one key. Equal hashes ⇒ equivalent CompileResult, so callers
		// may cache and reuse a compiled result while this value is unchanged.
		uint64_t ComputeStructureHash() const;
		CompileResult Execute() const;
		void Execute(const CompileResult& compileResult) const;
		// Also requires each pass's declared accesses on `commandBuffer` before the pass runs (emitted
		// only with explicit barriers). In Debug it checks, per pass, for requirements on graph
		// resources the pass did not declare (see GetRuntimeDiagnostics).
		void Execute(const CompileResult& compileResult, const Ref<RenderCommandBuffer>& commandBuffer) const;
		std::vector<ResourceLifetime> BuildAliasPlan() const;
		static bool RunValidationSelfTests(std::vector<std::string>* failures = nullptr);

		const std::vector<TextureDesc>& GetTextures() const { return m_Textures; }
		const std::vector<BufferDesc>& GetBuffers() const { return m_Buffers; }
		const std::vector<PassDesc>& GetPasses() const { return m_Passes; }

		// Per-resource arrays in CompileResult (Lifetimes, ResourceFirstWriter, ...) hold textures first
		// (slot == texture handle), then buffers.
		size_t GetResourceCount() const { return m_Textures.size() + m_Buffers.size(); }
		uint32_t GetResourceSlot(ResourceHandle resource) const;

		// Debug: diagnostics found while executing (UndeclaredAccess), deduplicated across frames.
		std::vector<Diagnostic> GetRuntimeDiagnostics() const;

		static constexpr PassFlags CombineFlags(PassFlags lhs, PassFlags rhs)
		{
			return static_cast<PassFlags>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
		}

		static constexpr bool HasFlag(PassFlags flags, PassFlags flag)
		{
			return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
		}

	private:
		bool IsValidResource(ResourceHandle resource) const;
		bool IsTransient(ResourceHandle resource) const;
		std::string GetResourceName(ResourceHandle resource) const;

		static bool AreAliasCompatible(const TextureDesc& lhs, const TextureDesc& rhs);
		static void NormalizeResourceList(std::vector<ResourceHandle>& resources);
		std::vector<ResourceLifetime> BuildResourceLifetimes(std::vector<Diagnostic>* diagnostics = nullptr) const;

		std::vector<TextureDesc> m_Textures;
		std::vector<BufferDesc> m_Buffers;
		std::vector<PassDesc> m_Passes;
		std::vector<Diagnostic> m_ExternalDiagnostics;

		// Written on the render thread by Execute's Debug checks, read on the main thread; shared
		// by the submitted lambdas so it outlives a Reset() of this graph.
		struct RuntimeDiagnosticSink : public RefCounted
		{
			mutable std::mutex Mutex;
			std::vector<Diagnostic> Diagnostics;
		};
		Ref<RuntimeDiagnosticSink> m_RuntimeDiagnostics = Ref<RuntimeDiagnosticSink>::Create();
	};

}
