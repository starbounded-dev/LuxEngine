namespace Lux
{
	// Base for asset-reference script fields. The engine serializes these as a UUID (AssetHandle)
	// and reconstructs the wrapper on Instantiate (see ScriptEngine::Instantiate). Each concrete
	// type must carry [EditorAssignable] (attributes are matched non-inherited).
	public abstract class AssetRef
	{
		public readonly ulong Handle;

		protected AssetRef() { Handle = 0; }
		internal AssetRef(ulong handle) { Handle = handle; }
	}

	[EditorAssignable]
	public class Prefab : AssetRef
	{
		public Prefab() { }
		internal Prefab(ulong handle) : base(handle) { }
	}

	[EditorAssignable]
	public class Mesh : AssetRef
	{
		public Mesh() { }
		internal Mesh(ulong handle) : base(handle) { }
	}

	[EditorAssignable]
	public class StaticMesh : AssetRef
	{
		public StaticMesh() { }
		internal StaticMesh(ulong handle) : base(handle) { }
	}

	[EditorAssignable]
	public class Material : AssetRef
	{
		public Material() { }
		internal Material(ulong handle) : base(handle) { }
	}

	[EditorAssignable]
	public class Texture2D : AssetRef
	{
		public Texture2D() { }
		internal Texture2D(ulong handle) : base(handle) { }
	}

	[EditorAssignable]
	public class Scene : AssetRef
	{
		public Scene() { }
		internal Scene(ulong handle) : base(handle) { }
	}
}
