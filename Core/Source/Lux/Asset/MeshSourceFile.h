#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Core/Math/AABB.h"

namespace Lux
{
	struct MeshSourceFile
	{
		enum class MeshFlags : uint32_t
		{
			HasMaterials = BIT(0)
		};

		struct Metadata
		{
			uint32_t Flags = 0;
			AABB BoundingBox;

			uint64_t NodeArrayOffset = 0;
			uint64_t NodeArraySize = 0;

			uint64_t SubmeshArrayOffset = 0;
			uint64_t SubmeshArraySize = 0;

			uint64_t MaterialArrayOffset = 0;
			uint64_t MaterialArraySize = 0;

			uint64_t VertexBufferOffset = 0;
			uint64_t VertexBufferSize = 0;

			uint64_t IndexBufferOffset = 0;
			uint64_t IndexBufferSize = 0;
		};

		struct FileHeader
		{
			const char HEADER[4] = { 'L', 'Z', 'M', 'S' };
			uint32_t Version = 1;
		};

		FileHeader Header;
		Metadata Data;
	};
}
