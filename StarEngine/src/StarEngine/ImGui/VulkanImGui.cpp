#include "sepch.h"

#include "ImGuiCore.h"

#include "StarEngine/Renderer/RendererAPI.h"
#include "StarEngine/Renderer/Renderer.h"

#include "../src/vulkan/vulkan-backend.h"

namespace ImGui {
	extern bool ImageButtonEx(ImGuiID id, ImTextureID user_texture_id, const ImVec2& image_size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& bg_col, const ImVec4& tint_col, ImGuiButtonFlags flags);
}

namespace StarEngine::UI {

	ImTextureID GetTextureID(Ref<Image2D> image)
	{
		return (ImTextureID)image->GetHandle().Get();
	}

	ImTextureID GetTextureIDLayer(Ref<Image2D> image, uint32_t imageLayer)
	{
		return (ImTextureID)image->GetHandle().Get();
	}

	ImTextureID GetTextureIDMip(Ref<Image2D> image, uint32_t mip)
	{
		return (ImTextureID)image->GetHandle().Get();
	}

	ImTextureID GetTextureID(Ref<Texture2D> texture)
	{
		return GetTextureID(texture->GetImage());
	}

	void Image(const Ref<Image2D>& image, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col)
	{
		SE_CORE_VERIFY(image, "Image is null");

		const auto textureID = GetTextureID(image);
		ImGui::Image(textureID, size, uv0, uv1, tint_col, border_col);
	}

	void Image(const Ref<Image2D>& image, uint32_t imageLayer, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col)
	{
		SE_CORE_VERIFY(image, "Image is null");

		const auto textureID = GetTextureIDLayer(image, imageLayer);
		ImGui::Image(textureID, size, uv0, uv1, tint_col, border_col);
	}

	void ImageMip(const Ref<Image2D>& image, uint32_t mip, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col)
	{
		SE_CORE_VERIFY(image, "Image is null");

		const auto textureID = GetTextureIDMip(image, mip);
		ImGui::Image(textureID, size, uv0, uv1, tint_col, border_col);
	}

	void Image(const Ref<Texture2D>& texture, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col)
	{
		SE_CORE_VERIFY(texture, "Texture is null");

		const auto textureID = GetTextureID(texture->GetImage());
		ImGui::Image(textureID, size, uv0, uv1, tint_col, border_col);
	}

	bool ImageButton(const Ref<Image2D>& image, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& bg_col, const ImVec4& tint_col)
	{
		return ImageButton(nullptr, image, size, uv0, uv1, bg_col, tint_col);
	}

	bool ImageButton(const char* stringID, const Ref<Image2D>& image, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& bg_col, const ImVec4& tint_col)
	{
		const auto textureID = GetTextureID(image);
		ImGuiID id = (ImGuiID)((((uint64_t)textureID) >> 32) ^ (uint32_t)(uint64_t)textureID);
		if (stringID)
		{
			const ImGuiID strID = ImGui::GetID(stringID);
			id = id ^ strID;
		}
		return ImGui::ImageButtonEx(id, textureID, size, uv0, uv1, bg_col, tint_col);

		SE_CORE_VERIFY(false, "Not supported");
		return false;
	}

	bool ImageButton(const Ref<Texture2D>& texture, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& bg_col, const ImVec4& tint_col)
	{
		return ImageButton(nullptr, texture, size, uv0, uv1, bg_col, tint_col);
	}

	bool ImageButton(const char* stringID, const Ref<Texture2D>& texture, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& bg_col, const ImVec4& tint_col)
	{
		SE_CORE_VERIFY(texture);
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

		SE_CORE_VERIFY(false, "Not supported");
		return false;
	}

}
