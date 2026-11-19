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
#include "Lux/Scene/Tilemap.h"
#include "Lux/UI/UIAccessibility.h"
#include "Lux/UI/UILayout.h"
#include "Lux/UI/UISystem.h"

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

	// ---- D3: tilemaps -------------------------------------------------------------------------------

	void CheckTilemapOps()
	{
		TilemapLayer layer("Test");
		Check(Tilemap::GetTile(layer, 3, 4) == -1, "an empty cell holds no tile");
		Check(Tilemap::SetTile(layer, 3, 4, 7) && Tilemap::GetTile(layer, 3, 4) == 7, "SetTile puts a tile in a cell");
		Check(!Tilemap::SetTile(layer, 3, 4, 7), "setting the same tile again changes nothing");
		Check(Tilemap::SetTile(layer, 3, 4, 2) && Tilemap::GetTile(layer, 3, 4) == 2, "a different tile replaces it");
		Check(Tilemap::SetTile(layer, -5, -7, 1) && Tilemap::GetTile(layer, -5, -7) == 1 && Tilemap::GetTile(layer, 5, 7) == -1 && Tilemap::GetTile(layer, -5, 7) == -1, "negative coordinates are their own cells");
		Tilemap::CellRect bounds;
		Check(Tilemap::GetBounds(layer, bounds) && bounds == Tilemap::CellRect{ -5, -7, 9, 12 }, "the bounds hold every tile");
		Check(Tilemap::SetTile(layer, 3, 4, -1) && Tilemap::GetTile(layer, 3, 4) == -1 && layer.Cells.size() == 1, "a negative tile erases the cell");
		Check(!Tilemap::SetTile(layer, 3, 4, -1), "erasing an empty cell changes nothing");
		TilemapLayer empty;
		Check(!Tilemap::GetBounds(empty, bounds), "an empty layer has no bounds");

		TilemapLayer area("Area");
		Check(Tilemap::FillRect(area, { 0, 0, 4, 3 }, 5) == 12 && area.Cells.size() == 12, "FillRect fills a block");
		Check(Tilemap::FillRect(area, { 1, 1, 2, 1 }, -1) == 2 && area.Cells.size() == 10, "an erasing FillRect empties cells");
		Check(Tilemap::FillRect(area, { 0, 0, 4, 3 }, 5) == 2, "FillRect counts only the cells that changed");

		// Flood fill: the connected region of equal cells, inside the limit.
		TilemapLayer room("Room");
		Tilemap::FillRect(room, { 0, 0, 5, 5 }, 1);
		Tilemap::FillRect(room, { 2, 0, 1, 5 }, 2); // a wall of tile 2 splits the room
		const Tilemap::CellRect limit{ -3, -3, 12, 12 };
		Check(Tilemap::FloodFill(room, 0, 0, 9, limit) == 10 && Tilemap::GetTile(room, 1, 4) == 9 && Tilemap::GetTile(room, 3, 0) == 1 && Tilemap::GetTile(room, 2, 2) == 2, "FloodFill stops at the wall and fills one side");
		Check(Tilemap::FloodFill(room, 20, 20, 4, limit) == 0, "a start outside the limit fills nothing");
		Check(Tilemap::FloodFill(room, -2, -2, 3, limit) == 12 * 12 - 25 && Tilemap::GetTile(room, -3, 8) == 3 && Tilemap::GetTile(room, 4, 4) == 1, "FloodFill of empty space fills it up to the limit, around the room");
		Check(Tilemap::FloodFill(room, 3, 1, 1, limit) == 0, "filling with the tile already there changes nothing");
		Check(Tilemap::FloodFill(room, 3, 0, -1, limit) == 10 && Tilemap::GetTile(room, 4, 4) == -1, "FloodFill with a negative tile erases the region");

		// Solid cells merge into the fewest blocks.
		std::vector<SpriteFrame> frames(3);
		frames[0].Solid = true;
		frames[2].Solid = true; // frame 1 is scenery
		TilemapLayer level("Level");
		Tilemap::FillRect(level, { 0, 0, 5, 2 }, 0);      // one 5 x 2 block
		Tilemap::FillRect(level, { 1, 2, 2, 1 }, 2);      // a 2 x 1 block on top of it, different width
		Tilemap::FillRect(level, { 10, 0, 3, 1 }, 0);
		Tilemap::SetTile(level, 11, 0, 1);                // scenery splits that run in two
		Tilemap::FillRect(level, { 20, 5, 1, 4 }, 0);     // a column: four rows of one cell merge upward
		Tilemap::SetTile(level, 30, 30, 99);              // a tile the sheet does not have is not solid
		std::vector<Tilemap::CellRect> blocks;
		Tilemap::MergeSolid(level, frames, blocks);
		int64_t area2 = 0;
		for (const auto& block : blocks)
			area2 += static_cast<int64_t>(block.Width) * block.Height;
		const auto has = [&blocks](Tilemap::CellRect rect) { return std::find(blocks.begin(), blocks.end(), rect) != blocks.end(); };
		Check(blocks.size() == 5 && has({ 0, 0, 5, 2 }) && has({ 1, 2, 2, 1 }) && has({ 10, 0, 1, 1 }) && has({ 12, 0, 1, 1 }) && has({ 20, 5, 1, 4 }),
			std::format("solid cells merge into {} blocks (a 5 x 2, a 2 x 1, two 1 x 1 and a 1 x 4)", blocks.size()));
		Check(area2 == 10 + 2 + 2 + 4, "the blocks cover exactly the solid cells");
		TilemapLayer floor("Floor");
		Tilemap::FillRect(floor, { -100, 0, 200, 1 }, 0);
		Tilemap::MergeSolid(floor, frames, blocks);
		Check(blocks.size() == 1 && blocks[0] == Tilemap::CellRect{ -100, 0, 200, 1 }, "a floor of 200 tiles is one block");
		Tilemap::MergeSolid(empty, frames, blocks);
		Check(blocks.empty(), "an empty layer has no solid blocks");

		// Which cells a view sees.
		SceneCamera camera;
		camera.SetOrthographic(10.0f, -100.0f, 100.0f);
		camera.SetViewportSize(1600, 900);
		const glm::mat4 viewProjection = camera.GetProjectionMatrix();
		Tilemap::CellRect visible;
		Check(Tilemap::ComputeVisibleCells(viewProjection, glm::mat4(1.0f), { 1.0f, 1.0f }, visible), "an orthographic view maps onto the tilemap plane");
		Check(visible.Contains(-8, -5) && visible.Contains(8, 4) && !visible.Contains(20, 0) && !visible.Contains(0, 20) && visible.Width < 25 && visible.Height < 15,
			std::format("the view of 17.8 x 10 units covers about that many cells ({} x {} from {},{})", visible.Width, visible.Height, visible.X, visible.Y));
		Tilemap::CellRect shifted;
		Tilemap::ComputeVisibleCells(viewProjection, glm::translate(glm::mat4(1.0f), glm::vec3(100.0f, 0.0f, 0.0f)), { 1.0f, 1.0f }, shifted);
		Check(shifted.X == visible.X - 100 && shifted.Width == visible.Width, "moving the tilemap moves the range the other way");
		Tilemap::CellRect scaled;
		Tilemap::ComputeVisibleCells(viewProjection, glm::mat4(1.0f), { 2.0f, 2.0f }, scaled);
		Check(scaled.Width < visible.Width && scaled.Width >= visible.Width / 2 - 2, "bigger cells need fewer of them");
		const glm::mat4 perspective = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f) * glm::lookAt(glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		Tilemap::CellRect seen;
		Check(Tilemap::ComputeVisibleCells(perspective, glm::mat4(1.0f), { 1.0f, 1.0f }, seen) && seen.Contains(0, 0) && seen.Contains(8, 4) && !seen.Contains(30, 0), "a perspective view looking at the plane maps too");
		Check(!Tilemap::ComputeVisibleCells(glm::mat4(0.0f), glm::mat4(1.0f), { 1.0f, 1.0f }, seen) && !Tilemap::ComputeVisibleCells(viewProjection, glm::mat4(0.0f), { 1.0f, 1.0f }, seen)
			&& !Tilemap::ComputeVisibleCells(viewProjection, glm::mat4(1.0f), { 0.0f, 1.0f }, seen), "a singular view or tilemap transform, or a cell with no size, cannot be culled, so everything is drawn");
	}

	void WriteTileSet(const std::filesystem::path& file)
	{
		// Frame 0 is a solid wall, frame 1 scenery, frame 2 a solid floor; no texture (loading one needs a GPU).
		SpriteSheet sheet;
		sheet.PixelsPerUnit = 16.0f;
		sheet.Frames.push_back({ "wall", { 0, 0, 16, 16 }, { 0.5f, 0.5f }, true });
		sheet.Frames.push_back({ "grass", { 16, 0, 16, 16 }, { 0.5f, 0.5f }, false });
		sheet.Frames.push_back({ "floor", { 32, 0, 16, 16 }, { 0.5f, 0.5f }, true });
		WriteText(file, sheet.ToYAML());
	}

	void CheckTilemapScene(const std::filesystem::path& root)
	{
		WriteText(root / "Tiles" / "Tiles.luxproj", "Project:\n  Name: Tiles\n  AssetDirectory: Assets\n");
		std::filesystem::create_directories(root / "Tiles" / "Assets");
		Ref<Project> project = Project::Load(root / "Tiles" / "Tiles.luxproj");
		if (!project)
		{
			Check(false, "tile project loads");
			return;
		}
		project->GetConfig().SortingLayers = { { 0, "Default" }, { 4, "Front" } };
		WriteTileSet(root / "Tiles" / "Assets" / "Dungeon.lsheet");
		const AssetHandle tileSet = AssetManager::ImportAsset("Dungeon.lsheet");
		Check(tileSet != 0 && AssetManager::GetAsset<SpriteSheet>(tileSet) != nullptr, "the tile set (a sprite sheet) imports");

		Ref<Scene> scene = Ref<Scene>::Create();
		Entity map = scene->CreateEntity("Map");
		map.GetComponent<TransformComponent>().Translation = { 10.0f, 0.0f, 0.0f };
		auto& tilemap = map.AddComponent<TilemapComponent>();
		tilemap.TileSet = tileSet;
		tilemap.CellSize = { 0.5f, 0.5f };
		tilemap.PointFilter = true;
		tilemap.SortingLayer = 4;
		tilemap.OrderInLayer = 3;
		tilemap.Friction = 0.8f;
		tilemap.Layers[0].Name = "Ground";
		Tilemap::FillRect(tilemap.Layers[0], { -3, -1, 6, 1 }, 2);
		Tilemap::SetTile(tilemap.Layers[0], 100000, -250000, 0); // a far corner of the plane
		tilemap.Layers.push_back(TilemapLayer("Decor"));
		tilemap.Layers[1].Collides = false;
		tilemap.Layers[1].Visible = false;
		Tilemap::SetTile(tilemap.Layers[1], 1, 0, 1);
		Tilemap::SetTile(tilemap.Layers[1], 2, 0, 1);

		const std::filesystem::path file = root / "Tiles" / "Map.luxscene";
		AssetReference::PathModeScope pathMode;
		Check(SceneSerializer(scene).Serialize(file), "a scene with a tilemap serializes");
		const std::string text = ReadText(file);
		Check(text.find("Runs") != std::string::npos && text.find("Visible: false") != std::string::npos && text.find("Collides: false") != std::string::npos && text.find("IsTrigger") == std::string::npos,
			"cells are written as runs, with the layer flags that are not the default");
		SceneSerializer(scene).Serialize(root / "Tiles" / "Map2.luxscene");
		Check(text == ReadText(root / "Tiles" / "Map2.luxscene"), "saving twice gives the same file");
		size_t runLines = 0;
		for (size_t at = text.find("- ["); at != std::string::npos; at = text.find("- [", at + 1))
			runLines++;
		Check(runLines == 3, std::format("the 6-tile floor row, the far tile and the 2 decor tiles are three runs ({})", runLines));

		Ref<Scene> loaded = Ref<Scene>::Create();
		Check(SceneSerializer(loaded).Deserialize(file), "and loads back");
		const auto verify = [&](Ref<Scene> checked, const char* how)
		{
			const auto& copy = checked->FindEntityByName("Map").GetComponent<TilemapComponent>();
			Check(copy.TileSet == tileSet && Near(copy.CellSize.x, 0.5f) && copy.PointFilter && copy.SortingLayer == 4 && copy.OrderInLayer == 3 && Near(copy.Friction, 0.8f) && copy.Layers.size() == 2,
				std::string("tilemap settings survive ") + how);
			Check(copy.Layers[0].Name == "Ground" && copy.Layers[0].Cells.size() == 7 && Tilemap::GetTile(copy.Layers[0], -3, -1) == 2 && Tilemap::GetTile(copy.Layers[0], 2, -1) == 2
				&& Tilemap::GetTile(copy.Layers[0], 3, -1) == -1 && Tilemap::GetTile(copy.Layers[0], 100000, -250000) == 0, std::string("the ground's tiles, negative and far cells included, survive ") + how);
			Check(copy.Layers[1].Name == "Decor" && !copy.Layers[1].Visible && !copy.Layers[1].Collides && copy.Layers[1].Cells.size() == 2, std::string("the second layer and its flags survive ") + how);
		};
		verify(loaded, "a save");
		verify(Scene::Copy(loaded), "the copy Play starts from");
		Check(scene->GetAssetList().contains(tileSet), "Scene::GetAssetList lists the tile set, so an export packs it");

		// Drawing: only what the view can see, one item per visible layer, sorted like sprites.
		auto& ground = scene->FindEntityByName("Map").GetComponent<TilemapComponent>();
		ground.Layers[1].Visible = true;
		Ref<Scene> view = Ref<Scene>::Create();
		Entity cameraEntity = view->CreateEntity("Camera");
		auto& cameraComponent = cameraEntity.AddComponent<CameraComponent>();
		cameraComponent.Camera.SetOrthographic(10.0f, -100.0f, 100.0f);
		cameraComponent.Camera.SetViewportSize(1600, 900);
		Entity sprite = view->CreateEntity("Sprite");
		sprite.AddComponent<SpriteRendererComponent>(glm::vec4(1.0f)).SortingLayer = 4;
		sprite.GetComponent<SpriteRendererComponent>().OrderInLayer = 5;
		Entity viewMap = view->CreateEntity("Map");
		viewMap.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 0.0f };
		viewMap.AddComponent<TilemapComponent>() = ground;
		viewMap.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 0.0f };
		FrameRenderPacket packet;
		packet.Overlay2DViewProjection = cameraComponent.Camera.GetProjectionMatrix();
		view->CaptureDraw2D(packet);
		// The camera is not SolidBackground: sprite + the tilemap's layers. The far tile is culled, and so is a layer with nothing in view.
		size_t tileItems = 0, tilesDrawn = 0;
		int tileItemIndex = -1, spriteItemIndex = -1;
		for (size_t i = 0; i < packet.Draw2D.size(); i++)
		{
			if (packet.Draw2D[i].Type == FrameRenderPacket::Draw2DItem::Kind::Tiles)
			{
				tileItems++;
				tilesDrawn += packet.Draw2D[i].Tiles.size();
				tileItemIndex = static_cast<int>(i);
			}
			else
				spriteItemIndex = static_cast<int>(i);
		}
		Check(tileItems == 2 && tilesDrawn == 6 + 2, std::format("two layers draw ({} items), 6 ground tiles and 2 decor tiles, the far tile culled ({} tiles)", tileItems, tilesDrawn));
		Check(spriteItemIndex > tileItemIndex, "the sprite (order 5) draws after the tilemap (order 3) on the same layer");
		if (tileItems == 2)
		{
			const auto& first = packet.Draw2D[0];
			const glm::vec3 corner = glm::vec3(first.Transform * glm::vec4(0.5f, 0.5f, 0.0f, 1.0f));
			Check(Near(corner.x, 0.25f) && Near(corner.y, 0.25f), "an item's transform scales a unit quad to one 0.5 unit cell");
			Check(first.PointFilter, "the point filter reaches the draw item");
		}
		viewMap.GetComponent<TilemapComponent>().Layers[1].Visible = false;
		FrameRenderPacket hidden;
		hidden.Overlay2DViewProjection = packet.Overlay2DViewProjection;
		view->CaptureDraw2D(hidden);
		size_t hiddenTiles = 0;
		for (const auto& item : hidden.Draw2D)
			hiddenTiles += item.Type == FrameRenderPacket::Draw2DItem::Kind::Tiles ? item.Tiles.size() : 0;
		Check(hiddenTiles == 6, "a hidden layer draws nothing");

		// A big map draws only the visible part, by walking the visible cells rather than every tile.
		Entity big = view->CreateEntity("Big");
		big.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 5.0f };
		auto& bigMap = big.AddComponent<TilemapComponent>();
		bigMap.TileSet = tileSet;
		bigMap.CellSize = { 1.0f, 1.0f };
		Tilemap::FillRect(bigMap.Layers[0], { 0, 0, 40, 40 }, 1);
		FrameRenderPacket bigPacket;
		bigPacket.Overlay2DViewProjection = packet.Overlay2DViewProjection;
		view->CaptureDraw2D(bigPacket);
		size_t bigTiles = 0;
		for (const auto& item : bigPacket.Draw2D)
			bigTiles = std::max(bigTiles, item.Type == FrameRenderPacket::Draw2DItem::Kind::Tiles ? item.Tiles.size() : size_t{ 0 });
		Tilemap::CellRect seenCells;
		Tilemap::ComputeVisibleCells(packet.Overlay2DViewProjection, glm::mat4(1.0f), { 1.0f, 1.0f }, seenCells);
		size_t expected = 0;
		for (int32_t y = 0; y < 40; y++)
			for (int32_t x = 0; x < 40; x++)
				expected += seenCells.Contains(x, y) ? 1 : 0;
		Check(bigTiles == expected && bigTiles > 20 && bigTiles < 200, std::format("a 40 x 40 map draws the {} tiles in view (of 1600)", bigTiles));
	}

	void CheckTilemapPhysics(const std::filesystem::path& root)
	{
		// Reuses the project the previous check made active; the tile set is the sheet it imported.
		const AssetHandle tileSet = AssetManager::ImportAsset("Dungeon.lsheet");
		Ref<Scene> scene = Ref<Scene>::Create();
		Entity map = AddBody(*scene, "Map", { 0.0f, 0.0f }, RigidBody2DComponent::Type::Static);
		auto& tilemap = map.AddComponent<TilemapComponent>();
		tilemap.TileSet = tileSet;
		tilemap.CellSize = { 1.0f, 1.0f };
		Tilemap::FillRect(tilemap.Layers[0], { -4, -1, 9, 1 }, 2);   // a solid floor, top at y = 0
		Tilemap::FillRect(tilemap.Layers[0], { 6, -1, 3, 1 }, 1);    // scenery: no collision
		tilemap.Layers.push_back(TilemapLayer("Soft"));
		tilemap.Layers[1].Collides = false;
		Tilemap::FillRect(tilemap.Layers[1], { 20, -1, 4, 1 }, 0);   // solid tiles on a layer that does not collide
		Entity onFloor = AddBody(*scene, "OnFloor", { 0.0f, 3.0f }, RigidBody2DComponent::Type::Dynamic);
		onFloor.AddComponent<CircleCollider2DComponent>().Radius = 0.25f;
		Entity overScenery = AddBody(*scene, "OverScenery", { 7.5f, 3.0f }, RigidBody2DComponent::Type::Dynamic);
		overScenery.AddComponent<CircleCollider2DComponent>().Radius = 0.25f;
		Entity overSoft = AddBody(*scene, "OverSoft", { 22.0f, 3.0f }, RigidBody2DComponent::Type::Dynamic);
		overSoft.AddComponent<CircleCollider2DComponent>().Radius = 0.25f;

		PhysicsScene2D physics(scene.Raw());
		physics.Start();
		PhysicsScene2D::RayHit hit;
		Check(physics.RayCast({ 0.5f, 5.0f }, { 0.0f, -1.0f }, 10.0f, 0, false, hit) && hit.EntityID == map.GetUUID() && Near(hit.Point.y, 0.0f, 0.01f), "the solid tiles are one collider at the right height");
		Check(!physics.RayCast({ 7.5f, 2.0f }, { 0.0f, -1.0f }, 10.0f, 0, false, hit), "scenery tiles have no collider");
		Check(!physics.RayCast({ 22.0f, 2.0f }, { 0.0f, -1.0f }, 10.0f, 0, false, hit), "a layer that does not collide has no collider");
		Step(physics, 240);
		Check(Near(YOf(onFloor), 0.25f, 0.05f), std::format("a ball lands on the solid floor tiles (y = {:.3f})", YOf(onFloor)));
		Check(YOf(overScenery) < -10.0f && YOf(overSoft) < -10.0f, "balls over scenery tiles and over a non-colliding layer fall through");

		// Tiles change while it plays: the shapes follow.
		auto& live = map.GetComponent<TilemapComponent>();
		for (int32_t x = -4; x <= 4; x++)
			Tilemap::SetTile(live.Layers[0], x, -1, -1);
		physics.RebuildTilemap(map);
		Check(!physics.RayCast({ 0.5f, 5.0f }, { 0.0f, -1.0f }, 10.0f, 0, false, hit), "after the floor is erased and the tilemap rebuilt, nothing is there");
		Tilemap::FillRect(live.Layers[0], { -4, 4, 9, 1 }, 0);
		physics.RebuildTilemap(map);
		Check(physics.RayCast({ 0.5f, 8.0f }, { 0.0f, -1.0f }, 10.0f, 0, false, hit) && Near(hit.Point.y, 5.0f, 0.01f), "and tiles put in later are solid");

		// The entity's scale stretches the cells: scale 2 makes the 1-unit cell 2 units.
		Ref<Scene> scaled = Ref<Scene>::Create();
		Entity bigCells = AddBody(*scaled, "BigCells", { 0.0f, 0.0f }, RigidBody2DComponent::Type::Static);
		bigCells.GetComponent<TransformComponent>().Scale = { 2.0f, 2.0f, 1.0f };
		auto& bigTilemap = bigCells.AddComponent<TilemapComponent>();
		bigTilemap.TileSet = tileSet;
		Tilemap::SetTile(bigTilemap.Layers[0], 1, 1, 0); // cell (1,1) spans (2,2)-(4,4)
		PhysicsScene2D scaledPhysics(scaled.Raw());
		scaledPhysics.Start();
		Check(scaledPhysics.RayCast({ 3.0f, 10.0f }, { 0.0f, -1.0f }, 20.0f, 0, false, hit) && Near(hit.Point.y, 4.0f, 0.01f), "scaling the entity scales the tiles' collision");
		Check(!scaledPhysics.RayCast({ 5.0f, 10.0f }, { 0.0f, -1.0f }, 20.0f, 0, false, hit), "and only the one tile is there");

		// Without a Rigidbody 2D a tilemap draws but does not collide.
		Ref<Scene> bodiless = Ref<Scene>::Create();
		Entity decor = bodiless->CreateEntity("Decor");
		auto& decorMap = decor.AddComponent<TilemapComponent>();
		decorMap.TileSet = tileSet;
		Tilemap::FillRect(decorMap.Layers[0], { 0, 0, 4, 1 }, 0);
		PhysicsScene2D bodilessPhysics(bodiless.Raw());
		bodilessPhysics.Start();
		Check(!bodilessPhysics.RayCast({ 1.5f, 5.0f }, { 0.0f, -1.0f }, 20.0f, 0, false, hit), "a tilemap with no Rigidbody 2D has no collision");
		(void)root;
	}

	// ---- M3 Game UI: layout math, scene components, drawing --------------------------------------

	void CheckUILayout()
	{
		using namespace Lux::UI;

		// Resolution scaling.
		const glm::vec2 reference{ 1920.0f, 1080.0f };
		Check(Near(CanvasScale(ScaleMode::ScaleWithScreen, reference, 1.0f, 0.5f, { 1920.0f, 1080.0f }), 1.0f), "a screen at the reference resolution scales by 1");
		Check(Near(CanvasScale(ScaleMode::ScaleWithScreen, reference, 1.0f, 0.5f, { 3840.0f, 2160.0f }), 2.0f), "a screen twice the reference scales by 2");
		Check(Near(CanvasScale(ScaleMode::ScaleWithScreen, reference, 1.0f, 0.0f, { 960.0f, 2160.0f }), 0.5f), "Match 0 follows the width");
		Check(Near(CanvasScale(ScaleMode::ScaleWithScreen, reference, 1.0f, 1.0f, { 960.0f, 2160.0f }), 2.0f), "Match 1 follows the height");
		Check(Near(CanvasScale(ScaleMode::ScaleWithScreen, reference, 1.0f, 0.5f, { 960.0f, 2160.0f }), 1.0f), "Match 0.5 of a halved width and a doubled height cancels out (log blend)");
		Check(Near(CanvasScale(ScaleMode::FitInside, reference, 1.0f, 0.5f, { 3840.0f, 1080.0f }), 1.0f), "FitInside takes the smaller scale");
		Check(Near(CanvasScale(ScaleMode::FillScreen, reference, 1.0f, 0.5f, { 3840.0f, 1080.0f }), 2.0f), "FillScreen takes the larger scale");
		Check(Near(CanvasScale(ScaleMode::ConstantPixelSize, reference, 1.5f, 0.5f, { 100.0f, 100.0f }), 1.5f), "ConstantPixelSize ignores the screen");
		const Rect canvas = CanvasRect({ 3840.0f, 2160.0f }, 2.0f);
		Check(canvas.Max == glm::vec2(1920.0f, 1080.0f), "the canvas rectangle is the screen over the scale");

		// Anchors.
		const Rect parent{ { 100.0f, 50.0f }, { 500.0f, 250.0f } }; // 400 x 200
		RectTransform centered; // anchors at the middle, +-50
		Check(ResolveRect(centered, parent) == Rect{ { 250.0f, 100.0f }, { 350.0f, 200.0f } }, "centered anchors keep a fixed size at the middle");
		RectTransform stretched;
		stretched.AnchorMin = { 0.0f, 0.0f };
		stretched.AnchorMax = { 1.0f, 1.0f };
		stretched.OffsetMin = { 10.0f, 20.0f };
		stretched.OffsetMax = { -30.0f, -40.0f };
		Check(ResolveRect(stretched, parent) == Rect{ { 110.0f, 70.0f }, { 470.0f, 210.0f } }, "stretched anchors make the offsets margins");
		RectTransform corner;
		corner.AnchorMin = corner.AnchorMax = { 1.0f, 0.0f };
		corner.OffsetMin = { -110.0f, 10.0f };
		corner.OffsetMax = { -10.0f, 60.0f };
		Check(ResolveRect(corner, parent) == Rect{ { 390.0f, 60.0f }, { 490.0f, 110.0f } }, "a bottom-right anchor keeps its distance from the corner");

		// Stacks.
		const Rect content{ { 0.0f, 0.0f }, { 300.0f, 100.0f } };
		std::vector<LayoutChild> three(3);
		for (LayoutChild& child : three)
			child.PreferredSize = { 60.0f, 40.0f };
		LayoutGroup row;
		row.Kind = LayoutKind::Horizontal;
		row.Spacing = 10.0f;
		row.Padding = { 5.0f, 5.0f, 5.0f, 5.0f };
		std::vector<Rect> rects = LayoutChildren(row, content, three);
		Check(rects[0] == Rect{ { 5.0f, 55.0f }, { 65.0f, 95.0f } } && rects[1].Min.x == 75.0f && rects[2].Max.x == 205.0f, "a row runs left to right with spacing and padding, aligned to the top");
		row.Align = LayoutAlign::Center;
		Check(LayoutChildren(row, content, three)[0].Min.y == 30.0f, "Center aligns a row's children vertically");
		row.ExpandChildren = true;
		rects = LayoutChildren(row, content, three);
		Check(Near(rects[0].Size().x, 60.0f + (290.0f - 180.0f - 20.0f) / 3.0f) && Near(rects[2].Max.x, 295.0f), "ExpandChildren shares the spare length equally and fills the content");
		LayoutGroup column;
		column.Kind = LayoutKind::Vertical;
		column.Spacing = 5.0f;
		rects = LayoutChildren(column, { { 0.0f, 0.0f }, { 100.0f, 300.0f } }, three);
		Check(rects[0] == Rect{ { 0.0f, 260.0f }, { 60.0f, 300.0f } } && rects[1].Max.y == 255.0f && rects[2].Min.y == 170.0f, "a column runs top to bottom");
		column.Align = LayoutAlign::End;
		Check(LayoutChildren(column, { { 0.0f, 0.0f }, { 100.0f, 300.0f } }, three)[0].Min.x == 40.0f, "End aligns a column's children to the right");

		// Grids.
		LayoutGroup grid;
		grid.Kind = LayoutKind::Grid;
		grid.Columns = 2;
		grid.CellSize = { 50.0f, 30.0f };
		grid.Spacing = 10.0f;
		rects = LayoutChildren(grid, { { 0.0f, 0.0f }, { 200.0f, 200.0f } }, three);
		Check(rects[0] == Rect{ { 0.0f, 170.0f }, { 50.0f, 200.0f } } && rects[1].Min.x == 60.0f && rects[2] == Rect{ { 0.0f, 130.0f }, { 50.0f, 160.0f } }, "a grid fills rows from the top left");
		Check(PreferredGroupSize(grid, three) == glm::vec2(110.0f, 70.0f), "a grid's preferred size covers its cells and spacing");
		Check(PreferredGroupSize(row, three) == glm::vec2(180.0f + 20.0f + 10.0f, 40.0f + 10.0f), "a row's preferred size adds its children, spacing and padding");
		Check(LayoutChildren(LayoutGroup{}, content, three)[0] == Rect{}, "no layout leaves the child's own rectangle to the caller");
		Check(LayoutChildren(row, content, {}).empty(), "a group without children lays out nothing");
	}

	// ---- M3 Game UI: widgets under simulated pointer and keyboard input ----------------------------

	void CheckUIWidgets(const std::filesystem::path& root)
	{
		Ref<Scene> scene = Ref<Scene>::Create();
		Entity canvas = scene->CreateEntity("Canvas");
		canvas.AddComponent<UICanvasComponent>().ScaleMode = UI::ScaleMode::ConstantPixelSize;
		const glm::vec2 screen{ 1000.0f, 500.0f };

		const auto makeWidget = [&](const char* name, Entity parent, glm::vec2 min, glm::vec2 max, UIWidgetKind kind)
		{
			Entity entity = scene->CreateEntity(name);
			entity.SetParent(parent);
			auto& element = entity.AddComponent<UIElementComponent>();
			element.Rect.AnchorMin = element.Rect.AnchorMax = { 0.0f, 0.0f };
			element.Rect.OffsetMin = min;
			element.Rect.OffsetMax = max;
			entity.AddComponent<UIImageComponent>();
			entity.AddComponent<UIWidgetComponent>().Kind = kind;
			return entity;
		};
		Entity button = makeWidget("Button", canvas, { 100.0f, 100.0f }, { 200.0f, 150.0f }, UIWidgetKind::Button);
		Entity toggle = makeWidget("Toggle", canvas, { 300.0f, 100.0f }, { 340.0f, 140.0f }, UIWidgetKind::Toggle);
		Entity slider = makeWidget("Slider", canvas, { 400.0f, 100.0f }, { 600.0f, 120.0f }, UIWidgetKind::Slider);
		Entity whole = makeWidget("Whole", canvas, { 400.0f, 150.0f }, { 600.0f, 170.0f }, UIWidgetKind::Slider);
		whole.GetComponent<UIWidgetComponent>().MaxValue = 10.0f;
		whole.GetComponent<UIWidgetComponent>().WholeNumbers = true;
		Entity disabled = makeWidget("Disabled", canvas, { 700.0f, 100.0f }, { 800.0f, 150.0f }, UIWidgetKind::Button);
		disabled.GetComponent<UIWidgetComponent>().Interactable = false;

		Entity list = makeWidget("List", canvas, { 100.0f, 200.0f }, { 300.0f, 300.0f }, UIWidgetKind::ScrollList);
		list.GetComponent<UIElementComponent>().Layout.Kind = UI::LayoutKind::Vertical;
		list.GetComponent<UIElementComponent>().Layout.Spacing = 10.0f;
		std::vector<Entity> rows;
		for (int i = 0; i < 5; i++)
			rows.push_back(makeWidget(("Row" + std::to_string(i)).c_str(), list, { 0.0f, 0.0f }, { 160.0f, 40.0f }, UIWidgetKind::Button));

		Entity field = makeWidget("Field", canvas, { 500.0f, 300.0f }, { 700.0f, 340.0f }, UIWidgetKind::TextInput);
		field.AddComponent<UITextComponent>();
		field.GetComponent<UIWidgetComponent>().MaxLength = 12;

		UI::InteractionState state;
		const auto step = [&](glm::vec2 at, bool down, bool present = true, float scroll = 0.0f, const std::u32string& typed = {}, UI::Keys keys = {})
		{
			UI::FrameInput input;
			input.Mouse.Position = at;
			input.Mouse.Down = down;
			input.Mouse.Present = present;
			input.Mouse.Scroll = scroll;
			input.Typed = typed;
			input.Edit = keys;
			input.DeltaTime = 0.1f;
			return UI::UpdateInteraction(*scene, state, screen, input);
		};
		const auto widgetOf = [](Entity entity) -> UIWidgetComponent& { return entity.GetComponent<UIWidgetComponent>(); };

		// Hover and click.
		std::vector<UI::Event> events = step({ 150.0f, 125.0f }, false);
		Check(widgetOf(button).Hovered && !widgetOf(toggle).Hovered && events.empty(), "the widget under the pointer is hovered");
		events = step({ 150.0f, 125.0f }, true);
		Check(widgetOf(button).Pressed && events.empty(), "pressing a button marks it pressed and does not click yet");
		events = step({ 150.0f, 125.0f }, false);
		Check(events.size() == 1 && events[0].Kind == UI::EventKind::Click && events[0].Element == button.GetUUID() && !widgetOf(button).Pressed, "releasing on the button clicks it");
		step({ 150.0f, 125.0f }, true);
		step({ 250.0f, 125.0f }, true);
		events = step({ 250.0f, 125.0f }, false);
		Check(events.empty(), "dragging off a button before releasing does not click it");
		step({ 750.0f, 125.0f }, true);
		events = step({ 750.0f, 125.0f }, false);
		Check(events.empty() && widgetOf(disabled).Hovered, "a disabled button is hovered but never clicks");
		events = step({ 150.0f, 125.0f }, false, false);
		Check(!widgetOf(button).Hovered, "a pointer outside the view hovers nothing");

		// Toggle.
		step({ 320.0f, 120.0f }, true);
		events = step({ 320.0f, 120.0f }, false);
		Check(widgetOf(toggle).Checked && events.size() == 1 && events[0].Kind == UI::EventKind::Toggled && events[0].Checked, "a click turns a toggle on");
		step({ 320.0f, 120.0f }, true);
		events = step({ 320.0f, 120.0f }, false);
		Check(!widgetOf(toggle).Checked && events.size() == 1 && !events[0].Checked, "and a second click turns it off");

		// Sliders: 200 wide, 20 high, so the handle is 10 wide and the track runs from x 405 to 595.
		events = step({ 500.0f, 110.0f }, true);
		Check(Near(widgetOf(slider).Value, 0.5f, 1e-3f) && events.size() == 1 && events[0].Kind == UI::EventKind::ValueChanged, "pressing the middle of a slider sets half its range");
		events = step({ 5000.0f, 110.0f }, true);
		Check(Near(widgetOf(slider).Value, 1.0f), "dragging past the end, even off the slider, clamps to the maximum");
		step({ 5000.0f, 110.0f }, false);
		step({ 100.0f, 110.0f }, false);
		Check(Near(widgetOf(slider).Value, 1.0f), "moving the pointer without the button does not move a slider");
		step({ 405.0f, 160.0f }, true);
		Check(Near(widgetOf(whole).Value, 0.0f), "a slider's left end is its minimum");
		step({ 500.0f, 160.0f }, true);
		Check(widgetOf(whole).Value == 5.0f, "a whole-number slider steps by 1");
		step({ 500.0f, 160.0f }, false);

		// A scroll list: 5 rows of 40 with 10 between make 240 of content in a 100 high list.
		std::vector<UI::ResolvedCanvas> resolved = UI::ResolveLayout(*scene, screen);
		const UI::ResolvedElement* listElement = nullptr;
		const UI::ResolvedElement* rowElement[5] = {};
		for (const UI::ResolvedElement& element : resolved[0].Elements)
		{
			if (element.Element == list)
				listElement = &element;
			for (int i = 0; i < 5; i++)
			{
				if (element.Element == rows[i])
					rowElement[i] = &element;
			}
		}
		Check(listElement && Near(listElement->ScrollRange, 140.0f), "a list's scroll range is its content minus its height");
		Check(rowElement[0] && rowElement[0]->Clipped && rowElement[0]->Clip == UI::Rect{ { 100.0f, 200.0f }, { 300.0f, 300.0f } } && rowElement[0]->ScreenRect == UI::Rect{ { 100.0f, 260.0f }, { 260.0f, 300.0f } }
			&& rowElement[4]->ScreenRect == UI::Rect{ { 100.0f, 60.0f }, { 260.0f, 100.0f } }, "rows are laid out from the list's top, clipped to the list");
		step({ 150.0f, 250.0f }, false, true, -1.0f);
		Check(Near(widgetOf(list).ScrollOffset, 60.0f), "a wheel notch down moves the content up by 60");
		step({ 150.0f, 250.0f }, false, true, -9.0f);
		Check(Near(widgetOf(list).ScrollOffset, 140.0f), "scrolling stops at the end of the content");
		step({ 150.0f, 250.0f }, false, true, 9.0f);
		Check(Near(widgetOf(list).ScrollOffset, 0.0f), "and at the start");
		step({ 700.0f, 400.0f }, false, true, -3.0f);
		Check(Near(widgetOf(list).ScrollOffset, 0.0f), "the wheel only scrolls the list under the pointer");
		widgetOf(list).ScrollOffset = 30.0f;
		step({ 150.0f, 195.0f }, false);
		Check(!widgetOf(rows[2]).Hovered, "the part of a row outside the list cannot be hovered");
		step({ 150.0f, 215.0f }, false);
		Check(widgetOf(rows[2]).Hovered && !widgetOf(list).Hovered, "the part inside can, and the row wins over the list behind it");
		step({ 150.0f, 215.0f }, true);
		events = step({ 150.0f, 215.0f }, false);
		Check(events.size() == 1 && events[0].Element == rows[2].GetUUID(), "a row in a scrolled list can be clicked");
		FrameRenderPacket packet;
		UI::CaptureDrawItems(UI::ResolveLayout(*scene, screen), screen, packet);
		bool outsideListDrawn = false;
		int rowQuads = 0;
		for (const auto& item : packet.UI)
		{
			const float low = item.Transform[3].y - item.Transform[1].y * 0.5f, high = item.Transform[3].y + item.Transform[1].y * 0.5f;
			if (std::abs(item.Transform[0].x - 160.0f) > 1e-3f) // a row is 160 wide, whole or cut at its top or bottom
				continue;
			rowQuads++;
			if (low < 200.0f - 1e-3f || high > 300.0f + 1e-3f)
				outsideListDrawn = true;
		}
		Check(rowQuads >= 2 && !outsideListDrawn, "the visible rows are drawn and no quad of them lies outside the list");

		// Text input.
		step({ 600.0f, 320.0f }, true);
		step({ 600.0f, 320.0f }, false);
		Check(widgetOf(field).Focused && state.Focus == field.GetUUID(), "clicking a text input focuses it");
		UI::Keys none;
		events = step({ 600.0f, 320.0f }, false, true, 0.0f, U"h\u00E9llo");
		const std::string typed = field.GetComponent<UITextComponent>().Text;
		Check(typed == "h\xC3\xA9llo" && events.size() == 1 && events[0].Kind == UI::EventKind::TextChanged, "typed characters are added, UTF-8 encoded");
		UI::Keys backspace;
		backspace.Backspace = true;
		step({ 600.0f, 320.0f }, false, true, 0.0f, {}, backspace);
		UI::Keys left;
		left.Left = true;
		step({ 600.0f, 320.0f }, false, true, 0.0f, {}, left);
		step({ 600.0f, 320.0f }, false, true, 0.0f, {}, left);
		step({ 600.0f, 320.0f }, false, true, 0.0f, {}, left);
		Check(field.GetComponent<UITextComponent>().Text == "h\xC3\xA9ll" && widgetOf(field).CaretIndex == 1, "Backspace removes the character before the caret and Left moves back over a whole character");
		step({ 600.0f, 320.0f }, false, true, 0.0f, U"X");
		Check(field.GetComponent<UITextComponent>().Text == "hX\xC3\xA9ll", "typing inserts at the caret");
		UI::Keys del;
		del.Delete = true;
		step({ 600.0f, 320.0f }, false, true, 0.0f, {}, del);
		Check(field.GetComponent<UITextComponent>().Text == "hXll", "Delete removes the character after the caret, a two-byte one whole");
		step({ 600.0f, 320.0f }, false, true, 0.0f, U"123456789012345");
		Check(field.GetComponent<UITextComponent>().Text.size() == 12, "Max Length caps the text");
		UI::Keys enter;
		enter.Enter = true;
		events = step({ 600.0f, 320.0f }, false, true, 0.0f, {}, enter);
		Check(events.size() == 1 && events[0].Kind == UI::EventKind::Submit, "Enter submits");
		UI::Keys escape;
		escape.Escape = true;
		step({ 600.0f, 320.0f }, false, true, 0.0f, {}, escape);
		Check(!widgetOf(field).Focused && state.Focus == 0, "Escape ends editing");
		step({ 600.0f, 320.0f }, false, true, 0.0f, U"Z");
		Check(field.GetComponent<UITextComponent>().Text.find('Z') == std::string::npos, "an unfocused input ignores typing");
		step({ 600.0f, 320.0f }, true);
		step({ 600.0f, 320.0f }, false);
		step({ 10.0f, 10.0f }, true);
		step({ 10.0f, 10.0f }, false);
		Check(!widgetOf(field).Focused, "clicking elsewhere ends editing");

		// Saved fields.
		widgetOf(slider).Value = 0.25f;
		widgetOf(list).ScrollOffset = 40.0f;
		widgetOf(field).Placeholder = "Name";
		widgetOf(toggle).Checked = true;
		widgetOf(disabled).AccentColor = { 1.0f, 0.0f, 0.0f, 1.0f };
		button.GetComponent<UIImageComponent>().SliceBorder = { 4.0f, 5.0f, 6.0f, 7.0f };
		button.GetComponent<UIImageComponent>().SliceScale = 2.0f;
		const std::filesystem::path file = root / "UI" / "Widgets.luxscene";
		std::filesystem::create_directories(file.parent_path());
		AssetReference::PathModeScope pathMode;
		Check(SceneSerializer(scene).Serialize(file), "a scene with widgets serializes");
		Ref<Scene> loaded = Ref<Scene>::Create();
		Check(SceneSerializer(loaded).Deserialize(file), "and loads back");
		const auto verify = [&](Ref<Scene> checked, const char* how)
		{
			const auto& s = checked->FindEntityByName("Slider").GetComponent<UIWidgetComponent>();
			const auto& w = checked->FindEntityByName("Whole").GetComponent<UIWidgetComponent>();
			const auto& l = checked->FindEntityByName("List").GetComponent<UIWidgetComponent>();
			const auto& f = checked->FindEntityByName("Field").GetComponent<UIWidgetComponent>();
			const auto& d = checked->FindEntityByName("Disabled").GetComponent<UIWidgetComponent>();
			Check(s.Kind == UIWidgetKind::Slider && Near(s.Value, 0.25f) && w.WholeNumbers && Near(w.MaxValue, 10.0f), std::string("slider value, range and steps survive ") + how);
			Check(l.Kind == UIWidgetKind::ScrollList && Near(l.ScrollOffset, 40.0f) && f.Placeholder == "Name" && f.MaxLength == 12, std::string("scroll offset, placeholder and max length survive ") + how);
			Check(checked->FindEntityByName("Toggle").GetComponent<UIWidgetComponent>().Checked && !d.Interactable && Near(d.AccentColor.r, 1.0f) && Near(d.AccentColor.g, 0.0f), std::string("toggle state, interactable and colors survive ") + how);
			const auto& image = checked->FindEntityByName("Button").GetComponent<UIImageComponent>();
			Check(image.SliceBorder == glm::vec4(4.0f, 5.0f, 6.0f, 7.0f) && Near(image.SliceScale, 2.0f), std::string("9-slice borders survive ") + how);
			Check(!s.Hovered && !s.Pressed && !s.Focused, std::string("runtime state is not saved with ") + how);
		};
		verify(loaded, "a save");
		verify(Scene::Copy(loaded), "the copy Play starts from");
	}

	// ---- M3 Game UI: keyboard and gamepad navigation ------------------------------------------------

	void CheckUINavigation()
	{
		Ref<Scene> scene = Ref<Scene>::Create();
		Entity canvas = scene->CreateEntity("Canvas");
		canvas.AddComponent<UICanvasComponent>().ScaleMode = UI::ScaleMode::ConstantPixelSize;
		const glm::vec2 screen{ 1000.0f, 500.0f };

		const auto makeWidget = [&](const char* name, Entity parent, glm::vec2 min, glm::vec2 max, UIWidgetKind kind)
		{
			Entity entity = scene->CreateEntity(name);
			entity.SetParent(parent);
			auto& element = entity.AddComponent<UIElementComponent>();
			element.Rect.AnchorMin = element.Rect.AnchorMax = { 0.0f, 0.0f };
			element.Rect.OffsetMin = min;
			element.Rect.OffsetMax = max;
			entity.AddComponent<UIImageComponent>();
			entity.AddComponent<UIWidgetComponent>().Kind = kind;
			return entity;
		};
		// A and F on the top row, B and C below, a slider and a text input at the bottom, a scroll list on the right.
		Entity a = makeWidget("A", canvas, { 100.0f, 300.0f }, { 200.0f, 340.0f }, UIWidgetKind::Button);
		Entity b = makeWidget("B", canvas, { 100.0f, 200.0f }, { 200.0f, 240.0f }, UIWidgetKind::Button);
		Entity c = makeWidget("C", canvas, { 300.0f, 200.0f }, { 400.0f, 240.0f }, UIWidgetKind::Button);
		Entity slider = makeWidget("Slider", canvas, { 100.0f, 100.0f }, { 300.0f, 120.0f }, UIWidgetKind::Slider);
		Entity field = makeWidget("Field", canvas, { 400.0f, 100.0f }, { 600.0f, 140.0f }, UIWidgetKind::TextInput);
		field.AddComponent<UITextComponent>();
		Entity toggle = makeWidget("Toggle", canvas, { 500.0f, 300.0f }, { 540.0f, 340.0f }, UIWidgetKind::Toggle);
		Entity bar = makeWidget("Bar", canvas, { 100.0f, 400.0f }, { 300.0f, 410.0f }, UIWidgetKind::ProgressBar);
		Entity list = makeWidget("List", canvas, { 700.0f, 100.0f }, { 900.0f, 200.0f }, UIWidgetKind::ScrollList);
		list.GetComponent<UIElementComponent>().Layout.Kind = UI::LayoutKind::Vertical;
		list.GetComponent<UIElementComponent>().Layout.Spacing = 10.0f;
		std::vector<Entity> rows;
		for (int i = 0; i < 5; i++)
			rows.push_back(makeWidget(("Row" + std::to_string(i)).c_str(), list, { 0.0f, 0.0f }, { 160.0f, 40.0f }, UIWidgetKind::Button));

		UI::InteractionState state;
		const auto step = [&](UI::Nav nav, glm::vec2 at = { -1.0f, -1.0f }, bool down = false)
		{
			UI::FrameInput input;
			input.Mouse.Position = at;
			input.Mouse.Present = at.x >= 0.0f;
			input.Mouse.Down = down;
			input.Navigate = nav;
			input.DeltaTime = 0.1f;
			return UI::UpdateInteraction(*scene, state, screen, input);
		};
		const auto navTo = [](bool UI::Nav::* key) { UI::Nav nav; nav.*key = true; return nav; };
		const auto widgetOf = [](Entity entity) -> UIWidgetComponent& { return entity.GetComponent<UIWidgetComponent>(); };

		step({});
		Check(state.Focus == 0 && !state.FocusVisible, "with no key pressed nothing is focused");
		step(navTo(&UI::Nav::Down));
		Check(state.Focus == a.GetUUID() && widgetOf(a).Focused && widgetOf(a).FocusRing && state.FocusVisible, "the first key focuses the first widget and shows the ring");
		step(navTo(&UI::Nav::Down));
		Check(state.Focus == b.GetUUID() && !widgetOf(a).Focused, "Down moves to the widget below");
		step(navTo(&UI::Nav::Right));
		Check(state.Focus == c.GetUUID(), "Right moves to the widget on the right");
		step(navTo(&UI::Nav::Up));
		Check(state.Focus == toggle.GetUUID(), "Up picks the nearest widget above, here the toggle");
		step(navTo(&UI::Nav::Next));
		Check(state.Focus == rows[0].GetUUID(), "Next goes to the next widget in the tree, skipping the list and the progress bar");
		step(navTo(&UI::Nav::Previous));
		Check(state.Focus == toggle.GetUUID(), "Previous goes back");
		step(navTo(&UI::Nav::Next));
		step(navTo(&UI::Nav::Next));
		step(navTo(&UI::Nav::Next));
		step(navTo(&UI::Nav::Next));
		step(navTo(&UI::Nav::Next));
		Check(state.Focus == rows[4].GetUUID() && Near(widgetOf(list).ScrollOffset, 140.0f), "focusing a row below the list scrolls it into view");
		step(navTo(&UI::Nav::Next));
		Check(state.Focus == a.GetUUID(), "Next wraps from the last widget to the first");
		step(navTo(&UI::Nav::Previous));
		Check(state.Focus == rows[4].GetUUID() && Near(widgetOf(list).ScrollOffset, 140.0f), "Previous wraps back");
		step(navTo(&UI::Nav::Up));
		Check(state.Focus == rows[3].GetUUID() && Near(widgetOf(list).ScrollOffset, 140.0f), "Up inside the list moves one row");
		for (int i = 0; i < 3; i++)
			step(navTo(&UI::Nav::Up));
		Check(state.Focus == rows[0].GetUUID() && Near(widgetOf(list).ScrollOffset, 0.0f), "and back to the top scrolls it up again");

		// Confirm and Cancel.
		state.Focus = b.GetUUID();
		std::vector<UI::Event> events = step(navTo(&UI::Nav::Confirm));
		Check(events.size() == 1 && events[0].Kind == UI::EventKind::Click && events[0].Element == b.GetUUID(), "Confirm clicks the focused button");
		state.Focus = toggle.GetUUID();
		events = step(navTo(&UI::Nav::Confirm));
		Check(widgetOf(toggle).Checked && events.size() == 1 && events[0].Kind == UI::EventKind::Toggled && events[0].Checked, "Confirm flips the focused toggle");
		events = step(navTo(&UI::Nav::Cancel));
		Check(events.size() == 1 && events[0].Kind == UI::EventKind::Cancel && events[0].Element == toggle.GetUUID(), "Cancel reports the focused widget");

		// A slider takes Left and Right itself.
		state.Focus = slider.GetUUID();
		events = step(navTo(&UI::Nav::Right));
		Check(state.Focus == slider.GetUUID() && Near(widgetOf(slider).Value, 0.05f) && events.size() == 1 && events[0].Kind == UI::EventKind::ValueChanged, "Right on a slider raises it by a twentieth of its range");
		step(navTo(&UI::Nav::Left));
		step(navTo(&UI::Nav::Left));
		Check(Near(widgetOf(slider).Value, 0.0f), "Left lowers it and it stops at the minimum");
		widgetOf(slider).WholeNumbers = true;
		widgetOf(slider).MaxValue = 10.0f;
		step(navTo(&UI::Nav::Right));
		step(navTo(&UI::Nav::Right));
		Check(widgetOf(slider).Value == 2.0f, "a whole-number slider moves by 1");
		step(navTo(&UI::Nav::Up));
		Check(state.Focus != slider.GetUUID(), "Up and Down still leave a slider");

		// A text input keeps Left, Right and Confirm for editing.
		state.Focus = field.GetUUID();
		events = step(navTo(&UI::Nav::Left));
		events = step(navTo(&UI::Nav::Confirm));
		Check(state.Focus == field.GetUUID() && events.empty(), "Left and Confirm do nothing to the focus while a text input is focused");
		events = step(navTo(&UI::Nav::Cancel));
		Check(state.Focus == field.GetUUID() && events.empty(), "and Cancel is left to the input (Escape ends editing there)");
		step(navTo(&UI::Nav::Up));
		Check(state.Focus != field.GetUUID(), "Up leaves it");

		// Disabled widgets are skipped; Start Focused picks the first target; the mouse hides the ring.
		widgetOf(b).Interactable = false;
		state.Focus = a.GetUUID();
		step(navTo(&UI::Nav::Down));
		Check(state.Focus != b.GetUUID(), "a disabled widget is never focused by navigation");
		widgetOf(b).Interactable = true;
		UI::InteractionState fresh;
		state = fresh;
		widgetOf(toggle).StartFocused = true;
		step(navTo(&UI::Nav::Down));
		Check(state.Focus == toggle.GetUUID(), "the first key focuses the Start Focused widget");
		step({}, { 150.0f, 220.0f }, true);
		step({}, { 150.0f, 220.0f }, false);
		Check(state.Focus == b.GetUUID() && !state.FocusVisible && !widgetOf(b).FocusRing && widgetOf(b).Focused, "a mouse click moves the focus without a ring");
		step(navTo(&UI::Nav::Right));
		Check(state.FocusVisible && widgetOf(c).Focused && widgetOf(c).FocusRing, "and the next key brings the ring back");

		// The ring is four quads around the widget.
		const auto drawn = [&]()
		{
			FrameRenderPacket packet;
			UI::CaptureDrawItems(UI::ResolveLayout(*scene, screen), screen, packet);
			return packet.UI.size();
		};
		const size_t withRing = drawn();
		widgetOf(c).FocusRing = false;
		Check(withRing == drawn() + 4, "a focus ring adds four quads");

		// Saved: Start Focused only.
		Check(widgetOf(toggle).StartFocused, "Start Focused is kept in the component");
		Ref<Scene> copy = Scene::Copy(scene);
		Check(copy->FindEntityByName("Toggle").GetComponent<UIWidgetComponent>().StartFocused, "and in the copy Play starts from");
	}

	// ---- M3 Game UI: which captions show ------------------------------------------------------------

	void CheckUICaptions()
	{
		const auto subtitle = [](const char* speaker, const char* text, bool offScreen = false, glm::vec3 position = { 0.0f, 0.0f, 0.0f })
		{
			AccessibleSubtitle entry;
			entry.Event.SpeakerName = speaker;
			entry.Event.Text = text;
			entry.Event.IsOffScreen = offScreen;
			entry.Event.SpeakerPosition = position;
			entry.SpeakerColor = { 0.5f, 0.6f, 0.7f, 1.0f };
			return entry;
		};
		const auto oneLine = [](const std::string&) { return 1; };
		const auto twoLines = [](const std::string& text) { return text.size() > 20 ? 2 : 1; };
		const auto ahead = [](const glm::vec3& position) { return position; }; // the world position is the direction here

		AudioAccessibilityPreferences preferences;
		preferences.MaxLines = 3;
		std::vector<AccessibleSubtitle> subtitles = { subtitle("Ann", "one"), subtitle("Bob", "two"), subtitle("Cy", "three"), subtitle("Di", "four") };
		auto shown = UI::CollectCaptions(subtitles, preferences, oneLine, ahead);
		Check(shown.size() == 3 && shown[0].Text == "Bob: two" && shown[2].Text == "Di: four", "the newest entries that fit in Maximum lines show, oldest first");
		Check(Near(shown[0].Color.b, 0.7f), "an entry keeps its speaker's color");

		preferences.SpeakerNames = false;
		Check(UI::CollectCaptions(subtitles, preferences, oneLine, ahead)[0].Text == "two", "speaker names can be turned off");
		preferences.SpeakerNames = true;

		subtitles.push_back(subtitle("Ed", "a long line that wraps over two lines"));
		shown = UI::CollectCaptions(subtitles, preferences, twoLines, ahead);
		Check(shown.size() == 2 && shown[1].Lines == 2 && shown[0].Text == "Di: four", "a two-line entry takes two of the three lines");
		preferences.MaxLines = 1;
		shown = UI::CollectCaptions(subtitles, preferences, twoLines, ahead);
		Check(shown.size() == 1 && shown[0].Text.starts_with("Ed:"), "the newest entry shows even when it is longer than Maximum lines");

		preferences.MaxLines = 3;
		std::vector<AccessibleSubtitle> distant = { subtitle("", "left", true, { -1.0f, 0.0f, 0.0f }), subtitle("", "right", true, { 1.0f, 0.0f, 0.0f }),
			subtitle("", "behind", true, { 0.0f, 0.0f, -1.0f }), subtitle("", "ahead", true, { 0.0f, 0.0f, 1.0f }), subtitle("", "seen", false, { 1.0f, 0.0f, 0.0f }) };
		preferences.MaxLines = 5;
		shown = UI::CollectCaptions(distant, preferences, oneLine, ahead);
		Check(shown[0].Text == "< left" && shown[1].Text == "> right" && shown[2].Text == "[behind] behind" && shown[3].Text == "[ahead] ahead" && shown[4].Text == "seen",
			"an off-screen speaker gets an arrow or a behind/ahead tag, an on-screen one none");
		preferences.DirectionIndicators = false;
		Check(UI::CollectCaptions(distant, preferences, oneLine, ahead)[0].Text == "left", "direction indicators can be turned off");
		Check(UI::CollectCaptions({}, preferences, oneLine, ahead).empty(), "no subtitles, no captions");
	}

	void CheckUIScene(const std::filesystem::path& root)
	{
		Ref<Scene> scene = Ref<Scene>::Create();
		Entity canvas = scene->CreateEntity("Canvas");
		auto& canvasComponent = canvas.AddComponent<UICanvasComponent>();
		canvasComponent.ReferenceResolution = { 1000.0f, 500.0f };
		canvasComponent.SortOrder = 2;

		Entity panel = scene->CreateEntity("Panel");
		panel.SetParent(canvas);
		auto& panelElement = panel.AddComponent<UIElementComponent>();
		panelElement.Rect.AnchorMin = { 0.0f, 0.0f };
		panelElement.Rect.AnchorMax = { 1.0f, 1.0f };
		panelElement.Rect.OffsetMin = { 100.0f, 50.0f };
		panelElement.Rect.OffsetMax = { -100.0f, -50.0f };
		panelElement.Layout.Kind = UI::LayoutKind::Vertical;
		panelElement.Layout.Spacing = 10.0f;
		panel.AddComponent<UIImageComponent>().Color = { 0.2f, 0.4f, 0.6f, 0.8f };

		Entity first = scene->CreateEntity("First");
		first.SetParent(panel);
		first.AddComponent<UIElementComponent>().Rect.OffsetMax = { 50.0f, 25.0f }; // a 100 x 75 ask
		first.AddComponent<UIImageComponent>();
		Entity hidden = scene->CreateEntity("Hidden");
		hidden.SetParent(panel);
		hidden.AddComponent<UIElementComponent>().Visible = false;
		hidden.AddComponent<UIImageComponent>();
		Entity second = scene->CreateEntity("Second");
		second.SetParent(panel);
		second.AddComponent<UIElementComponent>();
		second.AddComponent<UIImageComponent>();

		// Layout: a 2000 x 1000 screen scales the 1000 x 500 reference by 2. A hidden element takes no room.
		const glm::vec2 screen{ 2000.0f, 1000.0f };
		std::vector<UI::ResolvedCanvas> canvases = UI::ResolveLayout(*scene, screen);
		Check(canvases.size() == 1 && Near(canvases[0].Scale, 2.0f) && canvases[0].Elements.size() == 3, "one canvas resolves with its panel and two visible children");
		if (canvases.size() == 1 && canvases[0].Elements.size() == 3)
		{
			const auto& elements = canvases[0].Elements;
			Check(elements[0].Element == panel && Near(elements[0].ScreenRect.Min.x, 200.0f) && Near(elements[0].ScreenRect.Max.y, 900.0f), "the stretched panel is inset by its margins, in pixels");
			Check(elements[1].Element == first && elements[2].Element == second, "children follow hierarchy order and a hidden element is skipped");
			// Panel content in UI units is 800 x 400 from (100, 50); the first child asks 100 x 75 and sits at the top.
			Check(Near(elements[1].ScreenRect.Size().x, 200.0f) && Near(elements[1].ScreenRect.Size().y, 150.0f) && Near(elements[1].ScreenRect.Max.y, 900.0f), "the column places the first child at the panel top, scaled to pixels");
			Check(Near(elements[2].ScreenRect.Max.y, 900.0f - (75.0f + 10.0f) * 2.0f), "the second child sits below the first with the spacing");
		}

		// Drawing.
		FrameRenderPacket packet;
		UI::CaptureDrawItems(canvases, screen, packet);
		Check(packet.UI.size() == 3 && packet.UIScreenSize == screen, "three images produce three draw items for the screen");
		if (packet.UI.size() == 3)
		{
			Check(packet.UI[0].Type == FrameRenderPacket::Draw2DItem::Kind::Quad && Near(packet.UI[0].Color.b, 0.6f), "the panel is a flat quad of its color");
			Check(Near(packet.UI[0].Transform[3].x, 1000.0f) && Near(packet.UI[0].Transform[0].x, 1600.0f), "a quad's transform is its center and size in pixels");
		}

		canvasComponent.Enabled = false;
		Check(UI::ResolveLayout(*scene, screen).empty(), "a disabled canvas draws nothing");
		canvasComponent.Enabled = true;
		Check(UI::ResolveLayout(*scene, { 0.0f, 0.0f }).empty(), "a zero-size screen resolves nothing");

		// Serialization: the file, a load, and the copy Play starts from.
		const std::filesystem::path file = root / "UI" / "Menu.luxscene";
		std::filesystem::create_directories(file.parent_path());
		AssetReference::PathModeScope pathMode;
		Check(SceneSerializer(scene).Serialize(file), "a scene with a canvas serializes");
		SceneSerializer(scene).Serialize(root / "UI" / "Menu2.luxscene");
		Check(ReadText(file) == ReadText(root / "UI" / "Menu2.luxscene"), "saving twice gives the same file");
		Ref<Scene> loaded = Ref<Scene>::Create();
		Check(SceneSerializer(loaded).Deserialize(file), "and loads back");
		const auto verify = [&](Ref<Scene> checked, const char* how)
		{
			const auto& restoredCanvas = checked->FindEntityByName("Canvas").GetComponent<UICanvasComponent>();
			Check(restoredCanvas.ReferenceResolution == glm::vec2(1000.0f, 500.0f) && restoredCanvas.SortOrder == 2 && restoredCanvas.ScaleMode == UI::ScaleMode::ScaleWithScreen, std::string("canvas settings survive ") + how);
			const auto& restoredPanel = checked->FindEntityByName("Panel").GetComponent<UIElementComponent>();
			Check(restoredPanel.Rect.OffsetMin == glm::vec2(100.0f, 50.0f) && restoredPanel.Layout.Kind == UI::LayoutKind::Vertical && Near(restoredPanel.Layout.Spacing, 10.0f), std::string("element rectangle and layout survive ") + how);
			Check(!checked->FindEntityByName("Hidden").GetComponent<UIElementComponent>().Visible && Near(checked->FindEntityByName("Panel").GetComponent<UIImageComponent>().Color.g, 0.4f), std::string("visibility and image color survive ") + how);
			const auto resolved = UI::ResolveLayout(*checked, screen);
			Check(resolved.size() == 1 && resolved[0].Elements.size() == 3, std::string("the layout of a scene reloaded from ") + how + " matches");
		};
		verify(loaded, "a save");
		verify(Scene::Copy(loaded), "the copy Play starts from");

		// Text: what survives a save (no font is needed to serialize it).
		Entity label = scene->CreateEntity("Label");
		label.SetParent(canvas);
		label.AddComponent<UIElementComponent>();
		auto& text = label.AddComponent<UITextComponent>();
		text.Text = "Play\nQuit";
		text.Size = 48.0f;
		text.AlignH = UITextAlignH::Center;
		text.AlignV = UITextAlignV::Middle;
		text.Wrap = false;
		SceneSerializer(scene).Serialize(root / "UI" / "Label.luxscene");
		Ref<Scene> labelScene = Ref<Scene>::Create();
		SceneSerializer(labelScene).Deserialize(root / "UI" / "Label.luxscene");
		const auto& restoredText = labelScene->FindEntityByName("Label").GetComponent<UITextComponent>();
		Check(restoredText.Text == "Play\nQuit" && Near(restoredText.Size, 48.0f) && restoredText.AlignH == UITextAlignH::Center && restoredText.AlignV == UITextAlignV::Middle && !restoredText.Wrap, "text, size, alignment and wrap survive a save");
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
	CheckTilemapOps();
	CheckTilemapScene(outDir);
	CheckTilemapPhysics(outDir);
	CheckUILayout();
	CheckUIScene(outDir);
	CheckUIWidgets(outDir);
	CheckUINavigation();
	CheckUICaptions();

	std::printf("RESULT: %s (%d failure%s)\n", s_Failures == 0 ? "PASS" : "FAIL", s_Failures, s_Failures == 1 ? "" : "s");
	std::fflush(stdout);

	// Exit without Project/ScriptEngine/Core teardown: the asset manager would rewrite its registry
	// on shutdown and nothing here needs a clean exit.
	std::_Exit(s_Failures == 0 ? 0 : 1);
}
