// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "MaterialGraph.h"

#include "Lux/Serialization/Yaml.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <unordered_map>

namespace Lux {

	namespace {

		using Type = MaterialGraphType;

		std::string Float(float value)
		{
			if (!std::isfinite(value))
				value = 0.0f;
			std::string text = std::format("{:.7g}", value);
			if (text.find_first_of(".eE") == std::string::npos)
				text += ".0";
			return text;
		}

		std::string Literal(const glm::vec4& v, Type type)
		{
			switch (type)
			{
				case Type::Vec2: return std::format("vec2({}, {})", Float(v.x), Float(v.y));
				case Type::Vec3: return std::format("vec3({}, {}, {})", Float(v.x), Float(v.y), Float(v.z));
				case Type::Vec4: return std::format("vec4({}, {}, {}, {})", Float(v.x), Float(v.y), Float(v.z), Float(v.w));
				default: return Float(v.x);
			}
		}

		float SRGBToLinear(float c)
		{
			return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
		}

		glm::vec3 SRGBToLinear(const glm::vec3& c)
		{
			return { SRGBToLinear(c.r), SRGBToLinear(c.g), SRGBToLinear(c.b) };
		}

		std::string Convert(const std::string& expression, Type from, Type to)
		{
			if (from == to || from == Type::Generic || to == Type::Generic)
				return expression;
			if (from == Type::Float)
				return std::format("{}({})", MaterialGraphTypeToGLSL(to), expression);
			if (to == Type::Float)
				return std::format("({}).x", expression);
			if (from == Type::Vec2 && to == Type::Vec3)
				return std::format("vec3({}, 0.0)", expression);
			if (from == Type::Vec2 && to == Type::Vec4)
				return std::format("vec4({}, 0.0, 1.0)", expression);
			if (from == Type::Vec3 && to == Type::Vec4)
				return std::format("vec4({}, 1.0)", expression);
			if (to == Type::Vec2)
				return std::format("({}).xy", expression);
			return std::format("({}).xyz", expression); // Vec4 -> Vec3
		}

		// -- Node library ---------------------------------------------------------------------

		using Ctx = MaterialGraphEmitContext;
		const glm::vec4 Zero{ 0.0f };
		const glm::vec4 One{ 1.0f };

		std::string Var(Ctx& c, std::string_view suffix) { return std::format("n{}_{}", c.Node.ID, suffix); }

		std::string TextureParameter(Ctx& c, bool srgb)
		{
			MaterialGraphParameter parameter;
			parameter.Name = c.Node.Parameter.empty() ? std::format("#{}", c.Node.ID) : c.Node.Parameter;
			parameter.Kind = MaterialGraphParameterKind::Texture;
			parameter.DefaultTexture = c.Node.Texture;
			parameter.SRGB = srgb;
			parameter.Exposed = !c.Node.Parameter.empty();
			return c.AddParameter(c, parameter);
		}

		// A temporary the node's outputs share, so one texture sample feeds all of them.
		std::string Statement(Ctx& c, std::string_view type, std::string_view expression);

		std::vector<std::string> Unary(Ctx& c, std::string_view function) { return { std::format("{}({})", function, c.Inputs[0]) }; }
		std::vector<std::string> Binary(Ctx& c, std::string_view op) { return { std::format("({} {} {})", c.Inputs[0], op, c.Inputs[1]) }; }

		const std::vector<std::string_view> s_TextureModes = { "Color", "Data" };
		const std::vector<std::string_view> s_NoiseModes = { "fBm", "Multifractal", "Ridged", "Hybrid" };
		const std::vector<std::string_view> s_VoronoiModes = { "F1", "F2", "Edge" };
		const std::vector<std::string_view> s_RampModes = { "Linear", "Constant" };

		std::vector<MaterialGraphNodeInfo> BuildLibrary()
		{
			const MaterialGraphPinInfo A{ "A", Type::Generic, Zero };
			const MaterialGraphPinInfo B{ "B", Type::Generic, Zero };
			const MaterialGraphPinInfo X{ "X", Type::Generic, Zero };
			const MaterialGraphPinInfo Out{ "Result", Type::Generic };
			const MaterialGraphPinInfo UVIn{ "UV", Type::Vec2, Zero, "IN.UV" };
			const MaterialGraphPinInfo PositionIn{ "Vector", Type::Vec3, Zero, "IN.WorldPosition" };

			std::vector<MaterialGraphNodeInfo> library;
			auto add = [&](MaterialGraphNodeInfo info) { library.push_back(std::move(info)); };

			add({ "Output", "Material Output", "Output",
				{ { "Base Color", Type::Vec3, glm::vec4(0.8f) }, { "Metallic", Type::Float }, { "Roughness", Type::Float, glm::vec4(0.5f) },
				  { "Emission", Type::Vec3 }, { "Opacity", Type::Float, One }, { "Occlusion", Type::Float, One },
				  { "Normal", Type::Vec3 }, { "Specular", Type::Float, glm::vec4(0.5f) } },
				{}, {}, nullptr, "What the graph sets. Unconnected inputs keep the material's own values." });

			// Inputs
			add({ "UV", "UV", "Input", {}, { { "UV", Type::Vec2 } }, {}, [](Ctx&) -> std::vector<std::string> { return { "IN.UV" }; }, "The mesh's first texture coordinates." });
			add({ "WorldPosition", "World Position", "Input", {}, { { "Position", Type::Vec3 } }, {}, [](Ctx&) -> std::vector<std::string> { return { "IN.WorldPosition" }; } });
			add({ "ObjectPosition", "Object Position", "Input", {}, { { "Position", Type::Vec3 } }, {}, [](Ctx&) -> std::vector<std::string> { return { "IN.ObjectPosition" }; }, "Position on the object, before its transform: moves with the object." });
			add({ "Normal", "Normal", "Input", {}, { { "Normal", Type::Vec3 } }, {}, [](Ctx&) -> std::vector<std::string> { return { "IN.Normal" }; }, "The surface's world normal from the mesh." });
			add({ "ViewDirection", "View Direction", "Input", {}, { { "Direction", Type::Vec3 } }, {}, [](Ctx&) -> std::vector<std::string> { return { "IN.ViewDirection" }; }, "From the surface toward the camera." });
			add({ "CameraPosition", "Camera Position", "Input", {}, { { "Position", Type::Vec3 } }, {}, [](Ctx&) -> std::vector<std::string> { return { "IN.CameraPosition" }; } });
			add({ "Time", "Time", "Input", {}, { { "Seconds", Type::Float } }, {}, [](Ctx&) -> std::vector<std::string> { return { "IN.Time" }; } });

			// Constants and parameters
			add({ "Float", "Float", "Constant", {}, { { "Value", Type::Float } }, {},
				[](Ctx& c) -> std::vector<std::string>
				{
					if (c.Node.Parameter.empty())
						return { Float(c.Node.Value.x) };
					MaterialGraphParameter parameter;
					parameter.Name = c.Node.Parameter;
					parameter.Kind = MaterialGraphParameterKind::Float;
					parameter.Default = c.Node.Value;
					return { c.AddParameter(c, parameter) };
				}, "A number. Name it to expose it to instances." });
			add({ "Color", "Color", "Constant", {}, { { "Color", Type::Vec3 }, { "Alpha", Type::Float } }, {},
				[](Ctx& c) -> std::vector<std::string>
				{
					if (c.Node.Parameter.empty())
					{
						const glm::vec3 linear = SRGBToLinear(glm::vec3(c.Node.Value));
						return { Literal(glm::vec4(linear, 0.0f), Type::Vec3), Float(c.Node.Value.w) };
					}
					MaterialGraphParameter parameter;
					parameter.Name = c.Node.Parameter;
					parameter.Kind = MaterialGraphParameterKind::Color;
					parameter.Default = c.Node.Value;
					const std::string read = c.AddParameter(c, parameter);
					return { std::format("{}.rgb", read), std::format("{}.a", read) };
				}, "A colour. Name it to expose it to instances." });
			add({ "Vector", "Vector", "Constant", {}, { { "Vector", Type::Vec3 } }, {},
				[](Ctx& c) -> std::vector<std::string> { return { Literal(c.Node.Value, Type::Vec3) }; } });

			// Math
			add({ "Add", "Add", "Math", { A, B }, { Out }, {}, [](Ctx& c) { return Binary(c, "+"); } });
			add({ "Subtract", "Subtract", "Math", { A, B }, { Out }, {}, [](Ctx& c) { return Binary(c, "-"); } });
			add({ "Multiply", "Multiply", "Math", { A, { "B", Type::Generic, One } }, { Out }, {}, [](Ctx& c) { return Binary(c, "*"); } });
			add({ "Divide", "Divide", "Math", { A, { "B", Type::Generic, One } }, { Out }, {}, [](Ctx& c) { return Binary(c, "/"); } });
			add({ "Power", "Power", "Math", { { "Base", Type::Generic, Zero }, { "Exponent", Type::Generic, One } }, { Out }, {},
				[](Ctx& c) -> std::vector<std::string> { return { std::format("pow(max({}, 0.0), {})", c.Inputs[0], c.Inputs[1]) }; } });
			add({ "Min", "Minimum", "Math", { A, B }, { Out }, {}, [](Ctx& c) -> std::vector<std::string> { return { std::format("min({}, {})", c.Inputs[0], c.Inputs[1]) }; } });
			add({ "Max", "Maximum", "Math", { A, B }, { Out }, {}, [](Ctx& c) -> std::vector<std::string> { return { std::format("max({}, {})", c.Inputs[0], c.Inputs[1]) }; } });
			add({ "Abs", "Absolute", "Math", { X }, { Out }, {}, [](Ctx& c) { return Unary(c, "abs"); } });
			add({ "Floor", "Floor", "Math", { X }, { Out }, {}, [](Ctx& c) { return Unary(c, "floor"); } });
			add({ "Fract", "Fraction", "Math", { X }, { Out }, {}, [](Ctx& c) { return Unary(c, "fract"); } });
			add({ "Sqrt", "Square Root", "Math", { X }, { Out }, {}, [](Ctx& c) -> std::vector<std::string> { return { std::format("sqrt(max({}, 0.0))", c.Inputs[0]) }; } });
			add({ "Sin", "Sine", "Math", { X }, { Out }, {}, [](Ctx& c) { return Unary(c, "sin"); } });
			add({ "Cos", "Cosine", "Math", { X }, { Out }, {}, [](Ctx& c) { return Unary(c, "cos"); } });
			add({ "Saturate", "Saturate", "Math", { X }, { Out }, {}, [](Ctx& c) -> std::vector<std::string> { return { std::format("clamp({}, 0.0, 1.0)", c.Inputs[0]) }; } });
			add({ "OneMinus", "One Minus", "Math", { X }, { Out }, {}, [](Ctx& c) -> std::vector<std::string> { return { std::format("(1.0 - {})", c.Inputs[0]) }; } });
			add({ "Negate", "Negate", "Math", { X }, { Out }, {}, [](Ctx& c) -> std::vector<std::string> { return { std::format("(-{})", c.Inputs[0]) }; } });
			add({ "Clamp", "Clamp", "Math", { X, { "Min", Type::Generic, Zero }, { "Max", Type::Generic, One } }, { Out }, {},
				[](Ctx& c) -> std::vector<std::string> { return { std::format("clamp({}, {}, {})", c.Inputs[0], c.Inputs[1], c.Inputs[2]) }; } });
			add({ "Step", "Step", "Math", { { "Edge", Type::Generic, glm::vec4(0.5f) }, X }, { Out }, {},
				[](Ctx& c) -> std::vector<std::string> { return { std::format("step({}, {})", c.Inputs[0], c.Inputs[1]) }; } });
			add({ "Smoothstep", "Smoothstep", "Math", { { "Edge 0", Type::Generic, Zero }, { "Edge 1", Type::Generic, One }, X }, { Out }, {},
				[](Ctx& c) -> std::vector<std::string> { return { std::format("smoothstep({}, {}, {})", c.Inputs[0], c.Inputs[1], c.Inputs[2]) }; } });
			add({ "Lerp", "Mix", "Math", { A, { "B", Type::Generic, One }, { "Factor", Type::Float, glm::vec4(0.5f) } }, { Out }, {},
				[](Ctx& c) -> std::vector<std::string> { return { std::format("mix({}, {}, {})", c.Inputs[0], c.Inputs[1], c.Inputs[2]) }; }, "Blends A to B by Factor." });
			add({ "Remap", "Remap", "Math", { X, { "From Min", Type::Generic, Zero }, { "From Max", Type::Generic, One }, { "To Min", Type::Generic, Zero }, { "To Max", Type::Generic, One } }, { Out }, {},
				[](Ctx& c) -> std::vector<std::string>
				{
					return { std::format("({3} + ({0} - {1}) / max({2} - {1}, 1e-6) * ({4} - {3}))", c.Inputs[0], c.Inputs[1], c.Inputs[2], c.Inputs[3], c.Inputs[4]) };
				} });

			// Vector
			const MaterialGraphPinInfo VA{ "A", Type::Vec3 };
			const MaterialGraphPinInfo VB{ "B", Type::Vec3 };
			add({ "Dot", "Dot Product", "Vector", { VA, VB }, { { "Result", Type::Float } }, {}, [](Ctx& c) -> std::vector<std::string> { return { std::format("dot({}, {})", c.Inputs[0], c.Inputs[1]) }; } });
			add({ "Cross", "Cross Product", "Vector", { VA, VB }, { { "Result", Type::Vec3 } }, {}, [](Ctx& c) -> std::vector<std::string> { return { std::format("cross({}, {})", c.Inputs[0], c.Inputs[1]) }; } });
			add({ "Length", "Length", "Vector", { { "Vector", Type::Vec3 } }, { { "Length", Type::Float } }, {}, [](Ctx& c) { return Unary(c, "length"); } });
			add({ "Distance", "Distance", "Vector", { VA, VB }, { { "Distance", Type::Float } }, {}, [](Ctx& c) -> std::vector<std::string> { return { std::format("distance({}, {})", c.Inputs[0], c.Inputs[1]) }; } });
			add({ "Normalize", "Normalize", "Vector", { { "Vector", Type::Vec3, glm::vec4(0.0f, 0.0f, 1.0f, 0.0f) } }, { { "Vector", Type::Vec3 } }, {},
				[](Ctx& c) -> std::vector<std::string> { return { std::format("normalize({} + vec3(0.0, 0.0, 1e-8))", c.Inputs[0]) }; } });
			add({ "Reflect", "Reflect", "Vector", { { "Incident", Type::Vec3 }, { "Normal", Type::Vec3, glm::vec4(0.0f, 1.0f, 0.0f, 0.0f) } }, { { "Vector", Type::Vec3 } }, {},
				[](Ctx& c) -> std::vector<std::string> { return { std::format("reflect({}, {})", c.Inputs[0], c.Inputs[1]) }; } });
			add({ "Split", "Split", "Vector", { { "Vector", Type::Vec4 } }, { { "X", Type::Float }, { "Y", Type::Float }, { "Z", Type::Float }, { "W", Type::Float } }, {},
				[](Ctx& c) -> std::vector<std::string>
				{
					const std::string v = Statement(c, "vec4", c.Inputs[0]);
					return { v + ".x", v + ".y", v + ".z", v + ".w" };
				} });
			add({ "Combine", "Combine", "Vector", { { "X", Type::Float }, { "Y", Type::Float }, { "Z", Type::Float }, { "W", Type::Float, One } },
				{ { "Vec4", Type::Vec4 }, { "Vec3", Type::Vec3 }, { "Vec2", Type::Vec2 } }, {},
				[](Ctx& c) -> std::vector<std::string>
				{
					return { std::format("vec4({}, {}, {}, {})", c.Inputs[0], c.Inputs[1], c.Inputs[2], c.Inputs[3]),
						std::format("vec3({}, {}, {})", c.Inputs[0], c.Inputs[1], c.Inputs[2]),
						std::format("vec2({}, {})", c.Inputs[0], c.Inputs[1]) };
				} });

			// Textures
			add({ "SampleTexture", "Sample Texture", "Texture", { UVIn },
				{ { "RGBA", Type::Vec4 }, { "RGB", Type::Vec3 }, { "R", Type::Float }, { "G", Type::Float }, { "B", Type::Float }, { "A", Type::Float } },
				s_TextureModes,
				[](Ctx& c) -> std::vector<std::string>
				{
					const std::string index = TextureParameter(c, c.Node.Mode == 0);
					const std::string s = Statement(c, "vec4", std::format("SampleMaterialSceneTexture({}, {}, IN.MipBias)", index, c.Inputs[0]));
					return { s, s + ".rgb", s + ".r", s + ".g", s + ".b", s + ".a" };
				}, "Color reads the texture as a colour (sRGB); Data reads its numbers as they are (masks, roughness)." });
			add({ "NormalMap", "Normal Map", "Texture", { UVIn, { "Strength", Type::Float, One } }, { { "Normal", Type::Vec3 } }, {},
				[](Ctx& c) -> std::vector<std::string>
				{
					const std::string index = TextureParameter(c, false);
					return { std::format("LuxGraphNormalMap({}, {}, {}, IN)", index, c.Inputs[0], c.Inputs[1]) };
				}, "A tangent-space normal map, as a world normal for the Output's Normal." });
			add({ "Triplanar", "Triplanar Texture", "Texture", { { "Scale", Type::Float, One }, { "Sharpness", Type::Float, glm::vec4(4.0f) } },
				{ { "RGBA", Type::Vec4 }, { "RGB", Type::Vec3 }, { "R", Type::Float } }, s_TextureModes,
				[](Ctx& c) -> std::vector<std::string>
				{
					const std::string index = TextureParameter(c, c.Node.Mode == 0);
					const std::string s = Statement(c, "vec4", std::format("LuxGraphTriplanar({}, IN.WorldPosition * {}, IN.Normal, {}, IN.MipBias)", index, c.Inputs[0], c.Inputs[1]));
					return { s, s + ".rgb", s + ".r" };
				}, "The texture projected from the three world axes: no UVs, no stretching." });
			add({ "Bump", "Bump", "Texture", { { "Height", Type::Float }, { "Strength", Type::Float, glm::vec4(0.1f) } }, { { "Normal", Type::Vec3 } }, {},
				[](Ctx& c) -> std::vector<std::string>
				{
					return { std::format("PerturbNormalFromHeight(IN.Normal, IN.WorldPosition, {}, {})", c.Inputs[0], c.Inputs[1]) };
				}, "A normal from a height value (from noise, say), for the Output's Normal." });

			// Procedural
			add({ "Noise", "Noise", "Procedural",
				{ PositionIn, { "Scale", Type::Float, glm::vec4(5.0f) }, { "Detail", Type::Float, glm::vec4(3.0f) },
				  { "Roughness", Type::Float, glm::vec4(0.5f) }, { "Lacunarity", Type::Float, glm::vec4(2.0f) } },
				{ { "Factor", Type::Float }, { "Color", Type::Vec3 } }, s_NoiseModes,
				[](Ctx& c) -> std::vector<std::string>
				{
					const std::string p = Statement(c, "vec3", std::format("{} * {}", c.Inputs[0], c.Inputs[1]));
					const std::string args = std::format("{}, {}, {}, {}", c.Inputs[2], c.Inputs[3], c.Inputs[4], c.Node.Mode);
					return { std::format("LuxNoiseFractal({}, {})", p, args),
						std::format("vec3(LuxNoiseFractal({0}, {1}), LuxNoiseFractal({0} + vec3(31.4, 17.9, 7.3), {1}), LuxNoiseFractal({0} + vec3(13.1, 47.7, 23.5), {1}))", p, args) };
				}, "Gradient noise summed over octaves (Detail). Roughness is how much each finer octave adds." });
			add({ "Voronoi", "Voronoi", "Procedural",
				{ PositionIn, { "Scale", Type::Float, glm::vec4(5.0f) }, { "Randomness", Type::Float, One } },
				{ { "Distance", Type::Float }, { "Color", Type::Vec3 }, { "Position", Type::Vec3 } }, s_VoronoiModes,
				[](Ctx& c) -> std::vector<std::string>
				{
					const std::string v = Statement(c, "LuxVoronoiResult", std::format("LuxVoronoi({} * {}, {}, {})", c.Inputs[0], c.Inputs[1], c.Inputs[2], c.Node.Mode));
					return { v + ".Distance", v + ".Color", v + ".Position" };
				}, "Cells: distance to the nearest (F1), second nearest (F2), or to the edge between cells." });
			add({ "ColorRamp", "Color Ramp", "Procedural", { { "Factor", Type::Float, glm::vec4(0.5f) } }, { { "Color", Type::Vec3 } }, s_RampModes,
				[](Ctx& c) -> std::vector<std::string>
				{
					std::vector<glm::vec4> stops = c.Node.Ramp;
					if (stops.empty())
						stops = { glm::vec4(0.0f, 0.0f, 0.0f, 0.0f), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f) };
					std::ranges::sort(stops, {}, [](const glm::vec4& s) { return s.w; });
					const std::string t = Statement(c, "float", c.Inputs[0]);
					std::string result = Literal(glm::vec4(SRGBToLinear(glm::vec3(stops[0])), 0.0f), Type::Vec3);
					for (size_t i = 1; i < stops.size(); i++)
					{
						const std::string colour = Literal(glm::vec4(SRGBToLinear(glm::vec3(stops[i])), 0.0f), Type::Vec3);
						const std::string factor = c.Node.Mode == 1
							? std::format("step({}, {})", Float(stops[i].w), t)
							: std::format("clamp(({} - {}) / {}, 0.0, 1.0)", t, Float(stops[i - 1].w), Float(std::max(stops[i].w - stops[i - 1].w, 1e-5f)));
						result = std::format("mix({}, {}, {})", result, colour, factor);
					}
					return { result };
				}, "Maps a 0..1 factor to colours between stops." });

			return library;
		}

		struct Compiler
		{
			const MaterialGraph& Graph;
			MaterialGraph::CompileResult Result;
			uint32_t NextScalar = 0;
			std::string* Body = nullptr;

			static std::string AddParameter(MaterialGraphEmitContext& context, MaterialGraphParameter parameter)
			{
				Compiler& self = *static_cast<Compiler*>(context.Compiler);
				auto read = [](const MaterialGraphParameter& p) -> std::string
				{
					const uint32_t vector = p.Slot / 4;
					const char component = "xyzw"[p.Slot % 4];
					switch (p.Kind)
					{
						case MaterialGraphParameterKind::Float: return std::format("IN.Params[{}].{}", vector, component);
						case MaterialGraphParameterKind::Color: return std::format("IN.Params[{}]", vector);
						case MaterialGraphParameterKind::Texture: return std::format("floatBitsToUint(IN.Params[{}].{})", vector, component);
					}
					return "0.0";
				};

				for (const MaterialGraphParameter& existing : self.Result.Parameters)
				{
					if (existing.Name != parameter.Name)
						continue;
					if (existing.Kind != parameter.Kind)
					{
						self.Result.Errors.push_back({ context.Node.ID, std::format("'{}' is already a parameter of another kind", parameter.Name) });
						break;
					}
					return read(existing);
				}

				if (parameter.Kind == MaterialGraphParameterKind::Color)
					self.NextScalar = (self.NextScalar + 3) & ~3u;
				const uint32_t size = parameter.Kind == MaterialGraphParameterKind::Color ? 4u : 1u;
				if (self.NextScalar + size > MaterialGraph::MaxParameterScalars)
				{
					self.Result.Errors.push_back({ context.Node.ID, "Too many parameters and textures in this graph" });
					switch (parameter.Kind)
					{
						case MaterialGraphParameterKind::Float: return "0.0";
						case MaterialGraphParameterKind::Color: return "vec4(0.0)";
						case MaterialGraphParameterKind::Texture: return "0xffffffffu";
					}
				}
				parameter.Slot = self.NextScalar;
				self.NextScalar += size;
				self.Result.Parameters.push_back(parameter);
				return read(parameter);
			}
		};

		thread_local std::string* s_StatementBody = nullptr;

		std::string Statement(Ctx& c, std::string_view type, std::string_view expression)
		{
			const std::string name = Var(c, "s");
			if (s_StatementBody)
				*s_StatementBody += std::format("\t{} {} = {};\n", type, name, expression);
			return name;
		}
	}

	const char* MaterialGraphTypeToGLSL(MaterialGraphType type)
	{
		switch (type)
		{
			case MaterialGraphType::Vec2: return "vec2";
			case MaterialGraphType::Vec3: return "vec3";
			case MaterialGraphType::Vec4: return "vec4";
			default: return "float";
		}
	}

	std::span<const MaterialGraphNodeInfo> MaterialGraph::GetNodeLibrary()
	{
		static const std::vector<MaterialGraphNodeInfo> s_Library = BuildLibrary();
		return s_Library;
	}

	const MaterialGraphNodeInfo* MaterialGraph::FindNodeInfo(std::string_view type)
	{
		for (const MaterialGraphNodeInfo& info : GetNodeLibrary())
		{
			if (info.Type == type)
				return &info;
		}
		return nullptr;
	}

	Ref<MaterialGraph> MaterialGraph::CreateDefault()
	{
		Ref<MaterialGraph> graph = Ref<MaterialGraph>::Create();
		graph->AddNode("Output", glm::vec2(400.0f, 0.0f));
		return graph;
	}

	Ref<MaterialGraph> MaterialGraph::Clone() const
	{
		Ref<MaterialGraph> copy = Ref<MaterialGraph>::Create();
		copy->Nodes = Nodes;
		copy->Links = Links;
		copy->Comments = Comments;
		copy->NextID = NextID;
		copy->DispatchIndex = DispatchIndex;
		return copy;
	}

	MaterialGraphNode* MaterialGraph::FindNode(uint32_t id)
	{
		auto it = std::ranges::find(Nodes, id, &MaterialGraphNode::ID);
		return it != Nodes.end() ? &*it : nullptr;
	}

	const MaterialGraphNode* MaterialGraph::FindNode(uint32_t id) const
	{
		auto it = std::ranges::find(Nodes, id, &MaterialGraphNode::ID);
		return it != Nodes.end() ? &*it : nullptr;
	}

	const MaterialGraphLink* MaterialGraph::FindLinkInto(uint32_t node, uint32_t pin) const
	{
		for (const MaterialGraphLink& link : Links)
		{
			if (link.ToNode == node && link.ToPin == pin)
				return &link;
		}
		return nullptr;
	}

	MaterialGraphNode* MaterialGraph::AddNode(std::string_view type, glm::vec2 position)
	{
		const MaterialGraphNodeInfo* info = FindNodeInfo(type);
		if (!info)
			return nullptr;

		MaterialGraphNode node;
		node.ID = NextID++;
		node.Type = std::string(type);
		node.Position = position;
		for (const MaterialGraphPinInfo& pin : info->Inputs)
			node.InputValues.push_back(pin.Default);
		if (type == "Color")
			node.Value = glm::vec4(1.0f);
		if (type == "ColorRamp")
			node.Ramp = { glm::vec4(0.0f, 0.0f, 0.0f, 0.0f), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f) };
		Nodes.push_back(std::move(node));
		return &Nodes.back();
	}

	void MaterialGraph::RemoveNode(uint32_t id)
	{
		std::erase_if(Links, [id](const MaterialGraphLink& link) { return link.FromNode == id || link.ToNode == id; });
		std::erase_if(Nodes, [id](const MaterialGraphNode& node) { return node.ID == id; });
	}

	void MaterialGraph::CopyContentFrom(const MaterialGraph& other)
	{
		Nodes = other.Nodes;
		Links = other.Links;
		Comments = other.Comments;
		NextID = other.NextID;
	}

	bool MaterialGraph::Connect(uint32_t fromNode, uint32_t fromPin, uint32_t toNode, uint32_t toPin)
	{
		if (!CanConnect(fromNode, fromPin, toNode, toPin))
			return false;
		std::erase_if(Links, [&](const MaterialGraphLink& link) { return link.ToNode == toNode && link.ToPin == toPin; });
		Links.push_back({ NextID++, fromNode, fromPin, toNode, toPin });
		return true;
	}

	bool MaterialGraph::CanConnect(uint32_t fromNode, uint32_t fromPin, uint32_t toNode, uint32_t toPin) const
	{
		const MaterialGraphNode* from = FindNode(fromNode);
		const MaterialGraphNode* to = FindNode(toNode);
		if (!from || !to || fromNode == toNode)
			return false;
		const MaterialGraphNodeInfo* fromInfo = FindNodeInfo(from->Type);
		const MaterialGraphNodeInfo* toInfo = FindNodeInfo(to->Type);
		if (!fromInfo || !toInfo || fromPin >= fromInfo->Outputs.size() || toPin >= toInfo->Inputs.size())
			return false;

		// A loop: `to` already feeds `from`.
		std::vector<uint32_t> pending = { fromNode };
		std::vector<uint32_t> visited;
		while (!pending.empty())
		{
			const uint32_t current = pending.back();
			pending.pop_back();
			if (current == toNode)
				return false;
			if (std::ranges::find(visited, current) != visited.end())
				continue;
			visited.push_back(current);
			for (const MaterialGraphLink& link : Links)
			{
				if (link.ToNode == current)
					pending.push_back(link.FromNode);
			}
		}
		return true;
	}

	void MaterialGraph::RemoveLink(uint32_t id)
	{
		std::erase_if(Links, [id](const MaterialGraphLink& link) { return link.ID == id; });
	}

	MaterialGraph::CompileResult MaterialGraph::Compile() const
	{
		Compiler compiler{ *this };
		CompileResult& result = compiler.Result;

		const MaterialGraphNode* output = nullptr;
		for (const MaterialGraphNode& node : Nodes)
		{
			if (node.Type == "Output")
			{
				output = &node;
				break;
			}
		}
		if (!output)
		{
			result.Errors.push_back({ 0, "The graph has no Material Output node" });
			return result;
		}

		// Post-order from the output: every node after the nodes it reads. Nodes that do not reach the
		// output are left out.
		std::vector<uint32_t> order;
		std::unordered_map<uint32_t, int> state; // 1 visiting, 2 done
		std::function<void(uint32_t)> visit = [&](uint32_t id)
		{
			int& s = state[id];
			if (s == 2)
				return;
			if (s == 1)
			{
				result.Errors.push_back({ id, "The graph has a loop" });
				return;
			}
			s = 1;
			for (const MaterialGraphLink& link : Links)
			{
				if (link.ToNode == id)
					visit(link.FromNode);
			}
			state[id] = 2;
			order.push_back(id);
		};
		visit(output->ID);

		std::unordered_map<uint32_t, std::vector<Type>> outputTypes;
		auto inputExpression = [&](const MaterialGraphNode& node, uint32_t pin, const MaterialGraphPinInfo& info, Type& outType) -> std::string
		{
			if (const MaterialGraphLink* link = FindLinkInto(node.ID, pin))
			{
				auto it = outputTypes.find(link->FromNode);
				if (it != outputTypes.end() && link->FromPin < it->second.size())
				{
					outType = it->second[link->FromPin];
					return std::format("n{}_{}", link->FromNode, link->FromPin);
				}
			}
			if (!info.DefaultExpression.empty())
			{
				outType = info.Type;
				return std::string(info.DefaultExpression);
			}
			const glm::vec4 value = pin < node.InputValues.size() ? node.InputValues[pin] : info.Default;
			outType = info.Type == Type::Generic ? Type::Float : info.Type;
			return Literal(value, outType);
		};

		s_StatementBody = &result.Body;
		for (uint32_t id : order)
		{
			const MaterialGraphNode* node = FindNode(id);
			if (!node || node->Type == "Output")
				continue;
			const MaterialGraphNodeInfo* info = FindNodeInfo(node->Type);
			if (!info || !info->Emit)
			{
				result.Errors.push_back({ id, std::format("Unknown node type '{}'", node->Type) });
				outputTypes[id] = std::vector<Type>(info ? info->Outputs.size() : 0, Type::Float);
				continue;
			}

			std::vector<std::string> raw(info->Inputs.size());
			std::vector<Type> rawTypes(info->Inputs.size());
			Type generic = Type::Float;
			for (uint32_t pin = 0; pin < info->Inputs.size(); pin++)
			{
				raw[pin] = inputExpression(*node, pin, info->Inputs[pin], rawTypes[pin]);
				if (info->Inputs[pin].Type == Type::Generic)
					generic = std::max(generic, rawTypes[pin]);
			}

			MaterialGraphEmitContext context{ *node, generic, {}, &Compiler::AddParameter, &compiler };
			for (uint32_t pin = 0; pin < info->Inputs.size(); pin++)
			{
				const Type target = info->Inputs[pin].Type == Type::Generic ? generic : info->Inputs[pin].Type;
				context.Inputs.push_back(Convert(raw[pin], rawTypes[pin], target));
			}

			const std::vector<std::string> expressions = info->Emit(context);
			std::vector<Type>& types = outputTypes[id];
			for (uint32_t pin = 0; pin < info->Outputs.size(); pin++)
			{
				const Type type = info->Outputs[pin].Type == Type::Generic ? generic : info->Outputs[pin].Type;
				types.push_back(type);
				const std::string expression = pin < expressions.size() ? expressions[pin] : Literal(glm::vec4(0.0f), type);
				result.Body += std::format("\t{} n{}_{} = {};\n", MaterialGraphTypeToGLSL(type), id, pin, expression);
			}
		}
		s_StatementBody = nullptr;

		static constexpr std::string_view s_OutputFields[] = { "BaseColor", "Metallic", "Roughness", "Emission", "Opacity", "Occlusion", "Normal", "Specular" };
		const MaterialGraphNodeInfo* outputInfo = FindNodeInfo("Output");
		for (uint32_t pin = 0; pin < outputInfo->Inputs.size(); pin++)
		{
			const MaterialGraphLink* link = FindLinkInto(output->ID, pin);
			if (!link)
				continue;
			Type type = Type::Float;
			const std::string value = inputExpression(*output, pin, outputInfo->Inputs[pin], type);
			const std::string converted = Convert(value, type, outputInfo->Inputs[pin].Type);
			if (s_OutputFields[pin] == "Normal")
				result.Body += std::format("\tOUT.Normal = normalize({});\n", converted);
			else
				result.Body += std::format("\tOUT.{} = {};\n", s_OutputFields[pin], converted);
		}

		return result;
	}

	void MaterialGraph::Serialize(Yaml::Writer& out) const
	{
		out << Yaml::BeginMap;
		out << Yaml::Key << "NextID" << Yaml::Value << NextID;

		out << Yaml::Key << "Nodes" << Yaml::Value << Yaml::BeginSeq;
		for (const MaterialGraphNode& node : Nodes)
		{
			out << Yaml::BeginMap;
			out << Yaml::Key << "ID" << Yaml::Value << node.ID;
			out << Yaml::Key << "Type" << Yaml::Value << node.Type;
			out << Yaml::Key << "Position" << Yaml::Value << node.Position;
			if (!node.InputValues.empty())
				out << Yaml::Key << "Inputs" << Yaml::Value << node.InputValues;
			if (node.Value != glm::vec4(0.0f))
				out << Yaml::Key << "Value" << Yaml::Value << node.Value;
			if (node.Texture)
				out << Yaml::Key << "Texture" << Yaml::Value << static_cast<uint64_t>(node.Texture);
			if (node.Mode)
				out << Yaml::Key << "Mode" << Yaml::Value << node.Mode;
			if (!node.Parameter.empty())
				out << Yaml::Key << "Parameter" << Yaml::Value << node.Parameter;
			if (!node.Ramp.empty())
				out << Yaml::Key << "Ramp" << Yaml::Value << node.Ramp;
			out << Yaml::EndMap;
		}
		out << Yaml::EndSeq;

		out << Yaml::Key << "Links" << Yaml::Value << Yaml::BeginSeq;
		for (const MaterialGraphLink& link : Links)
		{
			out << Yaml::Flow << Yaml::BeginSeq << link.ID << link.FromNode << link.FromPin << link.ToNode << link.ToPin << Yaml::EndSeq;
		}
		out << Yaml::EndSeq;

		if (!Comments.empty())
		{
			out << Yaml::Key << "Comments" << Yaml::Value << Yaml::BeginSeq;
			for (const MaterialGraphComment& comment : Comments)
			{
				out << Yaml::BeginMap;
				out << Yaml::Key << "ID" << Yaml::Value << comment.ID;
				out << Yaml::Key << "Text" << Yaml::Value << comment.Text;
				out << Yaml::Key << "Position" << Yaml::Value << comment.Position;
				out << Yaml::Key << "Size" << Yaml::Value << comment.Size;
				out << Yaml::Key << "Color" << Yaml::Value << comment.Color;
				out << Yaml::EndMap;
			}
			out << Yaml::EndSeq;
		}
		out << Yaml::EndMap;
	}

	Ref<MaterialGraph> MaterialGraph::Deserialize(const Yaml::Node& node)
	{
		if (!node || !node.IsMap())
			return nullptr;

		Ref<MaterialGraph> graph = Ref<MaterialGraph>::Create();
		uint32_t highest = 0;
		for (const Yaml::Node item : node["Nodes"])
		{
			MaterialGraphNode graphNode;
			graphNode.ID = item["ID"].as<uint32_t>(0);
			graphNode.Type = item["Type"].as<std::string>("");
			graphNode.Position = item["Position"].as<glm::vec2>(glm::vec2(0.0f));
			for (const Yaml::Node value : item["Inputs"])
				graphNode.InputValues.push_back(value.as<glm::vec4>(glm::vec4(0.0f)));
			graphNode.Value = item["Value"].as<glm::vec4>(glm::vec4(0.0f));
			graphNode.Texture = item["Texture"].as<uint64_t>(0);
			graphNode.Mode = item["Mode"].as<int32_t>(0);
			graphNode.Parameter = item["Parameter"].as<std::string>("");
			for (const Yaml::Node stop : item["Ramp"])
				graphNode.Ramp.push_back(stop.as<glm::vec4>(glm::vec4(0.0f)));

			// A file from an older library: pad the pin values to what the node has now.
			if (const MaterialGraphNodeInfo* info = FindNodeInfo(graphNode.Type))
			{
				for (size_t pin = graphNode.InputValues.size(); pin < info->Inputs.size(); pin++)
					graphNode.InputValues.push_back(info->Inputs[pin].Default);
			}
			highest = std::max(highest, graphNode.ID);
			graph->Nodes.push_back(std::move(graphNode));
		}

		for (const Yaml::Node item : node["Links"])
		{
			MaterialGraphLink link;
			link.ID = item[0].as<uint32_t>(0);
			link.FromNode = item[1].as<uint32_t>(0);
			link.FromPin = item[2].as<uint32_t>(0);
			link.ToNode = item[3].as<uint32_t>(0);
			link.ToPin = item[4].as<uint32_t>(0);
			highest = std::max(highest, link.ID);
			if (graph->FindNode(link.FromNode) && graph->FindNode(link.ToNode))
				graph->Links.push_back(link);
		}

		for (const Yaml::Node item : node["Comments"])
		{
			MaterialGraphComment comment;
			comment.ID = item["ID"].as<uint32_t>(0);
			comment.Text = item["Text"].as<std::string>("");
			comment.Position = item["Position"].as<glm::vec2>(glm::vec2(0.0f));
			comment.Size = item["Size"].as<glm::vec2>(glm::vec2(300.0f, 200.0f));
			comment.Color = item["Color"].as<glm::vec4>(glm::vec4(0.3f, 0.3f, 0.3f, 0.5f));
			highest = std::max(highest, comment.ID);
			graph->Comments.push_back(std::move(comment));
		}

		graph->NextID = std::max(node["NextID"].as<uint32_t>(1), highest + 1);
		return graph;
	}

}
