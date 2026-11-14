// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "EditorToolHost.h"

#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Scene.h"

#include <choc/text/choc_JSON.h>

namespace Lux {

	const char* ToolOutcomeToString(ToolOutcome outcome)
	{
		switch (outcome)
		{
			case ToolOutcome::Completed: return "completed";
			case ToolOutcome::Rejected:  return "rejected";
			case ToolOutcome::Cancelled: return "cancelled";
			case ToolOutcome::Failed:    return "failed";
		}
		return "failed";
	}

	namespace {

		ToolResult MakeResult(uint64_t id, ToolOutcome outcome, std::string message)
		{
			ToolResult result;
			result.RequestID = id;
			result.Outcome = outcome;
			result.Message = std::move(message);
			result.Json = "null";
			return result;
		}

	}

	EditorToolHost::EditorToolHost()
	{
		RegisterBuiltinEditorTools(*this);
	}

	EditorToolHost::~EditorToolHost()
	{
		std::lock_guard lock(m_QueueMutex);
		for (Request& request : m_Queue)
			request.Promise.set_value(MakeResult(request.ID, ToolOutcome::Cancelled, "The editor tool host shut down."));
		m_Queue.clear();
	}

	void EditorToolHost::Register(ToolCommand command)
	{
		LUX_CORE_ASSERT(command.Handler, "Tool command without a handler");
		std::string name = command.Name;
		m_Commands[std::move(name)] = std::move(command);
	}

	EditorToolHost::Ticket EditorToolHost::Submit(std::string command, std::string paramsJson, uint64_t expectedSceneGeneration)
	{
		std::lock_guard lock(m_QueueMutex);
		Ticket ticket;
		ticket.ID = m_NextRequestID++;
		if (m_Queue.size() >= MaxQueuedRequests)
		{
			std::promise<ToolResult> full;
			full.set_value(MakeResult(ticket.ID, ToolOutcome::Rejected, std::format("Too many queued requests (limit {}).", MaxQueuedRequests)));
			ticket.Result = full.get_future().share();
			return ticket;
		}

		Request& request = m_Queue.emplace_back();
		request.ID = ticket.ID;
		request.Command = std::move(command);
		request.ParamsJson = std::move(paramsJson);
		request.ExpectedGeneration = expectedSceneGeneration;
		ticket.Result = request.Promise.get_future().share();
		return ticket;
	}

	bool EditorToolHost::Cancel(uint64_t requestID)
	{
		std::lock_guard lock(m_QueueMutex);
		for (auto it = m_Queue.begin(); it != m_Queue.end(); ++it)
		{
			if (it->ID != requestID)
				continue;
			it->Promise.set_value(MakeResult(requestID, ToolOutcome::Cancelled, "Cancelled before it ran."));
			m_Queue.erase(it);
			return true;
		}
		return false;
	}

	void EditorToolHost::SetScene(const Ref<Scene>& scene, bool editable)
	{
		if (scene != m_Scene)
		{
			m_Scene = scene;
			m_SceneGeneration++;
		}
		m_SceneEditable = editable && scene;
	}

	size_t EditorToolHost::GetQueuedCount() const
	{
		std::lock_guard lock(m_QueueMutex);
		return m_Queue.size();
	}

	size_t EditorToolHost::ProcessQueue(size_t maxRequests)
	{
		size_t processed = 0;
		while (processed < maxRequests)
		{
			Request request;
			{
				std::lock_guard lock(m_QueueMutex);
				if (m_Queue.empty())
					break;
				request = std::move(m_Queue.front());
				m_Queue.pop_front();
			}
			// Outside the lock: a command may submit or cancel other requests.
			request.Promise.set_value(Run(request));
			processed++;
		}
		return processed;
	}

	ToolResult EditorToolHost::Run(Request& request)
	{
		const auto it = m_Commands.find(request.Command);
		if (it == m_Commands.end())
			return MakeResult(request.ID, ToolOutcome::Rejected, std::format("Unknown command '{}'. tools.list lists them.", request.Command));
		const ToolCommand& command = it->second;

		// Checked when the request runs, not when it was made: the editor may have moved on since.
		if (!m_Scene)
			return MakeResult(request.ID, ToolOutcome::Rejected, "No scene is open.");
		if (request.ExpectedGeneration != 0 && request.ExpectedGeneration != m_SceneGeneration)
			return MakeResult(request.ID, ToolOutcome::Rejected, "The scene changed since this request was made.");
		if (command.Mutates && !m_SceneEditable)
			return MakeResult(request.ID, ToolOutcome::Rejected, "The scene is running; stop Play or Simulate to edit it.");

		choc::value::Value params = choc::value::createObject("");
		if (!request.ParamsJson.empty())
		{
			try
			{
				params = choc::json::parse(request.ParamsJson);
			}
			catch (const choc::json::ParseError& e)
			{
				return MakeResult(request.ID, ToolOutcome::Rejected, std::format("Parameters are not valid JSON: {} (line {}, column {})",
					e.message, e.lineAndColumn.line, e.lineAndColumn.column));
			}
			catch (...)
			{
				return MakeResult(request.ID, ToolOutcome::Rejected, "Parameters are not valid JSON.");
			}
			if (!params.isObject())
				return MakeResult(request.ID, ToolOutcome::Rejected, "Parameters must be a JSON object.");
		}

		ToolCall call;
		call.TargetScene = m_Scene;
		call.Params = params.getView();
		try
		{
			command.Handler(call);
		}
		catch (const ToolError& e)
		{
			return MakeResult(request.ID, ToolOutcome::Rejected, e.what());
		}
		// choc throws its own Error type, not a std::exception; whatever escapes, the request completes.
		catch (const std::exception& e)
		{
			LUX_CORE_ERROR_TAG("Editor", "Tool command '{}' failed: {}", command.Name, e.what());
			return MakeResult(request.ID, ToolOutcome::Failed, e.what());
		}
		catch (const choc::value::Error& e)
		{
			LUX_CORE_ERROR_TAG("Editor", "Tool command '{}' failed: {}", command.Name, e.description);
			return MakeResult(request.ID, ToolOutcome::Failed, e.description);
		}
		catch (...)
		{
			LUX_CORE_ERROR_TAG("Editor", "Tool command '{}' failed with an unknown exception", command.Name);
			return MakeResult(request.ID, ToolOutcome::Failed, "Unknown exception.");
		}

		if (command.Mutates && m_UndoHook && (call.SceneWide || !call.EditedEntities.empty()))
			m_UndoHook(command.Name, call.EditedEntities, call.SceneWide);

		ToolResult result = MakeResult(request.ID, ToolOutcome::Completed, {});
		if (!call.Result.isVoid())
		{
			try
			{
				result.Json = choc::json::toString(call.Result);
			}
			catch (...)
			{
				// The edit already happened (and is undoable); only its result could not be encoded.
				result.Message = "The command ran, but its result could not be encoded as JSON.";
			}
		}
		return result;
	}

	// -- Self tests ----------------------------------------------------------------------------------

	bool EditorToolHost::RunSelfTests(std::vector<std::string>* failures)
	{
		bool ok = true;
		auto fail = [&](std::string message)
		{
			ok = false;
			if (failures)
				failures->push_back("EditorToolHost: " + std::move(message));
		};

		EditorToolHost host;
		std::vector<std::string> undoLabels;
		host.SetUndoHook([&](const std::string& label, const std::vector<UUID>&, bool) { undoLabels.push_back(label); });

		// Submits, runs the queue, and returns the result.
		auto call = [&](const std::string& command, const std::string& params = {}, uint64_t generation = 0)
		{
			Ticket ticket = host.Submit(command, params, generation);
			host.ProcessQueue();
			return ticket.Result.get();
		};
		auto expect = [&](const ToolResult& result, ToolOutcome outcome, const char* what)
		{
			if (result.Outcome != outcome)
				fail(std::format("{}: expected {}, got {} ({})", what, ToolOutcomeToString(outcome), ToolOutcomeToString(result.Outcome), result.Message));
			return result.Outcome == outcome;
		};
		auto json = [](const ToolResult& result) { return choc::json::parse(result.Json); };

		// The steps run in a lambda so a throw anywhere is reported as a failure, not a crash.
		auto steps = [&]()
		{
			expect(call("scene.info"), ToolOutcome::Rejected, "a request without a scene");

			Ref<Scene> scene = Ref<Scene>::Create();
			host.SetScene(scene, true);
			const uint64_t generation = host.GetSceneGeneration();

			expect(call("no.such.command"), ToolOutcome::Rejected, "an unknown command");
			expect(call("entity.create", "[1, 2]"), ToolOutcome::Rejected, "parameters that are not an object");
			expect(call("entity.create", "{ broken"), ToolOutcome::Rejected, "malformed JSON");

			// Create a parent and a child; IDs travel as strings (uint64 does not fit a JSON double).
			ToolResult created = call("entity.create", R"({ "name": "Parent" })", generation);
			if (!expect(created, ToolOutcome::Completed, "entity.create"))
				return;
			const std::string parentID = json(created)["id"].getWithDefault<std::string>("");
			ToolResult child = call("entity.create", std::format(R"({{ "name": "Child", "parent": "{}" }})", parentID));
			expect(child, ToolOutcome::Completed, "entity.create with a parent");
			const std::string childID = json(child)["id"].getWithDefault<std::string>("");
			Entity parentEntity = scene->TryGetEntityWithUUID(std::stoull(parentID));
			Entity childEntity = scene->TryGetEntityWithUUID(std::stoull(childID));
			if (!parentEntity || !childEntity || childEntity.GetParentUUID() != parentEntity.GetUUID())
				fail("entity.create did not build the parent/child pair");
			if (undoLabels.size() != 2)
				fail(std::format("each mutation should be one undo step, got {}", undoLabels.size()));

			// Generic component edits through the serializer.
			expect(call("component.add", std::format(R"({{ "id": "{}", "component": "RigidBodyComponent", "values": {{ "Mass": 7.5 }} }})", childID)),
				ToolOutcome::Completed, "component.add with values");
			childEntity = scene->TryGetEntityWithUUID(std::stoull(childID));
			if (!childEntity || !childEntity.HasComponent<RigidBodyComponent>() || childEntity.GetComponent<RigidBodyComponent>().Mass != 7.5f)
				fail("component.add did not add a RigidBodyComponent with Mass 7.5");
			expect(call("component.set", std::format(R"({{ "id": "{}", "component": "TransformComponent", "values": {{ "Position": [1, 2, 3] }} }})", childID)),
				ToolOutcome::Completed, "component.set");
			childEntity = scene->TryGetEntityWithUUID(std::stoull(childID));
			if (!childEntity || childEntity.Transform().Translation != glm::vec3(1.0f, 2.0f, 3.0f))
				fail("component.set did not move the entity");
			if (!childEntity || childEntity.GetParentUUID() != std::stoull(parentID))
				fail("component.set lost the entity's parent");
			expect(call("component.set", std::format(R"({{ "id": "{}", "component": "TransformComponent", "values": {{ "NotAField": 1 }} }})", childID)),
				ToolOutcome::Rejected, "an unknown field");
			expect(call("component.add", std::format(R"({{ "id": "{}", "component": "NoSuchComponent" }})", childID)),
				ToolOutcome::Rejected, "an unknown component");
			expect(call("component.set", std::format(R"({{ "id": "{}", "component": "Parent", "values": {{}} }})", childID)),
				ToolOutcome::Rejected, "editing hierarchy keys as a component");
			expect(call("component.remove", std::format(R"({{ "id": "{}", "component": "RigidBodyComponent" }})", childID)),
				ToolOutcome::Completed, "component.remove");
			childEntity = scene->TryGetEntityWithUUID(std::stoull(childID));
			if (!childEntity || childEntity.HasComponent<RigidBodyComponent>())
				fail("component.remove left the RigidBodyComponent");

			// Queries.
			ToolResult list = call("entity.list");
			if (expect(list, ToolOutcome::Completed, "entity.list") && json(list).size() != 2)
				fail("entity.list should list two entities");
			ToolResult get = call("entity.get", std::format(R"({{ "id": "{}" }})", childID));
			if (expect(get, ToolOutcome::Completed, "entity.get") && json(get)["yaml"].getWithDefault<std::string>("").find("TransformComponent") == std::string::npos)
				fail("entity.get should return the entity's YAML");

			// Stale scenes, running scenes, cancellation, the queue limit.
			expect(call("scene.info", {}, generation + 1), ToolOutcome::Rejected, "a request for another scene generation");
			host.SetScene(scene, false);
			expect(call("entity.create", "{}"), ToolOutcome::Rejected, "a mutation while the scene runs");
			expect(call("entity.list"), ToolOutcome::Completed, "a query while the scene runs");
			host.SetScene(scene, true);
			{
				Ticket queued = host.Submit("entity.list");
				if (!host.Cancel(queued.ID) || queued.Result.get().Outcome != ToolOutcome::Cancelled)
					fail("Cancel should complete a queued request as cancelled");
			}
			{
				std::vector<Ticket> tickets;
				for (size_t i = 0; i <= MaxQueuedRequests; i++)
					tickets.push_back(host.Submit("scene.info"));
				if (tickets.back().Result.get().Outcome != ToolOutcome::Rejected)
					fail("a request past the queue limit should be rejected");
				if (host.ProcessQueue(MaxQueuedRequests) != MaxQueuedRequests)
					fail("the queue should hold exactly MaxQueuedRequests");
			}

			// Reparent, rename, delete.
			expect(call("entity.reparent", std::format(R"({{ "id": "{}", "parent": null }})", childID)), ToolOutcome::Completed, "entity.reparent to the root");
			childEntity = scene->TryGetEntityWithUUID(std::stoull(childID));
			if (!childEntity || childEntity.GetParentUUID() != 0)
				fail("entity.reparent did not detach the child");
			expect(call("entity.reparent", std::format(R"({{ "id": "{}", "parent": "{}" }})", parentID, childID)), ToolOutcome::Completed, "entity.reparent under the child");
			expect(call("entity.reparent", std::format(R"({{ "id": "{}", "parent": "{}" }})", childID, parentID)), ToolOutcome::Rejected, "a reparent that makes a cycle");
			expect(call("entity.rename", std::format(R"({{ "id": "{}", "name": "Renamed" }})", childID)), ToolOutcome::Completed, "entity.rename");
			childEntity = scene->TryGetEntityWithUUID(std::stoull(childID));
			if (!childEntity || childEntity.GetName() != "Renamed")
				fail("entity.rename did not rename");
			expect(call("entity.delete", std::format(R"({{ "id": "{}" }})", childID)), ToolOutcome::Completed, "entity.delete");
			if (scene->TryGetEntityWithUUID(std::stoull(childID)) || scene->TryGetEntityWithUUID(std::stoull(parentID)))
				fail("entity.delete should remove the entity and its children");
			expect(call("entity.delete", std::format(R"({{ "id": "{}" }})", childID)), ToolOutcome::Rejected, "deleting a missing entity");

			// A new scene starts a new generation.
			host.SetScene(Ref<Scene>::Create(), true);
			if (host.GetSceneGeneration() == generation)
				fail("switching scenes should start a new generation");

		};

		try
		{
			steps();
		}
		catch (const std::exception& e)
		{
			fail(std::format("threw: {}", e.what()));
		}
		catch (const choc::value::Error& e)
		{
			fail(std::format("threw: {}", e.description));
		}
		catch (const choc::json::ParseError& e)
		{
			fail(std::format("a result was not valid JSON: {}", e.message));
		}
		return ok;
	}

}
