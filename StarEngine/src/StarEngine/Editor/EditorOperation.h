#pragma once

//#include "StarEngine/Script/ScriptEntityStorage.hpp"
#include "StarEngine/Scene/Entity.h"
#include "StarEngine/Scene/Scene.h"

namespace StarEngine {
	/*
	 * NOTE: ImGui text widgets have their own internal undo stack so we don't
	 * 		 Need a text stack thankfully.
	 */

	/*
	 * Just some conceptual info for anyone poking around in here. An undo
	 * Stack does not express the changes made to state, but the way to get
	 * Back the previous state. In our case this mostly just means storing the
	 * Old state in its entirety and reverting to that, but if there is a more
	 * Complex operation required then it can be jimmied into the restore proc.
	 *
	 * State nodes can hold weak refs or even be left to dangle without
	 * Checking/Removing them as -- nominally -- any operation which kills
	 * State they're hanging onto will be restored *before* they themselves are
	 * Just by virtue of the way the stack works. If we want more complex
	 * Traversal with multiple timelines we can change this but that will be
	 * Way down the road.
	 */

	/*
	 * TODO: Big record of all operations not yet implemented in undo/redo stack:
	 *  - Settings Panes [Need independent stacks]
	 *  	- Project
	 *  	- Audio [Jay]
	 *  - Scene
	 *  	- Add/Remove Entity [Needs confirmation dialog]
	 *  	- Reparent Entity
	 *  - Properties
	 *  	- Paste Component
	 *  	- Modify Component Property
	 *  - Content Browser
	 *  	- Delete/Cut
	 *  	- Move
	 *  	- Paste/Create
	 *  - Misc
	 *  	- Material Editor
	 *  	- Asset Registry
	 *  	- Animation/Sound Graph Editors [Probably needs 0x/Jay to do them]
	 */

	// TODO: For gizmo multiselection and other bulk operations
	//  	 (like delete/move multiple selected files) should allow for a single
	//		 stack entry to represent many operations.

	class EditorOperation {
	public:
		EditorOperation() = default;
		virtual ~EditorOperation() = default;

		EditorOperation(EditorOperation&&) = default;
		EditorOperation& operator=(EditorOperation&&) = default;

		EditorOperation(const EditorOperation&) = default;
		EditorOperation& operator=(const EditorOperation&) = default;

		virtual void Apply() = 0;
	};

	struct RawPropertyState : EditorOperation {
	private:
		// The location to write back on restore.
		void* restore;
		// The node-local copy of the property.
		void* copy;
		size_t size;

	public:
		RawPropertyState() = default;
		RawPropertyState(void* restoreIn, void* copyIn, size_t sizeIn);

		virtual ~RawPropertyState() override = default;

		virtual void Apply() override;
	};

	template<typename T>
	struct CopyPropertyState : EditorOperation {
	private:
		T* restore;
		T copy;

	public:
		CopyPropertyState() = default;
		CopyPropertyState(T* restoreIn, T& copyIn) : restore(restoreIn), copy(copyIn) {
			//HZ_CORE_TRACE_TAG("EditorStack", "Storing state for restore to @{}", static_cast<void*>(restore));
		}

		virtual ~CopyPropertyState() override = default;

		virtual void Apply() override {
			//HZ_CORE_TRACE_TAG("EditorStack", "Applying state over @{}", static_cast<void*>(restore));
			std::swap(*restore, copy);
		}
	};
	/*
	template<typename T>
	struct ComponentState : EditorOperation {
		enum class Mode {
			Modify,
			Add,
			Remove
		};

	private:
		UUID scene;
		UUID entity;
		T copy;
		Mode mode;

	public:
		ComponentState() = default;
		ComponentState(Entity& entityIn, T& copyIn, Mode modeIn = Mode::Modify) :
				scene(entityIn.GetSceneUUID()), entity(entityIn.GetUUID()), copy(copyIn), mode(modeIn) {}

		ComponentState(Entity& entityIn) :
				scene(entityIn.GetSceneUUID()), entity(entityIn.GetUUID()), mode(Mode::Add) {}

		virtual ~ComponentState() override = default;

		virtual void Apply() override {
			auto sceneRef = Scene::GetScene(scene);
			auto entityRef = sceneRef->GetEntityWithUUID(entity);

			switch(mode) {
				case Mode::Modify: {
					std::swap(entityRef.GetComponent<T>(), copy);
					break;
				}
				case Mode::Add: {
					copy = entityRef.GetComponent<T>();
					entityRef.RemoveComponent<T>();
					mode = Mode::Remove;
					break;
				}
				case Mode::Remove: {
					entityRef.AddComponent<T>(std::move(copy));
					mode = Mode::Add;
					break;
				}
			}
		}
	};*/

	class EditorStack
	{
		using StatePtr = std::unique_ptr<EditorOperation>;

	private:
		static EditorStack s_Instance;

	private:
		// NOTE: "Future" entries are modified to contain the "old-new" state, so they can just be re-applied to revert
		//		 An undo.
		std::vector<StatePtr> m_Stack{};
		size_t m_Head = 0;

	public:
		UUID m_Mark = 0;

	public:
		static EditorStack& Get();

	private:
		EditorStack() = default;

	public:
		void Push(StatePtr&&);

		template<typename T, typename... Args>
		void PushCopy(Args&&... args) {
			Push(std::make_unique<CopyPropertyState<T>>(std::forward<Args>(args)...));
		}

		template<typename... Args>
		void PushRaw(Args&&... args) {
			Push(std::make_unique<RawPropertyState>(std::forward<Args>(args)...));
		}
		/*
		template<typename T, typename... Args>
		void PushComponent(Args&&... args) {
			Push(std::make_unique<ComponentState<T>>(std::forward<Args>(args)...));
		}*/

		// Undo head position.
		void Pop();

		// Redo head position.
		void Advance();

		// Next push will override previous entry.
		void Override();
	};

	// Pushes an editor stack entry with a location-static UUID set to override
	// entries from the same location on subsequent calls -- thereby agglomerating
	// continuous operations like gizmo usage.
	// NOTE: Must terminate the agglomeration with `HZ_EDITOR_COMPLETE_OP` so
	//		 a following operation of the same type won't be erroneously agglomerated.
#define SE_EDITOR_AGGLOMERATE_OP(push_type, ...) \
		do { \
			if (EditorStack::Get().m_Mark != _hz_editor_agglomerate_stamp) \
			{ \
				EditorStack::Get().push_type(__VA_ARGS__); \
				EditorStack::Get().m_Mark = _hz_editor_agglomerate_stamp; \
			} \
		} while(0) \

#define SE_EDITOR_OP_DECLARE const static UUID _hz_editor_agglomerate_stamp

#define SE_EDITOR_COMPLETE_OP \
		if(EditorStack::Get().m_Mark == _hz_editor_agglomerate_stamp) \
			EditorStack::Get().m_Mark = 0
}
