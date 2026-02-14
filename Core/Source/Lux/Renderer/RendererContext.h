#pragma once

#include "Lux/Core/Ref.h"

struct GLFWwindow;

namespace Lux {

	class RendererContext : public RefCounted
	{
	public:
		RendererContext() = default;
		virtual ~RendererContext() = default;

		virtual void Init() = 0;

		static Ref<RendererContext> Create();
	};

}
