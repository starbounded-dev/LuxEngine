// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Ref.h"

#include <NRI.h>
#include <Extensions/NRIRayTracing.h>

namespace Lux {

	// A top-level acceleration structure a shader reads with `accelerationStructureEXT` (ray queries).
	// DescriptorSetManager binds it like any other input (ComputePass::SetInput). The descriptor
	// and the identity pointer change only when the structure is recreated, which rebakes the sets
	// that hold it. Implemented by RayTracingScene; acceleration structures are never tracked by
	// the resource state tracker (RayTracingScene emits its own barrier after a build).
	class RHIAccelerationStructure : public RefCounted
	{
	public:
		virtual nri::Descriptor* GetDescriptor() const = 0;
		virtual const void* GetIdentity() const = 0;
	};

}
