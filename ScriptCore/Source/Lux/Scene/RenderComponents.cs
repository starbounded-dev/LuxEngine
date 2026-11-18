// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;

namespace Lux
{
	// The values of the native LightKind / LightField enums.
	internal enum LightKind { Directional = 0, Point = 1, Spot = 2, Area = 3, Sky = 4 }
	internal enum LightField
	{
		Intensity = 0, Range, Angle, ColorTemperature, ShadowAmount, Width, Height, Radius, Length, Rotation,
		CastShadows = 10, SoftShadows, UseColorTemperature, TwoSided
	}

	/// <summary>
	/// The base of the lights that shine from an entity: <see cref="DirectionalLightComponent"/>,
	/// <see cref="PointLightComponent"/>, <see cref="SpotLightComponent"/> and <see cref="AreaLightComponent"/>.
	/// <see cref="Component.Enabled"/> switches a light off without losing its settings.
	/// </summary>
	public abstract unsafe class LightComponent : Component
	{
		private protected abstract LightKind Kind { get; }

		private protected float GetFloat(LightField field) => InternalCalls.Light_GetScalar(Entity.ID, (int)Kind, (int)field);
		private protected void SetFloat(LightField field, float value) => InternalCalls.Light_SetScalar(Entity.ID, (int)Kind, (int)field, value);
		private protected bool GetFlag(LightField field) => InternalCalls.Light_GetScalar(Entity.ID, (int)Kind, (int)field) != 0.0f;
		private protected void SetFlag(LightField field, bool value) => InternalCalls.Light_SetScalar(Entity.ID, (int)Kind, (int)field, value ? 1.0f : 0.0f);

		/// <summary>The color of the light. Components are limited to 0 or more; alpha is ignored.</summary>
		public Color Color
		{
			get { Vector3 c; InternalCalls.Light_GetColor(Entity.ID, (int)Kind, &c); return new Color(c.X, c.Y, c.Z); }
			set { Vector3 c = new Vector3(value.R, value.G, value.B); InternalCalls.Light_SetColor(Entity.ID, (int)Kind, &c); }
		}

		/// <summary>How bright the light is, in the unit chosen in the inspector. Negative values become 0.</summary>
		public float Intensity { get => GetFloat(LightField.Intensity); set => SetFloat(LightField.Intensity, value); }

		/// <summary>Whether the light casts shadows.</summary>
		public bool CastShadows { get => GetFlag(LightField.CastShadows); set => SetFlag(LightField.CastShadows, value); }

		/// <summary>Whether its shadows have soft edges.</summary>
		public bool SoftShadows { get => GetFlag(LightField.SoftShadows); set => SetFlag(LightField.SoftShadows, value); }

		/// <summary>Whether <see cref="ColorTemperature"/> replaces <see cref="Color"/> as the light's tint.</summary>
		public bool UseColorTemperature { get => GetFlag(LightField.UseColorTemperature); set => SetFlag(LightField.UseColorTemperature, value); }

		/// <summary>The tint of the light as a black-body temperature in Kelvin, when <see cref="UseColorTemperature"/> is on.</summary>
		public float ColorTemperature { get => GetFloat(LightField.ColorTemperature); set => SetFloat(LightField.ColorTemperature, value); }
	}

	/// <summary>A light like the sun: parallel rays from the direction the entity faces.</summary>
	public sealed class DirectionalLightComponent : LightComponent
	{
		private protected override LightKind Kind => LightKind.Directional;

		/// <summary>How dark the shadows are, from 0 (none) to 1.</summary>
		public float ShadowAmount { get => GetFloat(LightField.ShadowAmount); set => SetFloat(LightField.ShadowAmount, value); }
	}

	/// <summary>A light that shines in every direction from a point.</summary>
	public sealed class PointLightComponent : LightComponent
	{
		private protected override LightKind Kind => LightKind.Point;

		/// <summary>The distance in meters where the light has faded to nothing.</summary>
		public float Range { get => GetFloat(LightField.Range); set => SetFloat(LightField.Range, value); }
	}

	/// <summary>A cone of light.</summary>
	public sealed class SpotLightComponent : LightComponent
	{
		private protected override LightKind Kind => LightKind.Spot;

		/// <summary>The distance in meters where the light has faded to nothing.</summary>
		public float Range { get => GetFloat(LightField.Range); set => SetFloat(LightField.Range, value); }

		/// <summary>The width of the cone in degrees.</summary>
		public float Angle { get => GetFloat(LightField.Angle); set => SetFloat(LightField.Angle, value); }
	}

	/// <summary>A light with a surface: a rectangle, disk, sphere or tube, set in the inspector.</summary>
	public sealed class AreaLightComponent : LightComponent
	{
		private protected override LightKind Kind => LightKind.Area;

		/// <summary>The width of a rectangle light, in meters.</summary>
		public float Width { get => GetFloat(LightField.Width); set => SetFloat(LightField.Width, value); }

		/// <summary>The height of a rectangle light, in meters.</summary>
		public float Height { get => GetFloat(LightField.Height); set => SetFloat(LightField.Height, value); }

		/// <summary>The radius of a disk, sphere or tube light, in meters.</summary>
		public float Radius { get => GetFloat(LightField.Radius); set => SetFloat(LightField.Radius, value); }

		/// <summary>The length of a tube light, in meters.</summary>
		public float Length { get => GetFloat(LightField.Length); set => SetFloat(LightField.Length, value); }

		/// <summary>Whether a rectangle or disk light shines from both faces.</summary>
		public bool TwoSided { get => GetFlag(LightField.TwoSided); set => SetFlag(LightField.TwoSided, value); }

		/// <summary>The distance in meters from the light's center where its influence has faded to nothing.</summary>
		public float Range { get => GetFloat(LightField.Range); set => SetFloat(LightField.Range, value); }
	}

	/// <summary>The light and reflections from the sky, the environment the scene sits in.</summary>
	public unsafe sealed class SkyLightComponent : Component
	{
		/// <summary>How bright the sky light is. Negative values become 0.</summary>
		public float Intensity
		{
			get => InternalCalls.Light_GetScalar(Entity.ID, (int)LightKind.Sky, (int)LightField.Intensity);
			set => InternalCalls.Light_SetScalar(Entity.ID, (int)LightKind.Sky, (int)LightField.Intensity, value);
		}

		/// <summary>Turns the sky, and the light it gives, about the vertical axis, in degrees.</summary>
		public float Rotation
		{
			get => InternalCalls.Light_GetScalar(Entity.ID, (int)LightKind.Sky, (int)LightField.Rotation);
			set => InternalCalls.Light_SetScalar(Entity.ID, (int)LightKind.Sky, (int)LightField.Rotation, value);
		}
	}

	/// <summary>The scalar values of a material that <see cref="StaticMeshComponent.GetMaterialValue"/> reads and writes.</summary>
	public enum MaterialValue
	{
		/// <summary>How metal the surface is, 0 to 1.</summary>
		Metalness = 1,
		/// <summary>How rough the surface is, 0 (mirror) to 1.</summary>
		Roughness = 2,
		/// <summary>How much the surface glows, 0 or more.</summary>
		Emission = 3,
		/// <summary>The opacity of a transparent material, 0 (invisible) to 1.</summary>
		Transparency = 4
	}

	/// <summary>
	/// A mesh drawn at the entity. A mesh can have several material slots, one per material of the model; a script
	/// can swap the mesh, assign a material to a slot, or change a slot's values for this entity alone.
	/// <see cref="Component.Enabled"/> shows or hides the mesh.
	/// </summary>
	public unsafe sealed class StaticMeshComponent : Component
	{
		/// <summary>The mesh asset, or null when none is set. Assigning one that is not a static mesh does nothing.</summary>
		public StaticMesh? StaticMesh
		{
			get { ulong handle = InternalCalls.StaticMesh_GetMesh(Entity.ID); return handle != 0 ? new StaticMesh(handle) : null; }
			set => InternalCalls.StaticMesh_SetMesh(Entity.ID, value != null ? value.Handle : 0);
		}

		/// <summary>The number of material slots the mesh has; 0 without a mesh.</summary>
		public int MaterialCount => InternalCalls.StaticMesh_GetMaterialCount(Entity.ID);

		/// <summary>The material a slot draws with: this entity's own, or the mesh's. Null for a slot the mesh does not have.</summary>
		/// <param name="slot">The slot, 0 to <see cref="MaterialCount"/> - 1.</param>
		public Material? GetMaterial(int slot)
		{
			ulong handle = InternalCalls.StaticMesh_GetMaterial(Entity.ID, slot);
			return handle != 0 ? new Material(handle) : null;
		}

		/// <summary>Makes a slot draw with a material, for this entity only. Null goes back to the mesh's own material.</summary>
		/// <param name="slot">The slot, 0 to <see cref="MaterialCount"/> - 1.</param>
		/// <param name="material">The material asset.</param>
		/// <exception cref="ArgumentException">The slot does not exist or the asset is not a material.</exception>
		public void SetMaterial(int slot, Material? material)
		{
			if (!InternalCalls.StaticMesh_SetMaterial(Entity.ID, slot, material != null ? material.Handle : 0))
				throw new ArgumentException("The slot does not exist or the asset is not a material.");
		}

		/// <summary>The color of a slot's material.</summary>
		/// <param name="slot">The slot, 0 to <see cref="MaterialCount"/> - 1.</param>
		/// <exception cref="ArgumentException">The slot has no material.</exception>
		public Color GetMaterialColor(int slot)
		{
			Vector4 value;
			if (!InternalCalls.StaticMesh_GetMaterialValue(Entity.ID, slot, 0, &value))
				throw new ArgumentException("The slot has no material.");
			return new Color(value.X, value.Y, value.Z);
		}

		/// <summary>
		/// Changes the color of a slot's material for this entity alone. The first change gives the entity its own copy of
		/// the material, so other entities that share the material keep theirs.
		/// </summary>
		/// <param name="slot">The slot, 0 to <see cref="MaterialCount"/> - 1.</param>
		/// <param name="color">The new color; alpha is ignored.</param>
		/// <exception cref="ArgumentException">The slot has no material.</exception>
		public void SetMaterialColor(int slot, Color color)
		{
			Vector4 value = new Vector4(color.R, color.G, color.B, 1.0f);
			if (!InternalCalls.StaticMesh_SetMaterialValue(Entity.ID, slot, 0, &value))
				throw new ArgumentException("The slot has no material.");
		}

		/// <summary>A scalar value of a slot's material.</summary>
		/// <param name="slot">The slot, 0 to <see cref="MaterialCount"/> - 1.</param>
		/// <param name="which">Which value to read.</param>
		/// <exception cref="ArgumentException">The slot has no material.</exception>
		public float GetMaterialValue(int slot, MaterialValue which)
		{
			Vector4 value;
			if (!InternalCalls.StaticMesh_GetMaterialValue(Entity.ID, slot, (int)which, &value))
				throw new ArgumentException("The slot has no material.");
			return value.X;
		}

		/// <summary>
		/// Changes a scalar value of a slot's material for this entity alone, like <see cref="SetMaterialColor"/>.
		/// Values are limited to their range.
		/// </summary>
		/// <param name="slot">The slot, 0 to <see cref="MaterialCount"/> - 1.</param>
		/// <param name="which">Which value to change.</param>
		/// <param name="value">The new value.</param>
		/// <exception cref="ArgumentException">The slot has no material.</exception>
		public void SetMaterialValue(int slot, MaterialValue which, float value)
		{
			Vector4 packed = new Vector4(value);
			if (!InternalCalls.StaticMesh_SetMaterialValue(Entity.ID, slot, (int)which, &packed))
				throw new ArgumentException("The slot has no material.");
		}
	}

	/// <summary>
	/// The scene's music director as authored in the inspector: what plays when the game starts. During the game, drive
	/// the music with <see cref="Music"/>.
	/// </summary>
	public unsafe sealed class MusicDirectorComponent : Component
	{
		/// <summary>Whether the music starts when the game does.</summary>
		public bool PlayOnAwake
		{
			get => InternalCalls.MusicDirector_GetScalar(Entity.ID, 0) != 0.0f;
			set => InternalCalls.MusicDirector_SetScalar(Entity.ID, 0, value ? 1.0f : 0.0f);
		}

		/// <summary>The music's Intensity at the start, 0 to 1.</summary>
		public float Intensity
		{
			get => InternalCalls.MusicDirector_GetScalar(Entity.ID, 1);
			set => InternalCalls.MusicDirector_SetScalar(Entity.ID, 1, value);
		}

		/// <summary>The State label the music starts in; empty for none.</summary>
		public string InitialState
		{
			get => InternalCalls.MusicDirector_GetInitialState(Entity.ID);
			set => InternalCalls.MusicDirector_SetInitialState(Entity.ID, value ?? string.Empty);
		}
	}
}
