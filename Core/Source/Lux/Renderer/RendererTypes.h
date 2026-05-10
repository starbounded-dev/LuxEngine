#pragma once

#include <cstdint>
#include <string_view>

namespace Lux {

	using RendererID = uint32_t;

	enum class RenderingTechnique : uint8_t
	{
		Forward = 0,
		Deferred = 1
	};

	inline const char* RenderingTechniqueToString(RenderingTechnique technique)
	{
		switch (technique)
		{
			case RenderingTechnique::Forward: return "Forward";
			case RenderingTechnique::Deferred: return "Deferred";
		}

		return "Forward";
	}

	inline RenderingTechnique RenderingTechniqueFromString(std::string_view value)
	{
		if (value == "Forward" || value == "forward")
			return RenderingTechnique::Forward;
		if (value == "Deferred" || value == "deferred")
			return RenderingTechnique::Deferred;

		return RenderingTechnique::Forward;
	}

}
