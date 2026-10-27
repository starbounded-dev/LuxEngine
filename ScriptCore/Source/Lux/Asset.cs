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

	/// <summary>A scene asset.</summary>
	[EditorAssignable]
	public class Scene : AssetRef
	{
		public Scene() { }
		internal Scene(ulong handle) : base(handle) { }
	}
}
