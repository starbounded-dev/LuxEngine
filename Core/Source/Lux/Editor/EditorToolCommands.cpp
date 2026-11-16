// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

// The editor tool host's built-in commands. Component edits are generic: the entity is serialized,
// the component's YAML is changed, and the entity is patched back through the same path undo uses
// (SceneSerializer::ApplyEntitySnapshots), so every serialized component and field is reachable
// without per-type code, and the result is checked by serializing again.

#include "lpch.h"
#include "EditorToolHost.h"

#include "Lux/Editor/SelectionManager.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Scene/SceneSerializer.h"
#include "Lux/Serialization/Yaml.h"

#include <charconv>
#include <format>
#include <optional>

namespace Lux {

	namespace {

		// -- A small editable YAML tree (Yaml::Node is read-only) -----------------------------------

		struct YamlTree
		{
			enum class Kind { Null, Scalar, Sequence, Map } Type = Kind::Null;
			std::string Text;
			std::vector<YamlTree> Items;     // Sequence
			// Map, in order. Parallel vectors: a vector of pair<string, YamlTree> inside YamlTree
			// needs the complete type, which libstdc++ rejects (a vector of YamlTree is fine).
			std::vector<std::string> Keys;
			std::vector<YamlTree> Values;

			YamlTree* Find(std::string_view key)
			{
				for (size_t i = 0; i < Keys.size(); i++)
				{
					if (Keys[i] == key)
						return &Values[i];
				}
				return nullptr;
			}

			YamlTree& Add(std::string key, YamlTree value)
			{
				Keys.push_back(std::move(key));
				Values.push_back(std::move(value));
				return Values.back();
			}

			void Remove(std::string_view key)
			{
				for (size_t i = 0; i < Keys.size(); i++)
				{
					if (Keys[i] == key)
					{
						Keys.erase(Keys.begin() + i);
						Values.erase(Values.begin() + i);
						return;
					}
				}
			}
		};

		YamlTree FromYaml(const Yaml::Node& node)
		{
			YamlTree tree;
			switch (node.Type())
			{
				case Yaml::NodeType::Scalar:
					tree.Type = YamlTree::Kind::Scalar;
					tree.Text = std::string(node.Scalar());
					break;
				case Yaml::NodeType::Sequence:
					tree.Type = YamlTree::Kind::Sequence;
					for (const auto& item : node)
						tree.Items.push_back(FromYaml(item));
					break;
				case Yaml::NodeType::Map:
					tree.Type = YamlTree::Kind::Map;
					for (const auto& item : node)
						tree.Add(item.first.as<std::string>(), FromYaml(item.second));
					break;
				default:
					break;
			}
			return tree;
		}

		void ToYaml(Yaml::Writer& out, const YamlTree& tree)
		{
			switch (tree.Type)
			{
				case YamlTree::Kind::Null:
					out << nullptr;
					break;
				case YamlTree::Kind::Scalar:
					out << std::string_view(tree.Text);
					break;
				case YamlTree::Kind::Sequence:
				{
					const bool flat = std::all_of(tree.Items.begin(), tree.Items.end(), [](const YamlTree& item) { return item.Type == YamlTree::Kind::Scalar; });
					if (flat)
						out << Yaml::Flow;
					out << Yaml::BeginSeq;
					for (const YamlTree& item : tree.Items)
						ToYaml(out, item);
					out << Yaml::EndSeq;
					break;
				}
				case YamlTree::Kind::Map:
					out << Yaml::BeginMap;
					for (size_t i = 0; i < tree.Keys.size(); i++)
					{
						out << Yaml::Key << tree.Keys[i] << Yaml::Value;
						ToYaml(out, tree.Values[i]);
					}
					out << Yaml::EndMap;
					break;
			}
		}

		YamlTree FromJson(const choc::value::ValueView& value)
		{
			YamlTree tree;
			if (value.isObject())
			{
				tree.Type = YamlTree::Kind::Map;
				for (uint32_t i = 0; i < value.size(); i++)
				{
					const auto member = value.getObjectMemberAt(i);
					tree.Add(member.name, FromJson(member.value));
				}
			}
			else if (value.isArray() || value.isVector())
			{
				tree.Type = YamlTree::Kind::Sequence;
				for (uint32_t i = 0; i < value.size(); i++)
					tree.Items.push_back(FromJson(value[i]));
			}
			else if (value.isString())
			{
				tree.Type = YamlTree::Kind::Scalar;
				tree.Text = std::string(value.getString());
			}
			else if (value.isBool())
			{
				tree.Type = YamlTree::Kind::Scalar;
				tree.Text = value.getBool() ? "true" : "false";
			}
			else if (value.isInt32() || value.isInt64())
			{
				tree.Type = YamlTree::Kind::Scalar;
				tree.Text = std::to_string(value.getInt64());
			}
			else if (value.isFloat32() || value.isFloat64())
			{
				char buffer[32];
				const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value.getFloat64());
				tree.Type = YamlTree::Kind::Scalar;
				tree.Text.assign(buffer, result.ptr);
			}
			return tree;
		}

		std::string Dump(const YamlTree& tree)
		{
			Yaml::Writer out;
			ToYaml(out, tree);
			return out.str();
		}

		// -- Parameters --------------------------------------------------------------------------------

		// Entity IDs are 64-bit and do not fit a JSON number (a double), so they travel as strings;
		// small integers are accepted too. Missing or null gives 0 when allowNull.
		UUID ParseID(const choc::value::ValueView& params, const char* name, bool allowNull = false)
		{
			const choc::value::ValueView value = params.isObject() && params.hasObjectMember(name) ? params[name] : choc::value::ValueView();
			if (value.isVoid())
			{
				if (allowNull)
					return 0;
				throw ToolError(std::format("Missing '{}'.", name));
			}
			uint64_t id = 0;
			if (value.isString())
			{
				const std::string_view text = value.getString();
				const auto result = std::from_chars(text.data(), text.data() + text.size(), id);
				if (result.ec != std::errc() || result.ptr != text.data() + text.size())
					throw ToolError(std::format("'{}' is not an entity ID.", name));
			}
			else if ((value.isInt32() || value.isInt64()) && value.getInt64() >= 0)
				id = static_cast<uint64_t>(value.getInt64());
			else
				throw ToolError(std::format("'{}' must be an entity ID string.", name));
			if (id == 0 && !allowNull)
				throw ToolError(std::format("'{}' must not be 0.", name));
			return id;
		}

		std::string ParseString(const choc::value::ValueView& params, const char* name, std::optional<std::string> fallback = std::nullopt)
		{
			if (params.hasObjectMember(name) && params[name].isString())
				return std::string(params[name].getString());
			if (params.hasObjectMember(name) && !params[name].isVoid())
				throw ToolError(std::format("'{}' must be a string.", name));
			if (fallback)
				return *fallback;
			throw ToolError(std::format("Missing '{}'.", name));
		}

		Entity RequireEntity(const ToolCall& call, const char* name = "id")
		{
			const UUID id = ParseID(call.Params, name);
			Entity entity = call.TargetScene->TryGetEntityWithUUID(id);
			if (!entity)
				throw ToolError(std::format("No entity {} in the scene.", (uint64_t)id));
			return entity;
		}

		choc::value::Value IDValue(UUID id) { return choc::value::createString(std::to_string((uint64_t)id)); }

		// -- Generic component edits -------------------------------------------------------------------

		// Keys of an entity's YAML that are identity or hierarchy, not components.
		bool IsReservedKey(std::string_view key)
		{
			return key == "Entity" || key == "Parent" || key == "Children" || key == "Folder" || key == "PrefabComponent";
		}

		std::string SerializeOne(const Ref<Scene>& scene, UUID id)
		{
			std::string meta;
			auto snapshots = SceneSerializer(scene).SerializeEntitySnapshots(std::vector<UUID>{ id }, meta);
			return snapshots[id];
		}

		enum class ComponentEdit { Add, Set, Remove };

		void EditComponent(ToolCall& call, ComponentEdit edit)
		{
			Entity entity = RequireEntity(call);
			const UUID id = entity.GetUUID();
			const std::string component = ParseString(call.Params, "component");
			if (component.empty() || IsReservedKey(component))
				throw ToolError(std::format("'{}' is not a component.", component));

			const bool hasValues = call.Params.hasObjectMember("values") && !call.Params["values"].isVoid();
			if (hasValues && !call.Params["values"].isObject())
				throw ToolError("'values' must be an object of field names and values.");
			if (edit == ComponentEdit::Set && !hasValues)
				throw ToolError("'values' is required.");

			const std::string before = SerializeOne(call.TargetScene, id);
			YamlTree tree = FromYaml(Yaml::Load(before));
			YamlTree* node = tree.Find(component);
			if (edit == ComponentEdit::Add && node)
				throw ToolError(std::format("The entity already has a {}.", component));
			if (edit != ComponentEdit::Add && !node)
				throw ToolError(std::format("The entity has no {}.", component));

			if (edit == ComponentEdit::Remove)
			{
				tree.Remove(component);
			}
			else
			{
				if (!node)
				{
					YamlTree empty;
					empty.Type = YamlTree::Kind::Map;
					node = &tree.Add(component, std::move(empty));
				}
				if (node->Type != YamlTree::Kind::Map)
					throw ToolError(std::format("{} has no fields to set.", component));
				if (hasValues)
				{
					const YamlTree values = FromJson(call.Params["values"]);
					for (size_t i = 0; i < values.Keys.size(); i++)
					{
						if (YamlTree* existing = node->Find(values.Keys[i]))
							*existing = values.Values[i];
						else
							node->Add(values.Keys[i], values.Values[i]);
					}
				}
			}

			SceneSerializer serializer(call.TargetScene);
			auto restore = [&](std::string reason) -> ToolError
			{
				serializer.ApplyEntitySnapshots(nullptr, { { id, before } });
				return ToolError(std::move(reason));
			};
			if (!serializer.ApplyEntitySnapshots(nullptr, { { id, Dump(tree) } }))
				throw restore("The edited entity could not be read back; nothing changed.");

			// The serializer ignores keys it does not know, so check what actually landed.
			YamlTree after = FromYaml(Yaml::Load(SerializeOne(call.TargetScene, id)));
			YamlTree* landed = after.Find(component);
			if (edit == ComponentEdit::Remove && landed)
				throw restore(std::format("{} cannot be removed.", component));
			if (edit != ComponentEdit::Remove)
			{
				if (!landed)
					throw restore(std::format("Unknown component '{}'. entity.get shows the names an entity uses.", component));
				if (hasValues)
				{
					for (const std::string& field : FromJson(call.Params["values"]).Keys)
					{
						if (!landed->Find(field))
							throw restore(std::format("{} has no field '{}'.", component, field));
					}
				}
			}
			call.EditedEntities = { id };
		}

		// Every entity and its descendants, so selections do not point at deleted entities.
		void DeselectTree(const Ref<Scene>& scene, Entity entity)
		{
			SelectionManager::Deselect(entity.GetUUID());
			for (UUID child : scene->GetAllChildren(entity))
				SelectionManager::Deselect(child);
		}

	}

	void RegisterBuiltinEditorTools(EditorToolHost& host)
	{
		host.Register({ "tools.list", "Lists every command: name, description, whether it edits the scene.", false,
			[&host](ToolCall& call)
			{
				call.Result = choc::value::createEmptyArray();
				for (const auto& [name, command] : host.GetCommands())
				{
					choc::value::Value entry = choc::value::createObject("");
					entry.addMember("name", name);
					entry.addMember("description", command.Description);
					entry.addMember("mutates", command.Mutates);
					call.Result.addArrayElement(entry);
				}
			} });

		host.Register({ "scene.info", "The open scene: name, generation (pass it back to reject stale requests), editable, entity count.", false,
			[&host](ToolCall& call)
			{
				call.Result = choc::value::createObject("");
				call.Result.addMember("name", call.TargetScene->GetName());
				call.Result.addMember("generation", std::to_string(host.GetSceneGeneration()));
				call.Result.addMember("editable", host.IsSceneEditable());
				call.Result.addMember("entityCount", static_cast<int64_t>(call.TargetScene->GetAllEntitiesWith<IDComponent>().size()));
			} });

		host.Register({ "entity.list", "Every entity: id, name, parent id (\"0\" for a root) and child ids.", false,
			[](ToolCall& call)
			{
				call.Result = choc::value::createEmptyArray();
				for (auto handle : call.TargetScene->GetAllEntitiesWith<IDComponent>())
				{
					Entity entity{ handle, call.TargetScene.Raw() };
					choc::value::Value entry = choc::value::createObject("");
					entry.addMember("id", IDValue(entity.GetUUID()));
					entry.addMember("name", entity.GetName());
					entry.addMember("parent", IDValue(entity.GetParentUUID()));
					choc::value::Value children = choc::value::createEmptyArray();
					for (UUID child : entity.Children())
						children.addArrayElement(IDValue(child));
					entry.addMember("children", children);
					call.Result.addArrayElement(entry);
				}
			} });

		host.Register({ "entity.get", "One entity {id}: its name and its serialized YAML, which names its components and fields.", false,
			[](ToolCall& call)
			{
				Entity entity = RequireEntity(call);
				call.Result = choc::value::createObject("");
				call.Result.addMember("id", IDValue(entity.GetUUID()));
				call.Result.addMember("name", entity.GetName());
				call.Result.addMember("yaml", SerializeOne(call.TargetScene, entity.GetUUID()));
			} });

		host.Register({ "entity.create", "Creates an empty entity {name?, parent?}; returns its id.", true,
			[](ToolCall& call)
			{
				const std::string name = ParseString(call.Params, "name", std::string("Entity"));
				const UUID parentID = ParseID(call.Params, "parent", true);
				Entity entity;
				if (parentID != 0)
				{
					Entity parent = call.TargetScene->TryGetEntityWithUUID(parentID);
					if (!parent)
						throw ToolError(std::format("No parent entity {} in the scene.", (uint64_t)parentID));
					entity = call.TargetScene->CreateChildEntity(parent, name);
				}
				else
				{
					entity = call.TargetScene->CreateEntity(name);
				}
				call.SceneWide = true;
				call.Result = choc::value::createObject("");
				call.Result.addMember("id", IDValue(entity.GetUUID()));
			} });

		host.Register({ "entity.delete", "Deletes an entity {id} and its children.", true,
			[](ToolCall& call)
			{
				Entity entity = RequireEntity(call);
				DeselectTree(call.TargetScene, entity);
				Scene::RecordPrefabRemoval(entity); // as the Hierarchy does: a prefab child deleted from an instance stays deleted
				call.TargetScene->DestroyEntity(entity);
				call.SceneWide = true;
			} });

		host.Register({ "entity.rename", "Renames an entity {id, name}.", true,
			[](ToolCall& call)
			{
				Entity entity = RequireEntity(call);
				entity.GetComponent<TagComponent>().Tag = ParseString(call.Params, "name");
				call.EditedEntities = { entity.GetUUID() };
			} });

		host.Register({ "entity.reparent", "Moves an entity {id} under another {parent}, or to the root with parent null; its local transform is kept, as in the Hierarchy.", true,
			[](ToolCall& call)
			{
				Entity entity = RequireEntity(call);
				const UUID parentID = ParseID(call.Params, "parent", true);
				Entity parent;
				if (parentID != 0)
				{
					parent = call.TargetScene->TryGetEntityWithUUID(parentID);
					if (!parent)
						throw ToolError(std::format("No parent entity {} in the scene.", (uint64_t)parentID));
					if (parent == entity || parent.IsDescendantOf(entity))
						throw ToolError("An entity cannot be moved under itself or one of its children.");
				}
				Scene::RecordPrefabReparent(entity, parent);
				entity.SetParent(parent);
				call.SceneWide = true;
			} });

		host.Register({ "component.add", "Adds a component {id, component, values?} by its serialized name (\"RigidBodyComponent\"); unset fields take their defaults.", true,
			[](ToolCall& call) { EditComponent(call, ComponentEdit::Add); } });

		host.Register({ "component.set", "Sets fields of a component {id, component, values: {Field: value}}, by their serialized names.", true,
			[](ToolCall& call) { EditComponent(call, ComponentEdit::Set); } });

		host.Register({ "component.remove", "Removes a component {id, component}.", true,
			[](ToolCall& call) { EditComponent(call, ComponentEdit::Remove); } });
	}

}
