// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Asset/Asset.h"
#include "Lux/Core/Base.h"
#include "Lux/Core/Ref.h"

#include <glm/glm.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Lux::Yaml { class Writer; class Node; }

namespace Lux {

	// A material graph: nodes that compute surface inputs (base colour, roughness, normal...) per
	// pixel. It compiles to a GLSL function the G-buffer and transparent shaders call for materials
	// that use it (MaterialGraphRegistry); its outputs replace the standard material's values that
	// they connect to, so a graph only has to set what it changes. No ImGui here: the canvas is in
	// the editor.

	// The value a pin carries. Generic pins take the widest type connected to the node's generic
	// inputs (float when none), so one Add node works for floats and vectors.
	enum class MaterialGraphType : uint8_t
	{
		Generic = 0,
		Float = 1,
		Vec2 = 2,
		Vec3 = 3,
		Vec4 = 4
	};

	const char* MaterialGraphTypeToGLSL(MaterialGraphType type);

	struct MaterialGraphNode
	{
		uint32_t ID = 0;
		std::string Type;
		glm::vec2 Position{ 0.0f };
		// Per input pin: the value it uses while nothing is connected to it.
		std::vector<glm::vec4> InputValues;
		// Constants, parameters and colours (colours in sRGB, as picked).
		glm::vec4 Value{ 0.0f };
		AssetHandle Texture = 0;
		// The node's variant: the noise type, a texture's colour space...
		int32_t Mode = 0;
		// Non-empty on a Float, Color or Texture node: exposed under this name, editable per material
		// and per instance without recompiling the shader.
		std::string Parameter;
		// Color Ramp stops: rgb = colour (sRGB), w = position 0..1, kept sorted by position.
		std::vector<glm::vec4> Ramp;
	};

	struct MaterialGraphLink
	{
		uint32_t ID = 0;
		uint32_t FromNode = 0;
		uint32_t FromPin = 0;
		uint32_t ToNode = 0;
		uint32_t ToPin = 0;
	};

	struct MaterialGraphComment
	{
		uint32_t ID = 0;
		std::string Text;
		glm::vec2 Position{ 0.0f };
		glm::vec2 Size{ 300.0f, 200.0f };
		glm::vec4 Color{ 0.3f, 0.3f, 0.3f, 0.5f };
	};

	enum class MaterialGraphParameterKind : uint8_t
	{
		Float,
		Color,   // a linear vec4 in the row; picked and stored in sRGB
		Texture  // a bindless texture index in the row
	};

	// A value the material row carries for the graph (GPUMaterialData::GraphParameters): the
	// exposed parameters, and every texture a graph samples.
	struct MaterialGraphParameter
	{
		std::string Name;        // "#<node>" for an unexposed texture
		MaterialGraphParameterKind Kind = MaterialGraphParameterKind::Float;
		uint32_t Slot = 0;       // scalar index into the row's GraphParameters (a Color starts a vec4)
		glm::vec4 Default{ 0.0f };
		AssetHandle DefaultTexture = 0;
		bool SRGB = true;        // Texture: sampled as colour (sRGB) or data (linear)
		bool Exposed = true;
	};

	// A parameter's value on a material: Value for Float (x) and Color (sRGB rgba), Texture for textures.
	struct MaterialGraphParameterValue
	{
		glm::vec4 Value{ 0.0f };
		AssetHandle Texture = 0;

		bool operator==(const MaterialGraphParameterValue&) const = default;
	};

	struct MaterialGraphPinInfo
	{
		std::string_view Name;
		MaterialGraphType Type = MaterialGraphType::Float;
		glm::vec4 Default{ 0.0f };
		// When set, an unconnected input reads this graph input instead of its value ("IN.UV").
		std::string_view DefaultExpression;
	};

	struct MaterialGraphEmitContext;
	using MaterialGraphEmitFn = std::vector<std::string>(*)(MaterialGraphEmitContext& context);

	struct MaterialGraphNodeInfo
	{
		std::string_view Type;
		std::string_view Name;
		std::string_view Category;
		std::vector<MaterialGraphPinInfo> Inputs;
		std::vector<MaterialGraphPinInfo> Outputs;
		std::vector<std::string_view> Modes; // names for MaterialGraphNode::Mode, when the node has variants
		MaterialGraphEmitFn Emit = nullptr;
		std::string_view Help;
	};

	class MaterialGraph : public RefCounted
	{
	public:
		static constexpr uint32_t MaxParameterScalars = 32; // GPUMaterialData::GraphParameters: 8 vec4

		std::vector<MaterialGraphNode> Nodes;
		std::vector<MaterialGraphLink> Links;
		std::vector<MaterialGraphComment> Comments;
		uint32_t NextID = 1;

		// The graph's function in the generated shader include, 1-based; 0 renders the standard
		// material. Set by MaterialGraphRegistry in the editor and read from the asset pack at run time.
		uint32_t DispatchIndex = 0;

		// A graph with just an Output node.
		static Ref<MaterialGraph> CreateDefault();
		Ref<MaterialGraph> Clone() const;

		MaterialGraphNode* FindNode(uint32_t id);
		const MaterialGraphNode* FindNode(uint32_t id) const;
		const MaterialGraphLink* FindLinkInto(uint32_t node, uint32_t pin) const;
		// Adds a node of `type` with its pins' default values. Null for an unknown type.
		MaterialGraphNode* AddNode(std::string_view type, glm::vec2 position);
		void RemoveNode(uint32_t id);
		// Connects an output to an input, replacing what that input had. False, changing nothing, for
		// pins that do not exist or a link that would make a loop.
		bool Connect(uint32_t fromNode, uint32_t fromPin, uint32_t toNode, uint32_t toPin);
		// Whether Connect would succeed, without changing anything.
		bool CanConnect(uint32_t fromNode, uint32_t fromPin, uint32_t toNode, uint32_t toPin) const;
		// Takes `other`'s nodes, links and comments (undo, revert); keeps this graph's identity, which
		// the material and its instances share.
		void CopyContentFrom(const MaterialGraph& other);
		void RemoveLink(uint32_t id);

		struct Error
		{
			uint32_t Node = 0; // 0 for the graph as a whole
			std::string Message;
		};

		struct CompileResult
		{
			std::string Body;       // statements of the surface function
			std::vector<MaterialGraphParameter> Parameters;
			std::vector<Error> Errors;
		};
		// The surface function's body. Parameters come out in slot order, stable for a given graph.
		CompileResult Compile() const;

		void Serialize(Yaml::Writer& out) const;
		static Ref<MaterialGraph> Deserialize(const Yaml::Node& node);

		static std::span<const MaterialGraphNodeInfo> GetNodeLibrary();
		static const MaterialGraphNodeInfo* FindNodeInfo(std::string_view type);
	};

	// Passed to a node's Emit while compiling.
	struct MaterialGraphEmitContext
	{
		const MaterialGraphNode& Node;
		MaterialGraphType Type; // the resolved type of the node's generic pins
		std::vector<std::string> Inputs; // each input as GLSL, already in the pin's type
		// Registers a value the row carries; returns its GLSL read.
		std::string (*AddParameter)(MaterialGraphEmitContext& context, MaterialGraphParameter parameter);
		void* Compiler = nullptr;
	};

}
