#pragma once

#include <algorithm>
#include <cmath>

namespace Lux {

	// ─────────────────────────────────────────────────────────────────────────
	// Physically based exposure helpers (Filament-style EV100 model).
	//
	// The renderer keeps scene color in linear HDR. The final exposure is a
	// single linear multiplier applied before tonemapping. These helpers convert
	// between physical camera settings, exposure value (EV100) and that
	// multiplier so that Manual / Camera / Auto exposure all share one model.
	//
	// References: google.github.io/filament/Filament.md (Physically based camera)
	// ─────────────────────────────────────────────────────────────────────────
	namespace Exposure {

		// EV100 from physical camera settings.
		//   EV100 = log2( N^2 / t ) - log2( S / 100 )
		// N = aperture (f-stops), t = shutter speed (seconds), S = ISO.
		inline float EV100FromCamera(float aperture, float shutterSpeed, float iso)
		{
			aperture = std::max(aperture, 1e-4f);
			shutterSpeed = std::max(shutterSpeed, 1e-6f);
			iso = std::max(iso, 1e-2f);
			return std::log2((aperture * aperture) / shutterSpeed) - std::log2(iso / 100.0f);
		}

		// EV100 from an average scene luminance (cd/m^2), used by auto-exposure.
		//   EV100 = log2( L * S / K ),  S = 100, K = 12.5 (reflected-light meter constant)
		inline float EV100FromLuminance(float averageLuminance)
		{
			averageLuminance = std::max(averageLuminance, 1e-5f);
			return std::log2(averageLuminance * 100.0f / 12.5f);
		}

		// Linear exposure multiplier from EV100.
		//   maxLuminance = 1.2 * 2^EV100 ; exposure = 1 / maxLuminance
		inline float ExposureFromEV100(float ev100)
		{
			return 1.0f / (1.2f * std::exp2(ev100));
		}

		// Convenience: full camera -> multiplier, with EV compensation (+ = brighter).
		inline float ExposureFromCamera(float aperture, float shutterSpeed, float iso, float exposureCompensation)
		{
			return ExposureFromEV100(EV100FromCamera(aperture, shutterSpeed, iso) - exposureCompensation);
		}

	} // namespace Exposure

} // namespace Lux
