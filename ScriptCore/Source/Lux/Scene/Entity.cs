using System;
using System.Runtime.InteropServices;

using Coral.Managed.Interop;

namespace Lux
{
	// [EditorAssignable] so a field of type Entity is stored as a UUID and reconstructed on
	// Instantiate (see ScriptEngine::Instantiate).
	[EditorAssignable]
	public class Entity
	{
		protected Entity() { ID = 0; }

		internal Entity(ulong id)
		{
			ID = id;
		}

		public readonly ulong ID;

		public unsafe string Name => InternalCalls.Entity_GetName(ID);

		// OnCreate / OnUpdate / OnDestroy are matched by NAME (not override); declaring them on
		// this base would make every subclass appear to define them. Collision hooks take an
		// Entity, which can't cross the boundary, so the engine invokes the ulong bridges below.
		protected virtual void OnCollisionBegin(Entity other) { }
		protected virtual void OnCollisionEnd(Entity other) { }

		internal void OnCollisionBeginInternal(ulong otherID) => OnCollisionBegin(new Entity(otherID));
		internal void OnCollisionEndInternal(ulong otherID) => OnCollisionEnd(new Entity(otherID));

		public unsafe Vector3 Translation
		{
			get
			{
				Vector3 result;
				InternalCalls.TransformComponent_GetTranslation(ID, &result);
				return result;
			}
			set { InternalCalls.TransformComponent_SetTranslation(ID, &value); }
		}

		public unsafe Vector3 Scale
		{
			get
			{
				Vector3 result;
				InternalCalls.TransformComponent_GetScale(ID, &result);
				return result;
			}
			set { InternalCalls.TransformComponent_SetScale(ID, &value); }
		}

		public unsafe bool HasComponent<T>() where T : Component, new()
		{
			return InternalCalls.Entity_HasComponent(ID, typeof(T));
		}

		public unsafe T AddComponent<T>() where T : Component, new()
		{
			InternalCalls.Entity_AddComponent(ID, typeof(T));
			return new T() { Entity = this };
		}

		public unsafe void RemoveComponent<T>() where T : Component, new()
		{
			InternalCalls.Entity_RemoveComponent(ID, typeof(T));
		}

		public T GetComponent<T>() where T : Component, new()
		{
			if (!HasComponent<T>())
				return null;
			return new T() { Entity = this };
		}

		public unsafe Entity FindEntityByName(string name)
		{
			ulong entityID = InternalCalls.Entity_FindEntityByName(name);
			return entityID == 0 ? null : new Entity(entityID);
		}

		public unsafe T As<T>() where T : Entity, new()
		{
			IntPtr handle = InternalCalls.GetScriptInstance(ID);
			if (handle == IntPtr.Zero)
				return null;
			return GCHandle.FromIntPtr(handle).Target as T;
		}
	}
}
