#pragma once

#include <cstdint>
#include <string>

namespace Lux {

	enum class RendererDiagnosticsMode : uint32_t
	{
		Off = 0,
		Basic,
		Full
	};

	struct RendererConfig
	{
		uint32_t FramesInFlight = 3;

		bool ComputeEnvironmentMaps = true;
		RendererDiagnosticsMode DiagnosticsMode = RendererDiagnosticsMode::Off;
#if defined(LUX_DIST)
		bool EnableGraphicsValidation = false;
#else
		bool EnableGraphicsValidation = true;
#endif

		// Tiering settings
		uint32_t EnvironmentMapResolution = 1024;
		uint32_t IrradianceMapComputeSamples = 512;

		std::string ShaderPackPath;
	};

	inline bool ShouldCollectBasicRendererDiagnostics(const RendererConfig& config)
	{
		return config.DiagnosticsMode != RendererDiagnosticsMode::Off;
	}

	inline bool ShouldCollectFullRendererDiagnostics(const RendererConfig& config)
	{
		return config.DiagnosticsMode == RendererDiagnosticsMode::Full;
	}

}
