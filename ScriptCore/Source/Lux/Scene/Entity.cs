// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;
using System.Runtime.InteropServices;

using Coral.Managed.Interop;

namespace Lux
{
	// [EditorAssignable] so a field of type Entity is stored as a UUID and reconstructed on
	// Instantiate (see ScriptEngine::Instantiate).
	/// <summary>
	/// An object in the running scene, and the base class of every entity script.
	/// </summary>
	/// <remarks>
	/// A script derives from <c>Entity</c> and the engine calls its hooks by name, so they need no <c>override</c>:
	/// <c>void OnCreate()</c> once when the entity starts (or is spawned), <c>void OnUpdate(float ts)</c> every frame
	/// with the frame time in seconds, <c>void OnFixedUpdate(float dt)</c> before every 3D physics step with the fixed
	/// timestep, and <c>void OnDestroy()</c> when it goes away. The collision and trigger hooks are overridden instead.
	/// An <c>Entity</c> is a handle: two instances with the same <see cref="ID"/> are the same entity. A public field of
	/// type <c>Entity</c> is set in the inspector.
	/// </remarks>
	[EditorAssignable]
	public class Entity
	{
		protected Entity() { ID = 0; }

		internal Entity(ulong id)
		{
			ID = id;
		}

		/// <summary>The entity's unique ID (its UUID), stable across saves.</summary>
		public readonly ulong ID;

		/// <summary>The entity's name, as shown in the Scene Hierarchy.</summary>
		public unsafe string Name
		{
			get => InternalCalls.Entity_GetName(ID);
			set => InternalCalls.Entity_SetName(ID, value);
		}

		// OnCreate / OnUpdate / OnDestroy are matched by NAME (not override). Collision hooks take
		// an Entity, which can't cross the native boundary, so the engine invokes the ulong bridges.
		/// <summary>
		/// Called when this entity's body starts touching another one (3D or 2D). Both entities' scripts are told,
		/// and a character controller touching static geometry counts.
		/// </summary>
		/// <param name="other">The entity on the other side of the contact.</param>
		protected virtual void OnCollisionBegin(Entity other) { }
		/// <summary>Called when a contact reported by <see cref="OnCollisionBegin"/> ends.</summary>
		/// <param name="other">The entity on the other side of the contact.</param>
		protected virtual void OnCollisionEnd(Entity other) { }

		internal void OnCollisionBeginInternal(ulong otherID) => OnCollisionBegin(new Entity(otherID));
		internal void OnCollisionEndInternal(ulong otherID) => OnCollisionEnd(new Entity(otherID));

		/// <summary>
		/// Called when a 3D trigger (a rigid body with Is Trigger set) and another body start overlapping. Both
		/// entities' scripts are told; character controllers count.
		/// </summary>
		/// <param name="other">The entity on the other side of the overlap.</param>
		protected virtual void OnTriggerBegin(Entity other) { }
		/// <summary>Called when an overlap reported by <see cref="OnTriggerBegin"/> ends.</summary>
		/// <param name="other">The entity on the other side of the overlap.</param>
		protected virtual void OnTriggerEnd(Entity other) { }

		internal void OnTriggerBeginInternal(ulong otherID) => OnTriggerBegin(new Entity(otherID));
		internal void OnTriggerEndInternal(ulong otherID) => OnTriggerEnd(new Entity(otherID));

		/// <summary>Position relative to the parent (to the world, without one), in meters.</summary>
		public unsafe Vector3 Translation
		{
			get { Vector3 r; InternalCalls.TransformComponent_GetTranslation(ID, &r); return r; }
			set { InternalCalls.TransformComponent_SetTranslation(ID, &value); }
		}

		/// <summary>Rotation relative to the parent, as Euler angles in radians.</summary>
		public unsafe Vector3 Rotation
		{
			get { Vector3 r; InternalCalls.TransformComponent_GetRotation(ID, &r); return r; }
			set { InternalCalls.TransformComponent_SetRotation(ID, &value); }
		}

		/// <summary>Scale relative to the parent.</summary>
		public unsafe Vector3 Scale
		{
			get { Vector3 r; InternalCalls.TransformComponent_GetScale(ID, &r); return r; }
			set { InternalCalls.TransformComponent_SetScale(ID, &value); }
		}

		/// <summary>
		/// The parent entity, or null for a root. Setting it keeps <see cref="Translation"/>, <see cref="Rotation"/>
		/// and <see cref="Scale"/> as they are, so they become relative to the new parent; null detaches the entity.
		/// A change that would make the entity its own ancestor is ignored.
		/// </summary>
		public unsafe Entity Parent
		{
			get
			{
				ulong parentID = InternalCalls.Entity_GetParent(ID);
				return parentID == 0 ? null : new Entity(parentID);
			}
			set => InternalCalls.Entity_SetParent(ID, value != null ? value.ID : 0);
		}

		/// <summary>The entity's direct children, in hierarchy order.</summary>
		public unsafe Entity[] Children
		{
			get
			{
				using NativeArray<ulong> ids = InternalCalls.Entity_GetChildren(ID);
				Entity[] children = new Entity[ids.Length];
				for (int i = 0; i < ids.Length; i++)
					children[i] = new Entity(ids[i]);
				return children;
			}
		}

		/// <summary>Whether the entity has a component of type <c>T</c>.</summary>
		public unsafe bool HasComponent<T>() where T : Component, new()
		{
			return InternalCalls.Entity_HasComponent(ID, typeof(T));
		}

		/// <summary>Adds a component of type <c>T</c> with its default settings.</summary>
		/// <returns>The new component.</returns>
		public unsafe T AddComponent<T>() where T : Component, new()
		{
			InternalCalls.Entity_AddComponent(ID, typeof(T));
			return new T() { Entity = this };
		}

		/// <summary>Removes the entity's component of type <c>T</c>, if it has one.</summary>
		public unsafe void RemoveComponent<T>() where T : Component, new()
		{
			InternalCalls.Entity_RemoveComponent(ID, typeof(T));
		}

		/// <summary>The entity's component of type <c>T</c>.</summary>
		/// <returns>The component, or null when the entity has none.</returns>
		public T GetComponent<T>() where T : Component, new()
		{
			if (!HasComponent<T>())
				return null;
			return new T() { Entity = this };
		}

		/// <summary>Finds an entity in the scene by its <see cref="Name"/>.</summary>
		/// <returns>An entity with exactly that name, or null.</returns>
		public unsafe Entity FindEntityByName(string name)
		{
			ulong entityID = InternalCalls.Entity_FindEntityByName(name);
			return entityID == 0 ? null : new Entity(entityID);
		}

		/// <summary>The script running on this entity, as script class <c>T</c>.</summary>
		/// <returns>The script, or null when the entity has no script or its script is not a <c>T</c>.</returns>
		public unsafe T As<T>() where T : Entity, new()
		{
			IntPtr handle = InternalCalls.GetScriptInstance(ID);
			if (handle == IntPtr.Zero)
				return null;
			return GCHandle.FromIntPtr(handle).Target as T;
		}

		/// <summary>
		/// Destroys this entity and its children at the end of the current update, so a script may destroy itself.
		/// </summary>
		public unsafe void Destroy() => InternalCalls.Scene_DestroyEntity(ID);

		/// <summary>Creates an empty entity (a transform only) at the scene's root.</summary>
		/// <param name="name">The new entity's <see cref="Name"/>.</param>
		public static unsafe Entity Create(string name = "Entity")
		{
			return new Entity(InternalCalls.Scene_CreateEntity(name));
		}

		/// <summary>Spawns an instance of a prefab where the prefab was saved.</summary>
		/// <returns>The instance's root, or null when <paramref name="prefab"/> is unset or missing. Its scripts'
		/// <c>OnCreate</c> has run when this returns.</returns>
		public static unsafe Entity Instantiate(Prefab prefab)
		{
			ulong id = prefab != null ? InternalCalls.Scene_InstantiatePrefab(prefab.Handle, null, null) : 0;
			return id != 0 ? new Entity(id) : null;
		}

		/// <summary>Spawns an instance of a prefab at a position and rotation.</summary>
		/// <param name="prefab">The prefab to spawn.</param>
		/// <param name="position">The root's position.</param>
		/// <param name="rotation">The root's rotation, Euler angles in radians like <see cref="Rotation"/>.</param>
		/// <returns>The instance's root, or null when <paramref name="prefab"/> is unset or missing. Its scripts'
		/// <c>OnCreate</c> has run when this returns.</returns>
		public static unsafe Entity Instantiate(Prefab prefab, Vector3 position, Vector3 rotation = default)
		{
			ulong id = prefab != null ? InternalCalls.Scene_InstantiatePrefab(prefab.Handle, &position, &rotation) : 0;
			return id != 0 ? new Entity(id) : null;
		}

		/// <summary>Destroys an entity at the end of the current update, like <see cref="Destroy()"/>. Null is ignored.</summary>
		public static unsafe void Destroy(Entity entity)
		{
			if (entity != null)
				InternalCalls.Scene_DestroyEntity(entity.ID);
		}
	}
}
