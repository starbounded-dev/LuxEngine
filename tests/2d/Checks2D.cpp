// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

// Headless checks for 2D as a full project type (M11). Real engine code - project and scene
// serializers, the editor camera, Box2D - with no window and no GPU device.
//
//   Checks2D <outdir>
//
// Built and run by tests/2d/run_2d_checks.py, which derives the project from Editor.vcxproj.

#include "Lux/Asset/AssetImporter.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Asset/AssetReference.h"
#include "Lux/Asset/SpriteSheetSerializer.h"
#include "Lux/Core/Base.h"
#include "Lux/Core/Log.h"
#include "Lux/Editor/EditorCamera.h"
#include "Lux/Physics/ContactPairTracker.h"
#include "Lux/Physics2D/PhysicsScene2D.h"
#include "Lux/Project/Project.h"
#include "Lux/Project/ProjectSerializer.h"
#include "Lux/Renderer/FrameRenderPacket.h"
#include "Lux/Renderer/Renderer2D.h"
#include "Lux/Renderer/SpriteAtlas.h"
#include "Lux/Renderer/SpriteSheet.h"
#include "Lux/Serialization/FileStream.h"
#include "Lux/Scene/Components.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Scene/SceneSerializer.h"
#include "Lux/Scene/SceneTemplates.h"

#include <GLFW/deps/stb_image_write.h>
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>

bool g_ApplicationRunning = true;

namespace Lux {

	// Every Scene builds a Renderer2D, which needs a GPU device; this tool has none. Linked with
	// /FORCE:MULTIPLE so these replace the engine's definitions.
	Renderer2D::Renderer2D(const Renderer2DSpecification& specification)
		: m_Specification(specification), c_MaxVertices(0), c_MaxIndices(0), c_MaxLineVertices(0), c_MaxLineIndices(0)
	{
	}

	Renderer2D::~Renderer2D()
	{
	}

}

using namespace Lux;

namespace {

	int s_Failures = 0;

	void Check(bool condition, const std::string& what)
	{
		std::printf("%s %s\n", condition ? "[ OK ]" : "[FAIL]", what.c_str());
		if (!condition)
			s_Failures++;
	}

	bool Near(float a, float b, float tolerance = 1e-3f)
	{
		return std::abs(a - b) <= tolerance;
	}

	std::string ReadText(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
	}

	void WriteText(const std::filesystem::path& path, const std::string& text)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream(path, std::ios::binary) << text;
	}

	// ---- D5: project type, camera background, 2D view, template -------------------------------

	void CheckProjectType(const std::filesystem::path& root)
	{
		const std::filesystem::path projectFile = root / "Game2D" / "Game2D.luxproj";
		WriteText(projectFile, "Project:\n  Name: Game2D\n  ProjectType: 2D\n  AssetDirectory: Assets\n");
		std::filesystem::create_directories(root / "Game2D" / "Assets");

		Ref<Project> project = Project::Load(projectFile);
		Check(project != nullptr, "2D project loads");
		if (!project)
			return;
		Check(project->GetConfig().Type == ProjectType::Project2D, "ProjectType: 2D is read");

		const std::filesystem::path resaved = root / "Game2D" / "Resaved.luxproj";
		Check(ProjectSerializer(project).Serialize(resaved), "2D project serializes");
		Check(ReadText(resaved).find("ProjectType: 2D") != std::string::npos, "a 2D project writes ProjectType: 2D");

		project->GetConfig().Type = ProjectType::Project3D;
		const std::filesystem::path resaved3D = root / "Game2D" / "Resaved3D.luxproj";
		ProjectSerializer(project).Serialize(resaved3D);
		Check(ReadText(resaved3D).find("ProjectType") == std::string::npos, "a 3D project writes no ProjectType (old files re-save unchanged)");

		WriteText(root / "Game3D" / "Game3D.luxproj", "Project:\n  Name: Game3D\n  AssetDirectory: Assets\n");
		std::filesystem::create_directories(root / "Game3D" / "Assets");
		Ref<Project> project3D = Project::Load(root / "Game3D" / "Game3D.luxproj");
		Check(project3D && project3D->GetConfig().Type == ProjectType::Project3D, "a project file without ProjectType is 3D");
	}

	void CheckCameraBackground(const std::filesystem::path& root)
	{
		Ref<Scene> scene = Ref<Scene>::Create();
		Entity camera = scene->CreateEntity("Camera");
		auto& component = camera.AddComponent<CameraComponent>();
		component.Camera.SetOrthographic(8.0f, -50.0f, 50.0f);
		component.SolidBackground = true;
		component.BackgroundColor = { 0.1f, 0.2f, 0.3f, 1.0f };
		Entity plain = scene->CreateEntity("Plain");
		plain.AddComponent<CameraComponent>().Primary = false;

		const std::filesystem::path file = root / "Background.luxscene";
		Check(SceneSerializer(scene).Serialize(file), "scene with a solid-background camera serializes");
		const std::string text = ReadText(file);
		Check(text.find("SolidBackground") != std::string::npos, "SolidBackground is written when on");
		size_t occurrences = 0;
		for (size_t at = text.find("SolidBackground"); at != std::string::npos; at = text.find("SolidBackground", at + 1))
			occurrences++;
		Check(occurrences == 1, "SolidBackground is written only for the camera that has it");

		Ref<Scene> loaded = Ref<Scene>::Create();
		Check(SceneSerializer(loaded).Deserialize(file), "scene deserializes");
		bool found = false;
		for (auto entity : loaded->GetAllEntitiesWith<CameraComponent>())
		{
			const auto& loadedCamera = loaded->GetAllEntitiesWith<CameraComponent>().get<CameraComponent>(entity);
			if (loadedCamera.SolidBackground)
			{
				found = true;
				Check(Near(loadedCamera.BackgroundColor.g, 0.2f) && Near(loadedCamera.Camera.GetOrthographicSize(), 8.0f), "background colour and ortho size round trip");
			}
		}
		Check(found, "the solid background survives a round trip");

		Ref<Scene> copy = Scene::Copy(loaded);
		bool copied = false;
		for (auto entity : copy->GetAllEntitiesWith<CameraComponent>())
			copied |= copy->GetAllEntitiesWith<CameraComponent>().get<CameraComponent>(entity).SolidBackground;
		Check(copied, "Scene::Copy keeps the solid background");
	}

	void CheckBackgroundDrawItem()
	{
		Ref<Scene> scene = Ref<Scene>::Create();
		Entity camera = scene->CreateEntity("Camera");
		auto& component = camera.AddComponent<CameraComponent>();
		component.Camera.SetOrthographic(10.0f, -100.0f, 100.0f);
		component.Camera.SetViewportSize(1600, 900);
		component.SolidBackground = true;
		component.BackgroundColor = { 0.25f, 0.5f, 0.75f, 1.0f };

		FrameRenderPacket packet;
		packet.Overlay2DViewProjection = component.Camera.GetProjectionMatrix() * glm::inverse(glm::mat4(1.0f));
		scene->CaptureDraw2D(packet);
		Check(packet.Draw2D.size() == 1, "an orthographic solid-background camera adds one 2D item");
		if (packet.Draw2D.empty())
			return;

		const FrameRenderPacket::Draw2DItem& item = packet.Draw2D.front();
		const float halfWidth = 10.0f * 1600.0f / 900.0f * 0.5f;
		const glm::vec3 corner = glm::vec3(item.Transform * glm::vec4(0.5f, 0.5f, 0.0f, 1.0f));
		Check(Near(corner.x, halfWidth) && Near(corner.y, 5.0f), std::format("the background quad covers the view rectangle (corner {:.3f}, {:.3f})", corner.x, corner.y));
		Check(Near(item.Color.b, 0.75f), "the background quad has the camera's colour");

		// A perspective view has no rectangle to fill.
		FrameRenderPacket perspective;
		perspective.Overlay2DViewProjection = glm::perspective(1.0f, 1.7f, 0.1f, 100.0f);
		scene->CaptureDraw2D(perspective);
		Check(perspective.Draw2D.empty(), "a perspective view draws no background quad");
	}

	void CheckEditorCamera2D()
	{
		EditorCamera camera(60.0f, 1600.0f, 900.0f, 0.1f, 10000.0f);
		camera.SetViewportBounds(0, 0, 1600, 900);
		camera.Set2DMode(true);
		camera.SetDistance(10.0f); // visible height in world units
		Check(camera.Is2DMode(), "the editor camera enters 2D mode");

		const auto toNDC = [&camera](const glm::vec3& world)
		{
			const glm::vec4 clip = camera.GetUnReversedViewProjection() * glm::vec4(world, 1.0f);
			return glm::vec3(clip) / clip.w;
		};
		glm::vec3 origin = toNDC({ 0.0f, 0.0f, 0.0f });
		Check(Near(origin.x, 0.0f) && Near(origin.y, 0.0f) && origin.z > 0.0f && origin.z < 1.0f, "the XY origin is the centre of the view, inside the depth range");
		const float halfWidth = 10.0f * 1600.0f / 900.0f * 0.5f;
		glm::vec3 corner = toNDC({ halfWidth, 5.0f, 0.0f });
		Check(Near(corner.x, 1.0f) && Near(corner.y, 1.0f), "the visible height is the distance and the aspect follows the panel");
		Check(Near(toNDC({ 0.0f, 0.0f, 80.0f }).x, 0.0f) && Near(toNDC({ 0.0f, 0.0f, -80.0f }).y, 0.0f), "depth does not shift the picture: it is orthographic");

		camera.Focus({ 3.0f, 4.0f, 9.0f });
		glm::vec3 focused = toNDC({ 3.0f, 4.0f, 0.0f });
		Check(Near(focused.x, 0.0f) && Near(focused.y, 0.0f), "Focus centres the entity's XY and ignores its Z");

		camera.SetViewportBounds(0, 0, 800, 800);
		corner = toNDC({ 3.0f + 5.0f, 4.0f + 5.0f, 0.0f });
		Check(Near(corner.x, 1.0f) && Near(corner.y, 1.0f), "a resize keeps the 2D projection");

		camera.Set2DMode(false);
		const glm::mat4 projection = camera.GetUnReversedProjectionMatrix();
		Check(!camera.Is2DMode() && std::abs(projection[2][3] + 1.0f) < 1e-4f, "leaving 2D mode restores the perspective projection");
	}

	void CheckStarterScene(const std::filesystem::path& root)
	{
		Ref<Scene> scene = Ref<Scene>::Create();
		SceneTemplates::Populate2DStarter(*scene);

		size_t cameras = 0, bodies = 0, sprites = 0;
		for ([[maybe_unused]] auto entity : scene->GetAllEntitiesWith<CameraComponent>())
			cameras++;
		for ([[maybe_unused]] auto entity : scene->GetAllEntitiesWith<RigidBody2DComponent>())
			bodies++;
		for ([[maybe_unused]] auto entity : scene->GetAllEntitiesWith<SpriteRendererComponent>())
			sprites++;
		Check(cameras == 1 && bodies == 2 && sprites == 2, std::format("the 2D template has a camera, a ground and a box ({} camera, {} bodies, {} sprites)", cameras, bodies, sprites));
		Check(scene->GetPrimaryCameraEntity() && scene->GetPrimaryCameraEntity().GetComponent<CameraComponent>().Camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic,
			"the template's primary camera is orthographic");

		const std::filesystem::path file = root / "Starter2D.luxscene";
		Check(SceneSerializer(scene).Serialize(file), "the starter scene serializes");
		Ref<Scene> loaded = Ref<Scene>::Create();
		Check(SceneSerializer(loaded).Deserialize(file), "the starter scene loads back");

		// The box falls onto the ground and rests on it: ground top y = -3.0, box half height 0.5.
		Ref<Scene> play = Scene::Copy(loaded);
		PhysicsScene2D physics(play.Raw());
		physics.Start();
		for (int step = 0; step < 240; step++)
			physics.SimulateStep(1.0f / 60.0f);
		float boxY = 99.0f;
		for (auto entity : play->GetAllEntitiesWith<TagComponent, TransformComponent>())
		{
			const auto [tag, transform] = play->GetAllEntitiesWith<TagComponent, TransformComponent>().get<TagComponent, TransformComponent>(entity);
			if (tag.Tag == "Box")
				boxY = transform.Translation.y;
		}
		physics.Stop();
		Check(Near(boxY, -2.5f, 0.05f), std::format("the template's box falls and rests on the ground (y = {:.3f}, expected -2.5)", boxY));
	}


	// ---- D4: sorting layers, draw order, pixel-perfect camera ---------------------------------

	void CheckSortingLayers(const std::filesystem::path& root)
	{
		WriteText(root / "Sorted" / "Sorted.luxproj", "Project:\n  Name: Sorted\n  AssetDirectory: Assets\n");
		std::filesystem::create_directories(root / "Sorted" / "Assets");
		Ref<Project> project = Project::Load(root / "Sorted" / "Sorted.luxproj");
		if (!project)
		{
			Check(false, "sorting project loads");
			return;
		}

		ProjectConfig& config = project->GetConfig();
		Check(config.SortingLayers.size() == 1 && config.SortingLayers[0].ID == 0 && config.SortingLayers[0].Name == "Default", "a project starts with the Default sorting layer");

		const std::filesystem::path plain = root / "Sorted" / "Plain.luxproj";
		ProjectSerializer(project).Serialize(plain);
		Check(ReadText(plain).find("SortingLayers") == std::string::npos, "the default layer list is not written (old files re-save unchanged)");

		config.SortingLayers = { { 0, "Default" }, { 7, "Foreground" }, { 3, "UI" } };
		const std::filesystem::path layered = root / "Sorted" / "Layered.luxproj";
		Check(ProjectSerializer(project).Serialize(layered), "a project with sorting layers serializes");
		Ref<Project> reloaded = Ref<Project>::Create();
		Check(ProjectSerializer(reloaded).Deserialize(layered), "and loads back");
		Check(reloaded->GetConfig().SortingLayers == config.SortingLayers, "sorting layers keep their IDs, names and order");
		Check(reloaded->GetSortingLayerRank(7) == 1 && reloaded->GetSortingLayerRank(3) == 2 && reloaded->GetSortingLayerRank(0) == 0, "rank is the position in the list");
		Check(reloaded->GetSortingLayerRank(99) == 0, "a deleted layer draws with Default");

		// A file that lost its Default layer gets it back at the front.
		WriteText(root / "Sorted" / "NoDefault.luxproj", "Project:\n  Name: NoDefault\n  AssetDirectory: Assets\n  SortingLayers:\n    - ID: 4\n      Name: Only\n");
		Ref<Project> noDefault = Ref<Project>::Create();
		ProjectSerializer(noDefault).Deserialize(root / "Sorted" / "NoDefault.luxproj");
		Check(noDefault->GetConfig().SortingLayers.size() == 2 && noDefault->GetConfig().SortingLayers[0].ID == 0, "a missing Default layer is restored first");

		// The exported game needs the layers too.
		config.StartSceneHandle = 1; // SerializeRuntime refuses a project without a start scene; the handle is not resolved here
		const std::filesystem::path runtimeFile = root / "Sorted" / "Project.lrp";
		Check(ProjectSerializer(project).SerializeRuntime(runtimeFile, AudioBankManifest{}), "the runtime project serializes");
		Ref<Project> runtime = Ref<Project>::Create();
		Check(ProjectSerializer(runtime).DeserializeRuntime(runtimeFile), "the runtime project loads");
		Check(runtime->GetConfig().SortingLayers == config.SortingLayers, "the runtime project carries the sorting layers");
	}

	void CheckDrawOrder()
	{
		Ref<Project> project = Ref<Project>::Create();
		project->GetConfig().SortingLayers = { { 0, "Default" }, { 7, "Foreground" } };
		Project::SetActive(project);

		Ref<Scene> scene = Ref<Scene>::Create();
		const auto addSprite = [&scene](const char* name, float z, uint32_t layer, int32_t order, float red)
		{
			Entity entity = scene->CreateEntity(name);
			entity.GetComponent<TransformComponent>().Translation.z = z;
			auto& sprite = entity.AddComponent<SpriteRendererComponent>(glm::vec4(red, 0.0f, 0.0f, 1.0f));
			sprite.SortingLayer = layer;
			sprite.OrderInLayer = order;
			return entity;
		};
		// Added in the order the registry would draw them; the expected draw order is C, D, A, E, B.
		addSprite("A", 0.0f, 0, 5, 0.1f);
		addSprite("B", 0.0f, 7, 0, 0.5f);
		addSprite("C", 0.0f, 0, -3, 0.3f);
		addSprite("D", -1.0f, 0, 5, 0.2f); // same layer and order as A, farther from the camera: drawn first
		addSprite("E", 0.0f, 99, 6, 0.4f);   // deleted layer: Default, order 6

		FrameRenderPacket packet;
		scene->CaptureDraw2D(packet);
		std::string names;
		for (const auto& item : packet.Draw2D)
		{
			const int tenths = static_cast<int>(item.Color.r * 10.0f + 0.5f);
			names += tenths == 1 ? 'A' : tenths == 2 ? 'D' : tenths == 3 ? 'C' : tenths == 4 ? 'E' : 'B';
		}
		Check(names == "CDAEB", std::format("sprites draw by sorting layer, then order, then depth (got {}, expected CDAEB)", names));

		Check(scene->GetDrawOrder(scene->FindEntityByName("B")) > scene->GetDrawOrder(scene->FindEntityByName("A")), "GetDrawOrder: the Foreground layer is above Default");
		Check(scene->GetDrawOrder(scene->FindEntityByName("A")) > scene->GetDrawOrder(scene->FindEntityByName("C")), "GetDrawOrder: a higher order is above a lower one");

		// The three renderers and the new fields survive a save and a copy.
		Entity circle = scene->CreateEntity("Circle");
		auto& circleComponent = circle.AddComponent<CircleRendererComponent>();
		circleComponent.SortingLayer = 7;
		circleComponent.OrderInLayer = -2;
		Entity text = scene->CreateEntity("Text");
		auto& textComponent = text.AddComponent<TextComponent>();
		textComponent.SortingLayer = 7;
		textComponent.OrderInLayer = 4;
		scene->FindEntityByName("A").GetComponent<SpriteRendererComponent>().PointFilter = true;
		const std::filesystem::path file = std::filesystem::temp_directory_path() / "lux-2d-sorting.luxscene";
		Check(SceneSerializer(scene).Serialize(file), "a scene with sorting data serializes");
		Ref<Scene> loaded = Ref<Scene>::Create();
		Check(SceneSerializer(loaded).Deserialize(file), "and loads back");
		Ref<Scene> copy = Scene::Copy(loaded);
		const auto& a = copy->FindEntityByName("A").GetComponent<SpriteRendererComponent>();
		const auto& c = copy->FindEntityByName("Circle").GetComponent<CircleRendererComponent>();
		const auto& t = copy->FindEntityByName("Text").GetComponent<TextComponent>();
		Check(a.SortingLayer == 0 && a.OrderInLayer == 5 && a.PointFilter, "sprite layer, order and point filter round trip through save and copy");
		Check(c.SortingLayer == 7 && c.OrderInLayer == -2 && t.SortingLayer == 7 && t.OrderInLayer == 4, "circle and text sorting round trip through save and copy");
		const std::string text2 = ReadText(file);
		size_t layerKeys = 0;
		for (size_t at = text2.find("SortingLayer"); at != std::string::npos; at = text2.find("SortingLayer", at + 1))
			layerKeys++;
		Check(layerKeys == 3 + 1, std::format("SortingLayer is written only where it is not the default ({} keys, expected 4)", layerKeys));
		std::filesystem::remove(file);
		Project::SetActive(nullptr);
	}

	void CheckPixelPerfectCamera(const std::filesystem::path& root)
	{
		SceneCamera camera;
		camera.SetOrthographic(10.0f);
		camera.SetViewportSize(1920, 1080);
		Check(camera.GetPixelSnapUnit() == 0.0f && camera.GetPixelScale() == 1, "a camera is not pixel perfect by default");

		camera.SetPixelsPerUnit(16.0f);
		camera.SetReferenceHeight(180.0f);
		camera.SetPixelPerfect(true);
		Check(camera.GetPixelScale() == 6, std::format("1080 px high with a 180 px reference is a 6x scale (got {})", camera.GetPixelScale()));
		Check(Near(camera.GetPixelSnapUnit(), 1.0f / 96.0f, 1e-6f), "a screen pixel is 1 / (pixels per unit x scale) world units");
		const glm::vec4 top = camera.GetUnReversedProjectionMatrix() * glm::vec4(0.0f, 11.25f * 0.5f, -0.5f, 1.0f);
		Check(Near(top.y / top.w, 1.0f), "the visible height is viewport pixels / (pixels per unit x scale) (11.25 units), not the orthographic size");

		camera.SetViewportSize(1000, 700);
		Check(camera.GetPixelScale() == 3 && Near(camera.GetPixelSnapUnit(), 1.0f / 48.0f, 1e-6f), "a resize picks the largest whole scale that fits the reference height");
		camera.SetViewportSize(320, 100);
		Check(camera.GetPixelScale() == 1, "the scale never drops below 1");

		// Save, load, copy.
		Ref<Scene> scene = Ref<Scene>::Create();
		Entity entity = scene->CreateEntity("Camera");
		auto& component = entity.AddComponent<CameraComponent>();
		component.Camera.SetOrthographic(10.0f);
		component.Camera.SetPixelsPerUnit(32.0f);
		component.Camera.SetReferenceHeight(270.0f);
		component.Camera.SetPixelPerfect(true);
		const std::filesystem::path file = root / "PixelPerfect.luxscene";
		Check(SceneSerializer(scene).Serialize(file), "a pixel-perfect camera serializes");
		Ref<Scene> loaded = Ref<Scene>::Create();
		Check(SceneSerializer(loaded).Deserialize(file), "and loads back");
		const auto& loadedCamera = loaded->FindEntityByName("Camera").GetComponent<CameraComponent>().Camera;
		Check(loadedCamera.IsPixelPerfect() && Near(loadedCamera.GetPixelsPerUnit(), 32.0f) && Near(loadedCamera.GetReferenceHeight(), 270.0f), "the pixel-perfect settings round trip");
		Check(Scene::Copy(loaded)->FindEntityByName("Camera").GetComponent<CameraComponent>().Camera.IsPixelPerfect(), "Scene::Copy keeps the pixel-perfect camera");

		Ref<Scene> plain = Ref<Scene>::Create();
		plain->CreateEntity("Camera").AddComponent<CameraComponent>();
		const std::filesystem::path plainFile = root / "PlainCamera.luxscene";
		SceneSerializer(plain).Serialize(plainFile);
		Check(ReadText(plainFile).find("PixelPerfect") == std::string::npos, "an ordinary camera writes no pixel-perfect keys");
	}


	// ---- D2: sprite sheets, clips, atlas ---------------------------------------------------------

	void WriteSolidPNG(const std::filesystem::path& path, int width, int height, uint8_t red, uint8_t green, uint8_t blue)
	{
		std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
		for (size_t i = 0; i < pixels.size(); i += 4)
		{
			pixels[i] = red;
			pixels[i + 1] = green;
			pixels[i + 2] = blue;
			pixels[i + 3] = 255;
		}
		std::filesystem::create_directories(path.parent_path());
		stbi_write_png(path.string().c_str(), width, height, 4, pixels.data(), width * 4);
	}

	void CheckSpriteSheetMath()
	{
		SpriteSheet sheet;
		sheet.CellSize = { 16, 16 };
		sheet.SliceGrid({ 64, 32 });
		Check(sheet.Frames.size() == 8 && sheet.Frames[5].Rect == glm::ivec4(16, 16, 16, 16), "a 64 x 32 image cut in 16 px cells has 8 frames, frame 5 at (16,16)");
		glm::vec2 uv0, uv1;
		Check(sheet.GetFrameUV(5, { 64, 32 }, uv0, uv1) && Near(uv0.x, 0.25f) && Near(uv0.y, 0.0f) && Near(uv1.x, 0.5f) && Near(uv1.y, 0.5f),
			"frame UVs: x from the left, y counted from the top, v = 0 at the bottom of the image");
		Check(sheet.GetFrameUV(0, { 64, 32 }, uv0, uv1) && Near(uv0.y, 0.5f) && Near(uv1.y, 1.0f), "the top row of frames is the top of v");
		Check(!sheet.GetFrameUV(8, { 64, 32 }, uv0, uv1), "a frame past the end has no UVs");

		sheet.Frames[1].Name = "idle";
		sheet.Frames[1].Pivot = { 0.5f, 0.0f };
		sheet.Frames[1].Solid = true;
		sheet.CellSize = { 8, 8 };
		sheet.Spacing = { 2, 2 };
		sheet.Margin = { 1, 1 };
		sheet.SliceGrid({ 40, 20 });
		Check(sheet.Frames.size() == 8 && sheet.Frames[1].Rect == glm::ivec4(11, 1, 8, 8) && sheet.Frames[4].Rect == glm::ivec4(1, 11, 8, 8), "spacing and margin place the cells");
		Check(sheet.Frames[1].Name == "idle" && sheet.Frames[1].Pivot.y == 0.0f && sheet.Frames[1].Solid, "re-cutting keeps a frame's name, pivot and Solid flag");

		SpriteSheet empty;
		empty.SliceGrid({ 4, 4 });
		Check(empty.Frames.empty(), "an image smaller than a cell has no frames");

		SpriteClip clip;
		clip.Frames = { 4, 5, 6 };
		clip.FramesPerSecond = 10.0f;
		bool finished = false;
		clip.Wrap = SpriteWrapMode::Loop;
		std::string loop;
		for (const float time : { 0.05f, 0.15f, 0.25f, 0.35f, 0.45f })
			loop += std::to_string(SpriteSheet::SampleClip(clip, time, finished));
		Check(loop == "45645" && !finished, std::format("Loop wraps (got {})", loop));
		clip.Wrap = SpriteWrapMode::PingPong;
		std::string pingPong;
		for (int step = 0; step < 6; step++)
			pingPong += std::to_string(SpriteSheet::SampleClip(clip, step * 0.1f + 0.05f, finished));
		Check(pingPong == "456545", std::format("PingPong goes there and back without repeating the ends (got {})", pingPong));
		clip.Wrap = SpriteWrapMode::Once;
		Check(SpriteSheet::SampleClip(clip, 0.25f, finished) == 6 && !finished, "Once is not finished on its last frame");
		Check(SpriteSheet::SampleClip(clip, 0.35f, finished) == 6 && finished, "Once stays on the last frame and reports finished");
		Check(Near(SpriteSheet::GetClipDuration(clip), 0.3f), "a clip's duration is frames / fps");
		clip.Wrap = SpriteWrapMode::PingPong;
		Check(Near(SpriteSheet::GetClipDuration(clip), 0.4f), "a ping-pong clip's duration counts the way back");
	}

	void CheckSpriteSheetFiles(const std::filesystem::path& root)
	{
		WriteText(root / "Sheets" / "Sheets.luxproj", "Project:\n  Name: Sheets\n  AssetDirectory: Assets\n");
		std::filesystem::create_directories(root / "Sheets" / "Assets");
		Ref<Project> project = Project::Load(root / "Sheets" / "Sheets.luxproj");
		if (!project)
		{
			Check(false, "sheet project loads");
			return;
		}
		WriteSolidPNG(root / "Sheets" / "Assets" / "Player.png", 32, 32, 200, 40, 40);
		const AssetHandle texture = AssetManager::ImportAsset("Player.png");
		Check(texture != 0 && AssetManager::GetAssetType(texture) == AssetType::Texture, "a PNG imports as a texture");

		SpriteSheet sheet;
		sheet.Texture = texture;
		sheet.PixelsPerUnit = 16.0f;
		sheet.CellSize = { 16, 32 };
		sheet.SliceGrid({ 32, 32 });
		sheet.Frames.clear();
		sheet.Frames.push_back({ "idle", { 0, 0, 16, 32 }, { 0.5f, 0.0f }, false });
		sheet.Frames.push_back({ "step1", { 16, 0, 16, 32 }, { 0.5f, 0.0f }, false });
		sheet.Frames.push_back({ "wall", { 0, 0, 8, 8 }, { 0.0f, 1.0f }, true });
		sheet.Clips.push_back({ "walk", { 0, 1 }, 10.0f, SpriteWrapMode::Loop });
		sheet.Clips.push_back({ "jump", { 1, 1, 0 }, 20.0f, SpriteWrapMode::Once });
		std::string error;
		Check(sheet.Validate(&error), "the test sheet is valid: " + error);

		SpriteSheet parsed;
		Check(parsed.FromYAML(sheet.ToYAML()), "a sheet reads back from its YAML");
		Check(parsed.Frames.size() == 3 && parsed.Frames[0].Name == "idle" && parsed.Frames[0].Pivot.y == 0.0f && parsed.Frames[2].Solid && !parsed.Frames[1].Solid
			&& parsed.Clips.size() == 2 && parsed.Clips[1].Name == "jump" && parsed.Clips[1].Wrap == SpriteWrapMode::Once && parsed.Clips[1].Frames == std::vector<uint32_t>({ 1, 1, 0 })
			&& Near(parsed.PixelsPerUnit, 16.0f) && parsed.CellSize == glm::ivec2(16, 32), "frames, pivots, Solid, clips and grid survive the YAML");

		SpriteSheet broken = sheet;
		broken.Clips[0].Frames.push_back(9);
		Check(!broken.Validate(), "a clip naming a missing frame is invalid");
		SpriteSheet before = parsed;
		Check(!parsed.FromYAML(broken.ToYAML()) && parsed.Clips.size() == before.Clips.size() && parsed.Clips[0].Frames == before.Clips[0].Frames, "an invalid sheet is refused and the loaded one is left alone");
		broken = sheet;
		broken.Clips[1].Name = "walk";
		Check(!broken.Validate(), "two clips with one name are invalid");

		// Through the asset system: save, import, load, pack and unpack.
		AssetReference::PathModeScope pathMode;
		WriteText(root / "Sheets" / "Assets" / "Player.lsheet", sheet.ToYAML());
		const AssetHandle handle = AssetManager::ImportAsset("Player.lsheet");
		Check(handle != 0 && AssetManager::GetAssetType(handle) == AssetType::SpriteSheet, ".lsheet imports as a SpriteSheet");
		Ref<SpriteSheet> loaded = AssetManager::GetAsset<SpriteSheet>(handle);
		Check(loaded && loaded->Frames.size() == 3 && loaded->Texture == texture, "the asset manager loads the sheet and resolves its texture path");
		if (!loaded)
			return;

		loaded->PixelsPerUnit = 32.0f;
		Check(AssetImporter::TrySerialize(loaded.As<Asset>()), "the asset importer saves an edited sheet");
		Check(ReadText(root / "Sheets" / "Assets" / "Player.lsheet").find("Player.png") != std::string::npos, "the saved file names its texture by path");

		const std::filesystem::path packFile = root / "Sheets" / "sheet.pack";
		AssetSerializationInfo info;
		{
			FileStreamWriter writer(packFile);
			Check(SpriteSheetSerializer().SerializeToAssetPack(handle, writer, info), "a sheet writes into an asset pack");
		}
		FileStreamReader reader(packFile);
		AssetPackFile::AssetInfo packed{ info.Offset, info.Size, static_cast<uint16_t>(AssetType::SpriteSheet), 0 };
		const Ref<Asset> unpacked = SpriteSheetSerializer().DeserializeFromAssetPack(reader, packed);
		const Ref<SpriteSheet> unpackedSheet = unpacked.As<SpriteSheet>();
		Check(unpackedSheet && unpackedSheet->Clips.size() == 2 && Near(unpackedSheet->PixelsPerUnit, 32.0f) && unpackedSheet->Texture == texture,
			"a sheet unpacks with its clips, its edit and its numeric texture handle");

		// A scene's asset list carries the sheet and its texture, so the export packs both.
		Ref<Scene> scene = Ref<Scene>::Create();
		Entity entity = scene->CreateEntity("Hero");
		auto& sprite = entity.AddComponent<SpriteRendererComponent>();
		sprite.Sheet = handle;
		const auto assets = scene->GetAssetList();
		Check(assets.contains(handle) && assets.contains(texture), "Scene::GetAssetList lists a sprite's sheet and the sheet's texture");

		// The rest runs on a sheet with no texture: loading a texture needs a GPU device.
		sheet.Texture = 0;
		sheet.PixelsPerUnit = 32.0f;
		WriteText(root / "Sheets" / "Assets" / "Plain.lsheet", sheet.ToYAML());
		sprite.Sheet = AssetManager::ImportAsset("Plain.lsheet");
		Check(sprite.Sheet != 0, "a sheet without a texture imports");

		// The sprite takes its size and anchor from the frame: 16 x 32 texels at 32 px/unit, pivot bottom centre.
		sprite.SpriteIndex = 0;
		FrameRenderPacket packet;
		scene->CaptureDraw2D(packet);
		Check(packet.Draw2D.size() == 1, "the sheet sprite is one 2D item");
		if (!packet.Draw2D.empty())
		{
			const glm::mat4& transform = packet.Draw2D[0].Transform;
			const glm::vec3 bottomLeft = glm::vec3(transform * glm::vec4(-0.5f, -0.5f, 0.0f, 1.0f));
			const glm::vec3 topRight = glm::vec3(transform * glm::vec4(0.5f, 0.5f, 0.0f, 1.0f));
			Check(Near(bottomLeft.x, -0.25f) && Near(bottomLeft.y, 0.0f) && Near(topRight.x, 0.25f) && Near(topRight.y, 1.0f),
				std::format("the frame is 0.5 x 1 units with its pivot (bottom centre) on the entity ({:.2f},{:.2f})-({:.2f},{:.2f})", bottomLeft.x, bottomLeft.y, topRight.x, topRight.y));
		}

		// The animator plays "walk" (frames 0, 1 at 10 fps) on that sprite.
		auto& animator = entity.AddComponent<SpriteAnimatorComponent>();
		animator.Clip = "walk";
		std::string shown;
		for (int step = 0; step < 4; step++)
		{
			scene->UpdateSpriteAnimators(Timestep(0.1f));
			shown += std::to_string(sprite.SpriteIndex);
		}
		Check(shown == "1010", std::format("the animator steps the sprite through the clip (got {})", shown));
		animator.Clip = "jump";
		animator.Time = 0.0f;
		for (int step = 0; step < 4; step++)
			scene->UpdateSpriteAnimators(Timestep(0.1f));
		Check(sprite.SpriteIndex == 0 && !animator.Playing && animator.Finished, "a Once clip ends on its last frame and stops the animator");
		const int32_t stopped = sprite.SpriteIndex;
		scene->UpdateSpriteAnimators(Timestep(1.0f));
		Check(sprite.SpriteIndex == stopped, "a stopped animator leaves the sprite alone");
		animator.Clip = "no such clip";
		animator.Playing = true;
		scene->UpdateSpriteAnimators(Timestep(0.1f));
		Check(sprite.SpriteIndex == stopped, "an unknown clip name is ignored");

		// Copy, save and load.
		animator.Clip = "walk";
		const std::filesystem::path file = root / "Sheets" / "Hero.luxscene";
		Check(SceneSerializer(scene).Serialize(file), "a scene with a sheet sprite and an animator serializes");
		Ref<Scene> loadedScene = Ref<Scene>::Create();
		Check(SceneSerializer(loadedScene).Deserialize(file), "and loads back");
		Ref<Scene> copy = Scene::Copy(loadedScene);
		const Entity hero = copy->FindEntityByName("Hero");
		Check(hero && hero.GetComponent<SpriteRendererComponent>().Sheet == sprite.Sheet && hero.GetComponent<SpriteAnimatorComponent>().Clip == "walk" && hero.GetComponent<SpriteAnimatorComponent>().Playing,
			"the sheet reference and the animator survive save, load and copy");
		Ref<Scene> duplicateScene = Scene::Copy(scene);
		const Entity duplicated = duplicateScene->DuplicateEntity(duplicateScene->FindEntityByName("Hero"));
		Check(duplicated && duplicated.HasComponent<SpriteAnimatorComponent>(), "duplicating an entity copies its animator");
	}

	void CheckSpriteAtlas(const std::filesystem::path& root)
	{
		// The packer: no overlaps, inside the atlas, padded, deterministic.
		const std::vector<glm::ivec2> sizes = { { 30, 20 }, { 10, 40 }, { 16, 16 }, { 16, 16 }, { 64, 8 }, { 5, 5 }, { 24, 30 } };
		glm::ivec2 atlasSize;
		std::vector<glm::ivec2> positions;
		Check(SpriteAtlas::PackRects(sizes, 2, 256, atlasSize, positions), "rectangles pack");
		bool overlap = false, outside = false, tight = false;
		for (size_t i = 0; i < sizes.size(); i++)
		{
			outside |= positions[i].x < 2 || positions[i].y < 2 || positions[i].x + sizes[i].x + 2 > atlasSize.x || positions[i].y + sizes[i].y + 2 > atlasSize.y;
			for (size_t j = i + 1; j < sizes.size(); j++)
			{
				const bool apart = positions[i].x + sizes[i].x + 2 <= positions[j].x || positions[j].x + sizes[j].x + 2 <= positions[i].x
					|| positions[i].y + sizes[i].y + 2 <= positions[j].y || positions[j].y + sizes[j].y + 2 <= positions[i].y;
				overlap |= !apart;
			}
		}
		tight = atlasSize.x <= 256 && atlasSize.y <= 256 && (atlasSize.x & (atlasSize.x - 1)) == 0 && (atlasSize.y & (atlasSize.y - 1)) == 0;
		Check(!overlap && !outside, "packed rectangles do not overlap and keep their padding from each other and the edge");
		Check(tight, std::format("the atlas is a power-of-two size within the limit ({} x {})", atlasSize.x, atlasSize.y));
		glm::ivec2 again;
		std::vector<glm::ivec2> positionsAgain;
		SpriteAtlas::PackRects(sizes, 2, 256, again, positionsAgain);
		Check(again == atlasSize && positionsAgain == positions, "packing is deterministic");
		Check(!SpriteAtlas::PackRects({ { 300, 10 } }, 1, 256, atlasSize, positions), "a rectangle wider than the limit is refused");
		Check(!SpriteAtlas::PackRects(std::vector<glm::ivec2>(40, glm::ivec2(100, 100)), 1, 256, atlasSize, positions), "too many rectangles for the limit are refused");
		Check(!SpriteAtlas::PackRects({ { 0, 4 } }, 1, 256, atlasSize, positions), "an empty rectangle is refused");

		// Real images: pixels land in their rectangles, in image row order.
		const std::filesystem::path images = root / "AtlasImages";
		WriteSolidPNG(images / "red.png", 10, 6, 255, 0, 0);
		WriteSolidPNG(images / "green.png", 4, 4, 0, 255, 0);
		WriteSolidPNG(images / "blue.png", 8, 12, 0, 0, 255);
		const std::vector<SpriteAtlas::Source> sources = { { "red", images / "red.png" }, { "green", images / "green.png" }, { "blue", images / "blue.png" } };
		SpriteAtlas::Result atlas;
		std::string error;
		Check(SpriteAtlas::Pack(sources, 1, 256, atlas, error), "three images pack: " + error);
		Check(atlas.Frames.size() == 3 && atlas.Frames[0].Name == "red" && atlas.Frames[2].Rect.z == 8 && atlas.Frames[2].Rect.w == 12, "frames follow the source order and keep their size and name");
		const auto pixel = [&atlas](int x, int y) { return &atlas.Pixels[(static_cast<size_t>(y) * atlas.Size.x + x) * 4]; };
		bool colours = true;
		const uint8_t expected[3][3] = { { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 } };
		for (int i = 0; i < 3; i++)
		{
			const glm::ivec4 rect = atlas.Frames[static_cast<size_t>(i)].Rect;
			for (const glm::ivec2 corner : { glm::ivec2(rect.x, rect.y), glm::ivec2(rect.x + rect.z - 1, rect.y + rect.w - 1) })
			{
				const uint8_t* texel = pixel(corner.x, corner.y);
				colours &= texel[0] == expected[i][0] && texel[1] == expected[i][1] && texel[2] == expected[i][2] && texel[3] == 255;
			}
		}
		Check(colours, "each source's corner texels are in its rectangle");
		Check(pixel(0, 0)[3] == 0, "the padding is transparent");

		const std::filesystem::path atlasFile = images / "atlas.png";
		Check(SpriteAtlas::WritePNG(atlasFile, atlas), "the atlas writes as a PNG");
		int width = 0, height = 0, channels = 0;
		stbi_set_flip_vertically_on_load(0);
		stbi_uc* decoded = stbi_load(atlasFile.string().c_str(), &width, &height, &channels, 4);
		Check(decoded && width == atlas.Size.x && height == atlas.Size.y && std::equal(atlas.Pixels.begin(), atlas.Pixels.end(), decoded), "the written PNG decodes to the packed pixels");
		stbi_image_free(decoded);

		Check(!SpriteAtlas::Pack({ { "missing", images / "nope.png" } }, 1, 256, atlas, error) && error.find("nope.png") != std::string::npos, "a missing image fails and names the file");
		Check(!SpriteAtlas::Pack({}, 1, 256, atlas, error), "nothing to pack fails");
	}

	// ---- P6: Box2D completion ----------------------------------------------------------------------

	Entity AddBody(Scene& scene, const char* name, glm::vec2 position, RigidBody2DComponent::Type type)
	{
		Entity entity = scene.CreateEntity(name);
		entity.GetComponent<TransformComponent>().Translation = { position.x, position.y, 0.0f };
		entity.AddComponent<RigidBody2DComponent>().BodyType = type;
		return entity;
	}

	void Step(PhysicsScene2D& physics, int steps)
	{
		for (int i = 0; i < steps; i++)
			physics.SimulateStep(1.0f / 60.0f);
	}

	float YOf(Entity entity)
	{
		return entity.GetComponent<TransformComponent>().Translation.y;
	}

	void CheckFixedClock()
	{
		Ref<Project> project = Ref<Project>::Create();
		project->GetConfig().Physics.FixedTimestep = 1.0f / 50.0f;
		project->GetConfig().Physics.Gravity = { 0.0f, -4.0f, 0.0f };
		Project::SetActive(project);

		Ref<Scene> scene = Ref<Scene>::Create();
		Entity body = AddBody(*scene, "Falling", { 0.0f, 100.0f }, RigidBody2DComponent::Type::Dynamic);
		body.AddComponent<CircleCollider2DComponent>();
		PhysicsScene2D physics(scene.Raw());
		Check(physics.AdvanceFixedClock(Timestep(0.1f)) == 0, "no steps before the world exists");
		physics.Start();
		Check(Near(physics.GetFixedTimestep(), 0.02f, 1e-6f), "the 2D world steps at the project's fixed timestep");
		Check(physics.AdvanceFixedClock(Timestep(0.045f)) == 2, "0.045 s is two 20 ms steps");
		Check(physics.AdvanceFixedClock(Timestep(0.016f)) == 1, "the leftover 5 ms and 16 ms make a third");
		Check(physics.AdvanceFixedClock(Timestep(0.005f)) == 0, "a short frame is not enough for a step");
		Check(physics.AdvanceFixedClock(Timestep(1.0f)) == 8, "a long hitch is capped at 8 steps");
		physics.SimulateStep(1.0f / 50.0f);
		Check(Near(YOf(body), 100.0f - 0.5f * 4.0f * 0.02f * 0.02f, 0.01f) || YOf(body) < 100.0f, "the project's gravity pulls the body");
		for (int i = 0; i < 49; i++)
			physics.SimulateStep(1.0f / 50.0f);
		// after one second at g = 4 the body has dropped about 2 m (semi-implicit Euler: 2.04)
		Check(Near(YOf(body), 100.0f - 2.0f, 0.15f), std::format("one second of 4 m/s^2 gravity drops it about 2 m (y = {:.3f})", YOf(body)));
		Project::SetActive(nullptr);
	}

	void CheckColliderShapes()
	{
		Ref<Scene> scene = Ref<Scene>::Create();
		Entity triangle = AddBody(*scene, "Triangle", { 0.0f, 0.0f }, RigidBody2DComponent::Type::Static);
		triangle.AddComponent<PolygonCollider2DComponent>().Points = { { -1.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 2.0f } };
		Entity capsule = AddBody(*scene, "Capsule", { 4.0f, 0.0f }, RigidBody2DComponent::Type::Static);
		auto& capsuleCollider = capsule.AddComponent<CapsuleCollider2DComponent>();
		capsuleCollider.Radius = 0.25f;
		capsuleCollider.Height = 1.0f;
		Entity lying = AddBody(*scene, "Lying", { 8.0f, 0.0f }, RigidBody2DComponent::Type::Static);
		auto& lyingCollider = lying.AddComponent<CapsuleCollider2DComponent>();
		lyingCollider.CapsuleDirection = CapsuleCollider2DComponent::Direction::Horizontal;
		lyingCollider.Radius = 0.5f;
		lyingCollider.Height = 3.0f;
		Entity floor = AddBody(*scene, "Floor", { 0.0f, -5.0f }, RigidBody2DComponent::Type::Static);
		floor.AddComponent<EdgeCollider2DComponent>().Points = { { -20.0f, 0.0f }, { 20.0f, 0.0f } };
		Entity ring = AddBody(*scene, "Ring", { 12.0f, 0.0f }, RigidBody2DComponent::Type::Static);
		auto& ringCollider = ring.AddComponent<EdgeCollider2DComponent>();
		ringCollider.Points = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f } };
		ringCollider.Loop = true;
		Entity zone = AddBody(*scene, "Zone", { 16.0f, 0.0f }, RigidBody2DComponent::Type::Static);
		auto& zoneCollider = zone.AddComponent<BoxCollider2DComponent>();
		zoneCollider.Size = { 1.0f, 1.0f };
		zoneCollider.IsTrigger = true;
		Entity degenerate = AddBody(*scene, "Degenerate", { 20.0f, 0.0f }, RigidBody2DComponent::Type::Static);
		degenerate.AddComponent<PolygonCollider2DComponent>().Points = { { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 2.0f, 2.0f } };

		PhysicsScene2D physics(scene.Raw());
		physics.Start();
		PhysicsScene2D::RayHit hit;

		Check(physics.RayCast({ 0.0f, 5.0f }, { 0.0f, -1.0f }, 20.0f, 0, false, hit) && hit.EntityID == triangle.GetUUID() && Near(hit.Point.y, 2.0f, 0.01f) && Near(hit.Distance, 3.0f, 0.01f),
			"a ray down the middle hits the polygon's apex");
		Check(physics.RayCast({ 0.5f, 5.0f }, { 0.0f, -1.0f }, 20.0f, 0, false, hit) && Near(hit.Point.y, 1.0f, 0.01f) && hit.Normal.x > 0.5f && hit.Normal.y > 0.2f,
			"a ray at x = 0.5 hits the slanted side, whose normal leans right");
		Check(!physics.RayCast({ 1.5f, 5.0f }, { 0.0f, -1.0f }, 3.0f, 0, false, hit), "a ray that stops short (maxDistance) hits nothing");

		Check(physics.RayCast({ 4.0f, 5.0f }, { 0.0f, -1.0f }, 20.0f, 0, false, hit) && hit.EntityID == capsule.GetUUID() && Near(hit.Point.y, 0.5f, 0.01f), "the vertical capsule is 1 m tall: a ray from above hits y = 0.5");
		Check(physics.RayCast({ 0.0f + 4.0f - 5.0f, 0.0f }, { 1.0f, 0.0f }, 20.0f, 0, false, hit) && hit.EntityID == capsule.GetUUID() && Near(hit.Point.x, 3.75f, 0.01f), "and 0.5 m wide: a ray from the left hits x = 3.75");
		Check(physics.RayCast({ 8.0f, 5.0f }, { 0.0f, -1.0f }, 20.0f, 0, false, hit) && hit.EntityID == lying.GetUUID() && Near(hit.Point.y, 0.5f, 0.01f), "the horizontal capsule is 1 m tall");
		Check(physics.RayCast({ 20.0f, 0.0f }, { -1.0f, 0.0f }, 3.0f, 0, false, hit) == false, "a ray from the right at the horizontal capsule's end is out of range");
		Check(physics.RayCast({ 5.0f, 0.0f }, { 1.0f, 0.0f }, 10.0f, 0, false, hit) && hit.EntityID == lying.GetUUID() && Near(hit.Point.x, 6.5f, 0.01f), "the horizontal capsule is 3 m long: its rounded end is at x = 6.5");

		Check(physics.RayCast({ 0.0f, -2.0f }, { 0.0f, -1.0f }, 20.0f, 0, false, hit) && hit.EntityID == floor.GetUUID() && Near(hit.Point.y, -5.0f, 0.01f), "the edge collider is a floor");
		Check(physics.RayCast({ 12.0f, 5.0f }, { 0.0f, -1.0f }, 20.0f, 0, false, hit) && Near(hit.Point.y, 1.0f, 0.01f), "a looped edge collider is closed: the top side is there");
		std::vector<PhysicsScene2D::RayHit> all;
		physics.RayCastAll({ 12.0f, 5.0f }, { 0.0f, -1.0f }, 8.0f, 0, false, all);
		Check(all.size() == 1 && all[0].EntityID == ring.GetUUID(), "RayCastAll reports one hit per entity (the loop is one entity)");

		physics.RayCastAll({ 0.0f, 10.0f }, { 0.0f, -1.0f }, 30.0f, 0, false, all);
		Check(all.size() == 2 && all[0].EntityID == triangle.GetUUID() && all[1].EntityID == floor.GetUUID() && all[0].Distance < all[1].Distance, "RayCastAll goes nearest first through the polygon and the floor");
		Check(!physics.RayCast({ 0.0f, 5.0f }, { 0.0f, -1.0f }, 20.0f, floor.GetUUID(), false, hit) || hit.EntityID == triangle.GetUUID(), "an ignored entity is passed through");
		physics.RayCastAll({ 0.0f, 10.0f }, { 0.0f, -1.0f }, 30.0f, triangle.GetUUID(), false, all);
		Check(all.size() == 1 && all[0].EntityID == floor.GetUUID(), "RayCastAll skips the ignored entity");
		Check(!physics.RayCast({ 0.0f, 0.0f }, { 0.0f, 0.0f }, 5.0f, 0, false, hit) && !physics.RayCast({ 0.0f, 5.0f }, { 0.0f, -1.0f }, 0.0f, 0, false, hit), "a zero direction or length hits nothing");

		// Triggers: seen only when asked for.
		Check(!physics.RayCast({ 16.0f, 5.0f }, { 0.0f, -1.0f }, 8.0f, 0, false, hit), "a ray passes through a trigger");
		Check(physics.RayCast({ 16.0f, 5.0f }, { 0.0f, -1.0f }, 8.0f, 0, true, hit) && hit.EntityID == zone.GetUUID(), "includeTriggers sees it");

		std::vector<UUID> found;
		physics.Overlap(PhysicsScene2D::QueryShape::Circle, { 4.0f, 0.6f }, { 0.2f, 0.0f }, 0.0f, 0, false, found);
		Check(found.size() == 1 && found[0] == capsule.GetUUID(), "a circle touching the capsule's rounded top overlaps it");
		physics.Overlap(PhysicsScene2D::QueryShape::Circle, { 4.0f, 1.0f }, { 0.2f, 0.0f }, 0.0f, 0, false, found);
		Check(found.empty(), "a circle above the capsule's top does not");
		physics.Overlap(PhysicsScene2D::QueryShape::Circle, { 4.3f, 0.45f }, { 0.1f, 0.0f }, 0.0f, 0, false, found);
		Check(found.empty(), "a circle off the rounded corner (outside the true curve) does not overlap, though inside the box's corner");
		physics.Overlap(PhysicsScene2D::QueryShape::Box, { 6.0f, 0.0f }, { 1.0f, 0.2f }, 0.0f, 0, false, found);
		Check(found.size() == 1 && found[0] == lying.GetUUID(), "a box reaching the horizontal capsule's end overlaps it");
		physics.Overlap(PhysicsScene2D::QueryShape::Box, { 6.0f, 0.0f }, { 1.0f, 0.2f }, glm::radians(90.0f), 0, false, found);
		Check(found.empty(), "turned 90 degrees the same box stands clear of it");
		physics.Overlap(PhysicsScene2D::QueryShape::Circle, { 16.0f, 0.0f }, { 0.5f, 0.0f }, 0.0f, 0, false, found);
		Check(found.empty(), "an overlap query skips triggers");
		physics.Overlap(PhysicsScene2D::QueryShape::Circle, { 16.0f, 0.0f }, { 0.5f, 0.0f }, 0.0f, 0, true, found);
		Check(found.size() == 1 && found[0] == zone.GetUUID(), "and finds them when asked");
		physics.Overlap(PhysicsScene2D::QueryShape::Circle, { 0.0f, 0.0f }, { 30.0f, 0.0f }, 0.0f, triangle.GetUUID(), false, found);
		Check(std::find(found.begin(), found.end(), triangle.GetUUID()) == found.end() && found.size() >= 4, "a big circle finds the others, leaving out the ignored entity, each once");
		physics.Overlap(PhysicsScene2D::QueryShape::Box, { 0.0f, -5.0f }, { 1.0f, 1.0f }, 0.0f, 0, false, found);
		Check(found.size() == 1 && found[0] == floor.GetUUID(), "an edge collider is found by an overlap");

		Check(!physics.RayCast({ 20.5f, 5.0f }, { 0.0f, -1.0f }, 20.0f, 0, false, hit), "a polygon with all its points on one line makes no collider (and does not crash)");

		// Scale applies to the shapes: the triangle doubled in X is twice as wide.
		Ref<Scene> scaled = Ref<Scene>::Create();
		Entity wide = AddBody(*scaled, "Wide", { 0.0f, 0.0f }, RigidBody2DComponent::Type::Static);
		wide.GetComponent<TransformComponent>().Scale = { 2.0f, 1.0f, 1.0f };
		wide.AddComponent<PolygonCollider2DComponent>().Points = { { -1.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 2.0f } };
		PhysicsScene2D scaledPhysics(scaled.Raw());
		scaledPhysics.Start();
		Check(scaledPhysics.RayCast({ 1.5f, 5.0f }, { 0.0f, -1.0f }, 20.0f, 0, false, hit) && Near(hit.Point.y, 0.5f, 0.01f), "the entity's scale stretches a polygon (x = 1.5 is on its side at y = 0.5)");
	}

	// An edge collider blocks from both sides, however its points are ordered.
	void CheckEdgeBlocksBothSides()
	{
		for (const bool leftToRight : { true, false })
		{
			Ref<Scene> scene = Ref<Scene>::Create();
			Entity floor = AddBody(*scene, "Floor", { 0.0f, 0.0f }, RigidBody2DComponent::Type::Static);
			auto& edge = floor.AddComponent<EdgeCollider2DComponent>();
			edge.Points = leftToRight ? std::vector<glm::vec2>{ { -10.0f, 0.0f }, { 0.0f, 0.0f }, { 10.0f, 0.0f } } : std::vector<glm::vec2>{ { 10.0f, 0.0f }, { 0.0f, 0.0f }, { -10.0f, 0.0f } };
			Entity above = AddBody(*scene, "Above", { -2.0f, 3.0f }, RigidBody2DComponent::Type::Dynamic);
			above.AddComponent<CircleCollider2DComponent>().Radius = 0.25f;
			Entity below = AddBody(*scene, "Below", { 2.0f, -3.0f }, RigidBody2DComponent::Type::Dynamic);
			below.AddComponent<CircleCollider2DComponent>().Radius = 0.25f;
			below.GetComponent<RigidBody2DComponent>().GravityScale = -1.0f; // falls up
			PhysicsScene2D physics(scene.Raw());
			physics.Start();
			Step(physics, 240);
			Check(Near(YOf(above), 0.25f, 0.05f) && Near(YOf(below), -0.25f, 0.05f),
				std::format("an edge collider drawn {} blocks from above (y = {:.3f}) and from below (y = {:.3f})", leftToRight ? "left to right" : "right to left", YOf(above), YOf(below)));
		}
	}

	void CheckContactsAndTriggers()
	{
		Ref<Scene> scene = Ref<Scene>::Create();
		Entity floor = AddBody(*scene, "Floor", { 0.0f, -2.0f }, RigidBody2DComponent::Type::Static);
		floor.AddComponent<BoxCollider2DComponent>().Size = { 10.0f, 0.5f };
		Entity capsule = AddBody(*scene, "Capsule", { 0.0f, 1.0f }, RigidBody2DComponent::Type::Dynamic);
		auto& capsuleCollider = capsule.AddComponent<CapsuleCollider2DComponent>();
		capsuleCollider.CapsuleDirection = CapsuleCollider2DComponent::Direction::Horizontal; // lying down: its three fixtures all land together
		capsuleCollider.Radius = 0.25f;
		capsuleCollider.Height = 2.0f;
		Entity zone = AddBody(*scene, "Zone", { 0.0f, -0.2f }, RigidBody2DComponent::Type::Static);
		auto& zoneCollider = zone.AddComponent<BoxCollider2DComponent>();
		zoneCollider.Size = { 2.0f, 0.1f };
		zoneCollider.IsTrigger = true;

		PhysicsScene2D physics(scene.Raw());
		physics.Start();
		Step(physics, 5);
		std::vector<PhysicsContactEvent> early;
		physics.DrainContactEvents(early);
		Check(early.empty(), "contacts are not recorded until Play asks for them");

		physics.SetRecordContacts(true);
		Step(physics, 150);
		std::vector<PhysicsContactEvent> events;
		physics.DrainContactEvents(events);
		size_t collisionBegins = 0, triggerBegins = 0, triggerEnds = 0;
		for (const auto& event : events)
		{
			const bool pair = (event.Entity1 == capsule.GetUUID() && event.Entity2 == floor.GetUUID()) || (event.Entity2 == capsule.GetUUID() && event.Entity1 == floor.GetUUID());
			const bool zonePair = (event.Entity1 == capsule.GetUUID() && event.Entity2 == zone.GetUUID()) || (event.Entity2 == capsule.GetUUID() && event.Entity1 == zone.GetUUID());
			if (pair && !event.Trigger && event.State == PhysicsContactEvent::Type::Begin)
				collisionBegins++;
			if (zonePair && event.Trigger && event.State == PhysicsContactEvent::Type::Begin)
				triggerBegins++;
			if (zonePair && event.Trigger && event.State == PhysicsContactEvent::Type::End)
				triggerEnds++;
		}
		Check(collisionBegins >= 2, std::format("a capsule lying on a floor makes one Box2D contact per fixture ({} Begin events)", collisionBegins));
		Check(triggerBegins >= 1 && triggerEnds == triggerBegins, std::format("falling through a trigger gives Begin and End events marked as triggers ({} / {})", triggerBegins, triggerEnds));
		Check(YOf(capsule) > -1.4f && YOf(capsule) < -1.2f, std::format("the trigger blocked nothing and the capsule rests on the floor (y = {:.3f})", YOf(capsule)));

		// The scene's pair tracker turns the per-fixture events into one Begin for the pair.
		ContactPairTracker tracker;
		int begins = 0;
		for (const auto& event : events)
		{
			if (!event.Trigger)
				tracker.Apply(event, [&begins](UUID, UUID, bool begin) { begins += begin ? 1 : 0; });
		}
		Check(begins == 1, std::format("scripts hear one OnCollisionBegin for the pair, not one per fixture ({})", begins));
	}

	void CheckJoints()
	{
		Ref<Scene> scene = Ref<Scene>::Create();

		// A rigid rod from a point in the world: the body swings and stays 2 m from the pivot.
		Entity pendulum = AddBody(*scene, "Pendulum", { 2.0f, 5.0f }, RigidBody2DComponent::Type::Dynamic);
		pendulum.AddComponent<CircleCollider2DComponent>().Radius = 0.2f;
		auto& rod = pendulum.AddComponent<Joint2DComponent>();
		rod.JointType = Joint2DComponent::Type::Distance;
		rod.ConnectedAnchor = { 0.0f, 5.0f }; // world point
		// A hinge with a limit: a bar pinned at its left end may only swing 10 degrees either way.
		Entity bar = AddBody(*scene, "Bar", { 20.0f, 5.0f }, RigidBody2DComponent::Type::Dynamic);
		bar.AddComponent<BoxCollider2DComponent>().Size = { 1.0f, 0.1f };
		auto& hinge = bar.AddComponent<Joint2DComponent>();
		hinge.JointType = Joint2DComponent::Type::Revolute;
		hinge.Anchor = { -1.0f, 0.0f };
		hinge.EnableLimit = true;
		hinge.LowerLimit = -10.0f;
		hinge.UpperLimit = 10.0f;
		// A slider: free along x between 0 and 1 from its start, never off the line.
		Entity slider = AddBody(*scene, "Slider", { 40.0f, 5.0f }, RigidBody2DComponent::Type::Dynamic);
		slider.AddComponent<BoxCollider2DComponent>().Size = { 0.3f, 0.3f };
		auto& rail = slider.AddComponent<Joint2DComponent>();
		rail.JointType = Joint2DComponent::Type::Prismatic;
		rail.EnableLimit = true;
		rail.LowerLimit = 0.0f;
		rail.UpperLimit = 1.0f;
		rail.Axis = { 1.0f, 0.0f };
		// Two bodies welded together fall as one.
		Entity base = AddBody(*scene, "Base", { 60.0f, 5.0f }, RigidBody2DComponent::Type::Dynamic);
		base.AddComponent<BoxCollider2DComponent>().Size = { 0.3f, 0.3f };
		Entity arm = AddBody(*scene, "Arm", { 61.5f, 5.0f }, RigidBody2DComponent::Type::Dynamic);
		arm.AddComponent<BoxCollider2DComponent>().Size = { 0.3f, 0.3f };
		auto& weld = arm.AddComponent<Joint2DComponent>();
		weld.JointType = Joint2DComponent::Type::Weld;
		weld.ConnectedEntity = base.GetUUID();
		// A joint that is switched off, and one to an entity with no body: no joint, no crash.
		Entity loose = AddBody(*scene, "Loose", { 80.0f, 5.0f }, RigidBody2DComponent::Type::Dynamic);
		loose.AddComponent<CircleCollider2DComponent>();
		loose.AddComponent<Joint2DComponent>().Enabled = false;
		Entity orphan = AddBody(*scene, "Orphan", { 90.0f, 5.0f }, RigidBody2DComponent::Type::Dynamic);
		orphan.AddComponent<CircleCollider2DComponent>();
		orphan.AddComponent<Joint2DComponent>().ConnectedEntity = scene->CreateEntity("Bodyless").GetUUID();

		PhysicsScene2D physics(scene.Raw());
		physics.Start();
		float maxRodError = 0.0f;
		float lowestAngle = 0.0f, highestAngle = 0.0f;
		float sliderMin = 1e9f, sliderMax = -1e9f, sliderDrift = 0.0f;
		float weldError = 0.0f;
		for (int i = 0; i < 240; i++)
		{
			physics.SimulateStep(1.0f / 60.0f);
			const glm::vec3 p = pendulum.GetComponent<TransformComponent>().Translation;
			maxRodError = std::max(maxRodError, std::abs(glm::length(glm::vec2(p.x, p.y) - glm::vec2(0.0f, 5.0f)) - 2.0f));
			const float angle = glm::degrees(bar.GetComponent<TransformComponent>().GetRotationEuler().z);
			lowestAngle = std::min(lowestAngle, angle);
			highestAngle = std::max(highestAngle, angle);
			const glm::vec3 s = slider.GetComponent<TransformComponent>().Translation;
			sliderMin = std::min(sliderMin, s.x);
			sliderMax = std::max(sliderMax, s.x);
			sliderDrift = std::max(sliderDrift, std::abs(s.y - 5.0f));
			const glm::vec3 a = base.GetComponent<TransformComponent>().Translation;
			const glm::vec3 b = arm.GetComponent<TransformComponent>().Translation;
			weldError = std::max(weldError, std::abs(glm::length(b - a) - 1.5f));
		}
		Check(maxRodError < 0.05f && YOf(pendulum) < 5.0f, std::format("a distance joint keeps its length while the body swings (largest error {:.4f} m)", maxRodError));
		Check(highestAngle <= 10.5f && lowestAngle >= -10.5f && (highestAngle > 0.5f || lowestAngle < -0.5f), std::format("a hinge limit keeps the bar within 10 degrees ({:.1f} .. {:.1f}) and it did swing", lowestAngle, highestAngle));
		Check(sliderMin >= 39.95f && sliderMax <= 41.05f && sliderDrift < 0.05f, std::format("a prismatic joint holds the slider to its rail: x {:.2f}..{:.2f}, off the rail by {:.3f}", sliderMin, sliderMax, sliderDrift));
		Check(weldError < 0.05f && YOf(base) < 4.0f && YOf(arm) < 4.0f, std::format("a weld joint carries both bodies down together (distance error {:.4f}, y = {:.2f})", weldError, YOf(base)));
		Check(YOf(loose) < 0.0f && YOf(orphan) < 0.0f, "a disabled joint and a joint to a bodyless entity do nothing, and the bodies still fall free");
	}

	void CheckBodiesComeAndGo()
	{
		Ref<Scene> scene = Ref<Scene>::Create();
		Entity floor = AddBody(*scene, "Floor", { 0.0f, -5.0f }, RigidBody2DComponent::Type::Static);
		floor.AddComponent<BoxCollider2DComponent>().Size = { 10.0f, 0.5f };
		PhysicsScene2D physics(scene.Raw());
		physics.Start();

		Entity late = AddBody(*scene, "Late", { 3.0f, 3.0f }, RigidBody2DComponent::Type::Dynamic);
		late.AddComponent<BoxCollider2DComponent>();
		PhysicsScene2D::RayHit hit;
		Check(!physics.RayCast({ 3.0f, 8.0f }, { 0.0f, -1.0f }, 3.0f, 0, false, hit), "an entity added during Play has no body yet");
		physics.CreateBody(late);
		Check(physics.RayCast({ 3.0f, 8.0f }, { 0.0f, -1.0f }, 6.0f, 0, false, hit) && hit.EntityID == late.GetUUID(), "CreateBody gives a spawned entity its body and colliders");
		physics.CreateBody(late);
		Step(physics, 30);
		Check(YOf(late) < 3.0f, "and the body is simulated");
		physics.DestroyBody(late.GetUUID());
		Check(!physics.RayCast({ 3.0f, 8.0f }, { 0.0f, -1.0f }, 3.0f, 0, false, hit), "DestroyBody takes it out of the world");
		Step(physics, 5); // the destroyed entity's body must not be touched again
		Check(true, "stepping after DestroyBody is safe");
	}

	void CheckPhysics2DSerialization(const std::filesystem::path& root)
	{
		Ref<Scene> scene = Ref<Scene>::Create();
		Entity anchor = AddBody(*scene, "Anchor", { 0.0f, 0.0f }, RigidBody2DComponent::Type::Static);
		Entity box = AddBody(*scene, "Box", { 1.0f, 0.0f }, RigidBody2DComponent::Type::Dynamic);
		auto& boxCollider = box.AddComponent<BoxCollider2DComponent>();
		boxCollider.IsTrigger = true;
		boxCollider.Restitution = 0.4f;
		boxCollider.RestitutionThreshold = 1.5f;
		auto& polygon = box.AddComponent<PolygonCollider2DComponent>();
		polygon.Points = { { 0.0f, 0.0f }, { 2.0f, 0.0f }, { 1.0f, 1.5f } };
		polygon.IsTrigger = true;
		polygon.Friction = 0.9f;
		auto& capsule = box.AddComponent<CapsuleCollider2DComponent>();
		capsule.Radius = 0.3f;
		capsule.Height = 1.4f;
		capsule.CapsuleDirection = CapsuleCollider2DComponent::Direction::Horizontal;
		auto& edge = box.AddComponent<EdgeCollider2DComponent>();
		edge.Points = { { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 2.0f, 0.0f } };
		edge.Loop = true;
		auto& joint = box.AddComponent<Joint2DComponent>();
		joint.JointType = Joint2DComponent::Type::Prismatic;
		joint.ConnectedEntity = anchor.GetUUID();
		joint.Anchor = { 0.5f, 0.25f };
		joint.EnableLimit = true;
		joint.LowerLimit = -1.0f;
		joint.UpperLimit = 3.0f;
		joint.EnableMotor = true;
		joint.MotorSpeed = 2.0f;
		joint.MaxMotorForce = 50.0f;
		joint.Axis = { 0.0f, 1.0f };
		joint.CollideConnected = true;
		joint.AutoLength = false;
		joint.Length = 2.5f;
		joint.Stiffness = 30.0f;
		joint.Damping = 3.0f;

		const std::filesystem::path file = root / "Physics2D.luxscene";
		Check(SceneSerializer(scene).Serialize(file), "a scene with every 2D collider and a joint serializes");
		Ref<Scene> loaded = Ref<Scene>::Create();
		Check(SceneSerializer(loaded).Deserialize(file), "and loads back");

		const auto verify = [&](Ref<Scene> checked, const char* how)
		{
			const Entity loadedBox = checked->FindEntityByName("Box");
			const Entity loadedAnchor = checked->FindEntityByName("Anchor");
			const auto& b = loadedBox.GetComponent<BoxCollider2DComponent>();
			const auto& p = loadedBox.GetComponent<PolygonCollider2DComponent>();
			const auto& c = loadedBox.GetComponent<CapsuleCollider2DComponent>();
			const auto& e = loadedBox.GetComponent<EdgeCollider2DComponent>();
			const auto& j = loadedBox.GetComponent<Joint2DComponent>();
			Check(b.IsTrigger && Near(b.Restitution, 0.4f) && Near(b.RestitutionThreshold, 1.5f), std::string("box collider trigger and restitution survive ") + how);
			Check(p.Points.size() == 3 && Near(p.Points[2].y, 1.5f) && p.IsTrigger && Near(p.Friction, 0.9f), std::string("polygon collider survives ") + how);
			Check(Near(c.Radius, 0.3f) && Near(c.Height, 1.4f) && c.CapsuleDirection == CapsuleCollider2DComponent::Direction::Horizontal, std::string("capsule collider survives ") + how);
			Check(e.Points.size() == 3 && e.Loop && Near(e.Points[1].x, 1.0f), std::string("edge collider survives ") + how);
			Check(j.JointType == Joint2DComponent::Type::Prismatic && j.ConnectedEntity == loadedAnchor.GetUUID() && Near(j.Anchor.y, 0.25f) && j.EnableLimit && Near(j.LowerLimit, -1.0f)
				&& Near(j.UpperLimit, 3.0f) && j.EnableMotor && Near(j.MotorSpeed, 2.0f) && Near(j.MaxMotorForce, 50.0f) && Near(j.Axis.y, 1.0f) && j.CollideConnected && !j.AutoLength
				&& Near(j.Length, 2.5f) && Near(j.Stiffness, 30.0f) && Near(j.Damping, 3.0f), std::string("joint, with its connected entity, survives ") + how);
		};
		verify(loaded, "a save");
		verify(Scene::Copy(loaded), "the copy Play starts from");

		// A fresh component writes nothing it does not need: trigger and restitution are keys only when set.
		Ref<Scene> plain = Ref<Scene>::Create();
		AddBody(*plain, "Plain", { 0.0f, 0.0f }, RigidBody2DComponent::Type::Dynamic).AddComponent<BoxCollider2DComponent>();
		const std::filesystem::path plainFile = root / "PlainPhysics2D.luxscene";
		SceneSerializer(plain).Serialize(plainFile);
		const std::string text = ReadText(plainFile);
		Check(text.find("IsTrigger") == std::string::npos && text.find("Restitution") == std::string::npos, "a default box collider writes no trigger or restitution keys (old scenes re-save unchanged)");

		// Duplicating a hierarchy moves a joint to the copy of the body it joined.
		Ref<Scene> family = Ref<Scene>::Create();
		Entity parent = AddBody(*family, "Parent", { 0.0f, 0.0f }, RigidBody2DComponent::Type::Static);
		Entity child = AddBody(*family, "Child", { 1.0f, 0.0f }, RigidBody2DComponent::Type::Dynamic);
		child.AddComponent<Joint2DComponent>().ConnectedEntity = parent.GetUUID();
		child.SetParent(parent);
		const Entity copyOfParent = family->DuplicateEntity(parent);
		const std::vector<UUID> copiedChildren = copyOfParent.Children();
		Check(copiedChildren.size() == 1 && family->TryGetEntityWithUUID(copiedChildren[0]).GetComponent<Joint2DComponent>().ConnectedEntity == copyOfParent.GetUUID(),
			"duplicating a hierarchy points the joint at the copy of its body");
	}

}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::printf("usage: Checks2D <outdir>\n");
		return 2;
	}

	std::setvbuf(stdout, nullptr, _IONBF, 0); // a crash must not swallow the last checks
	const std::filesystem::path outDir = std::filesystem::absolute(argv[1]);
	std::filesystem::create_directories(outDir);
	InitializeCore(outDir / "logs");

	CheckProjectType(outDir);
	CheckCameraBackground(outDir);
	CheckBackgroundDrawItem();
	CheckEditorCamera2D();
	CheckStarterScene(outDir);
	CheckSortingLayers(outDir);
	CheckDrawOrder();
	CheckPixelPerfectCamera(outDir);
	CheckSpriteSheetMath();
	CheckSpriteSheetFiles(outDir);
	CheckSpriteAtlas(outDir);
	CheckFixedClock();
	CheckColliderShapes();
	CheckEdgeBlocksBothSides();
	CheckContactsAndTriggers();
	CheckJoints();
	CheckBodiesComeAndGo();
	CheckPhysics2DSerialization(outDir);

	std::printf("RESULT: %s (%d failure%s)\n", s_Failures == 0 ? "PASS" : "FAIL", s_Failures, s_Failures == 1 ? "" : "s");
	std::fflush(stdout);

	// Exit without Project/ScriptEngine/Core teardown: the asset manager would rewrite its registry
	// on shutdown and nothing here needs a clean exit.
	std::_Exit(s_Failures == 0 ? 0 : 1);
}
