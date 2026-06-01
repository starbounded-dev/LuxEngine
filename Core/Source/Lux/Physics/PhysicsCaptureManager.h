#pragma once

#include "Lux/Core/Ref.h"

namespace Lux {

	class PhysicsCaptureManager : public RefCounted
	{
	public:
		virtual ~PhysicsCaptureManager() = default;
		virtual void BeginCapture() {}
		virtual void EndCapture() {}
	};

}
