// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;

namespace Lux
{
	/// <summary>
	/// A skinned mesh's animation: playback of a clip or an animation graph, graph parameters, bone transforms
	/// and bone attachments. Main thread only. A mesh plays either one clip (set in the inspector, or with
	/// <see cref="CrossFade"/>) or, when the component has a Graph, the graph's states.
	/// </summary>
	public unsafe class SkeletonComponent : Component
	{
		private float Get(int field) => InternalCalls.Animation_GetScalar(Entity.ID, field);

		private void Set(int field, float value)
		{
			if (!InternalCalls.Animation_SetScalar(Entity.ID, field, value))
				throw new ArgumentOutOfRangeException(nameof(value), "Invalid value or the entity has no skeleton.");
		}

		/// <summary>Whether the clip advances each frame. False holds the current pose.</summary>
		public bool Playing { get => Get(0) != 0; set => Set(0, value ? 1 : 0); }

		/// <summary>Seconds into the current clip. Setting it jumps; it wraps or clamps on the next update.</summary>
		public float Time { get => Get(1); set => Set(1, value); }

		/// <summary>Playback rate: 1 is normal, 2 twice as fast, negative plays backwards.</summary>
		public float Speed { get => Get(2); set => Set(2, value); }

		/// <summary>Whether the clip wraps at its end instead of holding its last pose. A graph state's own Loop setting wins.</summary>
		public bool Loop { get => Get(3) != 0; set => Set(3, value ? 1 : 0); }

		/// <summary>
		/// Switches to another clip, fading from the current one over <paramref name="seconds"/> (0 switches at once).
		/// Not available while the component has an animation graph: use <see cref="PlayState"/>.
		/// </summary>
		/// <param name="clip">The handle of an Animation asset (.lanim).</param>
		/// <param name="seconds">Fade length in seconds.</param>
		/// <returns>False when the handle is not an animation or the component runs a graph.</returns>
		public bool CrossFade(ulong clip, float seconds) => InternalCalls.Animation_CrossFade(Entity.ID, clip, seconds);

		/// <summary>Moves the animation graph to a state at once, fading over <paramref name="seconds"/>.</summary>
		/// <param name="state">The state's name in the graph.</param>
		/// <param name="seconds">Fade length in seconds.</param>
		/// <returns>False when there is no graph or no such state.</returns>
		public bool PlayState(string state, float seconds = 0.2f) => InternalCalls.Animation_PlayState(Entity.ID, state, seconds);

		/// <summary>The name of the animation graph's current state, or an empty string without a graph.</summary>
		public string CurrentState => InternalCalls.Animation_GetState(Entity.ID);

		/// <summary>Sets a Float parameter of the animation graph.</summary>
		/// <returns>False for an unknown parameter or one of another type.</returns>
		public bool SetFloat(string parameter, float value) => InternalCalls.Animation_SetParameter(Entity.ID, parameter, 0, value);

		/// <summary>Sets a Bool parameter of the animation graph.</summary>
		/// <returns>False for an unknown parameter or one of another type.</returns>
		public bool SetBool(string parameter, bool value) => InternalCalls.Animation_SetParameter(Entity.ID, parameter, 1, value ? 1 : 0);

		/// <summary>Sets a Trigger parameter: the next transition that tests it fires and clears it.</summary>
		/// <returns>False for an unknown parameter or one of another type.</returns>
		public bool SetTrigger(string parameter) => InternalCalls.Animation_SetParameter(Entity.ID, parameter, 2, 0);

		/// <summary>Reads a parameter of the animation graph (a Bool is 0 or 1, a Trigger 1 while pending).</summary>
		public float GetFloat(string parameter) => InternalCalls.Animation_GetParameter(Entity.ID, parameter);

		/// <summary>The world-space position and rotation (Euler radians, like <see cref="TransformComponent.Rotation"/>) of a bone in the current pose.</summary>
		/// <param name="bone">The bone's name in the skeleton.</param>
		/// <param name="position">The bone's world position.</param>
		/// <param name="rotation">The bone's world rotation.</param>
		/// <returns>False when the bone does not exist or no pose has been evaluated yet.</returns>
		public bool GetBoneTransform(string bone, out Vector3 position, out Vector3 rotation)
		{
			Vector3 p, r;
			bool ok = InternalCalls.Animation_GetBone(Entity.ID, bone, &p, &r);
			position = ok ? p : default;
			rotation = ok ? r : default;
			return ok;
		}

		/// <summary>
		/// Makes an entity follow a bone: from the next update its position and rotation track the bone (it keeps its
		/// own scale), whatever its parent. Attaching again moves it to another bone.
		/// </summary>
		/// <param name="child">The entity to move, for example a weapon.</param>
		/// <param name="bone">The bone's name in the skeleton.</param>
		/// <returns>False when the bone or entity does not exist.</returns>
		public bool AttachToBone(Entity child, string bone) => InternalCalls.Animation_Attach(Entity.ID, child.ID, bone);

		/// <summary>Stops an entity following a bone; it stays where it is.</summary>
		/// <returns>False when the entity was not attached.</returns>
		public bool Detach(Entity child) => InternalCalls.Animation_Detach(Entity.ID, child.ID);
	}
}
