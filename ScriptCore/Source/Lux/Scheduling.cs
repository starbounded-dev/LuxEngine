// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;
using System.Collections;
using System.Collections.Generic;

namespace Lux
{
	/// <summary>
	/// Something a coroutine can <c>yield return</c> to wait: <see cref="WaitForSeconds"/>, <see cref="WaitForSecondsRealtime"/>,
	/// <see cref="WaitUntil"/>, <see cref="WaitWhile"/>, <see cref="WaitForFixedUpdate"/>, or another <see cref="Coroutine"/>.
	/// </summary>
	public abstract class YieldInstruction
	{
		internal YieldInstruction() { }

		// True once the wait is over. Called each frame while the coroutine waits.
		internal abstract bool IsDone(double scaledTime, double unscaledTime, long fixedSteps);

		// Called once when the coroutine starts waiting, to take the clock reading the wait counts from.
		internal virtual void Begin(double scaledTime, double unscaledTime, long fixedSteps) { }
	}

	/// <summary>Waits for a number of seconds of game time. It does not advance while <see cref="Time.TimeScale"/> is 0.</summary>
	public sealed class WaitForSeconds : YieldInstruction
	{
		private readonly float m_Seconds;
		private double m_Due;

		/// <summary>A wait of <paramref name="seconds"/> of game time.</summary>
		public WaitForSeconds(float seconds) { m_Seconds = seconds; }

		internal override void Begin(double scaledTime, double unscaledTime, long fixedSteps) => m_Due = scaledTime + m_Seconds;
		internal override bool IsDone(double scaledTime, double unscaledTime, long fixedSteps) => scaledTime >= m_Due;
	}

	/// <summary>Waits for a number of seconds of real time, whatever <see cref="Time.TimeScale"/> is.</summary>
	public sealed class WaitForSecondsRealtime : YieldInstruction
	{
		private readonly float m_Seconds;
		private double m_Due;

		/// <summary>A wait of <paramref name="seconds"/> of real time.</summary>
		public WaitForSecondsRealtime(float seconds) { m_Seconds = seconds; }

		internal override void Begin(double scaledTime, double unscaledTime, long fixedSteps) => m_Due = unscaledTime + m_Seconds;
		internal override bool IsDone(double scaledTime, double unscaledTime, long fixedSteps) => unscaledTime >= m_Due;
	}

	/// <summary>Waits until a condition becomes true. It is checked once a frame, starting with the frame after the wait begins.</summary>
	public sealed class WaitUntil : YieldInstruction
	{
		private readonly Func<bool> m_Condition;

		/// <summary>A wait that ends when <paramref name="condition"/> returns true.</summary>
		public WaitUntil(Func<bool> condition) { m_Condition = condition ?? throw new ArgumentNullException(nameof(condition)); }

		internal override bool IsDone(double scaledTime, double unscaledTime, long fixedSteps) => m_Condition();
	}

	/// <summary>Waits while a condition is true. It is checked once a frame, starting with the frame after the wait begins.</summary>
	public sealed class WaitWhile : YieldInstruction
	{
		private readonly Func<bool> m_Condition;

		/// <summary>A wait that ends when <paramref name="condition"/> returns false.</summary>
		public WaitWhile(Func<bool> condition) { m_Condition = condition ?? throw new ArgumentNullException(nameof(condition)); }

		internal override bool IsDone(double scaledTime, double unscaledTime, long fixedSteps) => !m_Condition();
	}

	/// <summary>Waits for the next physics step, then continues just before the next <c>OnFixedUpdate</c> round has finished.</summary>
	public sealed class WaitForFixedUpdate : YieldInstruction
	{
		private long m_Start;

		internal override void Begin(double scaledTime, double unscaledTime, long fixedSteps) => m_Start = fixedSteps;
		internal override bool IsDone(double scaledTime, double unscaledTime, long fixedSteps) => fixedSteps > m_Start;
	}

	/// <summary>
	/// A routine started with <see cref="Entity.StartCoroutine"/>: a method that returns <see cref="IEnumerator"/> and pauses at
	/// each <c>yield return</c>. <c>yield return null</c> continues next frame; <c>yield return</c> a <see cref="YieldInstruction"/>
	/// waits as it says; <c>yield return</c> another <see cref="Coroutine"/> (or an <see cref="IEnumerator"/>) waits for that one to end.
	/// It stops when it finishes, when <see cref="Entity.StopCoroutine"/> is called, when its entity is destroyed, and
	/// when the scene ends. It is paused while its entity is switched off.
	/// </summary>
	public sealed class Coroutine : YieldInstruction
	{
		internal readonly ulong Owner;
		internal readonly Stack<IEnumerator> Frames = new Stack<IEnumerator>();
		internal YieldInstruction? Waiting;
		internal bool Cancelled;
		private bool m_Finished;

		internal Coroutine(ulong owner, IEnumerator routine)
		{
			Owner = owner;
			Frames.Push(routine);
		}

		/// <summary>Whether the routine has ended (finished or stopped).</summary>
		public bool IsFinished => m_Finished || Cancelled;

		internal void Finish() => m_Finished = true;

		internal override bool IsDone(double scaledTime, double unscaledTime, long fixedSteps) => IsFinished;
	}

	/// <summary>A callback set to run later, or every so often, by <see cref="Timer.After"/>, <see cref="Timer.Every"/>, <see cref="Entity.After"/> or <see cref="Entity.Every"/>.</summary>
	public sealed class Timer
	{
		internal readonly ulong Owner;
		internal readonly Action Callback;
		internal readonly float Interval;
		internal readonly bool Repeats;
		internal readonly bool Unscaled;
		internal double Due;
		internal bool Cancelled;

		internal Timer(ulong owner, Action callback, float interval, bool repeats, bool unscaled)
		{
			Owner = owner;
			Callback = callback;
			Interval = interval;
			Repeats = repeats;
			Unscaled = unscaled;
		}

		/// <summary>Whether the timer has not yet been cancelled or run out. A repeating timer stays active until cancelled.</summary>
		public bool IsActive => !Cancelled;

		/// <summary>Stops the timer; a callback that has not run yet never will. Cancelling twice does nothing.</summary>
		public void Cancel() => Cancelled = true;

		/// <summary>
		/// Runs <paramref name="callback"/> once, <paramref name="seconds"/> from now. It keeps running across
		/// <see cref="Scene.Load"/> and stops when the game does. Use <see cref="Entity.After"/> to tie it to an entity.
		/// </summary>
		/// <param name="seconds">The delay.</param>
		/// <param name="callback">What to run.</param>
		/// <param name="unscaled">Count real time instead of game time, so <see cref="Time.TimeScale"/> does not slow it.</param>
		public static Timer After(float seconds, Action callback, bool unscaled = false) => Scheduler.AddTimer(0, seconds, callback, false, unscaled);

		/// <summary>Runs <paramref name="callback"/> every <paramref name="seconds"/>, until cancelled. See <see cref="After"/>.</summary>
		/// <param name="seconds">The time between calls, above 0.</param>
		/// <param name="callback">What to run.</param>
		/// <param name="unscaled">Count real time instead of game time.</param>
		public static Timer Every(float seconds, Action callback, bool unscaled = false) => Scheduler.AddTimer(0, seconds, callback, true, unscaled);
	}

	// Runs every timer and coroutine. The engine calls Update once a frame (after the scripts' OnUpdate) and FixedUpdate once per
	// physics step; the clocks here are its own, so a pause or a Time.TimeScale of 0 simply stops the scaled one.
	internal static unsafe class Scheduler
	{
		private static readonly List<Timer> s_Timers = new List<Timer>();
		private static readonly List<Coroutine> s_Coroutines = new List<Coroutine>();
		private static double s_Scaled, s_Unscaled;
		private static long s_FixedSteps;

		private static bool OwnerAlive(ulong owner) => owner == 0 || InternalCalls.Entity_IsAlive(owner);
		private static bool OwnerRuns(ulong owner) => owner == 0 || InternalCalls.Entity_IsRunning(owner);

		internal static Timer AddTimer(ulong owner, float seconds, Action callback, bool repeats, bool unscaled)
		{
			if (callback == null)
				throw new ArgumentNullException(nameof(callback));
			if (float.IsNaN(seconds) || seconds < 0.0f || (repeats && seconds <= 0.0f))
				throw new ArgumentOutOfRangeException(nameof(seconds), seconds, repeats ? "A repeating timer needs an interval above 0." : "A delay cannot be negative.");
			Timer timer = new Timer(owner, callback, seconds, repeats, unscaled);
			timer.Due = (unscaled ? s_Unscaled : s_Scaled) + seconds;
			s_Timers.Add(timer);
			return timer;
		}

		internal static Coroutine AddCoroutine(ulong owner, IEnumerator routine)
		{
			if (routine == null)
				throw new ArgumentNullException(nameof(routine));
			Coroutine coroutine = new Coroutine(owner, routine);
			s_Coroutines.Add(coroutine);
			// Like Unity, the first step runs right away, up to the first yield.
			Step(coroutine);
			return coroutine;
		}

		internal static void StopCoroutines(ulong owner)
		{
			foreach (Coroutine coroutine in s_Coroutines)
			{
				if (coroutine.Owner == owner)
					coroutine.Cancelled = true;
			}
		}

		internal static void CancelTimers(ulong owner)
		{
			foreach (Timer timer in s_Timers)
			{
				if (timer.Owner == owner)
					timer.Cancelled = true;
			}
		}

		// Advances a coroutine until it waits or ends.
		private static void Step(Coroutine coroutine)
		{
			while (!coroutine.Cancelled && coroutine.Frames.Count > 0)
			{
				IEnumerator top = coroutine.Frames.Peek();
				bool more;
				try
				{
					more = top.MoveNext();
				}
				catch (Exception exception)
				{
					Log.Error($"A coroutine threw and was stopped: {exception}");
					coroutine.Cancelled = true;
					return;
				}

				if (!more)
				{
					coroutine.Frames.Pop();
					continue;
				}

				object? yielded = top.Current;
				if (yielded is IEnumerator nested)
				{
					coroutine.Frames.Push(nested);
					continue;
				}
				if (yielded is YieldInstruction instruction)
				{
					if (instruction is Coroutine other && other.IsFinished)
						continue;
					instruction.Begin(s_Scaled, s_Unscaled, s_FixedSteps);
					coroutine.Waiting = instruction;
					return;
				}
				coroutine.Waiting = null;   // null (or anything else): continue next frame
				return;
			}
			coroutine.Finish();
		}

		internal static void Update(float scaledDelta, float unscaledDelta)
		{
			s_Scaled += scaledDelta;
			s_Unscaled += unscaledDelta;

			// Callbacks may start timers and coroutines: those join the lists and first run next frame.
			int timerCount = s_Timers.Count;
			for (int i = 0; i < timerCount; i++)
			{
				Timer timer = s_Timers[i];
				if (timer.Cancelled)
					continue;
				if (!OwnerAlive(timer.Owner))
				{
					timer.Cancelled = true;
					continue;
				}
				if (!OwnerRuns(timer.Owner))
					continue;

				double now = timer.Unscaled ? s_Unscaled : s_Scaled;
				// A repeating timer that fell behind (a long frame) runs once per frame, not once per missed interval.
				if (now < timer.Due)
					continue;
				timer.Due = timer.Repeats ? Math.Max(timer.Due + timer.Interval, now) : double.MaxValue;
				if (!timer.Repeats)
					timer.Cancelled = true;
				try
				{
					timer.Callback();
				}
				catch (Exception exception)
				{
					Log.Error($"A timer callback threw: {exception}");
				}
			}

			int coroutineCount = s_Coroutines.Count;
			for (int i = 0; i < coroutineCount; i++)
			{
				Coroutine coroutine = s_Coroutines[i];
				if (coroutine.Cancelled || coroutine.IsFinished)
					continue;
				if (!OwnerAlive(coroutine.Owner))
				{
					coroutine.Cancelled = true;
					continue;
				}
				if (!OwnerRuns(coroutine.Owner))
					continue;

				YieldInstruction? waiting = coroutine.Waiting;
				if (waiting != null)
				{
					bool done;
					try
					{
						done = waiting.IsDone(s_Scaled, s_Unscaled, s_FixedSteps);
					}
					catch (Exception exception)
					{
						Log.Error($"A coroutine's wait threw and the coroutine was stopped: {exception}");
						coroutine.Cancelled = true;
						continue;
					}
					if (!done)
						continue;
				}
				coroutine.Waiting = null;
				Step(coroutine);
			}

			s_Timers.RemoveAll(timer => timer.Cancelled);
			s_Coroutines.RemoveAll(coroutine => coroutine.Cancelled || coroutine.IsFinished);
		}

		internal static void FixedUpdate() => s_FixedSteps++;

		internal static void Reset()
		{
			s_Timers.Clear();
			s_Coroutines.Clear();
			s_Scaled = 0.0;
			s_Unscaled = 0.0;
			s_FixedSteps = 0;
		}
	}
}
