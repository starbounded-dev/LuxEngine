// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

namespace Lux
{
	// Base for asset-reference script fields. The engine serializes these as a UUID (AssetHandle)
	// and reconstructs the wrapper on Instantiate (see ScriptEngine::Instantiate). Each concrete
	// type must carry [EditorAssignable] (attributes are matched non-inherited).
	/// <summary>
	/// The base of script fields that reference an asset. A public field of a derived type is assigned in the
	/// inspector by dragging the asset onto it.
	/// </summary>
	public abstract class AssetRef
	{
		/// <summary>The asset's handle (its UUID), or 0 when the field is unassigned.</summary>
		public readonly ulong Handle;

		protected AssetRef() { Handle = 0; }
		internal AssetRef(ulong handle) { Handle = handle; }
	}

	/// <summary>A prefab asset, for <see cref="Entity.Instantiate(Prefab)"/>.</summary>
	[EditorAssignable]
	public class Prefab : AssetRef
	{
		public Prefab() { }
		internal Prefab(ulong handle) : base(handle) { }
	}

	/// <summary>A mesh asset.</summary>
	[EditorAssignable]
	public class Mesh : AssetRef
	{
		public Mesh() { }
		internal Mesh(ulong handle) : base(handle) { }
	}

	/// <summary>A static mesh asset.</summary>
	[EditorAssignable]
	public class StaticMesh : AssetRef
	{
		public StaticMesh() { }
		internal StaticMesh(ulong handle) : base(handle) { }
	}

	/// <summary>A material asset.</summary>
	[EditorAssignable]
	public class Material : AssetRef
	{
		public Material() { }
		internal Material(ulong handle) : base(handle) { }
	}

	/// <summary>A texture asset.</summary>
	[EditorAssignable]
	public class Texture2D : AssetRef
	{
		public Texture2D() { }
		internal Texture2D(ulong handle) : base(handle) { }
	}

	/// <summary>
	/// A scene asset. Assign one to a field in the inspector, then load it from a script. A scene change is carried out
	/// when the frame's update ends: the call returns at once and the running scripts finish their update first.
	/// </summary>
	[EditorAssignable]
	public class Scene : AssetRef
	{
		public Scene() { }
		internal Scene(ulong handle) : base(handle) { }

		/// <summary>The scene that is running now. <c>Scene.Current.Load()</c> restarts it.</summary>
		public static unsafe Scene Current => new Scene(InternalCalls.Scene_GetCurrent());

		/// <summary>
		/// Replaces the running scene with this one. Everything in the old scene ends (its scripts get <c>OnDestroy</c>)
		/// except entities with <see cref="Entity.Persistent"/> set, which move to the new scene with their children and
		/// scripts, and <see cref="Time.TimeScale"/>, which carries over. If the scene cannot be loaded an error is logged and
		/// the current one keeps running. Several calls in one frame: the last one wins.
		/// </summary>
		public unsafe void Load() => InternalCalls.Scene_Load(Handle);

		/// <summary>
		/// Adds this scene's entities to the running scene without ending it, as spawning a prefab does: their scripts
		/// run <c>OnCreate</c> and their bodies start. Loading the same scene again adds another copy. The entities'
		/// IDs are kept unless the running scene already uses them; script fields that hold an <see cref="Entity"/> of the
		/// added scene are not updated for the new IDs.
		/// </summary>
		public unsafe void LoadAdditive() => InternalCalls.Scene_LoadAdditive(Handle);

		/// <summary>Removes every entity added by <see cref="LoadAdditive"/> from this scene, with their scripts' <c>OnDestroy</c>.</summary>
		public unsafe void Unload() => InternalCalls.Scene_UnloadAdditive(Handle);
	}

	/// <summary>A Sound asset (.lsound), played by the native audio backend. Pass it to
	/// <see cref="Audio.PlayOneShot(Sound)"/> or <see cref="Audio.CreateInstance(Sound)"/>.</summary>
	[EditorAssignable]
	public class Sound : AssetRef
	{
		public Sound() { }
		internal Sound(ulong handle) : base(handle) { }
	}

	/// <summary>A Sound Bank asset (.lbank): Sounds that load and unload together.</summary>
	[EditorAssignable]
	public class SoundBank : AssetRef
	{
		public SoundBank() { }
		internal SoundBank(ulong handle) : base(handle) { }
	}
}
