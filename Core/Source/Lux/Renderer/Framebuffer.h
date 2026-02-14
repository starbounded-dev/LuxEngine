#pragma once

#include <glm/glm.hpp>
#include <map>

#include "Lux/Renderer/RendererTypes.h"
#include "Image.h"

#include "nvrhi/nvrhi.h"

namespace Lux {

	class Framebuffer;

	enum class FramebufferBlendMode
	{
		None = 0,
		OneZero,
		SrcAlphaOneMinusSrcAlpha,
		Additive,
		Zero_SrcColor
	};

	enum class AttachmentLoadOp
	{
		Inherit = 0, Clear = 1, Load = 2
	};

	struct FramebufferTextureSpecification
	{
		FramebufferTextureSpecification() = default;
		FramebufferTextureSpecification(ImageFormat format) : Format(format) {}

		ImageFormat Format;
		bool Blend = true;
		FramebufferBlendMode BlendMode = FramebufferBlendMode::SrcAlphaOneMinusSrcAlpha;
		AttachmentLoadOp LoadOp = AttachmentLoadOp::Inherit;
		// TODO: filtering/wrap
	};

	struct FramebufferAttachmentSpecification
	{
		FramebufferAttachmentSpecification() = default;
		FramebufferAttachmentSpecification(const std::initializer_list<FramebufferTextureSpecification>& attachments)
			: Attachments(attachments) {
		}

		std::vector<FramebufferTextureSpecification> Attachments;
	};

	struct FramebufferSpecification
	{
		float Scale = 1.0f;
		uint32_t Width = 0;
		uint32_t Height = 0;
		glm::vec4 ClearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
		float DepthClearValue = 0.0f;
		bool ClearColorOnLoad = true;
		bool ClearDepthOnLoad = true;

		FramebufferAttachmentSpecification Attachments;
		uint32_t Samples = 1; // multisampling

		// TODO: Temp, needs scale
		bool NoResize = false;

		// Master switch (individual attachments can be disabled in FramebufferTextureSpecification)
		bool Blend = true;
		// None means use BlendMode in FramebufferTextureSpecification
		FramebufferBlendMode BlendMode = FramebufferBlendMode::None;

		// SwapChainTarget = screen buffer (i.e. no framebuffer)
		bool SwapChainTarget = false;

		// Will it be used for transfer ops?
		bool Transfer = false;

		// Note: these are used to attach multi-layered color/depth images 
		Ref<Image2D> ExistingImage;
		uint32_t ExistingImageLayer = (uint32_t)-1; // -1 means all (no specific layer)

		// Specify existing images to attach instead of creating
		// new images. attachment index -> image
		std::map<uint32_t, Ref<Image2D>> ExistingImages;

		// At the moment this will just create a new render pass
		// with an existing framebuffer
		Ref<Framebuffer> ExistingFramebuffer;

		std::string DebugName;
	};

	typedef union ClearColorValue
	{
		float       float32[4];
		int32_t     int32[4];
		uint32_t    uint32[4];
	} ClearColorValue;

	typedef struct ClearDepthStencilValue
	{
		float       Depth;
		uint32_t    Stencil;
	} ClearDepthStencilValue;

	typedef union ClearValue
	{
		ClearColorValue           Color;
		ClearDepthStencilValue    DepthStencil;
	} ClearValue;

	class Framebuffer : public RefCounted
	{
	public:
		static Ref<Framebuffer> Create(const FramebufferSpecification& specification) { return Ref<Framebuffer>::Create(specification); }

		void Resize(uint32_t width, uint32_t height, bool forceRecreate = false);
		void AddResizeCallback(const std::function<void(Ref<Framebuffer>)>& func);

		virtual uint32_t GetWidth() const { return m_Width; }
		virtual uint32_t GetHeight() const { return m_Height; }

		Ref<Image2D> GetImage(uint32_t attachmentIndex = 0) const { LUX_CORE_ASSERT(attachmentIndex < m_AttachmentImages.size()); return m_AttachmentImages[attachmentIndex]; }
		Ref<Image2D> GetDepthImage() const { return m_DepthAttachmentImage; }
		size_t GetColorAttachmentCount() const { return m_Specification.SwapChainTarget ? 1 : m_AttachmentImages.size(); }
		bool HasDepthAttachment() const { return (bool)m_DepthAttachmentImage; }

		virtual nvrhi::FramebufferHandle GetHandle() const { return m_Handle; }
		const nvrhi::FramebufferDesc& GetFramebufferDesc() const { return m_FramebufferDesc; }

		const std::vector<ClearValue>& GetClearValues() const { return m_ClearValues; }

		virtual const FramebufferSpecification& GetSpecification() const { return m_Specification; }

		void Invalidate();
		void RT_Invalidate();
		void Release();
	public:
		Framebuffer(const FramebufferSpecification& spec);

		virtual ~Framebuffer();
	protected:
		Framebuffer() = default;  // For subclasses like SwapChainFramebuffer
	private:
		FramebufferSpecification m_Specification;

		nvrhi::FramebufferHandle m_Handle = nullptr;
		nvrhi::FramebufferDesc m_FramebufferDesc;

		uint32_t m_Width = 0, m_Height = 0;

		std::vector<Ref<Image2D>> m_AttachmentImages;
		Ref<Image2D> m_DepthAttachmentImage;

		std::vector<ClearValue> m_ClearValues;

		std::vector<std::function<void(Ref<Framebuffer>)>> m_ResizeCallbacks;
	};

}
