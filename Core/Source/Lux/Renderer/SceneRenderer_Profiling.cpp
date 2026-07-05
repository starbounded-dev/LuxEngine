#include "lpch.h"
#include "SceneRendererInternal.h"

namespace Lux {

	SceneRenderer::ScopedCPUProfile::ScopedCPUProfile(SceneRenderer& renderer, const char* name)
		: Renderer(renderer), Name(name)
	{
		const RendererConfig& rendererConfig = Lux::Renderer::GetConfig();
		Active = ShouldCollectBasicRendererDiagnostics(rendererConfig);
		if (Active)
			ProfileTimer.emplace();

#if LUX_ENABLE_PROFILING
		// Open a Tracy zone whose name is the pass name. Because every SceneRenderer
		// pass already wraps itself in a ScopedCPUProfile, this single chokepoint makes
		// the entire render pipeline visible in a Tracy capture (no per-pass edits).
		TracyActive = ShouldCollectFullRendererDiagnostics(rendererConfig);
		if (TracyActive)
		{
			const uint64_t srcloc = ___tracy_alloc_srcloc_name(
				(uint32_t)__LINE__, __FILE__, sizeof(__FILE__) - 1,
				"SceneRenderer::Pass", 19,
				name, std::strlen(name), 0);
			ProfileZone = ___tracy_emit_zone_begin_alloc(srcloc, 1);
		}
#endif
	}

	SceneRenderer::ScopedCPUProfile::~ScopedCPUProfile()
	{
#if LUX_ENABLE_PROFILING
		if (TracyActive)
			___tracy_emit_zone_end(ProfileZone);
#endif
		if (Active && ProfileTimer)
			Renderer.RecordCPUProfile(Name, ProfileTimer->ElapsedMillis());
	}

	SceneRenderer::PassProfile& SceneRenderer::GetOrCreatePassProfile(const char* name)
	{
		for (PassProfile& profile : m_Statistics.PassProfiles)
		{
			if (std::strcmp(profile.Name, name) == 0)
				return profile;
		}

		PassProfile& profile = m_Statistics.PassProfiles.emplace_back();
		profile.Name = name;
		return profile;
	}

	void SceneRenderer::ResetProfilingData()
	{
		if (!ShouldCollectBasicRendererDiagnostics(Renderer::GetConfig()))
		{
			m_Statistics.TotalCPUTime = 0.0f;
			return;
		}

		if (m_Statistics.PassProfiles.size() != s_ProfiledSceneRendererPasses.size())
		{
			m_Statistics.PassProfiles.clear();
			m_Statistics.PassProfiles.reserve(s_ProfiledSceneRendererPasses.size());
			for (const char* passName : s_ProfiledSceneRendererPasses)
			{
				PassProfile& profile = m_Statistics.PassProfiles.emplace_back();
				profile.Name = passName;
			}
		}

		m_Statistics.TotalCPUTime = 0.0f;
		for (PassProfile& profile : m_Statistics.PassProfiles)
		{
			profile.CPUTime = 0.0f;
			profile.GPUTime = 0.0f;
			profile.Active = false;
			profile.GPUActive = false;
		}
	}

	void SceneRenderer::RecordCPUProfile(const char* name, float cpuTime)
	{
		PassProfile& profile = GetOrCreatePassProfile(name);
		profile.CPUTime += cpuTime;
		profile.Active = true;
		m_Statistics.TotalCPUTime += cpuTime;
	}

	void SceneRenderer::BeginProfiledGPU(const char* name)
	{
		PassProfile& profile = GetOrCreatePassProfile(name);
		profile.GPUActive = true;
		Renderer::BeginGPUPerfMarker(m_CommandBuffer, name);
	}

	void SceneRenderer::EndProfiledGPU()
	{
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::UpdateGPUProfileTimes()
	{
		if (!m_CommandBuffer)
			return;

		for (PassProfile& profile : m_Statistics.PassProfiles)
		{
			if (!profile.GPUActive)
				continue;

			profile.GPUTime = m_CommandBuffer->GetTimerQueryTime(profile.Name);
		}
	}

}
