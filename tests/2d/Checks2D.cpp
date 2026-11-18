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

	std::printf("RESULT: %s (%d failure%s)\n", s_Failures == 0 ? "PASS" : "FAIL", s_Failures, s_Failures == 1 ? "" : "s");
	std::fflush(stdout);

	// Exit without Project/ScriptEngine/Core teardown: the asset manager would rewrite its registry
	// on shutdown and nothing here needs a clean exit.
	std::_Exit(s_Failures == 0 ? 0 : 1);
}
