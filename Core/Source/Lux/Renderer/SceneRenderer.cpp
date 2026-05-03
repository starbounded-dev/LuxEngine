#include "lpch.h"
#include "SceneRenderer.h"

#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/Renderer2D.h"
#include "Lux/Core/Application.h"
#include "Lux/Asset/AssetManager.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstring>

namespace Lux {

	// Push-constant layout that every mesh-draw shader in this engine expects.
	// Must match the push_constant block declared in HazelPBR_Static, PreDepth,
	// DirShadowMap, SelectedGeometry, and Wireframe shaders.
	struct MeshDrawPushConstants
	{
		uint32_t ObjectIndexBase = 0; // first index into ObjectIndexes SSBO
		uint32_t LightIndex = 0; // shadow cascade index (0 = single cascade)
		uint32_t BoneTransformBase = 0; // unused (no animation)
		uint32_t BoneTransformStride = 0; // unused
	};

	namespace
	{
		Ref<TextureCube> GetEnvironmentRadianceMap(const Ref<Environment>& environment)
		{
			return environment && environment->RadianceMap ? environment->RadianceMap : Renderer::GetBlackCubeTexture();
		}

		Ref<TextureCube> GetEnvironmentIrradianceMap(const Ref<Environment>& environment)
		{
			return environment && environment->IrradianceMap ? environment->IrradianceMap : Renderer::GetBlackCubeTexture();
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Construction / destruction
	// ─────────────────────────────────────────────────────────────────────────

	SceneRenderer::SceneRenderer(Ref<Scene> scene, SceneRendererSpecification specification)
		: m_Scene(scene), m_Specification(specification)
	{
		Init();
	}

	SceneRenderer::~SceneRenderer()
	{
		Shutdown();
	}

	void SceneRenderer::Shutdown()
	{
		// Ref<> handles GPU resource lifetimes automatically.
	}

	void SceneRenderer::SetScene(Ref<Scene> scene)
	{
		LUX_CORE_ASSERT(!m_Active, "Cannot change scene while rendering");
		m_Scene = scene;
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Init
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::Init()
	{
		m_CommandBuffer = RenderCommandBuffer::Create(0, "SceneRenderer",       /*queries=*/true);
		m_UploadCommandBuffer = RenderCommandBuffer::Create(0, "SceneRenderer-Upload", /*queries=*/false);

		m_Renderer2D = Ref<Renderer2D>::Create(Renderer2DSpecification{});
		m_DebugRenderer = Ref<DebugRenderer>::Create();

		// Use window size if none specified
		if (m_Specification.ViewportWidth == 0) m_Specification.ViewportWidth = Application::Get().GetWindow().GetWidth();
		if (m_Specification.ViewportHeight == 0) m_Specification.ViewportHeight = Application::Get().GetWindow().GetHeight();
		m_ViewportWidth = m_Specification.ViewportWidth;
		m_ViewportHeight = m_Specification.ViewportHeight;
		m_InvViewportWidth = m_ViewportWidth > 0 ? 1.0f / (float)m_ViewportWidth : 0.0f;
		m_InvViewportHeight = m_ViewportHeight > 0 ? 1.0f / (float)m_ViewportHeight : 0.0f;

		// ── Uniform buffer sets ───────────────────────────────────────────────
		m_UBSCamera = UniformBufferSet::Create(sizeof(UBCamera));
		m_UBSScene = UniformBufferSet::Create(sizeof(UBScene));
		m_UBSShadow = UniformBufferSet::Create(sizeof(UBShadow));
		m_UBSRendererData = UniformBufferSet::Create(sizeof(UBRendererData));
		m_UBSPointLights = UniformBufferSet::Create(sizeof(UBPointLights));
		m_UBSSpotLights = UniformBufferSet::Create(sizeof(UBSpotLights));
		m_UBSSpotShadow = UniformBufferSet::Create(sizeof(UBSpotShadow));

		// ── Storage buffer sets (start with generous initial capacity) ─────────
		{
			StorageBufferSpecification spec;
			spec.GPUOnly = false;
			spec.DebugName = "InstanceTransforms";
			// Pre-allocate 4096 transform slots  (each is 48 bytes)
			m_SBSInstanceTransforms = StorageBufferSet::Create(spec, sizeof(TransformVertexData) * 4096);
		}
		{
			StorageBufferSpecification spec;
			spec.GPUOnly = false;
			spec.DebugName = "ObjectIndexes";
			m_SBSObjectIndexes = StorageBufferSet::Create(spec, sizeof(uint32_t) * 4096);
		}
		{
			StorageBufferSpecification indexSpec;
			indexSpec.GPUOnly = false;
			indexSpec.DebugName = "VisiblePointLightIndices";
			m_SBSVisiblePointLightIndices = StorageBufferSet::Create(indexSpec, sizeof(uint32_t) * 1024);

			indexSpec.DebugName = "VisibleSpotLightIndices";
			m_SBSVisibleSpotLightIndices = StorageBufferSet::Create(indexSpec, sizeof(uint32_t) * 1024);
		}

		// Common vertex layout for all opaque mesh pipelines
		VertexBufferLayout vertexLayout = {
			{ ShaderDataType::Float3, "a_Position" },
			{ ShaderDataType::Float3, "a_Normal"   },
			{ ShaderDataType::Float3, "a_Tangent"  },
			{ ShaderDataType::Float3, "a_Binormal" },
			{ ShaderDataType::Float2, "a_TexCoord" }
		};

		// ── Shadow map (single directional, ortho projection) ─────────────────
		{
			ImageSpecification shadowMapSpec;
			shadowMapSpec.DebugName = "ShadowMapArray";
			shadowMapSpec.Dimension = nvrhi::TextureDimension::Texture2DArray;
			shadowMapSpec.Format = ImageFormat::Depth;
			shadowMapSpec.Usage = ImageUsage::Attachment;
			shadowMapSpec.Width = 4096;
			shadowMapSpec.Height = 4096;
			shadowMapSpec.Layers = 4;
			m_ShadowMapImage = Image2D::Create(shadowMapSpec);
			m_ShadowMapImage->RT_Invalidate();

			FramebufferSpecification fbSpec;
			fbSpec.Width = 4096;
			fbSpec.Height = 4096;
			fbSpec.Attachments = { ImageFormat::Depth };
			fbSpec.DepthClearValue = 0.0f;
			fbSpec.DebugName = "ShadowMap";
			fbSpec.ExistingImage = m_ShadowMapImage;
			fbSpec.ExistingImageLayer = 0;

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "DirShadowMap";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("DirShadowMap");
			pipelineSpec.TargetFramebuffer = Framebuffer::Create(fbSpec);
			pipelineSpec.Layout = vertexLayout;
			pipelineSpec.DepthOperator = DepthCompareOperator::LessOrEqual;
			pipelineSpec.BackfaceCulling = false; // avoid peter-panning

			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "ShadowMapPass";
			rpSpec.Pipeline = Pipeline::Create(pipelineSpec);

			m_ShadowMapPass = RenderPass::Create(rpSpec);
			m_ShadowMapPass->SetInput("ShadowData", m_UBSShadow);
			m_ShadowMapPass->SetInput("InstanceTransforms", m_SBSInstanceTransforms);
			m_ShadowMapPass->SetInput("ObjectIndexes", m_SBSObjectIndexes);
			LUX_CORE_VERIFY(m_ShadowMapPass->Validate());
			m_ShadowMapPass->Bake();

			m_ShadowPassMaterial = Material::Create(pipelineSpec.Shader, "ShadowPass");
		}

		// ── Pre-depth pass ────────────────────────────────────────────────────
		{
			FramebufferSpecification fbSpec;
			fbSpec.Width = m_ViewportWidth;
			fbSpec.Height = m_ViewportHeight;
			fbSpec.Attachments = { ImageFormat::DEPTH32FSTENCIL8UINT };
			fbSpec.DepthClearValue = 0.0f;
			fbSpec.ClearDepthOnLoad = true;
			fbSpec.DebugName = "PreDepth";

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "PreDepth";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("PreDepth");
			pipelineSpec.TargetFramebuffer = Framebuffer::Create(fbSpec);
			pipelineSpec.Layout = vertexLayout;
			pipelineSpec.DepthOperator = DepthCompareOperator::GreaterOrEqual;

			m_PreDepthPipeline = Pipeline::Create(pipelineSpec);
			m_PreDepthMaterial = Material::Create(pipelineSpec.Shader, "PreDepth");

			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "PreDepthPass";
			rpSpec.Pipeline = m_PreDepthPipeline;

			m_PreDepthPass = RenderPass::Create(rpSpec);
			m_PreDepthPass->SetInput("Camera", m_UBSCamera);
			m_PreDepthPass->SetInput("InstanceTransforms", m_SBSInstanceTransforms);
			m_PreDepthPass->SetInput("ObjectIndexes", m_SBSObjectIndexes);
			LUX_CORE_VERIFY(m_PreDepthPass->Validate());
			m_PreDepthPass->Bake();
		}

		// ── Geometry framebuffer (owns the color + normal + material images) ──
		{
			FramebufferSpecification geoFBSpec;
			geoFBSpec.Width = m_ViewportWidth;
			geoFBSpec.Height = m_ViewportHeight;
			// color | view-space normals | metalness+roughness | depth (shared with pre-depth)
			geoFBSpec.Attachments = {
				ImageFormat::RGBA32F,
				ImageFormat::RGBA16F,
				ImageFormat::RGBA,
				ImageFormat::DEPTH32FSTENCIL8UINT
			};
			geoFBSpec.ExistingImages[3] = m_PreDepthPass->GetDepthOutput();
			geoFBSpec.Attachments.Attachments[0].LoadOp = AttachmentLoadOp::Load; // skybox writes first
			geoFBSpec.Attachments.Attachments[1].Blend = false;
			geoFBSpec.ClearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
			geoFBSpec.ClearDepthOnLoad = false;
			geoFBSpec.DebugName = "Geometry";
			m_GeometryPassFramebuffer = Framebuffer::Create(geoFBSpec);

			// "Load" variant: subsequent draws share the same images without clearing.
			geoFBSpec.ClearColorOnLoad = false;
			geoFBSpec.ExistingImages[0] = m_GeometryPassFramebuffer->GetImage(0);
			geoFBSpec.ExistingImages[1] = m_GeometryPassFramebuffer->GetImage(1);
			geoFBSpec.ExistingImages[2] = m_GeometryPassFramebuffer->GetImage(2);
			Ref<Framebuffer> loadFB = Framebuffer::Create(geoFBSpec);

			// Opaque PBR pipeline
			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "PBR-Static";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("LuxPBR_Static");
			pipelineSpec.TargetFramebuffer = loadFB;
			pipelineSpec.Layout = vertexLayout;
			pipelineSpec.DepthOperator = DepthCompareOperator::Equal; // rely on pre-depth
			pipelineSpec.DepthWrite = false;
			m_GeometryPipeline = Pipeline::Create(pipelineSpec);

			// Transparent PBR pipeline (alpha-blend, depth-test but no pre-depth Equal trick)
			pipelineSpec.DebugName = "PBR-Transparent";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("LuxPBR_Transparent");
			pipelineSpec.DepthOperator = DepthCompareOperator::GreaterOrEqual;
			pipelineSpec.DepthWrite = false;
			m_TransparentGeometryPipeline = Pipeline::Create(pipelineSpec);
		}

		// ── Geometry render passes ────────────────────────────────────────────
		{
			// Opaque pass
			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "GeometryPass";
			rpSpec.Pipeline = m_GeometryPipeline;
			m_GeometryPass = RenderPass::Create(rpSpec);

			m_GeometryPass->SetInput("Camera", m_UBSCamera);
			m_GeometryPass->SetInput("SceneData", m_UBSScene);
			m_GeometryPass->SetInput("ShadowData", m_UBSShadow);
			m_GeometryPass->SetInput("RendererData", m_UBSRendererData);
			m_GeometryPass->SetInput("PointLightData", m_UBSPointLights);
			m_GeometryPass->SetInput("SpotLightData", m_UBSSpotLights);
			m_GeometryPass->SetInput("SpotShadowData", m_UBSSpotShadow);
			m_GeometryPass->SetInput("VisiblePointLightIndicesBuffer", m_SBSVisiblePointLightIndices);
			m_GeometryPass->SetInput("VisibleSpotLightIndicesBuffer", m_SBSVisibleSpotLightIndices);
			m_GeometryPass->SetInput("InstanceTransforms", m_SBSInstanceTransforms);
			m_GeometryPass->SetInput("ObjectIndexes", m_SBSObjectIndexes);
			// Environment textures – overridden each frame in BeginScene once env is set
			m_GeometryPass->SetInput("u_EnvRadianceTex", Renderer::GetBlackCubeTexture());
			m_GeometryPass->SetInput("u_EnvIrradianceTex", Renderer::GetBlackCubeTexture());
			m_GeometryPass->SetInput("u_BRDFLUTTexture", Renderer::GetBRDFLutTexture());
			// Shadow map output from the shadow pass above
			m_GeometryPass->SetInput("u_ShadowMapTexture", m_ShadowMapPass->GetDepthOutput());
			m_GeometryPass->SetInput("u_SpotShadowTexture", Renderer::GetWhiteTexture()); // or a dummy 2D array if required
			LUX_CORE_VERIFY(m_GeometryPass->Validate());
			m_GeometryPass->Bake();

			// Transparent pass (same bindings, different pipeline)
			rpSpec.DebugName = "GeometryPass-Transparent";
			rpSpec.Pipeline = m_TransparentGeometryPipeline;
			m_GeometryPassTransparent = RenderPass::Create(rpSpec);

			m_GeometryPassTransparent->SetInput("Camera", m_UBSCamera);
			m_GeometryPassTransparent->SetInput("SceneData", m_UBSScene);
			m_GeometryPassTransparent->SetInput("ShadowData", m_UBSShadow);
			m_GeometryPassTransparent->SetInput("RendererData", m_UBSRendererData);
			m_GeometryPassTransparent->SetInput("PointLightData", m_UBSPointLights);
			m_GeometryPassTransparent->SetInput("SpotLightData", m_UBSSpotLights);
			m_GeometryPassTransparent->SetInput("SpotShadowData", m_UBSSpotShadow);
			m_GeometryPassTransparent->SetInput("VisiblePointLightIndicesBuffer", m_SBSVisiblePointLightIndices);
			m_GeometryPassTransparent->SetInput("VisibleSpotLightIndicesBuffer", m_SBSVisibleSpotLightIndices);
			m_GeometryPassTransparent->SetInput("InstanceTransforms", m_SBSInstanceTransforms);
			m_GeometryPassTransparent->SetInput("ObjectIndexes", m_SBSObjectIndexes);
			// Environment textures – overridden each frame in BeginScene once env is set
			m_GeometryPassTransparent->SetInput("u_EnvRadianceTex", Renderer::GetBlackCubeTexture());
			m_GeometryPassTransparent->SetInput("u_EnvIrradianceTex", Renderer::GetBlackCubeTexture());
			m_GeometryPassTransparent->SetInput("u_BRDFLUTTexture", Renderer::GetBRDFLutTexture());
			// Shadow map output from the shadow pass above
			m_GeometryPassTransparent->SetInput("u_ShadowMapTexture", m_ShadowMapPass->GetDepthOutput());
			m_GeometryPassTransparent->SetInput("u_SpotShadowTexture", Renderer::GetWhiteTexture()); // or a dummy 2D array if required
			LUX_CORE_VERIFY(m_GeometryPassTransparent->Validate());
			m_GeometryPassTransparent->Bake();
		}

		// ── Selected geometry (isolation for outline) ─────────────────────────
		{
			FramebufferSpecification fbSpec;
			fbSpec.Width = m_ViewportWidth;
			fbSpec.Height = m_ViewportHeight;
			fbSpec.Attachments = { ImageFormat::RGBA32F, ImageFormat::Depth };
			fbSpec.ClearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
			fbSpec.DepthClearValue = 1.0f;
			fbSpec.DebugName = "SelectedGeometry";

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "SelectedGeometry";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("SelectedGeometry");
			pipelineSpec.TargetFramebuffer = Framebuffer::Create(fbSpec);
			pipelineSpec.Layout = vertexLayout;
			pipelineSpec.DepthOperator = DepthCompareOperator::LessOrEqual;

			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "SelectedGeometryPass";
			rpSpec.Pipeline = Pipeline::Create(pipelineSpec);
			m_SelectedGeometryPass = RenderPass::Create(rpSpec);
			m_SelectedGeometryPass->SetInput("Camera", m_UBSCamera);
			m_SelectedGeometryPass->SetInput("InstanceTransforms", m_SBSInstanceTransforms);
			m_SelectedGeometryPass->SetInput("ObjectIndexes", m_SBSObjectIndexes);
			LUX_CORE_VERIFY(m_SelectedGeometryPass->Validate());
			m_SelectedGeometryPass->Bake();

			m_SelectedGeometryMaterial = Material::Create(pipelineSpec.Shader, "SelectedGeometry");
		}

		// ── Wireframe pass (on top of geometry, for selected meshes) ──────────
		{
			FramebufferSpecification fbSpec;
			fbSpec.Width = m_ViewportWidth;
			fbSpec.Height = m_ViewportHeight;
			fbSpec.ExistingImages[0] = m_GeometryPassFramebuffer->GetImage(0);
			fbSpec.Attachments = { ImageFormat::RGBA32F };
			fbSpec.ClearColorOnLoad = false;
			fbSpec.DebugName = "GeometryWireframe";

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "GeometryWireframe";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("Wireframe");
			pipelineSpec.TargetFramebuffer = Framebuffer::Create(fbSpec);
			pipelineSpec.Layout = vertexLayout;
			pipelineSpec.Wireframe = true;
			pipelineSpec.DepthTest = false;

			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "GeometryWireframePass";
			rpSpec.Pipeline = Pipeline::Create(pipelineSpec);
			m_GeometryWireframePass = RenderPass::Create(rpSpec);
			m_GeometryWireframePass->SetInput("Camera", m_UBSCamera);
			m_GeometryWireframePass->SetInput("InstanceTransforms", m_SBSInstanceTransforms);
			m_GeometryWireframePass->SetInput("ObjectIndexes", m_SBSObjectIndexes);
			LUX_CORE_VERIFY(m_GeometryWireframePass->Validate());
			m_GeometryWireframePass->Bake();

			m_WireframeMaterial = Material::Create(pipelineSpec.Shader, "Wireframe");
			m_WireframeMaterial->Set("u_MaterialUniforms.Color", glm::vec4{ 1.0f, 0.5f, 0.0f, 1.0f });
		}

		// ── Skybox (renders into geometry color attachment before geometry) ────
		{
			FramebufferSpecification fbSpec;
			fbSpec.Width = m_ViewportWidth;
			fbSpec.Height = m_ViewportHeight;
			fbSpec.ExistingImages[0] = m_GeometryPassFramebuffer->GetImage(0);
			fbSpec.Attachments = { ImageFormat::RGBA32F };
			fbSpec.ClearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
			fbSpec.DebugName = "Skybox";

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "Skybox";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("Skybox");
			pipelineSpec.TargetFramebuffer = Framebuffer::Create(fbSpec);
			pipelineSpec.DepthWrite = false;
			pipelineSpec.DepthTest = false;
			pipelineSpec.Layout = {
				{ ShaderDataType::Float3, "a_Position" },
				{ ShaderDataType::Float2, "a_TexCoord" }
			};
			m_SkyboxPipeline = Pipeline::Create(pipelineSpec);
			m_SkyboxMaterial = Material::Create(pipelineSpec.Shader, "Skybox");
			m_SkyboxMaterial->SetFlag(MaterialFlag::DepthTest, false);

			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "SkyboxPass";
			rpSpec.Pipeline = m_SkyboxPipeline;
			m_SkyboxPass = RenderPass::Create(rpSpec);
			m_SkyboxPass->SetInput("Camera", m_UBSCamera);
			LUX_CORE_VERIFY(m_SkyboxPass->Validate());
			m_SkyboxPass->Bake();
		}

		// ── Scene composite (tone-map + exposure + opacity) ───────────────────
		{
			FramebufferSpecification fbSpec;
			fbSpec.Width = m_ViewportWidth;
			fbSpec.Height = m_ViewportHeight;
			fbSpec.Attachments = { ImageFormat::RGBA, ImageFormat::Depth };
			fbSpec.ClearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
			fbSpec.DebugName = "SceneComposite";
			m_CompositingFramebuffer = Framebuffer::Create(fbSpec);

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "SceneComposite";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("SceneComposite");
			pipelineSpec.TargetFramebuffer = m_CompositingFramebuffer;
			pipelineSpec.DepthWrite = false;
			pipelineSpec.DepthTest = false;
			pipelineSpec.Layout = {
				{ ShaderDataType::Float3, "a_Position" },
				{ ShaderDataType::Float2, "a_TexCoord" }
			};

			m_CompositeMaterial = Material::Create(pipelineSpec.Shader, "SceneComposite");
			m_CompositeMaterial->Set("u_Uniforms.Opacity", m_Opacity);

			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "CompositePass";
			rpSpec.Pipeline = Pipeline::Create(pipelineSpec);
			m_CompositePass = RenderPass::Create(rpSpec);
			// The geometry color output feeds the composite shader
			m_CompositePass->SetInput("u_Texture", m_GeometryPass->GetOutput(0));
			LUX_CORE_VERIFY(m_CompositePass->Validate());
			m_CompositePass->Bake();
		}

		// ── Editor grid (renders into composite output, preserves depth) ──────
		{
			FramebufferSpecification fbSpec;
			fbSpec.Width = m_ViewportWidth;
			fbSpec.Height = m_ViewportHeight;
			fbSpec.ExistingImages[0] = m_CompositingFramebuffer->GetImage(0);
			fbSpec.ExistingImages[1] = m_CompositingFramebuffer->GetDepthImage();
			fbSpec.Attachments = { ImageFormat::RGBA, ImageFormat::Depth };
			fbSpec.ClearColorOnLoad = false;
			fbSpec.ClearDepthOnLoad = false;
			fbSpec.DebugName = "Grid";

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "Grid";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("Grid");
			pipelineSpec.TargetFramebuffer = Framebuffer::Create(fbSpec);
			pipelineSpec.DepthTest = true;
			pipelineSpec.DepthWrite = false;
			pipelineSpec.Layout = {
				{ ShaderDataType::Float3, "a_Position" },
				{ ShaderDataType::Float2, "a_TexCoord" }
			};

			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "GridPass";
			rpSpec.Pipeline = Pipeline::Create(pipelineSpec);
			m_GridRenderPass = RenderPass::Create(rpSpec);
			m_GridRenderPass->SetInput("Camera", m_UBSCamera);
			LUX_CORE_VERIFY(m_GridRenderPass->Validate());
			m_GridRenderPass->Bake();

			constexpr float gridScale = 16.025f;
			constexpr float gridSize = 0.025f;
			m_GridMaterial = Material::Create(pipelineSpec.Shader, "Grid");
			const static glm::mat4 transform =
				glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f))
				* glm::scale(glm::mat4(1.0f), glm::vec3(8.0f));
			m_GridMaterial->Set("u_Settings.Transform", transform);
			m_GridMaterial->Set("u_Settings.Scale", gridScale);
			m_GridMaterial->Set("u_Settings.Size", gridSize);
		}

		// ── Physics collider debug materials ─────────────────────────────────
		{
			Ref<Shader> wireframeShader = Renderer::GetShaderLibrary()->Get("Wireframe");
			m_SimpleColliderMaterial = Material::Create(wireframeShader, "SimpleCollider");
			m_SimpleColliderMaterial->Set("u_MaterialUniforms.Color", glm::vec4{ 0.2f, 1.0f, 0.2f, 1.0f });
			m_ComplexColliderMaterial = Material::Create(wireframeShader, "ComplexCollider");
			m_ComplexColliderMaterial->Set("u_MaterialUniforms.Color", glm::vec4{ 0.5f, 0.5f, 1.0f, 1.0f });
		}

		// Signal render thread that GPU resources are ready
		Renderer::Submit([instance = Ref<SceneRenderer>(this)]() mutable {
			instance->m_ResourcesCreatedGPU = true;
			});
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Viewport resize
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::SetViewportSize(uint32_t width, uint32_t height)
	{
		width = (uint32_t)(width * m_Specification.Tiering.RendererScale);
		height = (uint32_t)(height * m_Specification.Tiering.RendererScale);

		if (m_ViewportWidth != width || m_ViewportHeight != height)
		{
			m_ViewportWidth = width;
			m_ViewportHeight = height;
			m_InvViewportWidth = width > 0 ? 1.0f / (float)width : 0.0f;
			m_InvViewportHeight = height > 0 ? 1.0f / (float)height : 0.0f;
			m_NeedsResize = true;
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Per-frame scene data setters (call between BeginScene and EndScene)
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::SetLightEnvironment(const LightEnvironment& lightEnvironment)
	{
		m_SceneData.SceneLightEnvironment = lightEnvironment;
	}

	void SceneRenderer::SetEnvironment(Ref<Environment> environment, float intensity, float skyboxLod)
	{
		m_SceneData.SceneEnvironment = environment;
		m_SceneData.SceneEnvironmentIntensity = intensity;
		m_SceneData.SkyboxLod = skyboxLod;
	}

	// ─────────────────────────────────────────────────────────────────────────
	// BeginScene
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::BeginScene(const SceneRendererCamera& camera)
	{
		LUX_CORE_ASSERT(m_Scene, "No scene attached to SceneRenderer");
		LUX_CORE_ASSERT(!m_Active, "BeginScene called twice without EndScene");
		m_Active = true;

		if (m_ResourcesCreatedGPU)
			m_ResourcesCreated = true;

		if (!m_ResourcesCreated)
			return; // GPU resources not yet available

		// Open the upload command buffer for uniform/storage buffer writes
		m_UploadCommandBuffer->Begin();

		m_SceneData.SceneCamera = camera;

		// ── Handle viewport resize ────────────────────────────────────────────
		if (m_NeedsResize)
		{
			m_NeedsResize = false;
			m_ScreenSpaceProjectionMatrix = glm::ortho(0.0f, (float)m_ViewportWidth, 0.0f, (float)m_ViewportHeight);

			m_PreDepthPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_GeometryPassFramebuffer->Resize(m_ViewportWidth, m_ViewportHeight);
			m_GeometryPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_GeometryPassTransparent->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_SkyboxPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_SelectedGeometryPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_GeometryWireframePass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_CompositingFramebuffer->Resize(m_ViewportWidth, m_ViewportHeight);
			m_CompositePass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_GridRenderPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
		}

		// ── Camera uniform buffer ─────────────────────────────────────────────
		{
			const glm::mat4 viewProj = camera.Camera.GetProjectionMatrix() * camera.ViewMatrix;
			const glm::mat4 viewInverse = glm::inverse(camera.ViewMatrix);
			const glm::mat4 projInverse = glm::inverse(camera.Camera.GetProjectionMatrix());

			m_CameraUB.ViewProjection = viewProj;
			m_CameraUB.InverseViewProjection = viewInverse * projInverse;
			m_CameraUB.Projection = camera.Camera.GetProjectionMatrix();
			m_CameraUB.InverseProjection = projInverse;
			m_CameraUB.View = camera.ViewMatrix;
			m_CameraUB.InverseView = viewInverse;

			// Depth linearization for GTAO-style effects (kept for forward compat)
			float depthLinearizeMul = -m_CameraUB.Projection[3][2];
			float depthLinearizeAdd = m_CameraUB.Projection[2][2];
			if (depthLinearizeMul * depthLinearizeAdd < 0) depthLinearizeAdd = -depthLinearizeAdd;
			m_CameraUB.DepthUnpackConsts = { depthLinearizeMul, depthLinearizeAdd };

			const float* P = glm::value_ptr(camera.Camera.GetProjectionMatrix());
			m_CameraUB.NDCToViewMul = { 2.0f / P[0],  2.0f / P[5] };
			m_CameraUB.NDCToViewAdd = { -1.0f,        -1.0f };
			m_CameraUB.CameraTanHalfFOV = { 1.0f / P[0],  1.0f / P[5] };

			auto cameraData = m_CameraUB;
			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, cameraData]() mutable {
				instance->m_UBSCamera->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &cameraData, sizeof(UBCamera));
				});
		}

		// ── Scene (light) uniform buffer ──────────────────────────────────────
		{
			const auto& dirLight = m_SceneData.SceneLightEnvironment.DirectionalLights[0];
			m_SceneUB.Lights.Direction = dirLight.Direction;
			m_SceneUB.Lights.Radiance = dirLight.Radiance;
			m_SceneUB.Lights.Intensity = dirLight.Intensity;
			m_SceneUB.Lights.ShadowAmount = dirLight.ShadowAmount;
			m_SceneUB.CameraPosition = glm::vec3(glm::inverse(camera.ViewMatrix)[3]);
			m_SceneUB.EnvironmentMapIntensity = m_SceneData.SceneEnvironmentIntensity;

			auto sceneData = m_SceneUB;
			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, sceneData]() mutable {
				instance->m_UBSScene->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &sceneData, sizeof(UBScene));
				});
		}

		// ── Point lights uniform buffer ───────────────────────────────────────
		{
			const auto& pointLights = m_SceneData.SceneLightEnvironment.PointLights;
			m_PointLightsUB.Count = (uint32_t)glm::min((size_t)256, pointLights.size());
			if (m_PointLightsUB.Count > 0)
				std::memcpy(m_PointLightsUB.PointLights, pointLights.data(),
					sizeof(PointLight) * m_PointLightsUB.Count);

			auto plData = m_PointLightsUB;
			uint32_t plSize = (uint32_t)(16ull + sizeof(PointLight) * plData.Count);
			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, plData, plSize]() mutable {
				instance->m_UBSPointLights->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &plData, plSize);
				});
		}

		// ── Spot lights uniform buffer ────────────────────────────────────────
		{
			const auto& spotLights = m_SceneData.SceneLightEnvironment.SpotLights;
			m_SpotLightsUB.Count = (uint32_t)glm::min((size_t)256, spotLights.size());
			if (m_SpotLightsUB.Count > 0)
				std::memcpy(m_SpotLightsUB.SpotLights, spotLights.data(),
					sizeof(SpotLight) * m_SpotLightsUB.Count);

			auto slData = m_SpotLightsUB;
			uint32_t slSize = (uint32_t)(16ull + sizeof(SpotLight) * slData.Count);
			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, slData, slSize]() mutable {
				instance->m_UBSSpotLights->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &slData, slSize);
				});
		}

		// ── Directional shadow matrix ─────────────────────────────────────────
		// Single ortho shadow map centred on the camera position.
		{
			const auto& dirLight = m_SceneData.SceneLightEnvironment.DirectionalLights[0];

			if (dirLight.Intensity > 0.0f && dirLight.CastShadows)
			{
				const glm::vec3 lightDir = glm::normalize(dirLight.Direction);
				const glm::vec3 camPos = glm::vec3(glm::inverse(camera.ViewMatrix)[3]);

				// Pick an up vector that is not collinear with the light direction
				const glm::vec3 up = glm::abs(glm::dot(lightDir, glm::vec3(0, 1, 0))) < 0.99f
					? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);

				const glm::mat4 lightView = glm::lookAt(
					camPos - lightDir * 150.0f,
					camPos,
					up);

				const float halfSize = m_Options.MaxShadowDistance * 0.5f;
				const glm::mat4 lightProj = glm::ortho(
					-halfSize, halfSize,
					-halfSize, halfSize,
					-500.0f, 500.0f);

				m_ShadowUB.ViewProjection[0] = lightProj * lightView;
			}
			else
			{
				m_ShadowUB.ViewProjection[0] = glm::mat4(1.0f);
			}

			auto shadowData = m_ShadowUB;
			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, shadowData]() mutable {
				instance->m_UBSShadow->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &shadowData, sizeof(UBShadow));
				});
		}

		// ── Renderer data uniform buffer ──────────────────────────────────────
		{
			const auto& dirLight = m_SceneData.SceneLightEnvironment.DirectionalLights[0];
			m_RendererDataUB.SoftShadows = m_Options.SoftShadows && dirLight.SoftShadows;
			m_RendererDataUB.LightSize = dirLight.LightSize;
			m_RendererDataUB.MaxShadowDistance = m_Options.MaxShadowDistance;
			m_RendererDataUB.ShadowFade = m_Options.ShadowFade;
			m_RendererDataUB.CascadeSplits = glm::vec4(-1000000.0f);

			auto rdData = m_RendererDataUB;
			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, rdData]() mutable {
				instance->m_UBSRendererData->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &rdData, sizeof(UBRendererData));
				});
		}

		// ── Update environment texture bindings in geometry passes ────────────
		Ref<TextureCube> radianceMap = GetEnvironmentRadianceMap(m_SceneData.SceneEnvironment);
		Ref<TextureCube> irradianceMap = GetEnvironmentIrradianceMap(m_SceneData.SceneEnvironment);
		m_GeometryPass->SetInput("u_EnvRadianceTex", radianceMap);
		m_GeometryPass->SetInput("u_EnvIrradianceTex", irradianceMap);
		m_GeometryPassTransparent->SetInput("u_EnvRadianceTex", radianceMap);
		m_GeometryPassTransparent->SetInput("u_EnvIrradianceTex", irradianceMap);

		m_UploadCommandBuffer->End();
		m_UploadCommandBuffer->Submit();
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Mesh submission
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::SubmitStaticMesh(
		Ref<StaticMesh>    staticMesh,
		Ref<MeshSource>    meshSource,
		Ref<MaterialTable> materialTable,
		const glm::mat4& transform,
		Ref<Material>      overrideMaterial)
	{
		SubmitStaticMeshInternal(staticMesh, meshSource, materialTable, transform, overrideMaterial, false);
	}

	void SceneRenderer::SubmitSelectedStaticMesh(
		Ref<StaticMesh>    staticMesh,
		Ref<MeshSource>    meshSource,
		Ref<MaterialTable> materialTable,
		const glm::mat4& transform,
		Ref<Material>      overrideMaterial)
	{
		SubmitStaticMeshInternal(staticMesh, meshSource, materialTable, transform, overrideMaterial, true);
	}

	void SceneRenderer::SubmitStaticMeshInternal(
		Ref<StaticMesh>    staticMesh,
		Ref<MeshSource>    meshSource,
		Ref<MaterialTable> materialTable,
		const glm::mat4& transform,
		Ref<Material>      overrideMaterial,
		bool               isSelected)
	{
		LUX_CORE_ASSERT(m_Active, "SubmitStaticMesh called outside BeginScene/EndScene");

		const auto& submeshData = meshSource->GetSubmeshes();

		for (uint32_t submeshIndex : staticMesh->GetSubmeshes())
		{
			const auto& submesh = submeshData[submeshIndex];

			// Combine the entity transform with the submesh local transform
			const glm::mat4 submeshTransform = transform * submesh.Transform;

			// Resolve material handle
			AssetHandle materialHandle = 0;
			Ref<MaterialAsset> materialAsset;
			Ref<Material> resolvedOverrideMaterial = overrideMaterial;

			if (!resolvedOverrideMaterial)
			{
				Ref<MaterialTable> staticMeshMaterials = staticMesh ? staticMesh->GetMaterials() : nullptr;
				if (materialTable && materialTable->HasMaterial(submesh.MaterialIndex))
					materialHandle = materialTable->GetMaterial(submesh.MaterialIndex);
				else if (staticMeshMaterials && staticMeshMaterials->HasMaterial(submesh.MaterialIndex))
					materialHandle = staticMeshMaterials->GetMaterial(submesh.MaterialIndex);

				if (materialHandle)
					materialAsset = AssetManager::GetAsset<MaterialAsset>(materialHandle);

				if (!materialAsset)
					resolvedOverrideMaterial = Renderer::GetDefaultWhiteMaterial();
			}

			LUX_CORE_ASSERT(resolvedOverrideMaterial || materialAsset, "No material found for submesh {}", submeshIndex);

			const AssetHandle keyMaterialHandle = resolvedOverrideMaterial
				? AssetHandle((uint64_t)resolvedOverrideMaterial.Raw())
				: materialHandle;
			const MeshKey key{ staticMesh->Handle, keyMaterialHandle, submeshIndex, isSelected };

			// ── Store transform ───────────────────────────────────────────────
			const uint32_t transformIndex = (uint32_t)m_TransformData.size();
			auto& td = m_TransformData.emplace_back();
			td.MRow[0] = { submeshTransform[0][0], submeshTransform[1][0], submeshTransform[2][0], submeshTransform[3][0] };
			td.MRow[1] = { submeshTransform[0][1], submeshTransform[1][1], submeshTransform[2][1], submeshTransform[3][1] };
			td.MRow[2] = { submeshTransform[0][2], submeshTransform[1][2], submeshTransform[2][2], submeshTransform[3][2] };

			// ── Main draw list ────────────────────────────────────────────────
			m_MeshTransformMap[key].ObjectIndices.push_back(transformIndex);

			const bool isTransparent = materialAsset ? materialAsset->IsTransparent() : false;
			auto& destList = isTransparent ? m_TransparentStaticMeshDrawList : m_StaticMeshDrawList;
			auto& dc = destList[key];
			dc.StaticMesh = staticMesh;
			dc.MeshSource = meshSource;
			dc.SubmeshIndex = submeshIndex;
			dc.MaterialTable = materialTable;
			dc.OverrideMaterial = resolvedOverrideMaterial;
			dc.InstanceCount++;

			// ── Selected list ─────────────────────────────────────────────────
			if (isSelected)
			{
				auto& selDc = m_SelectedStaticMeshDrawList[key];
				selDc.StaticMesh = staticMesh;
				selDc.MeshSource = meshSource;
				selDc.SubmeshIndex = submeshIndex;
				selDc.MaterialTable = materialTable;
				selDc.OverrideMaterial = resolvedOverrideMaterial;
				selDc.InstanceCount++;
			}

			// ── Shadow pass list ──────────────────────────────────────────────
			const bool castsShadows = resolvedOverrideMaterial
				? true // override materials always cast shadows
				: (materialAsset && materialAsset->IsShadowCasting());

			if (castsShadows)
			{
				m_ShadowMeshTransformMap[key].Cascade.ObjectIndices.push_back(transformIndex);

				auto& shadowDc = m_StaticMeshShadowPassDrawList[key];
				shadowDc.StaticMesh = staticMesh;
				shadowDc.MeshSource = meshSource;
				shadowDc.SubmeshIndex = submeshIndex;
				shadowDc.MaterialTable = materialTable;
				shadowDc.OverrideMaterial = resolvedOverrideMaterial;
				shadowDc.InstanceCount++;
			}
		}
	}

	void SceneRenderer::SubmitPhysicsStaticDebugMesh(Ref<StaticMesh> staticMesh,
		Ref<MeshSource> meshSource,
		const glm::mat4& transform,
		bool isSimpleCollider)
	{
		SubmitStaticDebugMesh(m_StaticColliderDrawList, staticMesh, meshSource, transform,
			isSimpleCollider ? m_SimpleColliderMaterial : m_ComplexColliderMaterial);
	}

	void SceneRenderer::SubmitStaticDebugMesh(std::map<MeshKey, StaticDrawCommand>& drawList,
		Ref<StaticMesh>  staticMesh,
		Ref<MeshSource>  meshSource,
		const glm::mat4& transform,
		Ref<Material>    material)
	{
		const auto& submeshData = meshSource->GetSubmeshes();

		for (uint32_t submeshIndex : staticMesh->GetSubmeshes())
		{
			const glm::mat4 submeshTransform = transform * submeshData[submeshIndex].Transform;

			// Use the material pointer as a fake asset handle so each material gets its own MeshKey bucket
			const AssetHandle fakeHandle = (AssetHandle)(uint64_t)material.Raw();
			const MeshKey key{ staticMesh->Handle, fakeHandle, submeshIndex, false };

			const uint32_t transformIndex = (uint32_t)m_TransformData.size();
			m_MeshTransformMap[key].ObjectIndices.push_back(transformIndex);

			auto& td = m_TransformData.emplace_back();
			td.MRow[0] = { submeshTransform[0][0], submeshTransform[1][0], submeshTransform[2][0], submeshTransform[3][0] };
			td.MRow[1] = { submeshTransform[0][1], submeshTransform[1][1], submeshTransform[2][1], submeshTransform[3][1] };
			td.MRow[2] = { submeshTransform[0][2], submeshTransform[1][2], submeshTransform[2][2], submeshTransform[3][2] };

			auto& dc = drawList[key];
			dc.StaticMesh = staticMesh;
			dc.MeshSource = meshSource;
			dc.SubmeshIndex = submeshIndex;
			dc.OverrideMaterial = material;
			dc.InstanceCount++;
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// EndScene → FlushDrawList
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::EndScene()
	{
		LUX_CORE_ASSERT(m_Active);
		FlushDrawList();
		m_Active = false;
	}

	void SceneRenderer::WaitForThreads()
	{
		AssetManager::SyncWithAssetThread();
	}

	void SceneRenderer::FlushDrawList()
	{
		// Clear lists and bail if GPU resources not ready
		auto clearAll = [this]()
			{
				m_TransformData.clear();
				m_MeshTransformMap.clear();
				m_ShadowMeshTransformMap.clear();
				m_StaticMeshDrawList.clear();
				m_TransparentStaticMeshDrawList.clear();
				m_SelectedStaticMeshDrawList.clear();
				m_StaticMeshShadowPassDrawList.clear();
				m_StaticColliderDrawList.clear();
			};

		if (!m_ResourcesCreated)
		{
			clearAll();
			return;
		}

		// ── 1. Build the flat ObjectIndex array and assign base offsets ────────
		// Each MeshKey in m_MeshTransformMap gets a contiguous block of object indices.
		// The shader uses  objectIndex = ObjectIndexBase + gl_InstanceIndex
		// to look up its row in the InstanceTransforms SSBO.
		uint32_t cursor = 0;
		std::vector<uint32_t> objectIndexData;
		for (auto& [key, tmd] : m_MeshTransformMap)
		{
			tmd.ObjectIndexBase = cursor;
			for (uint32_t idx : tmd.ObjectIndices)
				objectIndexData.push_back(idx);
			cursor += (uint32_t)tmd.ObjectIndices.size();
		}
		// Do the same for the shadow-specific transform map
		for (auto& [key, shadowTmd] : m_ShadowMeshTransformMap)
		{
			shadowTmd.Cascade.ObjectIndexBase = cursor;
			for (uint32_t idx : shadowTmd.Cascade.ObjectIndices)
				objectIndexData.push_back(idx);
			cursor += (uint32_t)shadowTmd.Cascade.ObjectIndices.size();
		}

		// ── 2. Upload InstanceTransforms and ObjectIndexes SSBOs ──────────────
		m_UploadCommandBuffer->Begin();

		if (!m_TransformData.empty())
		{
			const auto transformData = m_TransformData;
			const auto indexData = objectIndexData;
			Ref<SceneRenderer> instance = this;

			Renderer::Submit([instance, transformData, indexData]() mutable {

				Ref<RenderCommandBuffer> cmd = instance->m_UploadCommandBuffer;

				// Grow InstanceTransforms SSBO if needed
				const uint32_t transformBytes = (uint32_t)(sizeof(TransformVertexData) * transformData.size());
				if (instance->m_SBSInstanceTransforms->RT_Get()->GetHandle()->getDesc().byteSize < transformBytes)
					instance->m_SBSInstanceTransforms->Resize(transformBytes * 2u);
				instance->m_SBSInstanceTransforms->RT_Get()->RT_SetData(cmd, transformData.data(), transformBytes);

				// Grow ObjectIndexes SSBO if needed
				if (!indexData.empty())
				{
					const uint32_t indexBytes = (uint32_t)(sizeof(uint32_t) * indexData.size());
					if (instance->m_SBSObjectIndexes->RT_Get()->GetHandle()->getDesc().byteSize < indexBytes)
						instance->m_SBSObjectIndexes->Resize(indexBytes * 2u);
					instance->m_SBSObjectIndexes->RT_Get()->RT_SetData(cmd, indexData.data(), indexBytes);
				}
				});
		}

		m_UploadCommandBuffer->End();
		m_UploadCommandBuffer->Submit();

		// ── 3. Execute render passes ──────────────────────────────────────────
		m_CommandBuffer->Begin();

		ShadowMapPass();
		PreDepthPass();
		SkyboxPass();
		GeometryPass();
		CompositePass();

		if (m_Options.ShowGrid)
			GridPass();

		// ── 4. Renderer2D (lines, collider outlines, debug renderer queue) ────
		{
			const auto& sceneCamera = m_SceneData.SceneCamera;
			const glm::mat4 viewProj = sceneCamera.Camera.GetProjectionMatrix() * sceneCamera.ViewMatrix;

			m_Renderer2D->SetTargetFramebuffer(m_CompositingFramebuffer);
			m_Renderer2D->BeginScene(viewProj, sceneCamera.ViewMatrix);

			// Flush any queued DebugRenderer work
			for (auto& fn : m_DebugRenderer->GetRenderQueue())
				fn(m_Renderer2D);
			m_DebugRenderer->ClearRenderQueue();

			// Physics collider outlines (2D wireframe lines)
			if (m_Options.ShowPhysicsColliders)
			{
				// Future: iterate m_StaticColliderDrawList and draw via Renderer2D AABB
				// For now the 3D collider meshes are drawn in GeometryPass via m_StaticColliderDrawList.
			}

			m_Renderer2D->EndScene();
		}

		m_CommandBuffer->End();
		m_CommandBuffer->Submit();

		// ── 5. Update statistics ──────────────────────────────────────────────
		UpdateStatistics();

		// ── 6. Clear draw lists for next frame ────────────────────────────────
		clearAll();
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Individual pass implementations
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::ShadowMapPass()
	{
		const auto& dirLight = m_SceneData.SceneLightEnvironment.DirectionalLights[0];
		if (dirLight.Intensity <= 0.0f || !dirLight.CastShadows)
		{
			// Clear the shadow map so geometry doesn't sample stale data
			Renderer::BeginRenderPass(m_CommandBuffer, m_ShadowMapPass, /*explicitClear=*/true);
			Renderer::EndRenderPass(m_CommandBuffer);
			return;
		}

		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "ShadowMapPass");
		Renderer::BeginRenderPass(m_CommandBuffer, m_ShadowMapPass, /*explicitClear=*/true);

		for (auto& [key, dc] : m_StaticMeshShadowPassDrawList)
		{
			auto it = m_ShadowMeshTransformMap.find(key);
			if (it == m_ShadowMeshTransformMap.end()) continue;

			const auto& cascadeTmd = it->second.Cascade;
			const uint32_t instCount = (uint32_t)cascadeTmd.ObjectIndices.size();
			if (instCount == 0) continue;

			StaticDrawCommand drawCmd = dc;
			drawCmd.InstanceCount = instCount;

			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, drawCmd, cascadeTmd]() mutable {
				instance->RT_DrawStaticMesh(
					instance->m_CommandBuffer, drawCmd, cascadeTmd, /*bindMaterial=*/false);
				});
		}

		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::PreDepthPass()
	{
		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "PreDepthPass");
		Renderer::BeginRenderPass(m_CommandBuffer, m_PreDepthPass, /*explicitClear=*/true);

		for (auto& [key, dc] : m_StaticMeshDrawList)
		{
			auto it = m_MeshTransformMap.find(key);
			if (it == m_MeshTransformMap.end()) continue;

			const auto& tmd = it->second;
			StaticDrawCommand drawCmd = dc;

			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, drawCmd, tmd]() mutable {
				instance->RT_DrawStaticMesh(
					instance->m_CommandBuffer, drawCmd, tmd, /*bindMaterial=*/false);
				});
		}

		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::SkyboxPass()
	{
		Ref<TextureCube> radianceMap = GetEnvironmentRadianceMap(m_SceneData.SceneEnvironment);
		if (!radianceMap)
			return;

		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "SkyboxPass");

		m_SkyboxMaterial->Set("u_Uniforms.TextureLod", m_SceneData.SkyboxLod);
		m_SkyboxMaterial->Set("u_Uniforms.Intensity", m_SceneData.SceneEnvironmentIntensity);
		m_SkyboxMaterial->Set("u_Texture", radianceMap);

		Renderer::BeginRenderPass(m_CommandBuffer, m_SkyboxPass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_SkyboxPass->GetPipeline(), m_SkyboxMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);

		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::GeometryPass()
	{
		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "GeometryPass");

		// Selected geometry mask, matching Hazel's static selected path. Lux does
		// not run animation or jump-flood outline passes here.
		Renderer::BeginRenderPass(m_CommandBuffer, m_SelectedGeometryPass);

		for (auto& [key, dc] : m_SelectedStaticMeshDrawList)
		{
			auto it = m_MeshTransformMap.find(key);
			if (it == m_MeshTransformMap.end()) continue;

			StaticDrawCommand drawCmd = dc;
			drawCmd.OverrideMaterial = m_SelectedGeometryMaterial;
			const auto& tmd = it->second;

			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, drawCmd, tmd]() mutable {
				instance->RT_DrawStaticMesh(
					instance->m_CommandBuffer, drawCmd, tmd, /*bindMaterial=*/true);
				});
		}

		Renderer::EndRenderPass(m_CommandBuffer);

		// ── Opaque geometry ───────────────────────────────────────────────────
		Renderer::BeginRenderPass(m_CommandBuffer, m_GeometryPass);

		for (auto& [key, dc] : m_StaticMeshDrawList)
		{
			auto it = m_MeshTransformMap.find(key);
			if (it == m_MeshTransformMap.end()) continue;

			StaticDrawCommand drawCmd = dc;
			const auto& tmd = it->second;

			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, drawCmd, tmd]() mutable {
				instance->RT_DrawStaticMesh(
					instance->m_CommandBuffer, drawCmd, tmd, /*bindMaterial=*/true);
				});
		}

		// Physics debug meshes drawn in the opaque geometry pass
		if (m_Options.ShowPhysicsColliders)
		{
			for (auto& [key, dc] : m_StaticColliderDrawList)
			{
				auto it = m_MeshTransformMap.find(key);
				if (it == m_MeshTransformMap.end()) continue;

				StaticDrawCommand drawCmd = dc;
				const auto& tmd = it->second;

				Ref<SceneRenderer> instance = this;
				Renderer::Submit([instance, drawCmd, tmd]() mutable {
					instance->RT_DrawStaticMesh(
						instance->m_CommandBuffer, drawCmd, tmd, /*bindMaterial=*/true);
					});
			}
		}

		Renderer::EndRenderPass(m_CommandBuffer);

		// ── Transparent geometry ──────────────────────────────────────────────
		if (!m_TransparentStaticMeshDrawList.empty())
		{
			Renderer::BeginRenderPass(m_CommandBuffer, m_GeometryPassTransparent);

			for (auto& [key, dc] : m_TransparentStaticMeshDrawList)
			{
				auto it = m_MeshTransformMap.find(key);
				if (it == m_MeshTransformMap.end()) continue;

				StaticDrawCommand drawCmd = dc;
				const auto& tmd = it->second;

				Ref<SceneRenderer> instance = this;
				Renderer::Submit([instance, drawCmd, tmd]() mutable {
					instance->RT_DrawStaticMesh(
						instance->m_CommandBuffer, drawCmd, tmd, /*bindMaterial=*/true);
					});
			}

			Renderer::EndRenderPass(m_CommandBuffer);
		}

		// ── Selected wireframe overlay ────────────────────────────────────────
		if (m_Options.ShowSelectedInWireframe && !m_SelectedStaticMeshDrawList.empty())
		{
			Renderer::BeginRenderPass(m_CommandBuffer, m_GeometryWireframePass);

			for (auto& [key, dc] : m_SelectedStaticMeshDrawList)
			{
				auto it = m_MeshTransformMap.find(key);
				if (it == m_MeshTransformMap.end()) continue;

				StaticDrawCommand drawCmd = dc;
				drawCmd.OverrideMaterial = m_WireframeMaterial;
				const auto& tmd = it->second;

				Ref<SceneRenderer> instance = this;
				Renderer::Submit([instance, drawCmd, tmd]() mutable {
					instance->RT_DrawStaticMesh(
						instance->m_CommandBuffer, drawCmd, tmd, /*bindMaterial=*/true);
					});
			}

			Renderer::EndRenderPass(m_CommandBuffer);
		}

		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::CompositePass()
	{
		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "CompositePass");

		m_CompositeMaterial->Set("u_Uniforms.Opacity", m_Opacity);

		Renderer::BeginRenderPass(m_CommandBuffer, m_CompositePass, /*explicitClear=*/true);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_CompositePass->GetPipeline(), m_CompositeMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);

		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::GridPass()
	{
		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "GridPass");
		Renderer::BeginRenderPass(m_CommandBuffer, m_GridRenderPass);
		Renderer::RenderQuad(m_CommandBuffer, m_GridRenderPass->GetPipeline(), m_GridMaterial, glm::mat4(1.0f));
		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::UpdateStatistics()
	{
		m_Statistics.DrawCalls = 0;
		m_Statistics.Meshes = 0;
		m_Statistics.Instances = 0;

		auto accumulate = [this](const std::map<MeshKey, StaticDrawCommand>& drawList)
			{
				for (const auto& [key, dc] : drawList)
				{
					m_Statistics.DrawCalls++;
					m_Statistics.Meshes++;
					m_Statistics.Instances += dc.InstanceCount;
				}
			};

		accumulate(m_SelectedStaticMeshDrawList);
		accumulate(m_StaticMeshDrawList);
		accumulate(m_TransparentStaticMeshDrawList);

		if (m_Options.ShowPhysicsColliders)
			accumulate(m_StaticColliderDrawList);

		m_Statistics.SavedDraws = m_Statistics.Instances > m_Statistics.DrawCalls
			? m_Statistics.Instances - m_Statistics.DrawCalls
			: 0;

		m_Statistics.TotalGPUTime = m_CommandBuffer->GetExecutionGPUTime(Renderer::GetCurrentFrameIndex());
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Render-thread draw helper
	// Must be called inside a Renderer::Submit() lambda so it executes
	// on the render thread while the command buffer is recording.
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::RT_DrawStaticMesh(
		Ref<RenderCommandBuffer>  cmd,
		const StaticDrawCommand& dc,
		const TransformMapData& tmd,
		bool                      bindMaterial)
	{
		// Non-const copy of the MeshSource Ref: MeshSource::GetVertexBuffer /
		// GetIndexBuffer / GetMaterials are not marked const, so we cannot call
		// them through a const Ref (which is what we get from a const StaticDrawCommand&).
		Ref<MeshSource> meshSource = dc.MeshSource;

		const auto& submesh = meshSource->GetSubmeshes()[dc.SubmeshIndex];
		nvrhi::GraphicsState& gs = cmd->GetGraphicsState();

		// ── Vertex buffer ─────────────────────────────────────────────────────
		nvrhi::VertexBufferBinding vbb;
		vbb.buffer = meshSource->GetVertexBuffer()->GetHandle();
		vbb.slot = 0;
		vbb.offset = 0;
		gs.vertexBuffers = { vbb };

		// ── Index buffer ──────────────────────────────────────────────────────
		nvrhi::IndexBufferBinding ibb;
		ibb.buffer = meshSource->GetIndexBuffer()->GetHandle();
		ibb.format = nvrhi::Format::R32_UINT;
		ibb.offset = 0;
		gs.indexBuffer = ibb;

		// ── Material (descriptor set 0) ───────────────────────────────────────
		Ref<Material> material;
		if (bindMaterial)
		{
			material = dc.OverrideMaterial;

			if (!material)
			{
				AssetHandle matHandle{};
				if (dc.MaterialTable && dc.MaterialTable->HasMaterial(submesh.MaterialIndex))
					matHandle = dc.MaterialTable->GetMaterial(submesh.MaterialIndex);
				else if (dc.StaticMesh && dc.StaticMesh->GetMaterials() && dc.StaticMesh->GetMaterials()->HasMaterial(submesh.MaterialIndex))
					matHandle = dc.StaticMesh->GetMaterials()->GetMaterial(submesh.MaterialIndex);
				else
				{
					const auto& sourceMaterials = meshSource->GetMaterials();
					if (submesh.MaterialIndex < sourceMaterials.size())
						matHandle = sourceMaterials[submesh.MaterialIndex];
				}

				if (matHandle)
					if (auto matAsset = AssetManager::GetAsset<MaterialAsset>(matHandle))
						material = matAsset->GetMaterial();
			}

			if (!material)
				material = Renderer::GetDefaultWhiteMaterial();

			if (material)
			{
				material->Prepare();
				auto bindingSet = material->GetBindingSet(Renderer::RT_GetCurrentFrameIndex());
				if (bindingSet)
					gs.bindings[0] = bindingSet;
			}
		}

		cmd->RT_CommitGraphicsState();

		// ── Push constants ────────────────────────────────────────────────────
		Buffer materialUniforms = material ? material->GetUniformStorageBuffer() : Buffer();
		const uint64_t pushConstantSize = std::max<uint64_t>(sizeof(MeshDrawPushConstants), materialUniforms.Size);
		std::vector<uint8_t> pushConstants(pushConstantSize);

		if (materialUniforms)
			std::memcpy(pushConstants.data(), materialUniforms.Data, materialUniforms.Size);

		auto& pc = *reinterpret_cast<MeshDrawPushConstants*>(pushConstants.data());
		pc.ObjectIndexBase = tmd.ObjectIndexBase;
		pc.LightIndex = 0; // cascade 0 (single shadow cascade)
		pc.BoneTransformBase = 0;
		pc.BoneTransformStride = 0;
		cmd->GetActive()->setPushConstants(pushConstants.data(), pushConstants.size());

		// ── Instanced draw ────────────────────────────────────────────────────
		nvrhi::DrawArguments drawArgs{};
		drawArgs.vertexCount = submesh.IndexCount;
		drawArgs.startIndexLocation = submesh.BaseIndex;
		drawArgs.startVertexLocation = submesh.BaseVertex;
		drawArgs.instanceCount = dc.InstanceCount;
		cmd->GetActive()->drawIndexed(drawArgs);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Output accessors
	// ─────────────────────────────────────────────────────────────────────────

	Ref<Image2D> SceneRenderer::GetFinalPassImage()
	{
		if (m_CompositePass)
			return m_CompositePass->GetOutput(0);
		return nullptr;
	}

	Ref<Pipeline> SceneRenderer::GetFinalPipeline()
	{
		return m_CompositePass ? m_CompositePass->GetPipeline() : nullptr;
	}

	Ref<RenderPass> SceneRenderer::GetFinalRenderPass()
	{
		return m_CompositePass;
	}

	void SceneRenderer::SetLineWidth(float width)
	{
		m_LineWidth = width;
	}

} // namespace Lux
