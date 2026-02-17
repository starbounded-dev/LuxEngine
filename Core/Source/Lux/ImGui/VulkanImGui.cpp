#include "lpch.h"
#include "UICore.h"

#include "Lux/Core/Application.h"
#include "Lux/Renderer/RendererAPI.h"
#include "Lux/Renderer/Renderer.h"

#include "ImGuiLayer.h"

namespace ImGui {
	extern bool ImageButtonEx(ImGuiID id, ImTextureID user_texture_id, const ImVec2& image_size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& bg_col, const ImVec4& tint_col, ImGuiButtonFlags flags);
}

namespace Lux::UI {

	ImTextureID GetTextureID(Ref<Image2D> image, ImageMode mode)
	{
		return Application::Get().GetImGuiLayer()->GetImGuiRenderer()->CreateFrameTexture(
			image->GetHandle().Get(), nvrhi::AllSubresources, (mode == ImageMode::Opaque), (mode == ImageMode::Depth));
	}

	ImTextureID GetTextureIDLayer(Ref<Image2D> image, uint32_t layer, ImageMode mode)
	{
		nvrhi::TextureSubresourceSet subresources = image->GetLayerImageView(layer);
		return Application::Get().GetImGuiLayer()->GetImGuiRenderer()->CreateFrameTexture(
			image->GetHandle().Get(), subresources, (mode == ImageMode::Opaque), (mode == ImageMode::Depth));
	}

	ImTextureID GetTextureIDMip(Ref<Image2D> image, uint32_t mip, ImageMode mode)
	{
		nvrhi::TextureSubresourceSet subresources = image->GetMipImageView(mip);
		return Application::Get().GetImGuiLayer()->GetImGuiRenderer()->CreateFrameTexture(
			image->GetHandle().Get(), subresources, (mode == ImageMode::Opaque), (mode == ImageMode::Depth));
	}

	ImTextureID GetTextureID(Ref<Texture2D> texture)
	{
		return GetTextureID(texture->GetImage(), ImageMode::Normal);
	}

	void Image(Ref<Image2D> image, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, ImageMode mode)
	{
		LUX_CORE_VERIFY(image, "Image is null");
		const auto textureID = GetTextureID(image, mode);
		ImGui::Image(textureID, size, uv0, uv1);
	}

	void Image(Ref<Image2D> image, uint32_t layer, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, ImageMode mode)
	{
		LUX_CORE_VERIFY(image, "Image is null");
		const auto textureID = GetTextureIDLayer(image, layer, mode);
		ImGui::Image(textureID, size, uv0, uv1);
	}

	void ImageMip(Ref<Image2D> image, uint32_t mip, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, ImageMode mode)
	{
		LUX_CORE_VERIFY(image, "Image is null");
		const auto textureID = GetTextureIDMip(image, mip, mode);
		ImGui::Image(textureID, size, uv0, uv1);
	}

	void Image(const Ref<Texture2D>& texture, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col)
	{
		LUX_CORE_VERIFY(texture, "Texture is null");
		LUX_CORE_VERIFY(texture->GetImage(), "Texture's Image is null");

		const auto textureID = GetTextureID(texture->GetImage(), ImageMode::Normal);
		ImGui::Image(textureID, size, uv0, uv1, tint_col, border_col);
	}

	bool ImageButton(const Ref<Image2D>& image, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& bg_col, const ImVec4& tint_col)
	{
		return ImageButton(nullptr, image, size, uv0, uv1, bg_col, tint_col);
	}

	bool ImageButton(const char* stringID, const Ref<Image2D>& image, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& bg_col, const ImVec4& tint_col)
	{
		const auto textureID = GetTextureID(image, ImageMode::Normal);
		ImGuiID id = (ImGuiID)((((uint64_t)textureID) >> 32) ^ (uint32_t)(uint64_t)textureID);
		if (stringID)
		{
			const ImGuiID strID = ImGui::GetID(stringID);
			id = id ^ strID;
		}
		return ImGui::ImageButtonEx(id, textureID, size, uv0, uv1, bg_col, tint_col);
	}

	bool ImageButton(const Ref<Texture2D>& texture, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& bg_col, const ImVec4& tint_col)
	{
		return ImageButton(nullptr, texture, size, uv0, uv1, bg_col, tint_col);
	}

	bool ImageButton(const char* stringID, const Ref<Texture2D>& texture, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& bg_col, const ImVec4& tint_col)
	{
		LUX_CORE_VERIFY(texture);
		if (!texture)
			return false;

		const auto textureID = GetTextureID(texture);
		ImGuiID id = (ImGuiID)((((uint64_t)textureID) >> 32) ^ (uint32_t)(uint64_t)textureID);
		if (stringID)
		{
			const ImGuiID strID = ImGui::GetID(stringID);
			id = id ^ strID;
		}
		return ImGui::ImageButtonEx(id, textureID, size, uv0, uv1, bg_col, tint_col);
	}
}
