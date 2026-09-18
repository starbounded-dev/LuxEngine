#include "lpch.h"
#include "MaterialPreview.h"

#include "Viewport/Viewport.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Project/Project.h"
#include "Lux/Scene/Components.h"

#include <glm/gtc/constants.hpp>

namespace Lux
{
	namespace
	{
		// Small by design: the preview is a thumbnail-sized panel, and rendering it every frame at
		// full editor resolution would compete with the main viewport.
		constexpr uint32_t kInitialPreviewSize = 256;
		constexpr uint32_t kMaxPreviewSize = 1024;

		constexpr float kMinPitch = -85.0f;
		constexpr float kMaxPitch = 85.0f;
		constexpr float kMinDistance = 1.2f;
		constexpr float kMaxDistance = 12.0f;
		constexpr float kDefaultDistance = 2.0f; // the unit primitives fill about 60% of the frame
		constexpr float kDefaultPitch = 15.0f;

		const char* GetShapeFilename(MaterialPreview::Shape shape)
		{
			switch (shape)
			{
				case MaterialPreview::Shape::Sphere:   return "Sphere.gltf";
				case MaterialPreview::Shape::Cube:     return "Cube.gltf";
				case MaterialPreview::Shape::Cylinder: return "Cylinder.gltf";
				case MaterialPreview::Shape::Plane:    return "Plane.gltf";
				case MaterialPreview::Shape::Torus:    return "Torus.gltf";
				default:                               return "Sphere.gltf";
			}
		}
	}

	MaterialPreview::MaterialPreview()
	{
		m_Scene = Ref<Scene>::Create();

		m_MeshEntity = m_Scene->CreateEntity("Preview Mesh");
		m_MeshEntity.AddComponent<StaticMeshComponent>();

		Entity sun = m_Scene->CreateEntity("Preview Sun");
		sun.GetComponent<TransformComponent>().SetRotationEuler(glm::radians(glm::vec3(-45.0f, 35.0f, 0.0f)));
		auto& light = sun.AddComponent<DirectionalLightComponent>();
		light.Intensity = 2.0f;
		light.CastShadows = false; // a single object on a plain background casts nothing visible

		m_SkyEntity = m_Scene->CreateEntity("Preview Sky");
		auto& sky = m_SkyEntity.AddComponent<SkyLightComponent>();
		sky.DynamicSky = true;
		sky.TurbidityAzimuthInclination = { 2.0f, 0.0f, 0.35f };

		FramebufferSpecification framebufferSpec;
		framebufferSpec.Attachments = { ImageFormat::RGBA32F, ImageFormat::Depth };
		framebufferSpec.Width = kInitialPreviewSize;
		framebufferSpec.Height = kInitialPreviewSize;
		framebufferSpec.ClearColorOnLoad = true;
		framebufferSpec.DebugName = "MaterialPreviewFramebuffer";

		SceneRendererSpecification rendererSpec;
		rendererSpec.ViewportWidth = kInitialPreviewSize;
		rendererSpec.ViewportHeight = kInitialPreviewSize;
		rendererSpec.EnableEditorRenderTargets = false;

		m_Viewport = Ref<Viewport>::Create("Material Preview");
		m_Viewport->Init(m_Scene, framebufferSpec, rendererSpec);
		m_Viewport->GetSceneRenderer()->GetOptions().ShowGrid = false;

		ApplyShape();
		ApplyCamera();
	}

	MaterialPreview::~MaterialPreview()
	{
		if (m_Viewport)
			m_Viewport->Shutdown();
	}

	void MaterialPreview::SetMaterial(AssetHandle materialHandle)
	{
		m_MaterialHandle = materialHandle;

		auto& mesh = m_MeshEntity.GetComponent<StaticMeshComponent>();
		// A single slot-0 entry applies to every submesh (see ResolveStaticMeshMaterialHandle).
		mesh.MaterialTable->Clear();
		if (materialHandle)
			mesh.MaterialTable->SetMaterial(0, materialHandle);
	}

	void MaterialPreview::SetShape(Shape shape)
	{
		if (shape == m_Shape)
			return;

		m_Shape = shape;
		ApplyShape();
	}

	void MaterialPreview::SetSkyEnabled(bool enabled)
	{
		m_SkyEnabled = enabled;
		m_SkyEntity.GetComponent<SkyLightComponent>().Intensity = enabled ? 1.0f : 0.0f;
	}

	void MaterialPreview::Orbit(float yawDegrees, float pitchDegrees)
	{
		m_Yaw += yawDegrees;
		m_Pitch = glm::clamp(m_Pitch + pitchDegrees, kMinPitch, kMaxPitch);
		ApplyCamera();
	}

	void MaterialPreview::Zoom(float factor)
	{
		m_Distance = glm::clamp(m_Distance * factor, kMinDistance, kMaxDistance);
		ApplyCamera();
	}

	void MaterialPreview::ResetCamera()
	{
		m_Yaw = 0.0f;
		m_Pitch = kDefaultPitch;
		m_Distance = kDefaultDistance;
		ApplyCamera();
	}

	Ref<Image2D> MaterialPreview::Render(uint32_t width, uint32_t height)
	{
		LUX_PROFILE_FUNCTION("MaterialPreview::Render");

		width = glm::clamp(width, 1u, kMaxPreviewSize);
		height = glm::clamp(height, 1u, kMaxPreviewSize);

		m_Viewport->SetSize({ (float)width, (float)height });
		m_Viewport->SyncSceneViewport(m_Scene);

		Ref<SceneRenderer> renderer = m_Viewport->GetSceneRenderer();
		if (!renderer || !renderer->IsReady())
			return nullptr;

		m_Scene->OnRenderEditor(renderer, m_Viewport->GetCamera(), nullptr);
		return m_Viewport->GetDisplayImage();
	}

	const char* MaterialPreview::GetShapeName(Shape shape)
	{
		switch (shape)
		{
			case Shape::Sphere:   return "Sphere";
			case Shape::Cube:     return "Cube";
			case Shape::Cylinder: return "Cylinder";
			case Shape::Plane:    return "Plane";
			case Shape::Torus:    return "Torus";
			default:              return "Sphere";
		}
	}

	void MaterialPreview::ApplyShape()
	{
		AssetHandle meshHandle = 0;
		if (Ref<EditorAssetManager> editorAssetManager = Project::GetEditorAssetManager())
			meshHandle = editorAssetManager->GetOrImportAsset(std::filesystem::path("Meshes") / "Source" / "Default" / GetShapeFilename(m_Shape));

		if (meshHandle && AssetManager::GetAssetType(meshHandle) != AssetType::MeshSource)
			meshHandle = 0;

		if (!meshHandle)
			LUX_CORE_WARN_TAG("Editor", "Material preview: built-in mesh '{}' is missing from the project's Meshes/Source/Default folder", GetShapeFilename(m_Shape));

		m_MeshEntity.GetComponent<StaticMeshComponent>().StaticMesh = meshHandle;

		// The plane is flat on the ground; tilt it toward the camera so the surface is visible.
		auto& transform = m_MeshEntity.GetComponent<TransformComponent>();
		transform.SetRotationEuler(m_Shape == Shape::Plane ? glm::radians(glm::vec3(60.0f, 0.0f, 0.0f)) : glm::vec3(0.0f));
	}

	void MaterialPreview::ApplyCamera()
	{
		m_Viewport->GetCamera().SetOrbitState(glm::vec3(0.0f), m_Distance, glm::radians(m_Pitch), glm::radians(m_Yaw));
	}
}
