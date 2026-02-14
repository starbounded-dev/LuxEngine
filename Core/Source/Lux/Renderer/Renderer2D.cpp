#include "lpch.h"
#include "Lux/Renderer/Renderer2D.h"

#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/Shader.h"
#include "Lux/Renderer/UniformBuffer.h"
#include "Lux/Renderer/VertexBuffer.h"
#include "Lux/Renderer/IndexBuffer.h"
#include "Lux/Renderer/Pipeline.h"
#include "Lux/Renderer/RenderPass.h"
#include "Lux/Renderer/Texture.h"
#include "Lux/Renderer/SwapChainFramebuffer.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Renderer/UI/Font.h"
#include "Lux/Renderer/UI/MSDFData.h"
#include "Lux/Core/Application.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Lux {

	struct QuadVertex
	{
		glm::vec3 Position;
		glm::vec4 Color;
		glm::vec2 TexCoord;
		float TexIndex;
		float TilingFactor;
		int EntityID;
	};

	struct TextVertex
	{
		glm::vec3 Position;
		glm::vec4 Color;
		glm::vec2 TexCoord;
		int EntityID;
	};

	struct CircleVertex
	{
		glm::vec3 WorldPosition;
		glm::vec3 LocalPosition;
		glm::vec4 Color;
		float Thickness;
		float Fade;
		int EntityID;
	};

	struct LineVertex
	{
		glm::vec3 Position;
		glm::vec4 Color;
		int EntityID;
	};

	struct Renderer2DData
	{
		static const uint32_t MaxQuads = 20000;
		static const uint32_t MaxVertices = MaxQuads * 4;
		static const uint32_t MaxIndices = MaxQuads * 6;
		static const uint32_t MaxTextureSlots = 32;

		Ref<VertexBuffer> QuadVertexBuffer;
		Ref<VertexBuffer> CircleVertexBuffer;
		Ref<VertexBuffer> LineVertexBuffer;
		Ref<VertexBuffer> TextVertexBuffer;
		Ref<IndexBuffer> QuadIndexBuffer;

		Ref<Shader> QuadShader;
		Ref<Shader> CircleShader;
		Ref<Shader> LineShader;
		Ref<Shader> TextShader;

		Ref<Texture2D> WhiteTexture;

		uint32_t QuadIndexCount = 0;
		QuadVertex* QuadVertexBufferBase = nullptr;
		QuadVertex* QuadVertexBufferPtr = nullptr;

		uint32_t CircleIndexCount = 0;
		CircleVertex* CircleVertexBufferBase = nullptr;
		CircleVertex* CircleVertexBufferPtr = nullptr;

		uint32_t LineVertexCount = 0;
		LineVertex* LineVertexBufferBase = nullptr;
		LineVertex* LineVertexBufferPtr = nullptr;

		uint32_t TextIndexCount = 0;
		TextVertex* TextVertexBufferBase = nullptr;
		TextVertex* TextVertexBufferPtr = nullptr;

		float LineWidth = 2.0f;

		std::array<Ref<Texture2D>, MaxTextureSlots> TextureSlots;
		uint32_t TextureSlotIndex = 1;

		Ref<Texture2D> FontAtlasTexture;

		glm::vec4 QuadVertexPositions[4];

		Renderer2D::Statistics Stats;

		struct CameraData
		{
			glm::mat4 ViewProjection;
		};
		CameraData CameraBuffer;
		Ref<UniformBuffer> CameraUniformBuffer;

		Ref<RenderCommandBuffer> CurrentCommandBuffer;
		Ref<Framebuffer> CurrentFramebuffer;

		std::unordered_map<size_t, Ref<RenderPass>> PipelineCache;
	};

	static Renderer2DData s_Data;

	static Ref<RenderPass> GetOrCreateRenderPass(Ref<Framebuffer> framebuffer, Ref<Shader> shader, const VertexBufferLayout& layout, PrimitiveTopology topology, const std::string& debugName)
	{
		// For SwapChainFramebuffer, GetHandle() changes each frame - use swapchain pointer as stable cache key
		size_t cacheKey;
		if (auto* swapchainFb = dynamic_cast<SwapChainFramebuffer*>(framebuffer.Raw()))
			cacheKey = (size_t)swapchainFb->GetSwapChain() ^ (shader->GetHash() << 16);
		else
			cacheKey = (size_t)framebuffer->GetHandle().Get() ^ (shader->GetHash() << 16);

		auto it = s_Data.PipelineCache.find(cacheKey);
		if (it != s_Data.PipelineCache.end())
			return it->second;

		PipelineSpecification pipelineSpec;
		pipelineSpec.Shader = shader;
		pipelineSpec.TargetFramebuffer = framebuffer;
		pipelineSpec.Layout = layout;
		pipelineSpec.Topology = topology;
		pipelineSpec.DepthTest = true;
		pipelineSpec.DepthWrite = false;
		pipelineSpec.DebugName = debugName;
		if (topology == PrimitiveTopology::Lines || topology == PrimitiveTopology::LineStrip)
			pipelineSpec.LineWidth = s_Data.LineWidth;

		Ref<Pipeline> pipeline = Pipeline::Create(pipelineSpec);

		RenderPassSpecification passSpec;
		passSpec.Pipeline = pipeline;
		passSpec.DebugName = debugName;
		passSpec.StartSet = 0;  // Renderer2D shaders use set 0 for Camera and textures

		Ref<RenderPass> pass = RenderPass::Create(passSpec);
		pass->SetInput("Camera", s_Data.CameraUniformBuffer);

		s_Data.PipelineCache[cacheKey] = pass;
		return pass;
	}

	static void FlushQuad(Ref<RenderCommandBuffer> cmd, Ref<Framebuffer> fb)
	{
		if (s_Data.QuadIndexCount == 0) return;

		uint32_t dataSize = (uint32_t)((uint8_t*)s_Data.QuadVertexBufferPtr - (uint8_t*)s_Data.QuadVertexBufferBase);
		s_Data.QuadVertexBuffer->SetData(s_Data.QuadVertexBufferBase, dataSize);

		VertexBufferLayout quadLayout({
			{ ShaderDataType::Float3, "a_Position" },
			{ ShaderDataType::Float4, "a_Color" },
			{ ShaderDataType::Float2, "a_TexCoord" },
			{ ShaderDataType::Float, "a_TexIndex" },
			{ ShaderDataType::Float, "a_TilingFactor" },
			{ ShaderDataType::Int, "a_EntityID" }
			});

		Ref<RenderPass> pass = GetOrCreateRenderPass(fb, s_Data.QuadShader, quadLayout, PrimitiveTopology::Triangles, "Renderer2D_Quad");
		for (uint32_t i = 0; i < s_Data.TextureSlotIndex; i++)
			pass->SetInput("u_Textures", s_Data.TextureSlots[i], i);

		Renderer::Submit([cmd, fb, pass]() mutable
			{
				pass->Prepare();
				auto bindingSets = pass->GetBindingSets(Renderer::RT_GetCurrentFrameIndex());

				nvrhi::GraphicsState& gs = cmd->GetGraphicsState();
				gs.pipeline = pass->GetPipeline()->GetHandle();
				gs.framebuffer = fb->GetHandle();
				gs.viewport.viewports = { nvrhi::Viewport((float)fb->GetWidth(), (float)fb->GetHeight()) };
				gs.viewport.scissorRects = { nvrhi::Rect((float)fb->GetWidth(), (float)fb->GetHeight()) };
				gs.vertexBuffers = { { s_Data.QuadVertexBuffer->GetHandle(), 0, 0 } };
				gs.indexBuffer = { s_Data.QuadIndexBuffer->GetHandle(), nvrhi::Format::R32_UINT, 0 };
				gs.bindings = bindingSets;

				cmd->RT_CommitGraphicsState();

				nvrhi::DrawArguments args;
				args.vertexCount = s_Data.QuadIndexCount;
				args.startIndexLocation = 0;
				args.startVertexLocation = 0;
				args.instanceCount = 1;
				cmd->GetActive()->drawIndexed(args);

				s_Data.Stats.DrawCalls++;
			});
	}

	static void FlushCircle(Ref<RenderCommandBuffer> cmd, Ref<Framebuffer> fb)
	{
		if (s_Data.CircleIndexCount == 0) return;

		uint32_t dataSize = (uint32_t)((uint8_t*)s_Data.CircleVertexBufferPtr - (uint8_t*)s_Data.CircleVertexBufferBase);
		s_Data.CircleVertexBuffer->SetData(s_Data.CircleVertexBufferBase, dataSize);

		VertexBufferLayout circleLayout({
			{ ShaderDataType::Float3, "a_WorldPosition" },
			{ ShaderDataType::Float3, "a_LocalPosition" },
			{ ShaderDataType::Float4, "a_Color" },
			{ ShaderDataType::Float, "a_Thickness" },
			{ ShaderDataType::Float, "a_Fade" },
			{ ShaderDataType::Int, "a_EntityID" }
			});

		Ref<RenderPass> pass = GetOrCreateRenderPass(fb, s_Data.CircleShader, circleLayout, PrimitiveTopology::Triangles, "Renderer2D_Circle");

		Renderer::Submit([cmd, fb, pass]() mutable
			{
				pass->Prepare();
				auto bindingSets = pass->GetBindingSets(Renderer::RT_GetCurrentFrameIndex());

				nvrhi::GraphicsState& gs = cmd->GetGraphicsState();
				gs.pipeline = pass->GetPipeline()->GetHandle();
				gs.framebuffer = fb->GetHandle();
				gs.viewport.viewports = { nvrhi::Viewport((float)fb->GetWidth(), (float)fb->GetHeight()) };
				gs.viewport.scissorRects = { nvrhi::Rect((float)fb->GetWidth(), (float)fb->GetHeight()) };
				gs.vertexBuffers = { { s_Data.CircleVertexBuffer->GetHandle(), 0, 0 } };
				gs.indexBuffer = { s_Data.QuadIndexBuffer->GetHandle(), nvrhi::Format::R32_UINT, 0 };
				gs.bindings = bindingSets;

				cmd->RT_CommitGraphicsState();

				nvrhi::DrawArguments args;
				args.vertexCount = s_Data.CircleIndexCount;
				args.startIndexLocation = 0;
				args.startVertexLocation = 0;
				args.instanceCount = 1;
				cmd->GetActive()->drawIndexed(args);

				s_Data.Stats.DrawCalls++;
			});
	}

	static void FlushLine(Ref<RenderCommandBuffer> cmd, Ref<Framebuffer> fb)
	{
		if (s_Data.LineVertexCount == 0) return;

		uint32_t dataSize = (uint32_t)((uint8_t*)s_Data.LineVertexBufferPtr - (uint8_t*)s_Data.LineVertexBufferBase);
		s_Data.LineVertexBuffer->SetData(s_Data.LineVertexBufferBase, dataSize);

		VertexBufferLayout lineLayout({
			{ ShaderDataType::Float3, "a_Position" },
			{ ShaderDataType::Float4, "a_Color" },
			{ ShaderDataType::Int, "a_EntityID" }
			});

		Ref<RenderPass> pass = GetOrCreateRenderPass(fb, s_Data.LineShader, lineLayout, PrimitiveTopology::Lines, "Renderer2D_Line");

		float lineWidth = s_Data.LineWidth;
		Renderer::Submit([cmd, fb, pass, lineWidth]() mutable
			{
				pass->Prepare();
				auto bindingSets = pass->GetBindingSets(Renderer::RT_GetCurrentFrameIndex());

				nvrhi::GraphicsState& gs = cmd->GetGraphicsState();
				gs.pipeline = pass->GetPipeline()->GetHandle();
				gs.framebuffer = fb->GetHandle();
				gs.viewport.viewports = { nvrhi::Viewport((float)fb->GetWidth(), (float)fb->GetHeight()) };
				gs.viewport.scissorRects = { nvrhi::Rect((float)fb->GetWidth(), (float)fb->GetHeight()) };
				gs.vertexBuffers = { { s_Data.LineVertexBuffer->GetHandle(), 0, 0 } };
				gs.bindings = bindingSets;
				gs.lineWidth = lineWidth;

				cmd->RT_CommitGraphicsState();

				cmd->GetActive()->draw(nvrhi::DrawArguments().setVertexCount(s_Data.LineVertexCount));

				s_Data.Stats.DrawCalls++;
			});
	}

	static void FlushText(Ref<RenderCommandBuffer> cmd, Ref<Framebuffer> fb)
	{
		if (s_Data.TextIndexCount == 0) return;

		uint32_t dataSize = (uint32_t)((uint8_t*)s_Data.TextVertexBufferPtr - (uint8_t*)s_Data.TextVertexBufferBase);
		s_Data.TextVertexBuffer->SetData(s_Data.TextVertexBufferBase, dataSize);

		VertexBufferLayout textLayout({
			{ ShaderDataType::Float3, "a_Position" },
			{ ShaderDataType::Float4, "a_Color" },
			{ ShaderDataType::Float2, "a_TexCoord" },
			{ ShaderDataType::Int, "a_EntityID" }
			});

		Ref<RenderPass> pass = GetOrCreateRenderPass(fb, s_Data.TextShader, textLayout, PrimitiveTopology::Triangles, "Renderer2D_Text");
		pass->SetInput("u_FontAtlas", s_Data.FontAtlasTexture);

		Renderer::Submit([cmd, fb, pass]() mutable
			{
				pass->Prepare();
				auto bindingSets = pass->GetBindingSets(Renderer::RT_GetCurrentFrameIndex());

				nvrhi::GraphicsState& gs = cmd->GetGraphicsState();
				gs.pipeline = pass->GetPipeline()->GetHandle();
				gs.framebuffer = fb->GetHandle();
				gs.viewport.viewports = { nvrhi::Viewport((float)fb->GetWidth(), (float)fb->GetHeight()) };
				gs.viewport.scissorRects = { nvrhi::Rect((float)fb->GetWidth(), (float)fb->GetHeight()) };
				gs.vertexBuffers = { { s_Data.TextVertexBuffer->GetHandle(), 0, 0 } };
				gs.indexBuffer = { s_Data.QuadIndexBuffer->GetHandle(), nvrhi::Format::R32_UINT, 0 };
				gs.bindings = bindingSets;

				cmd->RT_CommitGraphicsState();

				nvrhi::DrawArguments args;
				args.vertexCount = s_Data.TextIndexCount;
				args.startIndexLocation = 0;
				args.startVertexLocation = 0;
				args.instanceCount = 1;
				cmd->GetActive()->drawIndexed(args);

				s_Data.Stats.DrawCalls++;
			});
	}

	void Renderer2D::Init()
	{
		LUX_PROFILE_FUNCTION("Renderer2D::Init");

		s_Data.QuadVertexBuffer = VertexBuffer::Create(Renderer2DData::MaxVertices * sizeof(QuadVertex));
		s_Data.CircleVertexBuffer = VertexBuffer::Create(Renderer2DData::MaxVertices * sizeof(CircleVertex));
		s_Data.LineVertexBuffer = VertexBuffer::Create(Renderer2DData::MaxVertices * sizeof(LineVertex));
		s_Data.TextVertexBuffer = VertexBuffer::Create(Renderer2DData::MaxVertices * sizeof(TextVertex));

		s_Data.QuadVertexBufferBase = new QuadVertex[Renderer2DData::MaxVertices];
		s_Data.CircleVertexBufferBase = new CircleVertex[Renderer2DData::MaxVertices];
		s_Data.LineVertexBufferBase = new LineVertex[Renderer2DData::MaxVertices];
		s_Data.TextVertexBufferBase = new TextVertex[Renderer2DData::MaxVertices];

		uint32_t* quadIndices = new uint32_t[Renderer2DData::MaxIndices];
		uint32_t offset = 0;
		for (uint32_t i = 0; i < Renderer2DData::MaxIndices; i += 6)
		{
			quadIndices[i + 0] = offset + 0;
			quadIndices[i + 1] = offset + 1;
			quadIndices[i + 2] = offset + 2;
			quadIndices[i + 3] = offset + 2;
			quadIndices[i + 4] = offset + 3;
			quadIndices[i + 5] = offset + 0;
			offset += 4;
		}
		s_Data.QuadIndexBuffer = IndexBuffer::Create(Buffer(quadIndices, Renderer2DData::MaxIndices * sizeof(uint32_t)));
		delete[] quadIndices;

		s_Data.WhiteTexture = Renderer::GetWhiteTexture();
		s_Data.TextureSlots[0] = s_Data.WhiteTexture;

		s_Data.QuadShader = Renderer::GetShaderLibrary()->Get("Renderer2D");
		s_Data.CircleShader = Renderer::GetShaderLibrary()->Get("Renderer2D_Circle");
		s_Data.LineShader = Renderer::GetShaderLibrary()->Get("Renderer2D_Line");
		s_Data.TextShader = Renderer::GetShaderLibrary()->Get("Renderer2D_Text");

		s_Data.QuadVertexPositions[0] = { -0.5f, -0.5f, 0.0f, 1.0f };
		s_Data.QuadVertexPositions[1] = { 0.5f, -0.5f, 0.0f, 1.0f };
		s_Data.QuadVertexPositions[2] = { 0.5f,  0.5f, 0.0f, 1.0f };
		s_Data.QuadVertexPositions[3] = { -0.5f,  0.5f, 0.0f, 1.0f };

		s_Data.CameraUniformBuffer = UniformBuffer::Create(sizeof(Renderer2DData::CameraData), "Renderer2D_Camera");
	}

	void Renderer2D::Shutdown()
	{
		LUX_PROFILE_FUNCTION("Renderer2D::Shutdown");

		delete[] s_Data.QuadVertexBufferBase;
		delete[] s_Data.CircleVertexBufferBase;
		delete[] s_Data.LineVertexBufferBase;
		delete[] s_Data.TextVertexBufferBase;
		s_Data.QuadVertexBufferBase = nullptr;
		s_Data.CircleVertexBufferBase = nullptr;
		s_Data.LineVertexBufferBase = nullptr;
		s_Data.TextVertexBufferBase = nullptr;
		s_Data.PipelineCache.clear();
	}

	void Renderer2D::BeginScene(Ref<RenderCommandBuffer> commandBuffer, Ref<Framebuffer> framebuffer, const Camera& camera, const glm::mat4& transform)
	{
		LUX_PROFILE_FUNCTION("Renderer2D::BeginScene");

		s_Data.CurrentCommandBuffer = commandBuffer;
		s_Data.CurrentFramebuffer = framebuffer;
		s_Data.CameraBuffer.ViewProjection = camera.GetProjection() * glm::inverse(transform);
		s_Data.CameraUniformBuffer->SetData(commandBuffer, &s_Data.CameraBuffer, sizeof(Renderer2DData::CameraData));
		StartBatch();
	}

	void Renderer2D::BeginScene(Ref<RenderCommandBuffer> commandBuffer, Ref<Framebuffer> framebuffer, const EditorCamera& camera)
	{
		LUX_PROFILE_FUNCTION("Renderer2D::BeginScene");

		s_Data.CurrentCommandBuffer = commandBuffer;
		s_Data.CurrentFramebuffer = framebuffer;
		s_Data.CameraBuffer.ViewProjection = camera.GetViewProjection();
		s_Data.CameraUniformBuffer->SetData(commandBuffer, &s_Data.CameraBuffer, sizeof(Renderer2DData::CameraData));
		StartBatch();
	}

	void Renderer2D::BeginScene(Ref<RenderCommandBuffer> commandBuffer, Ref<Framebuffer> framebuffer, const OrthographicCamera& camera)
	{
		LUX_PROFILE_FUNCTION("Renderer2D::BeginScene");

		s_Data.CurrentCommandBuffer = commandBuffer;
		s_Data.CurrentFramebuffer = framebuffer;
		s_Data.CameraBuffer.ViewProjection = camera.GetViewProjectionMatrix();
		s_Data.CameraUniformBuffer->SetData(commandBuffer, &s_Data.CameraBuffer, sizeof(Renderer2DData::CameraData));
		StartBatch();
	}

	void Renderer2D::BeginScene(Ref<RenderCommandBuffer> commandBuffer, VulkanSwapChain* swapchain, const Camera& camera, const glm::mat4& transform)
	{
		BeginScene(commandBuffer, SwapChainFramebuffer::Create(swapchain), camera, transform);
	}

	void Renderer2D::BeginScene(Ref<RenderCommandBuffer> commandBuffer, VulkanSwapChain* swapchain, const EditorCamera& camera)
	{
		BeginScene(commandBuffer, SwapChainFramebuffer::Create(swapchain), camera);
	}

	void Renderer2D::BeginScene(Ref<RenderCommandBuffer> commandBuffer, VulkanSwapChain* swapchain, const OrthographicCamera& camera)
	{
		BeginScene(commandBuffer, SwapChainFramebuffer::Create(swapchain), camera);
	}

	void Renderer2D::BeginScene(const Camera& camera, const glm::mat4& transform)
	{
		LUX_CORE_WARN_TAG("Renderer2D", "BeginScene without command buffer/framebuffer is deprecated. Use overload with RenderCommandBuffer and Framebuffer.");
	}

	void Renderer2D::BeginScene(const EditorCamera& camera)
	{
		LUX_CORE_WARN_TAG("Renderer2D", "BeginScene without command buffer/framebuffer is deprecated. Use overload with RenderCommandBuffer and Framebuffer.");
	}

	void Renderer2D::BeginScene(const OrthographicCamera& camera)
	{
		LUX_CORE_WARN_TAG("Renderer2D", "BeginScene without command buffer/framebuffer is deprecated. Use overload with RenderCommandBuffer and Framebuffer.");
	}

	void Renderer2D::EndScene()
	{
		LUX_PROFILE_FUNCTION("Renderer2D::EndScene");
		Flush();
	}

	void Renderer2D::Flush()
	{
		if (!s_Data.CurrentCommandBuffer || !s_Data.CurrentFramebuffer)
		{
			LUX_CORE_WARN_TAG("Renderer2D", "Flush called without active scene. Call BeginScene with command buffer and framebuffer first.");
			return;
		}

		FlushQuad(s_Data.CurrentCommandBuffer, s_Data.CurrentFramebuffer);
		FlushCircle(s_Data.CurrentCommandBuffer, s_Data.CurrentFramebuffer);
		FlushLine(s_Data.CurrentCommandBuffer, s_Data.CurrentFramebuffer);
		FlushText(s_Data.CurrentCommandBuffer, s_Data.CurrentFramebuffer);
		StartBatch();
	}

	void Renderer2D::StartBatch()
	{
		s_Data.QuadIndexCount = 0;
		s_Data.QuadVertexBufferPtr = s_Data.QuadVertexBufferBase;

		s_Data.CircleIndexCount = 0;
		s_Data.CircleVertexBufferPtr = s_Data.CircleVertexBufferBase;

		s_Data.LineVertexCount = 0;
		s_Data.LineVertexBufferPtr = s_Data.LineVertexBufferBase;

		s_Data.TextIndexCount = 0;
		s_Data.TextVertexBufferPtr = s_Data.TextVertexBufferBase;

		s_Data.TextureSlotIndex = 1;
	}

	void Renderer2D::NextBatch()
	{
		Flush();
		StartBatch();
	}

	void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color)
	{
		DrawQuad(glm::vec3(position.x, position.y, 0.0f), size, color);
	}

	void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color)
	{
		LUX_PROFILE_FUNCTION("Renderer2D::DrawQuad");

		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
			* glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
		DrawQuad(transform, color);
	}

	void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const Ref<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor)
	{
		DrawQuad(glm::vec3(position.x, position.y, 0.0f), size, texture, tilingFactor, tintColor);
	}

	void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor)
	{
		LUX_PROFILE_FUNCTION("Renderer2D::DrawQuad");

		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
			* glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
		DrawQuad(transform, texture, tilingFactor, tintColor);
	}

	void Renderer2D::DrawQuad(const glm::mat4& transform, const glm::vec4& color, int entityID)
	{
		LUX_PROFILE_FUNCTION("Renderer2D::DrawQuad");

		constexpr size_t quadVertexCount = 4;
		const float textureIndex = 0.0f;
		constexpr glm::vec2 textureCoords[] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
		const float tilingFactor = 1.0f;

		if (s_Data.QuadIndexCount >= Renderer2DData::MaxIndices)
			NextBatch();

		for (size_t i = 0; i < quadVertexCount; i++)
		{
			s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPositions[i];
			s_Data.QuadVertexBufferPtr->Color = color;
			s_Data.QuadVertexBufferPtr->TexCoord = textureCoords[i];
			s_Data.QuadVertexBufferPtr->TexIndex = textureIndex;
			s_Data.QuadVertexBufferPtr->TilingFactor = tilingFactor;
			s_Data.QuadVertexBufferPtr->EntityID = entityID;
			s_Data.QuadVertexBufferPtr++;
		}

		s_Data.QuadIndexCount += 6;
		s_Data.Stats.QuadCount++;
	}

	void Renderer2D::DrawQuad(const glm::mat4& transform, const Ref<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor, int entityID)
	{
		LUX_PROFILE_FUNCTION("Renderer2D::DrawQuad");
		LUX_CORE_VERIFY(texture);

		constexpr size_t quadVertexCount = 4;
		constexpr glm::vec2 textureCoords[] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };

		if (s_Data.QuadIndexCount >= Renderer2DData::MaxIndices)
			NextBatch();

		float textureIndex = 0.0f;
		for (uint32_t i = 1; i < s_Data.TextureSlotIndex; i++)
		{
			if (s_Data.TextureSlots[i]->GetHash() == texture->GetHash())
			{
				textureIndex = (float)i;
				break;
			}
		}

		if (textureIndex == 0.0f)
		{
			if (s_Data.TextureSlotIndex >= Renderer2DData::MaxTextureSlots)
				NextBatch();

			textureIndex = (float)s_Data.TextureSlotIndex;
			s_Data.TextureSlots[s_Data.TextureSlotIndex] = texture;
			s_Data.TextureSlotIndex++;
		}

		for (size_t i = 0; i < quadVertexCount; i++)
		{
			s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPositions[i];
			s_Data.QuadVertexBufferPtr->Color = tintColor;
			s_Data.QuadVertexBufferPtr->TexCoord = textureCoords[i];
			s_Data.QuadVertexBufferPtr->TexIndex = textureIndex;
			s_Data.QuadVertexBufferPtr->TilingFactor = tilingFactor;
			s_Data.QuadVertexBufferPtr->EntityID = entityID;
			s_Data.QuadVertexBufferPtr++;
		}

		s_Data.QuadIndexCount += 6;
		s_Data.Stats.QuadCount++;
	}

	void Renderer2D::DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const glm::vec4& color)
	{
		DrawRotatedQuad(glm::vec3(position.x, position.y, 0.0f), size, rotation, color);
	}

	void Renderer2D::DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const glm::vec4& color)
	{
		LUX_PROFILE_FUNCTION("Renderer2D::DrawRotatedQuad");

		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
			* glm::rotate(glm::mat4(1.0f), glm::radians(rotation), { 0.0f, 0.0f, 1.0f })
			* glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
		DrawQuad(transform, color);
	}

	void Renderer2D::DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const Ref<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor)
	{
		DrawRotatedQuad(glm::vec3(position.x, position.y, 0.0f), size, rotation, texture, tilingFactor, tintColor);
	}

	void Renderer2D::DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const Ref<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor)
	{
		LUX_PROFILE_FUNCTION("Renderer2D::DrawRotatedQuad");

		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
			* glm::rotate(glm::mat4(1.0f), glm::radians(rotation), { 0.0f, 0.0f, 1.0f })
			* glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
		DrawQuad(transform, texture, tilingFactor, tintColor);
	}

	void Renderer2D::DrawCircle(const glm::mat4& transform, const glm::vec4& color, float thickness, float fade, int entityID)
	{
		LUX_PROFILE_FUNCTION("Renderer2D::DrawCircle");

		if (s_Data.CircleIndexCount >= Renderer2DData::MaxIndices)
			NextBatch();

		for (size_t i = 0; i < 4; i++)
		{
			s_Data.CircleVertexBufferPtr->WorldPosition = transform * s_Data.QuadVertexPositions[i];
			s_Data.CircleVertexBufferPtr->LocalPosition = s_Data.QuadVertexPositions[i] * 2.0f;
			s_Data.CircleVertexBufferPtr->Color = color;
			s_Data.CircleVertexBufferPtr->Thickness = thickness;
			s_Data.CircleVertexBufferPtr->Fade = fade;
			s_Data.CircleVertexBufferPtr->EntityID = entityID;
			s_Data.CircleVertexBufferPtr++;
		}

		s_Data.CircleIndexCount += 6;
		s_Data.Stats.QuadCount++;
	}

	void Renderer2D::DrawLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color, int entityID)
	{
		s_Data.LineVertexBufferPtr->Position = p0;
		s_Data.LineVertexBufferPtr->Color = color;
		s_Data.LineVertexBufferPtr->EntityID = entityID;
		s_Data.LineVertexBufferPtr++;

		s_Data.LineVertexBufferPtr->Position = p1;
		s_Data.LineVertexBufferPtr->Color = color;
		s_Data.LineVertexBufferPtr->EntityID = entityID;
		s_Data.LineVertexBufferPtr++;

		s_Data.LineVertexCount += 2;
	}

	void Renderer2D::DrawRect(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color, int entityID)
	{
		glm::vec3 p0 = glm::vec3(position.x - size.x * 0.5f, position.y - size.y * 0.5f, position.z);
		glm::vec3 p1 = glm::vec3(position.x + size.x * 0.5f, position.y - size.y * 0.5f, position.z);
		glm::vec3 p2 = glm::vec3(position.x + size.x * 0.5f, position.y + size.y * 0.5f, position.z);
		glm::vec3 p3 = glm::vec3(position.x - size.x * 0.5f, position.y + size.y * 0.5f, position.z);

		DrawLine(p0, p1, color, entityID);
		DrawLine(p1, p2, color, entityID);
		DrawLine(p2, p3, color, entityID);
		DrawLine(p3, p0, color, entityID);
	}

	void Renderer2D::DrawRect(const glm::mat4& transform, const glm::vec4& color, int entityID)
	{
		glm::vec3 lineVertices[4];
		for (size_t i = 0; i < 4; i++)
			lineVertices[i] = transform * s_Data.QuadVertexPositions[i];

		DrawLine(lineVertices[0], lineVertices[1], color, entityID);
		DrawLine(lineVertices[1], lineVertices[2], color, entityID);
		DrawLine(lineVertices[2], lineVertices[3], color, entityID);
		DrawLine(lineVertices[3], lineVertices[0], color, entityID);
	}

	void Renderer2D::DrawSprite(const glm::mat4& transform, SpriteRendererComponent& src, int entityID)
	{
		if (src.Texture && AssetManager::IsAssetHandleValid(src.Texture))
		{
			Ref<Texture2D> texture = AssetManager::GetAsset<Texture2D>(src.Texture);
			DrawQuad(transform, texture, src.TilingFactor, src.Color, entityID);
		}
		else
		{
			DrawQuad(transform, src.Color, entityID);
		}
	}

	void Renderer2D::DrawString(const std::string& string, Ref<Font> font, const glm::mat4& transform, const TextParams& textParams, int entityID)
	{
		LUX_CORE_VERIFY(font && font->GetMSDFData());

		const auto& fontGeometry = font->GetMSDFData()->FontGeometry;
		const auto& metrics = fontGeometry.getMetrics();
		Ref<Texture2D> fontAtlas = font->GetFontAtlas();

		s_Data.FontAtlasTexture = fontAtlas;

		double x = 0.0;
		double fsScale = 1.0 / (metrics.ascenderY - metrics.descenderY);
		double y = 0.0;

		const msdf_atlas::GlyphGeometry* spaceGlyph = fontGeometry.getGlyph(' ');
		const float spaceGlyphAdvance = spaceGlyph ? (float)spaceGlyph->getAdvance() : 0.5f;

		for (size_t i = 0; i < string.size(); i++)
		{
			char character = string[i];
			if (character == '\r')
				continue;

			if (character == '\n')
			{
				x = 0;
				y -= fsScale * metrics.lineHeight + textParams.LineSpacing;
				continue;
			}

			if (character == ' ')
			{
				float advance = spaceGlyphAdvance;
				if (i < string.size() - 1)
				{
					char nextCharacter = string[i + 1];
					double dAdvance;
					fontGeometry.getAdvance(dAdvance, (msdf_atlas::unicode_t)(unsigned char)character, (msdf_atlas::unicode_t)(unsigned char)nextCharacter);
					advance = (float)dAdvance;
				}
				x += fsScale * advance + textParams.Kerning;
				continue;
			}

			if (character == '\t')
			{
				x += 4.0f * (fsScale * spaceGlyphAdvance + textParams.Kerning);
				continue;
			}

			const msdf_atlas::GlyphGeometry* glyph = fontGeometry.getGlyph((msdf_atlas::unicode_t)(unsigned char)character);
			if (!glyph)
				glyph = fontGeometry.getGlyph('?');
			if (!glyph)
				continue;

			double al, ab, ar, at;
			glyph->getQuadAtlasBounds(al, ab, ar, at);
			glm::vec2 texCoordMin((float)al, (float)ab);
			glm::vec2 texCoordMax((float)ar, (float)at);

			double pl, pb, pr, pt;
			glyph->getQuadPlaneBounds(pl, pb, pr, pt);
			glm::vec2 quadMin((float)pl, (float)pb);
			glm::vec2 quadMax((float)pr, (float)pt);

			quadMin *= fsScale; quadMax *= fsScale;
			quadMin += glm::vec2(x, y); quadMax += glm::vec2(x, y);

			float texelWidth = 1.0f / fontAtlas->GetWidth();
			float texelHeight = 1.0f / fontAtlas->GetHeight();
			texCoordMin *= glm::vec2(texelWidth, texelHeight);
			texCoordMax *= glm::vec2(texelWidth, texelHeight);

			s_Data.TextVertexBufferPtr->Position = transform * glm::vec4(quadMin, 0.0f, 1.0f);
			s_Data.TextVertexBufferPtr->Color = textParams.Color;
			s_Data.TextVertexBufferPtr->TexCoord = texCoordMin;
			s_Data.TextVertexBufferPtr->EntityID = entityID;
			s_Data.TextVertexBufferPtr++;

			s_Data.TextVertexBufferPtr->Position = transform * glm::vec4(quadMin.x, quadMax.y, 0.0f, 1.0f);
			s_Data.TextVertexBufferPtr->Color = textParams.Color;
			s_Data.TextVertexBufferPtr->TexCoord = { texCoordMin.x, texCoordMax.y };
			s_Data.TextVertexBufferPtr->EntityID = entityID;
			s_Data.TextVertexBufferPtr++;

			s_Data.TextVertexBufferPtr->Position = transform * glm::vec4(quadMax, 0.0f, 1.0f);
			s_Data.TextVertexBufferPtr->Color = textParams.Color;
			s_Data.TextVertexBufferPtr->TexCoord = texCoordMax;
			s_Data.TextVertexBufferPtr->EntityID = entityID;
			s_Data.TextVertexBufferPtr++;

			s_Data.TextVertexBufferPtr->Position = transform * glm::vec4(quadMax.x, quadMin.y, 0.0f, 1.0f);
			s_Data.TextVertexBufferPtr->Color = textParams.Color;
			s_Data.TextVertexBufferPtr->TexCoord = { texCoordMax.x, texCoordMin.y };
			s_Data.TextVertexBufferPtr->EntityID = entityID;
			s_Data.TextVertexBufferPtr++;

			s_Data.TextIndexCount += 6;
			s_Data.Stats.QuadCount++;

			if (i < string.size() - 1)
			{
				double advance = glyph->getAdvance();
				char nextCharacter = string[i + 1];
				fontGeometry.getAdvance(advance, (msdf_atlas::unicode_t)(unsigned char)character, (msdf_atlas::unicode_t)(unsigned char)nextCharacter);
				x += fsScale * advance + textParams.Kerning;
			}
		}
	}

	void Renderer2D::DrawString(const std::string& string, const glm::mat4& transform, const TextComponent& component, int entityID)
	{
		Ref<Font> font = Font::GetFontAssetForTextComponent(component);
		DrawString(string, font, transform, { component.Color, component.Kerning, component.LineSpacing }, entityID);
	}

	float Renderer2D::GetLineWidth()
	{
		return s_Data.LineWidth;
	}

	void Renderer2D::SetLineWidth(float width)
	{
		s_Data.LineWidth = width;
	}

	void Renderer2D::ResetStats()
	{
		s_Data.Stats = {};
	}

	Renderer2D::Statistics Renderer2D::GetStats()
	{
		return s_Data.Stats;
	}

}
