// Play-mode checks for the M4 C# API (docs/M4_CSHARP_API_PLAN.md), reported through the same Report as PlayTests.cs.
// Like the older checks they count frames or physics steps, never wall-clock time.
using System;
using Lux;

namespace PlayTest
{
	// SC2: Time. The scaled clock follows TimeScale, OnUpdate receives it, OnFixedUpdate gets the fixed step.
	public class ClockCheck : Entity
	{
		int m_Frame;
		long m_FirstFrameCount;
		int m_FixedCalls, m_FixedAtFreeze;
		bool m_FixedStepMatches = true;
		string m_Problem;

		void Fail(string reason) { if (m_Problem == null) m_Problem = reason; }

		void OnUpdate(float ts)
		{
			m_Frame++;
			if (Math.Abs(ts - Time.DeltaTime) > 1e-6f)
				Fail($"frame {m_Frame}: OnUpdate got {ts} but Time.DeltaTime is {Time.DeltaTime}");
			if (m_Frame == 1)
				m_FirstFrameCount = Time.FrameCount;
			if (Time.FrameCount != m_FirstFrameCount + m_Frame - 1)
				Fail($"frame {m_Frame}: FrameCount is {Time.FrameCount}, expected {m_FirstFrameCount + m_Frame - 1}");

			if (m_Frame == 10)
				Time.TimeScale = 0.5f;
			if (m_Frame > 10 && m_Frame <= 30 && Math.Abs(Time.DeltaTime - 0.5f * Time.UnscaledDeltaTime) > 1e-5f)
				Fail($"frame {m_Frame}: at scale 0.5 Time.DeltaTime is {Time.DeltaTime}, unscaled {Time.UnscaledDeltaTime}");
			if (m_Frame == 30)
			{
				Time.TimeScale = 0.0f;
				m_FixedAtFreeze = m_FixedCalls;
			}
			if (m_Frame > 30 && m_Frame <= 50 && (ts != 0.0f || Time.DeltaTime != 0.0f))
				Fail($"frame {m_Frame}: at scale 0 OnUpdate got {ts}, Time.DeltaTime {Time.DeltaTime}");
			if (m_Frame == 50)
			{
				// The fixed clock may carry one step of time over from before the freeze.
				if (m_FixedCalls - m_FixedAtFreeze > 1)
					Fail($"{m_FixedCalls - m_FixedAtFreeze} physics steps ran while the scale was 0");
				Time.TimeScale = 1.0f;
			}
			if (m_Frame == 60)
			{
				if (Time.TimeScale != 1.0f || Time.DeltaTime <= 0.0f)
					Fail($"after restoring the scale: TimeScale {Time.TimeScale}, DeltaTime {Time.DeltaTime}");
				if (!(Time.ElapsedTime < Time.UnscaledTime))
					Fail($"scaled time {Time.ElapsedTime} should be behind real time {Time.UnscaledTime} after a slow-motion stretch");
				if (!m_FixedStepMatches)
					Fail("inside OnFixedUpdate Time.DeltaTime or Time.FixedDeltaTime differed from the step it received");
				Report.Result("Clock", m_Problem == null, m_Problem ?? "");
			}
		}

		void OnFixedUpdate(float dt)
		{
			m_FixedCalls++;
			if (Math.Abs(Time.DeltaTime - dt) > 1e-6f || Math.Abs(Time.FixedDeltaTime - dt) > 1e-6f)
				m_FixedStepMatches = false;
		}
	}

	// SC2: quaternion rotation and world-space transform members. The parent is yawed a quarter turn.
	public class TransformCheck : Entity
	{
		bool m_Done;

		void OnUpdate(float ts)
		{
			if (m_Done)
				return;
			m_Done = true;
			string problem = null;

			Vector3 world = WorldTranslation;
			if (Vector3.Distance(world, new Vector3(8.0f, 1.0f, -40.0f)) > 1e-3f)
				problem = $"WorldTranslation is {world}, expected (8, 1, -40) through the yawed parent";
			Vector3 forward = Forward;
			if (problem == null && Vector3.Distance(forward, new Vector3(-1.0f, 0.0f, 0.0f)) > 1e-3f)
				problem = $"Forward is {forward}, expected (-1, 0, 0)";

			if (problem == null)
			{
				Rotation = new Vector3(0.2f, 0.6f, -0.4f);
				Vector3 viaQuaternion = RotationQuaternion.ToEuler();
				if (Vector3.Distance(viaQuaternion, new Vector3(0.2f, 0.6f, -0.4f)) > 1e-4f)
					problem = $"RotationQuaternion.ToEuler() is {viaQuaternion} after setting Rotation to (0.2, 0.6, -0.4)";
			}
			if (problem == null)
			{
				RotationQuaternion = Quaternion.Euler(new Vector3(0.0f, 0.5f, 0.0f));
				if (Vector3.Distance(Rotation, new Vector3(0.0f, 0.5f, 0.0f)) > 1e-4f)
					problem = $"Rotation is {Rotation} after setting RotationQuaternion to a 0.5 rad yaw";
			}
			Report.Result("Transform", problem == null, problem ?? "");
		}
	}

	// S3: counts its own OnUpdate calls, so ActiveCheck can see when Active or ScriptEnabled stopped it.
	public class ToggleCounter : Entity
	{
		public int Count;

		void OnUpdate(float ts) => Count++;
	}

	// S3: Entity.Active, ActiveInHierarchy, ScriptEnabled and Component.Enabled, driven from a script.
	public class ActiveCheck : Entity
	{
		int m_Frame, m_TargetMark, m_ChildMark;
		Entity m_Target, m_Child;
		ToggleCounter m_TargetCounter, m_ChildCounter;
		string m_Problem;

		void Fail(string reason) { if (m_Problem == null) m_Problem = reason; }

		void OnUpdate(float ts)
		{
			m_Frame++;
			if (m_Frame == 1)
			{
				m_Target = FindEntityByName("ToggleTarget");
				m_Child = FindEntityByName("ToggleChild");
				m_TargetCounter = m_Target?.As<ToggleCounter>();
				m_ChildCounter = m_Child?.As<ToggleCounter>();
				if (m_TargetCounter == null || m_ChildCounter == null)
					Fail("the ToggleTarget / ToggleChild scripts were not found");
			}
			if (m_Problem != null && m_Frame < 60)
			{
				if (m_Frame == 59)
					Report.Result("ActiveEnabled", false, m_Problem);
				return;
			}

			switch (m_Frame)
			{
				case 5:
					SpriteChecks();
					break;
				case 10:
					if (m_TargetCounter.Count == 0 || m_ChildCounter.Count == 0)
						Fail("the counters were not running before anything was switched off");
					m_Target.Active = false;
					if (m_Target.Active || m_Target.ActiveInHierarchy)
						Fail("Active did not read back false");
					if (!m_Child.Active || m_Child.ActiveInHierarchy)
						Fail("a child of an inactive entity should keep its own Active but not be active in the hierarchy");
					break;
				case 11:
					m_TargetMark = m_TargetCounter.Count;
					m_ChildMark = m_ChildCounter.Count;
					break;
				case 20:
					if (m_TargetCounter.Count != m_TargetMark || m_ChildCounter.Count != m_ChildMark)
						Fail($"scripts on a switched-off entity and its child kept running ({m_TargetCounter.Count - m_TargetMark}, {m_ChildCounter.Count - m_ChildMark} more updates)");
					m_Target.Active = true;
					break;
				case 30:
					if (m_TargetCounter.Count <= m_TargetMark || m_ChildCounter.Count <= m_ChildMark)
						Fail("scripts did not resume after Active was set back to true");
					m_Child.ScriptEnabled = false;
					if (m_Child.ScriptEnabled || !m_Target.ScriptEnabled)
						Fail("ScriptEnabled did not read back");
					m_ChildMark = m_ChildCounter.Count;
					m_TargetMark = m_TargetCounter.Count;
					break;
				case 40:
					if (m_ChildCounter.Count != m_ChildMark)
						Fail("a script with ScriptEnabled = false kept updating");
					if (m_TargetCounter.Count <= m_TargetMark)
						Fail("switching one script off stopped its neighbour");
					m_Child.ScriptEnabled = true;
					m_ChildMark = m_ChildCounter.Count;
					break;
				case 50:
					if (m_ChildCounter.Count <= m_ChildMark)
						Fail("the script did not resume after ScriptEnabled = true");
					Report.Result("ActiveEnabled", m_Problem == null, m_Problem ?? "");
					break;
			}
		}

		void SpriteChecks()
		{
			Entity sprite = FindEntityByName("SwitchSprite");
			SpriteRendererComponent component = sprite?.GetComponent<SpriteRendererComponent>();
			if (component == null)
			{
				Fail("SwitchSprite has no SpriteRendererComponent");
				return;
			}
			if (!component.Enabled)
				Fail("a new sprite renderer should be enabled");
			component.Enabled = false;
			if (sprite.GetComponent<SpriteRendererComponent>().Enabled)
				Fail("Component.Enabled = false was not stored");
			component.Enabled = true;
			if (!sprite.GetComponent<SpriteRendererComponent>().Enabled)
				Fail("Component.Enabled = true was not stored");

			// A collider has no switch: it reads as on and stays on.
			BoxColliderComponent collider = FindEntityByName("Floor")?.GetComponent<BoxColliderComponent>();
			if (collider == null || !collider.Enabled)
				Fail("a component with no switch should read as enabled");
		}
	}

	// S4: finders by name, tag and component, and the user tag.
	public class FindCheck : Entity
	{
		int m_Frame;

		void OnUpdate(float ts)
		{
			if (++m_Frame != 3)
				return;
			string problem = null;
			void Expect(bool ok, string what) { if (!ok && problem == null) problem = what; }

			Entity[] twins = Entity.FindAll("TwinFind");
			Expect(twins.Length == 2 && Math.Abs(twins[0].Translation.X - 1.0f) < 1e-4f && Math.Abs(twins[1].Translation.X - 2.0f) < 1e-4f,
				$"FindAll(name) returned {twins.Length} entities, expected the two twins in creation order");
			Expect(Entity.Find("TwinFind") != null && Math.Abs(Entity.Find("TwinFind").Translation.X - 1.0f) < 1e-4f, "Find(name) should return the first twin");
			Expect(Entity.Find("NoSuchEntity") == null, "Find of a missing name should be null");

			Expect(Entity.FindAllWithTag("coin").Length == 2, $"FindAllWithTag(coin) found {Entity.FindAllWithTag("coin").Length}, expected 2 (the switched-off one and its child are skipped)");
			Expect(Entity.FindAllWithTag("coin", true).Length == 4, $"FindAllWithTag(coin, includeInactive) found {Entity.FindAllWithTag("coin", true).Length}, expected 4");
			Entity gem = Entity.FindWithTag("gem");
			Expect(gem != null && gem.Name == "TagGem" && gem.Tag == "gem" && gem.CompareTag("gem") && !gem.CompareTag("coin"), "FindWithTag(gem) / Tag / CompareTag");
			Expect(Entity.FindWithTag("nothing") == null && Entity.FindWithTag("") == null, "an unknown or empty tag should find nothing");
			Expect(Entity.FindWithTag("coin", true)?.Name == "TagCoinA", "the first coin in creation order is TagCoinA");

			if (gem != null)
			{
				gem.Tag = "coin";
				Expect(Entity.FindAllWithTag("coin").Length == 3, "a tag set from a script should be found at once");
				gem.Tag = "gem";
				Expect(Entity.FindAllWithTag("coin").Length == 2 && Entity.FindWithTag("gem") != null, "putting the tag back should restore the lookup");
			}

			Entity[] sprites = Entity.FindAllWithComponent<SpriteRendererComponent>();
			Expect(sprites.Length == 3 && sprites[0].Name == "SwitchSprite" && sprites[1].Name == "SpriteTwo" && sprites[2].Name == "Animated2D", $"FindAllWithComponent found {sprites.Length} sprites in the wrong order or number");
			Expect(Entity.FindWithComponent<CircleRendererComponent>() == null, "no entity has a circle renderer");
			Expect(Entity.FindWithComponent<SpriteRendererComponent>().Name == "SwitchSprite", "FindWithComponent returns the first");
			Report.Result("Find", problem == null, problem ?? "");
		}
	}

	// P3: Physics.Raycast / RaycastAll / SphereCast / BoxCast / CapsuleCast / Overlap* with layer masks, ignore and triggers.
	// Along +X at z=-70: QueryWall (Default layer, x 59.5..60.5), a QueryTrigger in front (x 57.75..58.25) and QueryProp
	// (Props layer, x 63.5..64.5). Rays start at x=56.
	public class QueryCheck : Entity
	{
		int m_Frame;

		static bool Near(float a, float b, float tolerance = 0.02f) => Math.Abs(a - b) <= tolerance;

		void OnUpdate(float ts)
		{
			if (++m_Frame != 4)
				return;
			string problem = null;
			void Expect(bool ok, string what) { if (!ok && problem == null) problem = what; }

			Entity wall = Entity.Find("QueryWall"), trigger = Entity.Find("QueryTrigger"), prop = Entity.Find("QueryProp");
			if (wall == null || trigger == null || prop == null)
			{
				Report.Result("Queries", false, "the query fixtures are missing from the scene");
				return;
			}
			Vector3 start = new Vector3(56.0f, 1.0f, -70.0f);
			uint defaultLayer = Physics.GetLayerMask("Default"), propsLayer = Physics.GetLayerMask("Props");
			Expect(defaultLayer == 1u && propsLayer == 2u, $"layer masks were {defaultLayer} and {propsLayer}, expected 1 and 2");

			// Raycast: the wall, skipping the trigger.
			bool hit = Physics.Raycast(start, Vector3.Right, out RaycastHit ray);
			Expect(hit && ray.Entity.ID == wall.ID && Near(ray.Distance, 3.5f) && Near(ray.Point.X, 59.5f) && Near(ray.Normal.X, -1.0f),
				$"Raycast hit {(hit ? ray.Entity.ID.ToString() : "nothing")} at {ray.Distance:F3}, normal {ray.Normal}; expected the wall at 3.5 facing -X");
			Expect(Physics.Raycast(start, new Vector3(10.0f, 0.0f, 0.0f), 100.0f), "a direction need not be a unit vector");
			Expect(!Physics.Raycast(start, Vector3.Right, 2.0f), "a ray shorter than the wall should miss");
			Expect(!Physics.Raycast(start, Vector3.Left), "a ray away from everything should miss");
			Expect(!Physics.Raycast(start, Vector3.Zero), "a zero direction hits nothing");

			// Triggers.
			Physics.Raycast(start, Vector3.Right, out RaycastHit viaTrigger, 1000.0f, Physics.AllLayers, null, true);
			Expect(viaTrigger.Entity != null && viaTrigger.Entity.ID == trigger.ID && Near(viaTrigger.Distance, 1.75f), $"with includeTriggers the ray should stop at the trigger (1.75), got {viaTrigger.Distance:F3}");

			// Layer masks and ignore.
			Physics.Raycast(start, Vector3.Right, out RaycastHit onlyProps, 1000.0f, propsLayer);
			Expect(onlyProps.Entity != null && onlyProps.Entity.ID == prop.ID && Near(onlyProps.Distance, 7.5f), "a Props-only mask should skip the wall and hit the prop (7.5)");
			Physics.Raycast(start, Vector3.Right, out RaycastHit onlyDefault, 1000.0f, defaultLayer);
			Expect(onlyDefault.Entity != null && onlyDefault.Entity.ID == wall.ID, "a Default-only mask should hit the wall");
			Physics.Raycast(start, Vector3.Right, out RaycastHit ignoring, 1000.0f, Physics.AllLayers, wall);
			Expect(ignoring.Entity != null && ignoring.Entity.ID == prop.ID, "ignoring the wall should hit the prop behind it");
			Expect(!Physics.Raycast(start, Vector3.Right, 1000.0f, 0u), "an empty mask hits nothing");

			// RaycastAll.
			RaycastHit[] all = Physics.RaycastAll(start, Vector3.Right);
			Expect(all.Length == 2 && all[0].Entity.ID == wall.ID && all[1].Entity.ID == prop.ID && all[0].Distance < all[1].Distance,
				$"RaycastAll found {all.Length} hits, expected the wall then the prop (not the trigger, not the back faces)");
			Expect(Physics.RaycastAll(start, Vector3.Right, 1000.0f, Physics.AllLayers, null, true).Length == 3, "RaycastAll with triggers should add the trigger");
			Expect(Physics.RaycastAll(start, Vector3.Right, 1000.0f, propsLayer).Length == 1, "RaycastAll honours the mask");
			Expect(Physics.RaycastAll(start, Vector3.Left).Length == 0, "RaycastAll with nothing ahead is empty");

			// Shape casts: the distance is how far the shape centre travels.
			bool sphereHit = Physics.SphereCast(start, 0.5f, Vector3.Right, out RaycastHit sphere);
			Expect(sphereHit && sphere.Entity.ID == wall.ID && Near(sphere.Distance, 3.0f) && Near(sphere.Point.X, 59.5f), $"SphereCast distance {sphere.Distance:F3} at x {sphere.Point.X:F3}, expected 3.0 at 59.5");
			bool boxHit = Physics.BoxCast(start, new Vector3(0.25f), Quaternion.Identity, Vector3.Right, out RaycastHit box);
			Expect(boxHit && Near(box.Distance, 3.25f), $"BoxCast distance {box.Distance:F3}, expected 3.25");
			Physics.BoxCast(start, new Vector3(0.25f), Quaternion.AngleAxis(Mathf.PI * 0.25f, Vector3.Up), Vector3.Right, out RaycastHit turned);
			Expect(Near(turned.Distance, 3.5f - 0.25f * Mathf.Sqrt(2.0f)), $"a box turned 45 degrees should reach the wall at {3.5f - 0.25f * Mathf.Sqrt(2.0f):F3}, got {turned.Distance:F3}");
			bool capsuleHit = Physics.CapsuleCast(start, 0.3f, 0.5f, Quaternion.Identity, Vector3.Right, out RaycastHit capsule);
			Expect(capsuleHit && Near(capsule.Distance, 3.2f), $"CapsuleCast distance {capsule.Distance:F3}, expected 3.2");
			Expect(!Physics.SphereCast(start, 0.5f, Vector3.Right, out _, 2.0f), "a sphere that cannot reach the wall should miss");
			Physics.SphereCast(start, 0.5f, Vector3.Right, out RaycastHit sphereThroughMask, 1000.0f, propsLayer);
			Expect(sphereThroughMask.Entity != null && sphereThroughMask.Entity.ID == prop.ID, "SphereCast honours the mask");

			// Overlaps.
			Vector3 wallCentre = new Vector3(59.5f, 1.0f, -70.0f);
			Entity[] near = Physics.OverlapSphere(wallCentre, 2.0f);
			Expect(near.Length == 1 && near[0].ID == wall.ID, $"OverlapSphere(r=2) found {near.Length} entities, expected just the wall");
			Expect(Physics.OverlapSphere(wallCentre, 2.0f, Physics.AllLayers, null, true).Length == 2, "OverlapSphere with triggers should add the trigger");
			Expect(Physics.OverlapSphere(wallCentre, 2.0f, propsLayer).Length == 0, "OverlapSphere honours the mask");
			Expect(Physics.OverlapSphere(wallCentre, 2.0f, Physics.AllLayers, wall).Length == 0, "OverlapSphere honours ignore");
			Entity[] wide = Physics.OverlapSphere(wallCentre, 5.0f);
			Expect(wide.Length == 2 && wide[0].ID != wide[1].ID, $"OverlapSphere(r=5) found {wide.Length} entities, expected the wall and the prop once each");
			Entity[] boxes = Physics.OverlapBox(wallCentre, new Vector3(0.1f), Quaternion.Identity);
			Expect(boxes.Length == 1 && boxes[0].ID == wall.ID, "OverlapBox");
			Entity[] capsules = Physics.OverlapCapsule(wallCentre, 0.2f, 0.3f, Quaternion.Identity);
			Expect(capsules.Length == 1 && capsules[0].ID == wall.ID, "OverlapCapsule");
			Expect(Physics.CheckSphere(wallCentre, 0.2f) && !Physics.CheckSphere(new Vector3(0.0f, 50.0f, -200.0f), 1.0f), "CheckSphere");

			Report.Result("Queries", problem == null, problem ?? "");
		}
	}

	// SC4: the light, camera, music director and static mesh components. The rig entity carries every light kind; the
	// two probes share one mesh with two material slots.
	public class ComponentCheck : Entity
	{
		int m_Frame;

		static bool Near(float a, float b, float tolerance = 1e-4f) => Math.Abs(a - b) <= tolerance;

		void OnUpdate(float ts)
		{
			if (++m_Frame != 3)
				return;
			string problem = null;
			void Expect(bool ok, string what) { if (!ok && problem == null) problem = what; }

			// Lights.
			var point = GetComponent<PointLightComponent>();
			var spot = GetComponent<SpotLightComponent>();
			var area = GetComponent<AreaLightComponent>();
			var sun = GetComponent<DirectionalLightComponent>();
			var sky = GetComponent<SkyLightComponent>();
			Expect(point != null && spot != null && area != null && sun != null && sky != null, "a light component is missing from the rig");
			if (point != null && spot != null && area != null && sun != null && sky != null)
			{
				Expect(Near(point.Intensity, 2.0f) && Near(point.Range, 6.0f) && !point.CastShadows, $"point light read {point.Intensity}, {point.Range}, {point.CastShadows}; expected 2, 6, false");
				point.Intensity = 5.0f; point.Range = 9.0f; point.CastShadows = true; point.Color = new Color(0.25f, 0.5f, 0.75f);
				point = GetComponent<PointLightComponent>();
				Expect(Near(point.Intensity, 5.0f) && Near(point.Range, 9.0f) && point.CastShadows, "point light changes were not stored");
				Expect(point.Color == new Color(0.25f, 0.5f, 0.75f, 1.0f), $"point light color read back as {point.Color}");
				point.Intensity = -3.0f;
				Expect(point.Intensity == 0.0f, "a negative intensity should become 0");
				point.Enabled = false;
				Expect(!GetComponent<PointLightComponent>().Enabled, "Component.Enabled should switch a light off");
				point.Enabled = true;

				Expect(Near(spot.Angle, 40.0f) && Near(spot.Range, 8.0f), $"spot light read {spot.Angle}, {spot.Range}; expected 40, 8");
				spot.Angle = 25.0f;
				Expect(Near(GetComponent<SpotLightComponent>().Angle, 25.0f), "spot angle was not stored");
				Expect(Near(area.Width, 2.0f) && Near(area.Height, 1.0f) && Near(area.Range, 5.0f), "area light did not read its authored size");
				area.Width = 3.0f; area.TwoSided = true;
				Expect(Near(GetComponent<AreaLightComponent>().Width, 3.0f) && GetComponent<AreaLightComponent>().TwoSided, "area light changes were not stored");
				Expect(Near(sun.ShadowAmount, 1.0f), "the sun should start with full shadows");
				sun.ShadowAmount = 0.5f;
				Expect(Near(GetComponent<DirectionalLightComponent>().ShadowAmount, 0.5f), "sun shadow amount was not stored");
				Expect(Near(sky.Intensity, 1.0f) && Near(sky.Rotation, 10.0f), "sky light did not read its authored values");
				sky.Rotation = 45.0f;
				Expect(Near(GetComponent<SkyLightComponent>().Rotation, 45.0f), "sky rotation was not stored");
			}

			// Camera.
			var camera = GetComponent<CameraComponent>();
			if (camera == null)
				Expect(false, "the rig has no camera");
			else
			{
				Expect(Near(camera.FieldOfView, 50.0f) && Near(camera.NearClip, 0.1f) && Near(camera.FarClip, 500.0f) && !camera.Orthographic, $"camera read {camera.FieldOfView}, {camera.NearClip}, {camera.FarClip}");
				camera.FieldOfView = 70.0f; camera.FarClip = 800.0f;
				Expect(Near(GetComponent<CameraComponent>().FieldOfView, 70.0f) && Near(GetComponent<CameraComponent>().FarClip, 800.0f), "camera changes were not stored");
				bool threw = false;
				try { camera.FieldOfView = 0.0f; } catch (ArgumentOutOfRangeException) { threw = true; }
				Expect(threw && Near(camera.FieldOfView, 70.0f), "a field of view of 0 should be rejected");
				camera.Orthographic = true; camera.OrthographicSize = 6.0f;
				Expect(GetComponent<CameraComponent>().Orthographic && Near(GetComponent<CameraComponent>().OrthographicSize, 6.0f), "orthographic settings were not stored");
				camera.Orthographic = false;
			}

			// Music director.
			var music = GetComponent<MusicDirectorComponent>();
			if (music == null)
				Expect(false, "the rig has no music director");
			else
			{
				Expect(!music.PlayOnAwake && Near(music.Intensity, 0.25f) && music.InitialState == "Calm", $"music director read {music.PlayOnAwake}, {music.Intensity}, \"{music.InitialState}\"");
				music.InitialState = "Tense"; music.Intensity = 0.75f; music.PlayOnAwake = true;
				var again = GetComponent<MusicDirectorComponent>();
				Expect(again.InitialState == "Tense" && Near(again.Intensity, 0.75f) && again.PlayOnAwake, "music director changes were not stored");
			}

			// Static mesh and materials.
			Entity probeA = Entity.Find("MeshProbeA"), probeB = Entity.Find("MeshProbeB");
			var meshA = probeA?.GetComponent<StaticMeshComponent>();
			var meshB = probeB?.GetComponent<StaticMeshComponent>();
			if (meshA == null || meshB == null)
				Expect(false, "the mesh probes are missing");
			else
			{
				// The importer decides how many slots the mesh has (it may add a default material); find the red and blue ones by color.
				int slots = meshA.MaterialCount, redSlot = -1, blueSlot = -1;
				for (int i = 0; i < slots; i++)
				{
					Color c = meshA.GetMaterialColor(i);
					if (c.R > 0.9f && c.G < 0.1f && c.B < 0.1f) redSlot = i;
					if (c.B > 0.9f && c.R < 0.1f && c.G < 0.1f) blueSlot = i;
				}
				Expect(meshA.StaticMesh != null && slots >= 2 && redSlot >= 0 && blueSlot >= 0 && redSlot != blueSlot,
					$"the probe mesh has {slots} material slots with red at {redSlot} and blue at {blueSlot}; expected both");
				Expect(meshA.GetMaterial(slots) == null, "a slot the mesh does not have has no material");
				if (redSlot >= 0 && blueSlot >= 0)
				{
					Material redA = meshA.GetMaterial(redSlot), blueA = meshA.GetMaterial(blueSlot), redB = meshB.GetMaterial(redSlot);
					Expect(redA != null && blueA != null && redA.Handle != blueA.Handle && redB != null && redA.Handle == redB.Handle, "both probes should start on the mesh's own materials");
					meshA.SetMaterialColor(redSlot, new Color(0.0f, 1.0f, 0.0f));
					meshA.SetMaterialValue(redSlot, MaterialValue.Roughness, 0.25f);
					Expect(meshA.GetMaterialColor(redSlot).G > 0.95f && Near(meshA.GetMaterialValue(redSlot, MaterialValue.Roughness), 0.25f), "the slot did not take its new color and roughness");
					Expect(meshA.GetMaterial(redSlot).Handle != redA.Handle, "the first change should give the entity its own copy of the material");
					Expect(meshA.GetMaterial(blueSlot).Handle == blueA.Handle, "changing one slot must leave the others on their own materials");
					for (int i = 0; i < slots && i != redSlot; i++)
						Expect(meshA.GetMaterial(i).Handle == meshB.GetMaterial(i).Handle, $"slot {i} of the probes should still be the same material");
					Expect(Near(meshB.GetMaterialColor(redSlot).R, 1.0f, 0.02f) && meshB.GetMaterial(redSlot).Handle == redB.Handle, "the other entity sharing the material must keep it");
					meshA.SetMaterial(redSlot, redA);
					Expect(meshA.GetMaterial(redSlot).Handle == redA.Handle, "SetMaterial(slot, material) should assign the asset");
					bool threw = false;
					try { meshA.SetMaterial(slots + 3, redA); } catch (ArgumentException) { threw = true; }
					Expect(threw, "assigning a material to a missing slot should throw");
					meshA.SetMaterial(redSlot, null);
					Expect(meshA.GetMaterial(redSlot).Handle == redA.Handle, "SetMaterial(slot, null) should go back to the mesh's own material");
				}

				StaticMesh original = meshA.StaticMesh;
				meshA.StaticMesh = null;
				Expect(meshA.StaticMesh == null && meshA.MaterialCount == 0, "StaticMesh = null should clear the mesh");
				meshA.StaticMesh = original;
				Expect(meshA.StaticMesh != null && meshA.StaticMesh.Handle == original.Handle && meshA.MaterialCount == slots, "restoring the mesh should bring its slots back");
				meshA.Enabled = false;
				Expect(!probeA.GetComponent<StaticMeshComponent>().Enabled, "Enabled = false should hide the mesh (Visible)");
				meshA.Enabled = true;
			}

			Report.Result("Components", problem == null, problem ?? "");
		}
	}

	// S2: counts how many of the scene's scripts were told to end, so the check can see the old scene really ended.
	public class SceneWitness : Entity
	{
		public static int Destroys;

		void OnDestroy() => Destroys++;
	}

	// S2: created by the second scene, to show its scripts start fresh after Scene.Load.
	public class SecondSceneProbe : Entity
	{
		public static int Creates;

		void OnCreate() => Creates++;
	}

	// S2: the additive scene's scripts, counted when they start and end.
	public class AddedProbe : Entity
	{
		public static int Creates, Destroys;

		void OnCreate() => Creates++;
		void OnDestroy() => Destroys++;
	}

	// S2: replaces the running scene, keeps a persistent entity with its script, and adds and removes an additive scene.
	// Runs last, once every other check has reported, because Scene.Load ends everything else in the main scene.
	public class SceneLoadCheck : Entity
	{
		public Scene Second;
		public Scene Extra;

		const int OtherChecks = 31;
		int m_Phase, m_Wait, m_Creates;
		int m_Counter;                 // survives the load only if this very script instance does
		double m_ElapsedBefore;
		ulong m_KidID;
		string m_Problem;

		void Fail(string reason) { if (m_Problem == null) m_Problem = reason; }

		void OnCreate() => m_Creates++;

		void OnUpdate(float ts)
		{
			m_Counter++;
			switch (m_Phase)
			{
				case 0:   // wait for every other check, then ask for the second scene
				{
					if (Report.Count < OtherChecks)
						return;
					Persistent = true;
					Entity kid = Create("PersistentKid");
					kid.Parent = this;
					m_KidID = kid.ID;
					Translation = new Vector3(12.0f, 34.0f, 56.0f);
					Time.TimeScale = 0.5f;
					m_ElapsedBefore = Time.ElapsedTime;
					if (Second == null || Second.Handle == 0 || Scene.Current.Handle == Second.Handle)
					{
						Report.Result("SceneLoading", false, "the Second scene field is not set to another scene");
						m_Phase = 99;
						return;
					}
					Second.Load();
					Second.Load();   // the last request of a frame wins, and a repeat changes nothing
					m_Phase = 1;
					m_Counter = 100;
					return;
				}
				case 1:   // the second scene has taken over
				{
					// The swap happens at the start of the next frame; allow a few frames before calling it a failure.
					if (Scene.Current.Handle != Second.Handle && ++m_Wait < 5)
						return;
					if (Scene.Current.Handle != Second.Handle)
						Fail($"the running scene is still {Scene.Current.Handle}, expected {Second.Handle}");
					if (m_Creates != 1)
						Fail($"OnCreate ran {m_Creates} times; a persistent script must not start again");
					if (m_Counter < 101)
						Fail($"the script instance did not survive the load (counter {m_Counter}, expected above 100)");
					if (SceneWitness.Destroys == 0)
						Fail("scripts of the old scene did not get OnDestroy");
					if (ApplicationCheck.QuittingCalls != 0)
						Fail("Application.Quitting must not fire when a script loads another scene");
					if (Entity.Find("QueryWall", true) != null)
						Fail("an entity of the old scene is still there");
					Entity marker = Entity.Find("SecondMarker");
					if (marker == null || SecondSceneProbe.Creates != 1)
						Fail($"the second scene's marker was not created (probe starts: {SecondSceneProbe.Creates})");
					if (Vector3.Distance(WorldTranslation, new Vector3(12.0f, 34.0f, 56.0f)) > 1e-3f)
						Fail($"the persistent entity moved to {WorldTranslation}");
					Entity kid = Entity.Find("PersistentKid", true);
					if (kid == null || kid.ID != m_KidID || kid.Parent == null || kid.Parent.ID != ID)
						Fail("the persistent entity's child did not come along under its parent");
					if (!Persistent)
						Fail("the entity should still be persistent in the new scene");
					if (Time.TimeScale != 0.5f || Time.ElapsedTime < m_ElapsedBefore)
						Fail($"the game clock did not carry over (scale {Time.TimeScale}, elapsed {Time.ElapsedTime} against {m_ElapsedBefore})");
					Time.TimeScale = 1.0f;
					if (Extra == null || Extra.Handle == 0)
					{
						Report.Result("SceneLoading", false, m_Problem ?? "the Extra scene field is not set");
						m_Phase = 99;
						return;
					}
					Extra.LoadAdditive();
					m_Phase = 2;
					m_Wait = 0;
					return;
				}
				case 2:   // the additive scene is in
				{
					if (AddedProbe.Creates == 0 && m_Wait++ < 5)
						return;
					Entity parent = Entity.Find("AddedParent");
					Entity child = Entity.Find("AddedChild");
					if (parent == null || child == null || child.Parent == null || child.Parent.ID != parent.ID)
						Fail("the additive scene's hierarchy is not in the running scene");
					if (AddedProbe.Creates != 1)
						Fail($"the additive scene's script started {AddedProbe.Creates} times, expected 1");
					if (Entity.Find("SecondMarker") == null)
						Fail("loading a scene additively removed the running scene's entities");
					Extra.LoadAdditive();   // a second copy of the same scene
					m_Phase = 3;
					m_Wait = 0;
					return;
				}
				case 3:   // two copies: the second one got new IDs
				{
					if (AddedProbe.Creates < 2 && m_Wait++ < 5)
						return;
					Entity[] parents = Entity.FindAll("AddedParent");
					if (parents.Length != 2 || parents[0].ID == parents[1].ID)
						Fail($"loading the same scene twice should give two separate copies, found {parents.Length}");
					foreach (Entity added in parents)
					{
						Entity[] kids = added.Children;
						if (kids.Length != 1 || kids[0].Parent == null || kids[0].Parent.ID != added.ID)
							Fail("a copy of the additive scene lost its hierarchy");
					}
					Extra.Unload();
					m_Phase = 4;
					m_Wait = 0;
					return;
				}
				case 4:   // everything the additive scene added is gone
				{
					if (AddedProbe.Destroys < 2 && m_Wait++ < 5)
						return;
					if (Entity.Find("AddedParent", true) != null || Entity.Find("AddedChild", true) != null)
						Fail("Unload left entities of the additive scene behind");
					if (AddedProbe.Destroys != 2)
						Fail($"the additive scripts were destroyed {AddedProbe.Destroys} times, expected 2");
					if (Entity.Find("SecondMarker") == null)
						Fail("Unload removed more than the additive scene");
					Report.Result("SceneLoading", m_Problem == null, m_Problem ?? "");
					m_Phase = 99;
					return;
				}
			}
		}
	}

	// SC5: timers and coroutines driven by the engine's frame, with an owner that is destroyed.
	public class TimerCheck : Entity
	{
		bool m_Started, m_Reported;
		double m_Start, m_RealStart, m_RealWaited;
		int m_OneShot, m_Repeats, m_VictimTicks, m_VictimAtDeath;
		readonly System.Collections.Generic.List<string> m_Log = new System.Collections.Generic.List<string>();
		Entity m_Victim;

		System.Collections.IEnumerator Routine()
		{
			m_Log.Add("start");
			yield return null;
			m_Log.Add("frame");
			yield return new WaitForSeconds(0.3f);
			m_Log.Add("seconds");
			yield return new WaitForFixedUpdate();
			m_Log.Add("fixed");
			double before = Time.UnscaledTime;
			yield return new WaitForSecondsRealtime(0.25f);
			m_RealWaited = Time.UnscaledTime - before;
			m_Log.Add("realtime");
			yield return new WaitUntil(() => m_OneShot == 1);
			m_Log.Add("until");
		}

		void OnUpdate(float ts)
		{
			if (!m_Started)
			{
				m_Started = true;
				m_Start = Time.ElapsedTime;
				m_RealStart = Time.UnscaledTime;
				After(0.2f, () => m_OneShot++);
				Every(0.1f, () => m_Repeats++);
				StartCoroutine(Routine());
				m_Victim = Create("TimerVictim");
				m_Victim.Every(0.05f, () => m_VictimTicks++);
				After(0.3f, () => { m_VictimAtDeath = m_VictimTicks; m_Victim.Destroy(); });
				return;
			}
			if (m_Reported || Time.ElapsedTime - m_Start < 1.5)
				return;
			m_Reported = true;

			string problem = null;
			void Expect(bool ok, string what) { if (!ok && problem == null) problem = what; }
			Expect(m_OneShot == 1, $"the one-shot timer ran {m_OneShot} times, expected 1");
			Expect(m_Repeats >= 11 && m_Repeats <= 15, $"a 0.1 s repeating timer ran {m_Repeats} times in 1.5 s, expected about 14");
			Expect(string.Join(",", m_Log) == "start,frame,seconds,fixed,realtime,until", $"the coroutine ran: {string.Join(",", m_Log)}");
			Expect(m_RealWaited >= 0.249, $"WaitForSecondsRealtime(0.25) waited {m_RealWaited:F3} s");
			Expect(m_VictimAtDeath >= 4 && m_VictimTicks == m_VictimAtDeath, $"a timer of a destroyed entity kept running: {m_VictimTicks} ticks, {m_VictimAtDeath} when it died");
			Expect(Entity.Find("TimerVictim", true) == null, "the victim entity should be gone");
			Report.Result("Timers", problem == null, problem ?? "");
		}
	}

	// SC6: Application.Paused, PauseChanged, Resized, FocusChanged and Quitting. The handlers are static, so they live on after the
	// scene that registered them is replaced; Quitting must fire once, when the game stops, and not when a scene is loaded.
	public class ApplicationCheck : Entity
	{
		public static int QuittingCalls;
		static readonly System.Collections.Generic.List<bool> s_PauseEvents = new System.Collections.Generic.List<bool>();
		static int s_Resizes, s_FocusEvents;
		bool m_Started, m_Reported;
		double m_RealStart;

		static void OnQuitting()
		{
			QuittingCalls++;
			Report.Result("Quitting", QuittingCalls == 1, $"Application.Quitting was raised {QuittingCalls} times");
		}

		void OnCreate()
		{
			Application.Quitting += OnQuitting;
			Application.PauseChanged += paused => s_PauseEvents.Add(paused);
			Application.Resized += (width, height) => s_Resizes++;
			Application.FocusChanged += focused => s_FocusEvents++;
		}

		void OnUpdate(float ts)
		{
			if (!m_Started)
			{
				m_Started = true;
				m_RealStart = Time.UnscaledTime;
				// A paused game gets no OnUpdate: real-time timers are how it unpauses itself.
				Timer.After(0.2f, () => Application.Paused = false, true);
				Application.Paused = true;
				return;
			}
			if (m_Reported || Time.UnscaledTime - m_RealStart < 0.6)
				return;
			m_Reported = true;

			string problem = null;
			void Expect(bool ok, string what) { if (!ok && problem == null) problem = what; }
			Expect(!Application.Paused, "the game should be unpaused again");
			Expect(s_PauseEvents.Count == 2 && s_PauseEvents[0] && !s_PauseEvents[1], $"PauseChanged gave {string.Join(",", s_PauseEvents)}, expected True,False");
			Expect(Application.Width > 0 && Application.Height > 0, $"the view is {Application.Width}x{Application.Height}");
			Expect(QuittingCalls == 0, "Quitting must not fire while the game runs");
			Report.Result("Application", problem == null, problem ?? "");
		}
	}
}
