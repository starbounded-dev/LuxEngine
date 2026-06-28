#include "lpch.h"
#include "DebugRenderer.h"

namespace Lux {

	void DebugRenderer::DrawLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_RenderQueue.emplace_back([p0, p1, color](Ref<Renderer2D> renderer)
			{
				renderer->DrawLine(p0, p1, color);
			});
	}

	void DebugRenderer::DrawCircle(const glm::vec3& centre, const glm::vec3& rotation, float radius, const glm::vec4& color)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_RenderQueue.emplace_back([centre, rotation, radius, color](Ref<Renderer2D> renderer)
			{
				renderer->DrawCircle(centre, rotation, radius, color);
			});
	}

	void DebugRenderer::DrawTransform(const glm::mat4& transform, float scale)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_RenderQueue.emplace_back([transform, scale](Ref<Renderer2D> renderer)
			{
				renderer->DrawTransform(transform, scale);
			});
	}

	void DebugRenderer::DrawQuadBillboard(const glm::vec3& translation, const glm::vec2& size, const glm::vec4& color)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_RenderQueue.emplace_back([translation, size, color](Ref<Renderer2D> renderer)
			{
				renderer->DrawQuadBillboard(translation, size, color);
			});
	}

	void DebugRenderer::SetLineWidth(float thickness)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_RenderQueue.emplace_back([thickness](Ref<Renderer2D> renderer)
			{
				renderer->SetLineWidth(thickness);
			});
	}

}
