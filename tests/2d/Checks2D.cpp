// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

// Headless checks for 2D as a full project type (M11). Real engine code - project and scene
// serializers, the editor camera, Box2D - with no window and no GPU device.
//
//   Checks2D <outdir>
//
// Built and run by tests/2d/run_2d_checks.py, which derives the project from Editor.vcxproj.

#include "Lux/Core/Base.h"
#include "Lux/Core/Log.h"
#include "Lux/Editor/EditorCamera.h"
#include "Lux/Physics2D/PhysicsScene2D.h"
#include "Lux/Project/Project.h"
#include "Lux/Project/ProjectSerializer.h"
#include "Lux/Renderer/FrameRenderPacket.h"
#include "Lux/Renderer/Renderer2D.h"
#include "Lux/Scene/Components.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Scene/SceneSerializer.h"
#include "Lux/Scene/SceneTemplates.h"

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
			physics.Simulate(Timestep(1.0f / 60.0f));
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

}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::printf("usage: Checks2D <outdir>\n");
		return 2;
	}

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

	std::printf("RESULT: %s (%d failure%s)\n", s_Failures == 0 ? "PASS" : "FAIL", s_Failures, s_Failures == 1 ? "" : "s");
	std::fflush(stdout);

	// Exit without Project/ScriptEngine/Core teardown: the asset manager would rewrite its registry
	// on shutdown and nothing here needs a clean exit.
	std::_Exit(s_Failures == 0 ? 0 : 1);
}
