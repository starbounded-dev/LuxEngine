// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Core/UUID.h"

#include <choc/containers/choc_Value.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace Lux {

	class Scene;

	// How a tool request ended. Rejected: the request was refused as asked (bad parameters, unknown
	// command, stale or running scene, full queue); nothing changed. Failed: the command threw while
	// running. Cancelled: withdrawn before it ran, or dropped at shutdown.
	enum class ToolOutcome : uint8_t { Completed, Rejected, Cancelled, Failed };
	const char* ToolOutcomeToString(ToolOutcome outcome);

	struct ToolResult
	{
		uint64_t RequestID = 0;
		ToolOutcome Outcome = ToolOutcome::Completed;
		std::string Message;   // why it was rejected or failed; empty on success
		std::string Json;      // the command's result as JSON ("null" when it returns nothing)
	};

	// Thrown by a command to reject its request (bad or impossible parameters). The host turns it into
	// ToolOutcome::Rejected; anything else a command throws becomes ToolOutcome::Failed.
	class ToolError : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};

	// What a command sees while it runs, on the owner thread.
	struct ToolCall
	{
		Ref<Scene> TargetScene;
		choc::value::ValueView Params;   // a JSON object (empty object when none were given)
		choc::value::Value Result;       // set by the command; void means "null"

		// A mutating command names what it changed, for the undo step: the entities whose own data
		// changed, or SceneWide for hierarchy changes and anything else.
		std::vector<UUID> EditedEntities;
		bool SceneWide = false;
	};

	struct ToolCommand
	{
		std::string Name;          // "entity.create"
		std::string Description;   // one line, for tools.list
		bool Mutates = false;      // mutating commands need an editable scene and become one undo step
		std::function<void(ToolCall&)> Handler;
	};

	// Runs scene queries and edits for scripted or external tools on the editor's main thread.
	//
	// Any thread may Submit a request; it waits in a bounded queue until the owner thread calls
	// ProcessQueue, which runs it against the scene current at that moment. A request may name the
	// scene generation it was built against (scene.info returns it); if the editor has switched
	// scenes since, the request is rejected instead of editing the wrong scene. Mutating commands are
	// rejected while the scene runs (Play/Simulate), and each completed one is handed to the undo hook
	// as one undo step, labelled with the command name.
	//
	// Results come back through a shared_future, completed on the owner thread.
	class EditorToolHost
	{
	public:
		static constexpr size_t MaxQueuedRequests = 256;

		struct Ticket
		{
			uint64_t ID = 0;
			std::shared_future<ToolResult> Result;
		};

		// Called after each completed mutating request: label, edited entities, scene-wide.
		using UndoHook = std::function<void(const std::string& label, const std::vector<UUID>& entities, bool sceneWide)>;

		EditorToolHost();   // registers the built-in commands (EditorToolCommands.cpp)
		~EditorToolHost();  // cancels whatever is still queued

		EditorToolHost(const EditorToolHost&) = delete;
		EditorToolHost& operator=(const EditorToolHost&) = delete;

		// Owner thread, before any request runs.
		void Register(ToolCommand command);
		void SetUndoHook(UndoHook hook) { m_UndoHook = std::move(hook); }

		// Any thread. paramsJson is a JSON object or empty; expectedSceneGeneration 0 accepts any scene.
		Ticket Submit(std::string command, std::string paramsJson = {}, uint64_t expectedSceneGeneration = 0);
		// Any thread. True if the request was still queued (it completes as Cancelled).
		bool Cancel(uint64_t requestID);

		// Owner thread. A different scene starts a new generation; editable is false while it runs.
		void SetScene(const Ref<Scene>& scene, bool editable);
		uint64_t GetSceneGeneration() const { return m_SceneGeneration; }
		bool IsSceneEditable() const { return m_SceneEditable; }

		// Owner thread. Runs up to maxRequests queued requests; returns how many ran.
		size_t ProcessQueue(size_t maxRequests = 64);
		size_t GetQueuedCount() const;

		// Owner thread: the command table, for tools.list.
		const std::map<std::string, ToolCommand>& GetCommands() const { return m_Commands; }

		// Exercises the host and the built-in commands on a scratch scene. Appends failures.
		static bool RunSelfTests(std::vector<std::string>* failures = nullptr);

	private:
		struct Request
		{
			uint64_t ID = 0;
			std::string Command;
			std::string ParamsJson;
			uint64_t ExpectedGeneration = 0;
			std::promise<ToolResult> Promise;
		};

		ToolResult Run(Request& request);

		std::map<std::string, ToolCommand> m_Commands;
		UndoHook m_UndoHook;

		mutable std::mutex m_QueueMutex;
		std::deque<Request> m_Queue;
		uint64_t m_NextRequestID = 1;

		Ref<Scene> m_Scene;
		uint64_t m_SceneGeneration = 0;
		bool m_SceneEditable = false;
	};

	// EditorToolCommands.cpp
	void RegisterBuiltinEditorTools(EditorToolHost& host);

}
